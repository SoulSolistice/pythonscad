#!/usr/bin/env python3
"""Our edges whose midpoint is near a point - how a CAD system's fault centre is matched back."""
import math
import os
import re
import sys
from importlib.machinery import SourceFileLoader
av = SourceFileLoader("av", os.path.join(os.path.dirname(os.path.abspath(__file__)), "at-vertex.py")).load_module()

ents = av.parse(sys.argv[1])
target = [float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])]
tol = float(sys.argv[5]) if len(sys.argv) > 5 else 0.25
rows = []
for i, (kind, args) in ents.items():
    if kind != "EDGE_CURVE":
        continue
    r = av.refs(args)
    if len(r) < 3:
        continue
    try:
        p1 = av.nums(ents[av.refs(ents[r[0]][1])[0]][1].split("(", 1)[1])[:3]
        p2 = av.nums(ents[av.refs(ents[r[1]][1])[0]][1].split("(", 1)[1])[:3]
    except Exception:
        continue
    mid = [(p1[k] + p2[k]) / 2 for k in range(3)]
    d = math.dist(mid, target)
    if d <= tol:
        ck, ca = ents[r[2]]
        base = ck
        if ck == "SURFACE_CURVE":
            cr = av.refs(ca)
            base = ents[cr[0]][0]
        rows.append((d, i, base, p1, p2))
rows.sort()
print("our edges with a midpoint within %.3f of (%.4f %.4f %.4f): %d" % (tol, target[0], target[1], target[2], len(rows)))
for d, i, base, p1, p2 in rows[:12]:
    print("  %.4f  #%-6d %-24s (%.4f %.4f %.4f) -> (%.4f %.4f %.4f)"
          % (d, i, base, p1[0], p1[1], p1[2], p2[0], p2[1], p2[2]))
