#!/usr/bin/env python3
"""How far each face's own corners are from the surface that face is on.

The quantity a strict reader measures, broken down by kind of surface, which is
what scripts/step-occt-strict.py reports as one number per file. Use this when
that number moves and the question is *which* surface moved it: on
step-declare-grid-scad the whole of a 0.096 sat on the planes while the
cylinders and the sweep were already exact, and one figure could not have said
so.

    python3 scripts/step-corner-stray.py build/foo.step [more.step ...]

Beware the p95 on a face with few corners: with nineteen it is the maximum, so
a single outlier sets it. See doc/step-corner-exactness-handover.md.
"""
import sys, math
from collections import defaultdict
from OCP.STEPControl import STEPControl_Reader
from OCP.IFSelect import IFSelect_RetDone
from OCP.TopExp import TopExp_Explorer
from OCP.TopAbs import TopAbs_FACE, TopAbs_VERTEX
from OCP.TopoDS import TopoDS
from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Surface
from OCP.GeomAbs import (GeomAbs_Plane, GeomAbs_Cylinder, GeomAbs_Cone,
                         GeomAbs_BSplineSurface)
from OCP.GeomAPI import GeomAPI_ProjectPointOnSurf
NAME = {GeomAbs_Plane: "plane", GeomAbs_Cylinder: "cylinder", GeomAbs_Cone: "cone",
        GeomAbs_BSplineSurface: "bspline"}
def off(a, face, p):
    t = a.GetType()
    if t == GeomAbs_Plane: return abs(a.Plane().Distance(p))
    if t in (GeomAbs_Cylinder, GeomAbs_Cone):
        q = a.Cylinder() if t == GeomAbs_Cylinder else a.Cone()
        ax = q.Axis(); d, o = ax.Direction(), ax.Location()
        v = (p.X()-o.X(), p.Y()-o.Y(), p.Z()-o.Z())
        h = v[0]*d.X()+v[1]*d.Y()+v[2]*d.Z()
        r = math.sqrt(max(0.0, sum(x*x for x in v)-h*h))
        if t == GeomAbs_Cylinder: return abs(r-q.Radius())
        return abs(r-(q.RefRadius()+h*math.tan(q.SemiAngle())))*math.cos(q.SemiAngle())
    pr = GeomAPI_ProjectPointOnSurf(p, BRep_Tool.Surface_s(face))
    return pr.LowerDistance() if pr.NbPoints() > 0 else 0.0
for path in sys.argv[1:]:
    r = STEPControl_Reader(); assert r.ReadFile(path) == IFSelect_RetDone
    r.TransferRoots(); sh = r.OneShape()
    per = defaultdict(list)
    e = TopExp_Explorer(sh, TopAbs_FACE)
    while e.More():
        f = TopoDS.Face_s(e.Current()); a = BRepAdaptor_Surface(f)
        k = NAME.get(a.GetType(), "other"); seen = set()
        v = TopExp_Explorer(f, TopAbs_VERTEX)
        while v.More():
            vt = TopoDS.Vertex_s(v.Current()); h = vt.TShape().This()
            if h not in seen:
                seen.add(h); per[k].append(off(a, f, BRep_Tool.Pnt_s(vt)))
            v.Next()
        e.Next()
    print(f"\n{path.split('/')[-1]}  corners off the surface they are on, by face kind")
    for k in sorted(per):
        s = sorted(per[k])
        print(f"   {k:9s} n={len(s):5d}  p95 {s[int(len(s)*0.95)]:.4e}  max {s[-1]:.4e}")
