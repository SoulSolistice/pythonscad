#!/usr/bin/env python3
import sys, math, os
from importlib.machinery import SourceFileLoader
_here = os.path.dirname(os.path.abspath(__file__))
voc = SourceFileLoader("voc", os.path.join(_here, "vertex-on-curve.py")).load_module()
ents = voc.parse(sys.argv[1]); face = int(sys.argv[2])
oriented = {i: voc.refs(a)[0] for i,(k,a) in ents.items() if k=="ORIENTED_EDGE" and voc.refs(a)}
loops = {i: voc.refs(a) for i,(k,a) in ents.items() if k=="EDGE_LOOP"}
bounds = {i: voc.refs(a)[0] for i,(k,a) in ents.items()
          if (k.endswith("FACE_BOUND") or k.endswith("FACE_OUTER_BOUND")) and voc.refs(a)}
r = voc.refs(ents[face][1]); surf = r[-1]
print("face #%d on %s" % (face, ents[surf][0]), ents[surf][1][:110])
for b in r[:-1]:
    lp = bounds.get(b)
    print("  bound #%d loop #%s outer=%s" % (b, lp, ".T." in ents[b][1].split(",")[-1]))
    seq = []
    for o in loops.get(lp, []):
        ec = oriented.get(o)
        if ec is None: continue
        er = voc.refs(ents[ec][1])
        ck = ents[er[2]][0]
        if ck == "SURFACE_CURVE": ck = ents[voc.refs(ents[er[2]][1])[0]][0]
        p1 = voc.point(ents, voc.refs(ents[er[0]][1])[0])
        p2 = voc.point(ents, voc.refs(ents[er[1]][1])[0])
        seq.append((ec, ck, er[0], p1, er[1], p2, math.dist(p1,p2)))
    print("    %d edges" % len(seq))
    for ec, ck, v1, p1, v2, p2, ln in seq:
        print("      #%-6d %-24s len %9.5f  #%-6d (%.4f %.4f %.4f) -> #%-6d (%.4f %.4f %.4f)"
              % (ec, ck, ln, v1, p1[0], p1[1], p1[2], v2, p2[0], p2[1], p2[2]))
