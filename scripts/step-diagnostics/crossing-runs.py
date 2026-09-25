#!/usr/bin/env python3
"""Where two faces meet along several edges: is it one curve, or a zig-zag of many?

Two faces that share a boundary share one curve of the model - where two
surfaces cross, that is their intersection. A file may write it as one edge or
as many; what matters is whether the many still follow one curve or turn at
every vertex, which is what a CAD view draws as a zig-zag. For each pair of
faces sharing more than one edge this reports the chains of edges between them,
the vertices inside a chain that no third face uses (which a single edge could
take), the turn of the tangent at each such vertex, and how far each sits from
the two surfaces.

    crossing-runs.py <file.stp> [--min-edges N] [--kinds cone,bspl]
"""
import argparse
import math
from collections import defaultdict

from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Curve, BRepAdaptor_Surface
from OCP.GeomAPI import GeomAPI_ProjectPointOnSurf
from OCP.gp import gp_Pnt, gp_Vec
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPControl import STEPControl_Reader
from OCP.TopAbs import TopAbs_EDGE, TopAbs_FACE, TopAbs_VERTEX
from OCP.TopExp import TopExp as TE
from OCP.TopoDS import TopoDS
from OCP.TopTools import TopTools_IndexedDataMapOfShapeListOfShape, TopTools_IndexedMapOfShape

ST = {0: "plane", 1: "cyl", 2: "cone", 3: "sphere", 4: "torus", 5: "bezier", 6: "bspl",
      7: "revol", 8: "extr", 9: "offset", 10: "other"}
CT = {0: "line", 1: "circle", 2: "ellipse", 3: "hyperbola", 4: "parabola", 5: "bezier",
      6: "bspl", 7: "offset", 8: "other"}

ap = argparse.ArgumentParser()
ap.add_argument("file")
ap.add_argument("--min-edges", type=int, default=2)
ap.add_argument("--kinds", default="", help="only pairs whose two surface kinds are these, e.g. cone,bspl")
ap.add_argument("--list", type=int, default=12, help="chains to detail")
args = ap.parse_args()

r = STEPControl_Reader()
assert r.ReadFile(args.file) == IFSelect_RetDone
r.TransferRoots()
sh = r.OneShape()

faces = TopTools_IndexedMapOfShape()
TE.MapShapes_s(sh, TopAbs_FACE, faces)
edge_faces = TopTools_IndexedDataMapOfShapeListOfShape()
TE.MapShapesAndAncestors_s(sh, TopAbs_EDGE, TopAbs_FACE, edge_faces)
vert_faces = TopTools_IndexedDataMapOfShapeListOfShape()
TE.MapShapesAndAncestors_s(sh, TopAbs_VERTEX, TopAbs_FACE, vert_faces)
verts = TopTools_IndexedMapOfShape()
TE.MapShapes_s(sh, TopAbs_VERTEX, verts)


def face_id(f):
    return faces.FindIndex(f)


def kind(fi):
    return ST.get(int(BRepAdaptor_Surface(TopoDS.Face_s(faces.FindKey(fi))).GetType()), "?")


want = set(k for k in args.kinds.split(",") if k)

# edges by the pair of faces they separate
pairs = defaultdict(list)
for i in range(1, edge_faces.Extent() + 1):
    ed = TopoDS.Edge_s(edge_faces.FindKey(i))
    fs = sorted(set(face_id(f) for f in edge_faces.FindFromIndex(i)))
    if len(fs) != 2:
        continue
    pairs[tuple(fs)].append(ed)


def vid(v):
    return verts.FindIndex(v)


def ends(ed):
    v0 = TE.FirstVertex_s(ed)
    v1 = TE.LastVertex_s(ed)
    return vid(v0), vid(v1)


def tangent_at(ed, v):
    c = BRepAdaptor_Curve(ed)
    t0, t1 = c.FirstParameter(), c.LastParameter()
    first = vid(TE.FirstVertex_s(ed)) == v
    t = t0 if first else t1
    p = gp_Pnt()
    d = gp_Vec()
    c.D1(t, p, d)
    # pointing away from the vertex, into the edge
    if not first:
        d.Reverse()
    return d


def surf_dist(fi, p):
    srf = BRep_Tool.Surface_s(TopoDS.Face_s(faces.FindKey(fi)))
    pr = GeomAPI_ProjectPointOnSurf(p, srf)
    return pr.LowerDistance() if pr.NbPoints() > 0 else float("inf")


report = []
for (fa, fb), eds in pairs.items():
    if len(eds) < args.min_edges:
        continue
    ka, kb = kind(fa), kind(fb)
    if want and set((ka, kb)) != want:
        continue
    # chain the edges by shared vertices
    at = defaultdict(list)
    for k, ed in enumerate(eds):
        a, b = ends(ed)
        at[a].append(k)
        at[b].append(k)
    seen = set()
    chains = []
    for start in range(len(eds)):
        if start in seen:
            continue
        comp = []
        stack = [start]
        while stack:
            k = stack.pop()
            if k in seen:
                continue
            seen.add(k)
            comp.append(k)
            for v in ends(eds[k]):
                stack.extend(at[v])
        chains.append(comp)
    for comp in chains:
        vs = defaultdict(list)
        for k in comp:
            for v in ends(eds[k]):
                vs[v].append(k)
        inner = []
        for v, ks in vs.items():
            if len(ks) != 2:
                continue
            owners = set(face_id(f) for f in vert_faces.FindFromKey(verts.FindKey(v)))
            if owners != {fa, fb}:
                continue
            t1 = tangent_at(eds[ks[0]], v)
            t2 = tangent_at(eds[ks[1]], v)
            if t1.Magnitude() < 1e-15 or t2.Magnitude() < 1e-15:
                continue
            # a smooth pass-through has the two tangents opposite: turn 0
            turn = math.degrees(math.pi - t1.Angle(t2))
            p = BRep_Tool.Pnt_s(TopoDS.Vertex_s(verts.FindKey(v)))
            inner.append((turn, surf_dist(fa, p), surf_dist(fb, p), p))
        curves = defaultdict(int)
        for k in comp:
            curves[CT.get(int(BRepAdaptor_Curve(eds[k]).GetType()), "?")] += 1
        report.append((fa, fb, ka, kb, len(comp), dict(curves), inner))

report.sort(key=lambda r_: -r_[4])
total_edges = sum(r_[4] for r_ in report)
total_inner = sum(len(r_[6]) for r_ in report)
print("%d chains of %d edges between two faces; %d vertices inside a chain that no third face uses"
      % (len(report), total_edges, total_inner))
for fa, fb, ka, kb, n, curves, inner in report[: args.list]:
    turns = sorted(t[0] for t in inner)
    med = turns[len(turns) // 2] if turns else 0.0
    worst = turns[-1] if turns else 0.0
    off = max((max(t[1], t[2]) for t in inner), default=0.0)
    print("  faces %d/%d %s/%s: %d edges (%s), %d inner vertices, turn median %.3f max %.3f deg, "
          "vertex off a surface <= %.2e"
          % (fa, fb, ka, kb, n, " ".join("%s=%d" % kv for kv in sorted(curves.items())),
             len(inner), med, worst, off))
