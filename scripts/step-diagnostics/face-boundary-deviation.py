"""Per-face view of the same defect: for every face of a STEP file, how far its
boundary vertices sit from the surface the face was given.

Coarser than edge-on-surface.py - a vertex can land on the surface while the
edge between two of them bows off it - but it names the faces, with their kind,
area and radius, so it says *which* patches a fit is straining on rather than
only that some are.

    python face-boundary-deviation.py file.stp [file.stp ...]

Needs the OCP bindings (pip install cadquery-ocp).
"""

import sys, os, collections
from OCP.STEPControl import STEPControl_Reader
from OCP.TopExp import TopExp_Explorer
from OCP.TopAbs import TopAbs_FACE, TopAbs_VERTEX
from OCP.TopoDS import TopoDS
from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Surface
from OCP.GeomAPI import GeomAPI_ProjectPointOnSurf
from OCP.GeomAbs import (GeomAbs_Plane, GeomAbs_Cylinder, GeomAbs_Cone,
                         GeomAbs_Sphere, GeomAbs_Torus, GeomAbs_BSplineSurface)
from OCP.GProp import GProp_GProps
from OCP.BRepGProp import BRepGProp
KIND = {GeomAbs_Plane:"plane",GeomAbs_Cylinder:"cyl",GeomAbs_Cone:"cone",
        GeomAbs_Sphere:"sph",GeomAbs_Torus:"tor",GeomAbs_BSplineSurface:"bspl"}
for path in sys.argv[1:]:
    r=STEPControl_Reader(); r.ReadFile(path); r.TransferRoots(); shape=r.OneShape()
    cen=collections.Counter(); bad=[]
    ex=TopExp_Explorer(shape,TopAbs_FACE)
    while ex.More():
        f=TopoDS.Face_s(ex.Current()); ex.Next()
        ad=BRepAdaptor_Surface(f); kind=KIND.get(ad.GetType(),"other")
        cen[kind]+=1
        surf=BRep_Tool.Surface_s(f); worst=0.0
        vx=TopExp_Explorer(f,TopAbs_VERTEX)
        while vx.More():
            v=TopoDS.Vertex_s(vx.Current()); vx.Next()
            p=BRep_Tool.Pnt_s(v); pr=GeomAPI_ProjectPointOnSurf(p,surf)
            if pr.NbPoints()>0: worst=max(worst,pr.LowerDistance())
        if worst>1e-3:
            g=GProp_GProps(); BRepGProp.SurfaceProperties_s(f,g)
            rad = ad.Cylinder().Radius() if kind=="cyl" else (
                  ad.Cone().RefRadius() if kind=="cone" else 0.0)
            bad.append((kind,worst,g.Mass(),rad))
    print("== %s   %s" % (os.path.basename(path), dict(cen)))
    for k,w,a,rad in sorted(bad,key=lambda t:-t[1]):
        print("   %-5s dev %8.5f  area %10.3f  r %7.3f" % (k,w,a,rad))
