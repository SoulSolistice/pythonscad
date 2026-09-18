#!/usr/bin/env python3
"""Line edges on a sweep face: are their ends on the sweep, or only their middles off?

A crossing curve interpolates its endpoints, so it can replace a chord only if
the chord's ends are already on both surfaces. This splits the offenders by that.
"""
import sys
from OCP.STEPControl import STEPControl_Reader
from OCP.IFSelect import IFSelect_RetDone
from OCP.TopExp import TopExp_Explorer
from OCP.TopAbs import TopAbs_FACE, TopAbs_EDGE
from OCP.TopoDS import TopoDS
from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Curve, BRepAdaptor_Surface
from OCP.GeomAPI import GeomAPI_ProjectPointOnSurf
from OCP.TopTools import TopTools_IndexedDataMapOfShapeListOfShape
from OCP.TopExp import TopExp as TE

ST = {0: "plane", 1: "cyl", 2: "cone", 3: "sphere", 4: "torus", 5: "bezier", 6: "bspl"}
r = STEPControl_Reader()
assert r.ReadFile(sys.argv[1]) == IFSelect_RetDone
r.TransferRoots()
sh = r.OneShape()
m = TopTools_IndexedDataMapOfShapeListOfShape()
TE.MapShapesAndAncestors_s(sh, TopAbs_EDGE, TopAbs_FACE, m)

rows = []
e = TopExp_Explorer(sh, TopAbs_EDGE)
seen = set()
while e.More():
    ed = TopoDS.Edge_s(e.Current())
    e.Next()
    c = BRepAdaptor_Curve(ed)
    if int(c.GetType()) != 0:
        continue
    faces = [TopoDS.Face_s(f) for f in m.FindFromKey(ed)]
    kinds = [ST.get(int(BRepAdaptor_Surface(f).GetType()), "?") for f in faces]
    if "bspl" not in kinds:
        continue
    t0, t1 = c.FirstParameter(), c.LastParameter()
    key = (round(c.Value(t0).X(), 5), round(c.Value(t0).Y(), 5), round(c.Value(t1).X(), 5))
    if key in seen:
        continue
    seen.add(key)
    per = []
    for f, k in zip(faces, kinds):
        srf = BRep_Tool.Surface_s(f)

        def d(t):
            pr = GeomAPI_ProjectPointOnSurf(c.Value(t), srf)
            return pr.LowerDistance() if pr.NbPoints() > 0 else -1

        ends = max(d(t0), d(t1))
        mid = max(d(t0 + (t1 - t0) * s / 10.0) for s in range(1, 10))
        per.append((k, ends, mid))
    worst_mid = max(p[2] for p in per)
    if worst_mid <= 1e-4:
        continue
    rows.append((worst_mid, "/".join(sorted(kinds)), per, c.Value(t0), c.Value(t1)))

rows.sort(key=lambda x: -x[0])
ends_on = sum(1 for r_ in rows if all(p[1] <= 1e-6 for p in r_[2]))
print("%d line edges on a sweep face lie off a surface they bound" % len(rows))
print("  %d have both ends on every surface they bound (a crossing curve can replace them)" % ends_on)
print("  %d have an end off one of them" % (len(rows) - ends_on))
for worst, pair, per, a, b in rows[:14]:
    print("  %.4f %-12s %s   (%.3f %.3f %.3f)->(%.3f %.3f %.3f)"
          % (worst, pair, "  ".join("%s ends %.1e mid %.4f" % p for p in per),
             a.X(), a.Y(), a.Z(), b.X(), b.Y(), b.Z()))
