"""Per-face view of everything SOLIDWORKS has a separate fault code for.

`swFaceBadEdge` (17) is what is left over once SOLIDWORKS has ruled out a bad
vertex (16), a bad edge order (18), bad loops (20), a pcurve out of tolerance
(11) and a wire that does not close (15) - it has a distinct code for each of
those.  So a diagnosis of 17 has to measure all of them and find which one
separates the faulty faces from the rest, rather than assuming the first
plausible answer.

Per face this reports:

    offsurf   the largest distance from a bounding edge's 3D curve to the
              face's own surface, sampled along the curve.  A chord over a
              curved surface shows up here and nowhere else: its two ends are
              exact and its middle is not, so no measurement over vertices can
              see it.
    pcurve    the largest distance between the 3D curve and the point its
              pcurve on this face maps to - code 11's quantity.  Read it
              *against offsurf*, never alone: OpenCASCADE builds a projected
              pcurve when the file stores none, and a projected one realises
              the minimum distance, so this column equals offsurf for every
              edge whose pcurve the file did not write.  Where it is *larger*,
              a pcurve the file did write disagrees with its own 3D curve by
              more than a projection would have - which is the only thing this
              column can say on its own, and it says it loudly: 1.083 against
              an offsurf of 0.125 on lid10.  OCP does not expose CurveOnSurface's
              theIsStored flag, so counting stored pcurves means counting
              PCURVE entities in the file instead.
    vtxoff    the largest distance from a bounding vertex to the edge curve it
              is supposed to be on.  Code 7's quantity.
    minedge   the shortest edge of the face, for the sliver branch.
    gap       the largest jump between the end of one edge of a wire and the
              start of the next, for the closure branch.

The centre and area columns are printed in the same units and to the same
precision as scripts/step-interop-solidworks.ps1 -FaultDetail, so the two lists
can be joined and the question becomes which column separates them.

    python face-bad-edge.py file.stp [file.stp ...] [--top N] [--csv out.csv]

Needs the OCP bindings (pip install cadquery-ocp).
"""
import sys, os, csv, collections
from OCP.STEPControl import STEPControl_Reader
from OCP.IFSelect import IFSelect_RetDone
from OCP.TopExp import TopExp_Explorer, TopExp
from OCP.TopAbs import TopAbs_FACE, TopAbs_VERTEX, TopAbs_WIRE
from OCP.TopoDS import TopoDS
from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Surface, BRepAdaptor_Curve
from OCP.BRepTools import BRepTools_WireExplorer
from OCP.ShapeAnalysis import ShapeAnalysis_Surface
from OCP.GeomAbs import (GeomAbs_Plane, GeomAbs_Cylinder, GeomAbs_Cone,
                         GeomAbs_Sphere, GeomAbs_Torus, GeomAbs_BSplineSurface,
                         GeomAbs_BezierSurface, GeomAbs_SurfaceOfRevolution,
                         GeomAbs_SurfaceOfExtrusion, GeomAbs_Line, GeomAbs_Circle,
                         GeomAbs_Ellipse, GeomAbs_BSplineCurve, GeomAbs_BezierCurve)
from OCP.GProp import GProp_GProps
from OCP.BRepGProp import BRepGProp
from OCP.Bnd import Bnd_Box
from OCP.BRepBndLib import BRepBndLib

SK = {GeomAbs_Plane: "plane", GeomAbs_Cylinder: "cylinder", GeomAbs_Cone: "cone",
      GeomAbs_Sphere: "sphere", GeomAbs_Torus: "torus",
      GeomAbs_BSplineSurface: "bspline", GeomAbs_BezierSurface: "bezier",
      GeomAbs_SurfaceOfRevolution: "revolve", GeomAbs_SurfaceOfExtrusion: "extrude"}
CK = {GeomAbs_Line: "line", GeomAbs_Circle: "circle", GeomAbs_Ellipse: "ellipse",
      GeomAbs_BSplineCurve: "bspline", GeomAbs_BezierCurve: "bezier"}

NSAMP = 7  # odd, so a chord's midpoint - its worst point - is always sampled


