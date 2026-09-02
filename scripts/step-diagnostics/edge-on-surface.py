"""Sample every edge of a STEP file along its length and measure how far it
lies from each of the two faces it bounds.

This is the measurement that found the defect described in
doc/step-interop-validation.md: the analytic exporter replaces a run of facets
with a cylinder but leaves the mesh's straight polyline boundary in place, so
the edge is a chord where the surface is an arc. Reported per
curve-kind-on-surface-kind pair, because that is what makes it legible at a
glance - `line-on-cyl` with a worst of 0.17 is the whole finding, and
`line-on-plane` at exactly zero is the control sitting in the same table.

A kernel that sews such a file has to widen its tolerance to swallow the gap,
which some do silently. Run it on an analytic export and its faceted control
together; the control should be all zeros.

    python edge-on-surface.py file.stp [file.stp ...]

Needs the OCP bindings (pip install cadquery-ocp).
"""

import sys, os, collections
from OCP.STEPControl import STEPControl_Reader
from OCP.TopExp import TopExp_Explorer, TopExp
from OCP.TopAbs import TopAbs_FACE, TopAbs_EDGE
from OCP.TopoDS import TopoDS
from OCP.TopTools import TopTools_IndexedDataMapOfShapeListOfShape
from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Surface, BRepAdaptor_Curve
from OCP.GeomAPI import GeomAPI_ProjectPointOnSurf
from OCP.GeomAbs import (GeomAbs_Plane, GeomAbs_Cylinder, GeomAbs_Cone,
                         GeomAbs_Sphere, GeomAbs_Torus, GeomAbs_BSplineSurface,
                         GeomAbs_Line, GeomAbs_Circle, GeomAbs_Ellipse,
                         GeomAbs_BSplineCurve)
SK={GeomAbs_Plane:"plane",GeomAbs_Cylinder:"cyl",GeomAbs_Cone:"cone",
    GeomAbs_Sphere:"sph",GeomAbs_Torus:"tor",GeomAbs_BSplineSurface:"bspl"}
CK={GeomAbs_Line:"line",GeomAbs_Circle:"circ",GeomAbs_Ellipse:"ell",
    GeomAbs_BSplineCurve:"bspl"}
N=12
for path in sys.argv[1:]:
    r=STEPControl_Reader(); r.ReadFile(path); r.TransferRoots(); shape=r.OneShape()
    m=TopTools_IndexedDataMapOfShapeListOfShape()
    TopExp.MapShapesAndAncestors_s(shape,TopAbs_EDGE,TopAbs_FACE,m)
    pairs=collections.Counter(); worst=collections.defaultdict(float); flagged=collections.Counter()
    tot=0
    for i in range(1,m.Extent()+1):
        e=TopoDS.Edge_s(m.FindKey(i)); faces=list(m.FindFromIndex(i))
        ck=CK.get(BRepAdaptor_Curve(e).GetType(),"other")
        ac=BRepAdaptor_Curve(e); u0,u1=ac.FirstParameter(),ac.LastParameter(); c=ac
        if u1<=u0: continue
        tot+=1
        for fs in faces:
            f=TopoDS.Face_s(fs); sk=SK.get(BRepAdaptor_Surface(f).GetType(),"other")
            surf=BRep_Tool.Surface_s(f); d=0.0
            for k in range(N+1):
                p=c.Value(u0+(u1-u0)*k/N)
                pr=GeomAPI_ProjectPointOnSurf(p,surf)
                if pr.NbPoints()>0: d=max(d,pr.LowerDistance())
            key="%s-on-%s"%(ck,sk); pairs[key]+=1
            worst[key]=max(worst[key],d)
            if d>1e-4: flagged[key]+=1
    print("== %s  (%d edges)"%(os.path.basename(path),tot))
    for k in sorted(worst,key=lambda x:-worst[x]):
        print("   %-14s n=%-5d off>1e-4: %-5d worst %9.6f"%(k,pairs[k],flagged[k],worst[k]))
