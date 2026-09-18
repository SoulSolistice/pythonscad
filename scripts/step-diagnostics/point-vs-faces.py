#!/usr/bin/env python3
"""How far a point is from the surface of every face that uses it, B-splines included."""
import sys
from OCP.STEPControl import STEPControl_Reader
from OCP.IFSelect import IFSelect_RetDone
from OCP.TopExp import TopExp_Explorer
from OCP.TopAbs import TopAbs_FACE, TopAbs_VERTEX
from OCP.TopoDS import TopoDS
from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Surface
from OCP.GeomAPI import GeomAPI_ProjectPointOnSurf
from OCP.gp import gp_Pnt
ST = {0:"plane",1:"cylinder",2:"cone",3:"sphere",4:"torus",5:"bezier",6:"bspline",
      7:"revolution",8:"extrusion",9:"offset",10:"other"}
r = STEPControl_Reader(); assert r.ReadFile(sys.argv[1]) == IFSelect_RetDone
r.TransferRoots(); sh = r.OneShape()
target = gp_Pnt(float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]))
tol = float(sys.argv[5]) if len(sys.argv) > 5 else 1e-6
e = TopExp_Explorer(sh, TopAbs_FACE); fi = 0
rows = []
while e.More():
    f = TopoDS.Face_s(e.Current())
    uses = False
    vx = TopExp_Explorer(f, TopAbs_VERTEX)
    while vx.More():
        p = BRep_Tool.Pnt_s(TopoDS.Vertex_s(vx.Current()))
        if p.Distance(target) <= tol:
            uses = True
            break
        vx.Next()
    if uses:
        srf = BRep_Tool.Surface_s(f)
        pr = GeomAPI_ProjectPointOnSurf(target, srf)
        d = pr.LowerDistance() if pr.NbPoints() > 0 else -1
        rows.append((d, fi, ST.get(int(BRepAdaptor_Surface(f).GetType()), "?"),
                     BRep_Tool.Tolerance_s(f)))
    fi += 1; e.Next()
print("faces using (%.4f %.4f %.4f): %d" % (target.X(), target.Y(), target.Z(), len(rows)))
for d, fi, kind, ft in sorted(rows, reverse=True):
    print("  off by %10.6f   face #%-5d %-10s face tol %.3g" % (d, fi, kind, ft))