def face_report(face):
    ad = BRepAdaptor_Surface(face)
    kind = SK.get(ad.GetType(), "type%d" % ad.GetType())
    surf = BRep_Tool.Surface_s(face)
    sas = ShapeAnalysis_Surface(surf)
    g = GProp_GProps()
    BRepGProp.SurfaceProperties_s(face, g)
    box = Bnd_Box()
    BRepBndLib.Add_s(face, box)
    xm, ym, zm, xM, yM, zM = box.Get()

    offsurf = pcurve = vtxoff = gap = 0.0
    minedge = float("inf")
    offkind = ""
    nedge = 0

    wx = TopExp_Explorer(face, TopAbs_WIRE)
    while wx.More():
        wire = TopoDS.Wire_s(wx.Current())
        wx.Next()
        we = BRepTools_WireExplorer(wire, face)
        prev_end = None
        first_start = None
        while we.More():
            e = TopoDS.Edge_s(we.Current())
            we.Next()
            nedge += 1
            ac = BRepAdaptor_Curve(e)
            ck = CK.get(ac.GetType(), "type%d" % ac.GetType())
            u0, u1 = ac.FirstParameter(), ac.LastParameter()
            if not (u1 > u0):
                continue
            g2 = GProp_GProps()
            BRepGProp.LinearProperties_s(e, g2)
            minedge = min(minedge, g2.Mass())

            # The OCP binding takes First/Last as arguments and returns only
            # the curve; calling it the way the C++ signature reads gives a
            # TypeError, which - caught - looks exactly like "this edge has no
            # pcurve".  A whole file then reports 100% pcurve-less and its
            # pcurve column reads a flawless zero.
            try:
                c2d = BRep_Tool.CurveOnSurface_s(e, face, 0.0, 0.0)
                t0, t1 = BRep_Tool.Range_s(e, face)
            except Exception:
                c2d = None
            worst_here = 0.0
            for i in range(NSAMP):
                s = i / (NSAMP - 1)
                p = ac.Value(u0 + s * (u1 - u0))
                uv = sas.ValueOfUV(p, 1e-7)
                d = surf.Value(uv.X(), uv.Y()).Distance(p)
                if d > worst_here:
                    worst_here = d
                if c2d is not None:
                    w = c2d.Value(t0 + s * (t1 - t0))
                    pcurve = max(pcurve, p.Distance(surf.Value(w.X(), w.Y())))
            if worst_here > offsurf:
                offsurf, offkind = worst_here, "%s-on-%s" % (ck, kind)

            # Take the two vertices in the edge's *oriented* sense.  Doing this
            # by hand from TopAbs_FORWARD gets it wrong on a reversed face and
            # invents wire gaps of a millimetre that are not there; TopExp is
            # the only thing that knows the cumulative orientation.
            v1, v2 = TopExp.FirstVertex_s(e, True), TopExp.LastVertex_s(e, True)
            pv1, pv2 = BRep_Tool.Pnt_s(v1), BRep_Tool.Pnt_s(v2)
            p0, p1 = ac.Value(u0), ac.Value(u1)
            vtxoff = max(vtxoff,
                         min(pv1.Distance(p0), pv1.Distance(p1)),
                         min(pv2.Distance(p0), pv2.Distance(p1)))

            if prev_end is not None:
                gap = max(gap, prev_end.Distance(pv1))
            else:
                first_start = pv1
            prev_end = pv2
        if prev_end is not None and first_start is not None:
            gap = max(gap, prev_end.Distance(first_start))

    return dict(kind=kind, area=g.Mass(), nedge=nedge,
                cx=(xm + xM) / 2, cy=(ym + yM) / 2, cz=(zm + zM) / 2,
                offsurf=offsurf, offkind=offkind, pcurve=pcurve,
                vtxoff=vtxoff, minedge=0.0 if minedge == float("inf") else minedge,
                gap=gap)


def main():
    args = list(sys.argv[1:])
    top, csvpath, files, i = 12, None, [], 0
    while i < len(args):
        if args[i] == "--top":
            top = int(args[i + 1]); i += 2
        elif args[i] == "--csv":
            csvpath = args[i + 1]; i += 2
        else:
            files.append(args[i]); i += 1

    allrows = []
    for path in files:
        r = STEPControl_Reader()
        if r.ReadFile(path) != IFSelect_RetDone:
            print("== %s   UNREADABLE" % os.path.basename(path)); continue
        r.TransferRoots()
        shape = r.OneShape()
        rows = []
        ex = TopExp_Explorer(shape, TopAbs_FACE)
        fi = 0
        while ex.More():
            f = TopoDS.Face_s(ex.Current()); ex.Next()
            d = face_report(f)
            d["file"] = os.path.basename(path); d["face"] = fi
            rows.append(d); fi += 1
        allrows += rows
        byk = collections.Counter(d["kind"] for d in rows)
        print("== %s   %d faces  %s" % (os.path.basename(path), len(rows), dict(byk)))
        ne = sum(d["nedge"] for d in rows)
        for k in ("offsurf", "pcurve", "vtxoff", "gap"):
            print("   %-8s max %.6e   over 1e-3: %d of %d faces"
                  % (k, max([d[k] for d in rows] or [0]),
                     sum(1 for d in rows if d[k] > 1e-3), len(rows)))
        print("   edges    %d face-uses; %d faces where a written pcurve is "
              "further off than a projected one would be"
              % (ne, sum(1 for d in rows if d["pcurve"] > d["offsurf"] + 1e-9)))
        sl = sorted(rows, key=lambda d: d["minedge"])
        print("   minedge  min %.6e   under 1e-2: %d faces"
              % (sl[0]["minedge"] if sl else 0,
                 sum(1 for d in rows if 0 < d["minedge"] < 1e-2)))
        print("   %-4s %-9s %10s %5s %-24s %10s %10s %10s %10s %-18s"
              % ("face", "kind", "area", "edges", "centre", "offsurf",
                 "pcurve", "vtxoff", "minedge", "worst pair"))
        for d in sorted(rows, key=lambda d: -d["offsurf"])[:top]:
            print("   %-4d %-9s %10.4f %5d %-24s %10.6f %10.6f %10.6f %10.6f %-18s"
                  % (d["face"], d["kind"], d["area"], d["nedge"],
                     "%.4f;%.4f;%.4f" % (d["cx"], d["cy"], d["cz"]),
                     d["offsurf"], d["pcurve"], d["vtxoff"], d["minedge"], d["offkind"]))
        print()

    if csvpath and allrows:
        with open(csvpath, "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=list(allrows[0].keys()))
            w.writeheader(); w.writerows(allrows)
        print("wrote %s (%d rows)" % (csvpath, len(allrows)))


if __name__ == "__main__":
    main()
