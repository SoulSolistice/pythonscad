#!/usr/bin/env python3
"""What the reader constructed, against what we wrote.

For every OCCT edge whose vertex is off its own curve, print the edge as the
reader holds it - curve kind, its two vertices, every face it bounds and that
face's surface - and then ask the STEP text whether *we* wrote an edge on those
two vertices at all, and as what.

Where the answer is "we wrote no such edge", the reader built it, and the stray
is the reader's own construction rather than anything of ours.
"""

import re
import sys
import math

from OCP.STEPControl import STEPControl_Reader
from OCP.IFSelect import IFSelect_RetDone
from OCP.TopExp import TopExp_Explorer, TopExp
from OCP.TopAbs import TopAbs_FACE, TopAbs_EDGE
from OCP.TopoDS import TopoDS
from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Curve, BRepAdaptor_Surface
from OCP.GeomAPI import GeomAPI_ProjectPointOnCurve
from OCP.TopTools import TopTools_IndexedDataMapOfShapeListOfShape
from OCP.TopExp import TopExp as TopExpTool

CT = {0: "line", 1: "circle", 2: "ellipse", 3: "hyperbola", 4: "parabola",
      5: "bezier", 6: "bspline", 7: "offset", 8: "other"}
ST = {0: "plane", 1: "cylinder", 2: "cone", 3: "sphere", 4: "torus", 5: "bezier",
      6: "bspline", 7: "revolution", 8: "extrusion", 9: "offset", 10: "other"}


def ours(path):
    """vertex coordinate -> id, and the edges we wrote, keyed by vertex pair."""
    text = open(path, "r", encoding="utf-8", errors="replace").read()
    ents = {}
    for m in re.finditer(r"#(\d+)\s*=\s*([A-Z_0-9]+)\s*\((.*?)\)\s*;", text, flags=re.S):
        ents[int(m.group(1))] = (m.group(2), m.group(3))
    pts = {}
    for i, (kind, args) in ents.items():
        if kind == "CARTESIAN_POINT":
            vals = [float(x) for x in re.findall(r"[-+]?[0-9]*\.?[0-9]+(?:[EeDd][-+]?[0-9]+)?",
                                                 args.split("(", 1)[1])][:3]
            if len(vals) == 3:
                pts[i] = vals
    verts = {}
    for i, (kind, args) in ents.items():
        if kind == "VERTEX_POINT":
            r = [int(x) for x in re.findall(r"#(\d+)", args)]
            if r and r[0] in pts:
                verts[i] = pts[r[0]]
    edges = []
    for i, (kind, args) in ents.items():
        if kind != "EDGE_CURVE":
            continue
        r = [int(x) for x in re.findall(r"#(\d+)", args)]
        if len(r) < 3 or r[0] not in verts or r[1] not in verts:
            continue
        edges.append((verts[r[0]], verts[r[1]], ents[r[2]][0], i))
    return verts, edges


def near(a, b, tol=1e-6):
    return all(abs(a[i] - b[i]) <= tol for i in range(3))


def main():
    path = sys.argv[1]
    verts, our_edges = ours(path)
    r = STEPControl_Reader()
    assert r.ReadFile(path) == IFSelect_RetDone
    r.TransferRoots()
    sh = r.OneShape()

    emap = TopTools_IndexedDataMapOfShapeListOfShape()
    TopExpTool.MapShapesAndAncestors_s(sh, TopAbs_EDGE, TopAbs_FACE, emap)

    seen = set()
    rows = []
    e = TopExp_Explorer(sh, TopAbs_EDGE)
    while e.More():
        ed = TopoDS.Edge_s(e.Current())
        key = ed.TShape()
        if key in seen:
            e.Next()
            continue
        seen.add(key)
        try:
            ck = CT.get(int(BRepAdaptor_Curve(ed).GetType()), "?")
            c3d = BRep_Tool.Curve_s(ed, 0.0, 1.0)
            ends = []
            worst = 0.0
            for v in (TopExp.FirstVertex_s(ed), TopExp.LastVertex_s(ed)):
                p = BRep_Tool.Pnt_s(v)
                ends.append((p.X(), p.Y(), p.Z()))
                pr = GeomAPI_ProjectPointOnCurve(p, c3d)
                if pr.NbPoints() > 0:
                    worst = max(worst, pr.LowerDistance())
            if worst > 1e-6:
                faces = []
                for f in emap.FindFromKey(ed):
                    faces.append(ST.get(int(BRepAdaptor_Surface(TopoDS.Face_s(f)).GetType()), "?"))
                rows.append((worst, ck, ends, faces, BRep_Tool.Tolerance_s(ed)))
        except Exception as exc:
            print("  (edge skipped: %s)" % exc)
        e.Next()

    rows.sort(reverse=True)
    print("edges the reader holds whose vertex is off their curve: %d" % len(rows))
    for worst, ck, ends, faces, tol in rows:
        print("\n  %.6f  %-8s tol %.3g  between %s" % (worst, ck, tol, "/".join(faces)))
        for p in ends:
            print("      vertex (%.4f %.4f %.4f)%s"
                  % (p[0], p[1], p[2],
                     "" if any(near(v, p) for v in verts.values()) else "   <- not a vertex we wrote"))
        mine = [(k, i) for (a, b, k, i) in our_edges
                if (near(a, ends[0]) and near(b, ends[1])) or (near(a, ends[1]) and near(b, ends[0]))]
        if mine:
            print("      we wrote: " + ", ".join("%s #%d" % (k, i) for k, i in mine))
        else:
            print("      we wrote no edge on those two vertices - the reader built this one")
    return 0


if __name__ == "__main__":
    sys.exit(main())
