#!/usr/bin/env python3
"""Every property this exporter is known to get wrong, and the fault each becomes.

One table per file, in the order the faults are numbered rather than the order
they were found. Each row is a property of the *written* file, measured here, and
each is annotated with the SOLIDWORKS fault code it turns into and with what it
cost when it was found - because a number without that is just a number.

Everything here was learned between 2026-09-14 and 2026-09-17, mostly by being
wrong first. The short version of the method is in
`doc/step-export-development.md`, *How to interrogate a reader*: a reader's fault
*list* is the last thing to ask, because `IFace2::Check` and the Import
Diagnostics dialog are different analyses and neither contains the other. These
are our own measurements of our own file, which is the part we control.

    python scripts/step-diagnostics/fault-census.py file.stp [file.stp ...]

Text first, so nothing is repaired on the way in. Rows marked (OCP) need
`cadquery-ocp` and are skipped without it - they are the ones that have to
evaluate a B-spline surface, which is not worth reimplementing here.
"""

import math
import os
import re
import sys
from importlib.machinery import SourceFileLoader

_here = os.path.dirname(os.path.abspath(__file__))
voc = SourceFileLoader("vertex_on_curve", os.path.join(_here, "vertex-on-curve.py")).load_module()
seam = SourceFileLoader("seam_straddle", os.path.join(_here, "seam-straddle.py")).load_module()


def _bare(args):
    return voc.floats(re.sub(r"#\d+", " ", re.sub(r"'[^']*'", " ", args)))


def topology(ents):
    oriented = {i: voc.refs(a)[0] for i, (k, a) in ents.items()
                if k == "ORIENTED_EDGE" and voc.refs(a)}
    loops = {i: voc.refs(a) for i, (k, a) in ents.items() if k == "EDGE_LOOP"}
    bounds = {i: voc.refs(a)[0] for i, (k, a) in ents.items()
              if (k.endswith("FACE_BOUND") or k.endswith("FACE_OUTER_BOUND")) and voc.refs(a)}
    return oriented, loops, bounds


def face_edges(ents):
    """face id -> [edge ids], and vertex id -> set of face ids."""
    oriented, loops, bounds = topology(ents)
    per_face, per_vertex = {}, {}
    for i, (k, a) in ents.items():
        if k != "ADVANCED_FACE":
            continue
        r = voc.refs(a)
        for b in r[:-1]:
            lp = bounds.get(b)
            if lp is None:
                continue
            for o in loops.get(lp, []):
                ec = oriented.get(o)
                if ec is None or ec not in ents:
                    continue
                per_face.setdefault(i, []).append(ec)
                for v in voc.refs(ents[ec][1])[:2]:
                    per_vertex.setdefault(v, set()).add(i)
    return per_face, per_vertex


def surface_distance(ents, sid, p):
    """Exact for the analytic surfaces; None for anything that needs evaluating."""
    kind, args = ents[sid]
    r = voc.refs(args)
    if not r:
        return None, kind.lower()
    try:
        origin, axis, _ = voc.placement(ents, r[0])
    except Exception:
        return None, kind.lower()
    n = voc.unit(axis)
    rel = voc.sub(p, origin)
    nums = _bare(args)
    if kind == "PLANE":
        return abs(voc.dot(rel, n)), "plane"
    along = voc.dot(rel, n)
    across = voc.norm(voc.sub(rel, [x * along for x in n]))
    if kind == "CYLINDRICAL_SURFACE" and nums:
        return abs(across - nums[-1]), "cylinder"
    if kind == "SPHERICAL_SURFACE" and nums:
        return abs(voc.norm(rel) - nums[-1]), "sphere"
    if kind == "CONICAL_SURFACE" and len(nums) >= 2:
        rad, half = nums[-2], nums[-1]
        return abs(across - (rad + along * math.tan(half))) * math.cos(half), "cone"
    return None, kind.lower()


def vertex_point(ents, v):
    return voc.point(ents, voc.refs(ents[v][1])[0])


