#!/usr/bin/env python3
"""Every plane bordering a sweep face: is it really a facet of that sweep?

A plane that meets a declared sweep at a few degrees, with every corner on the
sweep, is not a second surface at all - it is the sweep, left faceted. That is
what a failed crossing curve between the two looks like from the outside, and
it is a claim question (why did the sweep not take this facet?) rather than a
curve question. On lid10 on 2026-09-18, 12 of the 13 planes bordering the
thread were its own flank facets, refused by the grid claim at the run-in and
run-out.

Per plane: how many of its corners are on the sweep it borders (to 1e-6), the
farthest one, the angle between the two at each shared edge, and its centre.
"""
import math
import sys

from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Curve, BRepAdaptor_Surface
from OCP.GeomAPI import GeomAPI_ProjectPointOnSurf
from OCP.GeomLProp import GeomLProp_SLProps
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPControl import STEPControl_Reader
from OCP.TopAbs import TopAbs_EDGE, TopAbs_FACE, TopAbs_VERTEX
from OCP.TopExp import TopExp as TE
from OCP.TopoDS import TopoDS
from OCP.TopTools import TopTools_IndexedDataMapOfShapeListOfShape, TopTools_IndexedMapOfShape

PLANE, BSPLINE = 0, 6

r = STEPControl_Reader()
assert r.ReadFile(sys.argv[1]) == IFSelect_RetDone
r.TransferRoots()
sh = r.OneShape()
m = TopTools_IndexedDataMapOfShapeListOfShape()
TE.MapShapesAndAncestors_s(sh, TopAbs_EDGE, TopAbs_FACE, m)
fmap = TopTools_IndexedMapOfShape()
TE.MapShapes_s(sh, TopAbs_FACE, fmap)


def kind(f):
    return int(BRepAdaptor_Surface(f).GetType())


def corners(f):
    vm = TopTools_IndexedMapOfShape()
    TE.MapShapes_s(f, TopAbs_VERTEX, vm)
    return [BRep_Tool.Pnt_s(TopoDS.Vertex_s(vm.FindKey(i))) for i in range(1, vm.Extent() + 1)]


def normal(srf, p):
    u, v = GeomAPI_ProjectPointOnSurf(p, srf).LowerDistanceParameters()
    n = GeomLProp_SLProps(srf, u, v, 1, 1e-9).Normal()
    return (n.X(), n.Y(), n.Z())


planes = {}
for i in range(1, m.Extent() + 1):
    faces = [TopoDS.Face_s(f) for f in m.FindFromIndex(i)]
    kinds = [kind(f) for f in faces]
    if sorted(kinds) != [PLANE, BSPLINE]:
        continue
    plane, sweep = faces[kinds.index(PLANE)], faces[kinds.index(BSPLINE)]
    c = BRepAdaptor_Curve(TopoDS.Edge_s(m.FindKey(i)))
    mid = c.Value((c.FirstParameter() + c.LastParameter()) / 2)
    ssrf = BRep_Tool.Surface_s(sweep)
    dot = abs(sum(a * b for a, b in zip(normal(ssrf, mid), normal(BRep_Tool.Surface_s(plane), mid))))
    rec = planes.get(fmap.FindIndex(plane))
    if rec is None:
        pts = corners(plane)
        dist = [GeomAPI_ProjectPointOnSurf(p, ssrf).LowerDistance() for p in pts]
        rec = planes[fmap.FindIndex(plane)] = {"pts": pts, "dist": dist, "angles": []}
    rec["angles"].append(math.degrees(math.acos(min(1.0, dot))))

print("%d planes border a sweep face" % len(planes))
facets = 0
for rec in sorted(planes.values(), key=lambda x: min(x["angles"])):
    pts, dist, ang = rec["pts"], rec["dist"], rec["angles"]
    cx, cy, cz = (sum(getattr(p, a)() for p in pts) / len(pts) for a in ("X", "Y", "Z"))
    # Within a sagitta and at a shallow angle: the sweep's own facet. Both
    # figures are descriptive, for sorting a listing, and gate nothing.
    facet = max(dist) < 0.15 and min(ang) < 25
    facets += facet
    print("  corners %2d, on the sweep %2d, farthest %.4f   meets it at %5.1f deg   centre (%.2f %.2f %.2f)%s"
          % (len(pts), sum(d < 1e-6 for d in dist), max(dist), min(ang), cx, cy, cz,
             "   the sweep's own facet" if facet else ""))
print("%d look like the sweep's own facets: every corner within 0.15 of it, meeting it under 25 degrees" % facets)
