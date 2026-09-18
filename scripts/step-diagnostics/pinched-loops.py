#!/usr/bin/env python3
"""Faces whose own loop visits a vertex more than twice, and faces with a degenerate edge.

    python scripts/step-diagnostics/pinched-loops.py file.stp

Lists what `fault-census.py` counts in its code-21 row, face by face, so the
pinches can be found in the file. Faces on closed surfaces are excluded from
that list for the same reason the census excludes them.

A loop that returns to the same vertex pinches the face there: the two sides of
the pinch touch, which is what a reader calls "face is self intersecting"
(SOLIDWORKS code 21). A thread's run-in is where a sweep's profile shrinks to
nothing, so it is where to expect one.

Also reports edges whose two vertices are the same point, and faces with an
extremely thin loop - the visual "degenerated run-in".
"""

import math
import os
import sys
from importlib.machinery import SourceFileLoader

_here = os.path.dirname(os.path.abspath(__file__))
voc = SourceFileLoader(
    "vertex_on_curve",
    os.path.join(_here, "vertex-on-curve.py")).load_module()
seam = SourceFileLoader("seam_straddle", os.path.join(_here, "seam-straddle.py")).load_module()

# A face on a closed surface carries its seam edge twice, so a vertex on that
# seam is visited four times by a perfectly legal loop. The census excludes
# these and so does this, or the two would disagree about what a pinch is.
CLOSED = ("CYLINDRICAL_SURFACE", "CONICAL_SURFACE", "SPHERICAL_SURFACE", "TOROIDAL_SURFACE")


def main():
    path = sys.argv[1]
    ents = voc.parse(path)
    oriented = {i: voc.refs(a)[0] for i, (k, a) in ents.items() if k == "ORIENTED_EDGE" and voc.refs(a)}
    loops = {i: voc.refs(a) for i, (k, a) in ents.items() if k == "EDGE_LOOP"}
    bounds = {i: voc.refs(a)[0] for i, (k, a) in ents.items()
              if (k.endswith("FACE_BOUND") or k.endswith("FACE_OUTER_BOUND")) and voc.refs(a)}

    def vert_point(v):
        return voc.point(ents, voc.refs(ents[v][1])[0])

    # degenerate edges: both vertices at one place
    degen = []
    for i, (k, a) in ents.items():
        if k != "EDGE_CURVE":
            continue
        r = voc.refs(a)
        if len(r) < 2:
            continue
        try:
            p1, p2 = vert_point(r[0]), vert_point(r[1])
        except Exception:
            continue
        if math.dist(p1, p2) < 1e-9:
            degen.append((i, p1))

    pinched = []
    thin = []
    for i, (k, a) in ents.items():
        if k != "ADVANCED_FACE":
            continue
        r = voc.refs(a)
        surf = ents[r[-1]][0]
        legal_seam = surf in CLOSED or seam.closed_in_v(ents, r[-1])
        for b in r[:-1]:
            lp = bounds.get(b)
            if lp is None:
                continue
            verts = []
            for o in loops.get(lp, []):
                ec = oriented.get(o)
                if ec is None or ec not in ents:
                    continue
                er = voc.refs(ents[ec][1])
                if len(er) >= 2:
                    verts.append(er[0])
                    verts.append(er[1])
            uniq = {}
            for v in verts:
                uniq[v] = uniq.get(v, 0) + 1
            # each vertex of a simple loop is shared by exactly two edges
            repeats = [v for v, n in uniq.items() if n > 2]
            if repeats and not legal_seam:
                pinched.append((i, surf, [(v, vert_point(v)) for v in repeats]))
            pts = [vert_point(v) for v in uniq]
            if len(pts) >= 3:
                cx = [sum(p[j] for p in pts) / len(pts) for j in range(3)]
                spread = max(math.dist(p, cx) for p in pts)
                closest = min(math.dist(pts[m], pts[n]) for m in range(len(pts))
                              for n in range(m + 1, len(pts)))
                if spread > 0 and closest / spread < 1e-4:
                    thin.append((i, surf, closest, spread))

    print("%s" % path)
    print("  edges whose two vertices are one point: %d" % len(degen))
    for i, p in degen[:6]:
        print("      #%-6d at (%.4f %.4f %.4f)" % (i, p[0], p[1], p[2]))
    print("  faces whose loop visits a vertex more than twice: %d" % len(pinched))
    for i, surf, repeats in pinched[:8]:
        where = ", ".join("(%.4f %.4f %.4f)" % (p[0], p[1], p[2]) for _, p in repeats[:2])
        print("      face #%-6d %-28s at %s" % (i, surf, where))
    print("  faces with two corners far closer than the face is wide: %d" % len(thin))
    for i, surf, closest, spread in thin[:8]:
        print("      face #%-6d %-28s closest %.3e over %.3f" % (i, surf, closest, spread))
    return 0


if __name__ == "__main__":
    sys.exit(main())