def census(path):
    ents = voc.parse(path)
    per_face, per_vertex = face_edges(ents)
    rows = []

    # --- code 7: a vertex off the curve its own edge is written as -------------
    off_curve = voc.offenders(path)
    rows.append(("7", "vertex off its own edge's curve", len(off_curve),
                 off_curve[0][0] if off_curve else 0.0,
                 "a substituted curve must pass through its run's own endpoints; "
                 "lid10 once wrote 14 ELLIPSEs missing theirs by up to 1.6196"))

    # --- code 16: a vertex off the surface of a face that uses it --------------
    # Indexed once: the same walk done per vertex is O(vertices x faces x edges)
    # and takes minutes on lid10, which is not a tool anybody runs.
    edges_at = {}
    for i, (k, a) in ents.items():
        if k != "EDGE_CURVE":
            continue
        for v in voc.refs(a)[:2]:
            edges_at.setdefault(v, []).append(i)
    # ...and the stray each vertex carries, once, because both the row below and
    # the one after it want it.
    stray_at = {}
    for v, fs in per_vertex.items():
        try:
            p = vertex_point(ents, v)
        except Exception:
            continue
        worst_here = 0.0
        for f in fs:
            d, _ = surface_distance(ents, voc.refs(ents[f][1])[-1], p)
            if d is not None:
                worst_here = max(worst_here, d)
        stray_at[v] = worst_here
    off_face, worst16 = 0, 0.0
    for v, worst_here in stray_at.items():
        try:
            owed = 1e-6 * max(1.0, voc.norm(vertex_point(ents, v)))
        except Exception:
            continue
        if worst_here > owed:
            off_face += 1
            worst16 = max(worst16, worst_here)
    rows.append(("16", "vertex off a face's own surface", off_face, worst16,
                 "the corner placement left it; a reader widens that vertex until it "
                 "covers the stray, which is what turns a short edge into code 13"))

    # --- code 13: a short edge at a strayed vertex -----------------------------
    # Not a fault in the file - an edge 0.219 long is a perfectly good edge. It
    # becomes one when the reader has widened a vertex at its end past its
    # length, which is how lid10 gained `swEdgeVerticesTouch` the day the plane
    # veto stopped holding corners and left one behind.
    at_risk, tightest = 0, None
    for v, fs in per_vertex.items():
        try:
            p = vertex_point(ents, v)
        except Exception:
            continue
        stray = stray_at.get(v, 0.0)
        if stray <= 1e-6 * max(1.0, voc.norm(p)):
            continue
        for ec in edges_at.get(v, ()):
            er = voc.refs(ents[ec][1])
            try:
                pa, pb = vertex_point(ents, er[0]), vertex_point(ents, er[1])
            except Exception:
                continue
            length = math.dist(pa, pb)
            if length > 0 and length < 4 * stray:
                at_risk += 1
                if tightest is None or length / stray < tightest:
                    tightest = length / stray
    rows.append(("13", "short edge at a strayed vertex", at_risk,
                 tightest if tightest is not None else 0.0,
                 "length/stray of the tightest; under about 2 a reader merges the "
                 "two ends and calls the edge degenerate"))

    # --- code 21: a loop that visits a vertex more than twice ------------------
    # A face on a closed surface legitimately carries its seam edge twice, which
    # puts a vertex on the loop four times; what is reported here is the rest.
    pinched = 0
    oriented, loops, bounds = topology(ents)
    for i, (k, a) in ents.items():
        if k != "ADVANCED_FACE":
            continue
        r = voc.refs(a)
        closed_surface = ents[r[-1]][0] in ("CYLINDRICAL_SURFACE", "CONICAL_SURFACE",
                                            "SPHERICAL_SURFACE", "TOROIDAL_SURFACE") or \
            seam.closed_in_v(ents, r[-1])
        if closed_surface:
            continue
        for b in r[:-1]:
            lp = bounds.get(b)
            if lp is None:
                continue
            seen = {}
            for o in loops.get(lp, []):
                ec = oriented.get(o)
                if ec is None or ec not in ents:
                    continue
                for v in voc.refs(ents[ec][1])[:2]:
                    seen[v] = seen.get(v, 0) + 1
            if any(c > 2 for c in seen.values()):
                pinched += 1
    rows.append(("21", "loop visiting a vertex more than twice", pinched, 0.0,
                 "a pinch, which is what self-intersecting means for a face; the "
                 "seam of a closed face is excluded because it is legal"))

    # --- no code of its own: a boundary crossing its surface's seam ------------
    crossing = 0
    for i, (k, a) in sorted(ents.items()):
        if k != "ADVANCED_FACE":
            continue
        if seam.closed_in_v(ents, voc.refs(a)[-1]):
            crossing += seam.crossings(ents, i)
    rows.append(("-", "face loop crossing its surface's seam", crossing, 0.0,
                 "the pcurve has nowhere to go but the long way round; the reader "
                 "rebuilds the face, invents a seam edge and widens a vertex to 1.08"))

    # --- the curves we did write, since the fix for most of this is a curve ----
    kinds = {}
    for i, (k, a) in ents.items():
        if k in ("LINE", "CIRCLE", "ELLIPSE", "HYPERBOLA", "PARABOLA") or \
           k.startswith("B_SPLINE_CURVE"):
            kinds[k.replace("_WITH_KNOTS", "")] = kinds.get(k.replace("_WITH_KNOTS", ""), 0) + 1
    return rows, kinds


def occt_rows(path):
    """(OCP) code 17, which needs a B-spline surface evaluated to be honest."""
    try:
        from OCP.STEPControl import STEPControl_Reader
        from OCP.IFSelect import IFSelect_RetDone
        from OCP.TopExp import TopExp_Explorer
        from OCP.TopAbs import TopAbs_EDGE, TopAbs_FACE
        from OCP.TopoDS import TopoDS
        from OCP.BRep import BRep_Tool
        from OCP.BRepAdaptor import BRepAdaptor_Curve, BRepAdaptor_Surface
        from OCP.GeomAPI import GeomAPI_ProjectPointOnSurf
        from OCP.TopTools import TopTools_IndexedDataMapOfShapeListOfShape
        from OCP.TopExp import TopExp as TE
    except Exception:
        return None
    CT = {0: "line", 1: "circle", 2: "ellipse", 3: "hyperbola", 4: "parabola", 5: "bezier",
          6: "bspline"}
    ST = {0: "plane", 1: "cyl", 2: "cone", 3: "sphere", 4: "torus", 5: "bezier", 6: "bspl"}
    r = STEPControl_Reader()
    if r.ReadFile(path) != IFSelect_RetDone:
        return None
    r.TransferRoots()
    sh = r.OneShape()
    m = TopTools_IndexedDataMapOfShapeListOfShape()
    TE.MapShapesAndAncestors_s(sh, TopAbs_EDGE, TopAbs_FACE, m)
    worst, count, pairs = 0.0, 0, {}
    e = TopExp_Explorer(sh, TopAbs_EDGE)
    while e.More():
        ed = TopoDS.Edge_s(e.Current())
        try:
            c = BRepAdaptor_Curve(ed)
            ck = CT.get(int(c.GetType()), "?")
            for f in m.FindFromKey(ed):
                fc = TopoDS.Face_s(f)
                sk = ST.get(int(BRepAdaptor_Surface(fc).GetType()), "?")
                srf = BRep_Tool.Surface_s(fc)
                here = 0.0
                for t in range(11):
                    q = c.Value(c.FirstParameter()
                                + (c.LastParameter() - c.FirstParameter()) * t / 10.0)
                    pr = GeomAPI_ProjectPointOnSurf(q, srf)
                    if pr.NbPoints() > 0:
                        here = max(here, pr.LowerDistance())
                if here > 1e-4:
                    count += 1
                    worst = max(worst, here)
                    key = "%s-on-%s" % (ck, sk)
                    pairs[key] = pairs.get(key, 0) + 1
        except Exception:
            pass
        e.Next()
    return count, worst, pairs


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    bad = 0
    for path in sys.argv[1:]:
        rows, kinds = census(path)
        print("\n%s" % os.path.basename(path))
        print("  %-4s %-42s %8s %12s" % ("code", "property", "count", "worst"))
        for code, what, n, worst, why in rows:
            print("  %-4s %-42s %8d %12s" % (code, what, n, ("%.6f" % worst) if worst else "-"))
            if n:
                bad += 1
                print("       %s" % why)
        occt = occt_rows(path)
        if occt is None:
            print("  17   edge off a face's own surface            (OCP not installed - skipped)")
        else:
            count, worst, pairs = occt
            print("  %-4s %-42s %8d %12s" % ("17", "edge off a face's own surface (OCP)", count,
                                             ("%.6f" % worst) if worst else "-"))
            if count:
                bad += 1
                print("       %s" % ", ".join("%s %d" % kv for kv in sorted(pairs.items())))
                print("       a chord where the surface curves; the conic a plane cuts is exact "
                      "and writable, hyperbola and parabola included")
        print("  curves written: %s" % ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
