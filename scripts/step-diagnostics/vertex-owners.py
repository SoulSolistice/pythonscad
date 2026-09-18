#!/usr/bin/env python3
"""Every vertex against every surface whose face uses it - are its owners all satisfied?

SOLIDWORKS code 16 is "vertex does not lie on face's surface", asked once per
face at the vertex. A corner the placement put on the crossing of two surfaces is
exact for those two and says nothing about a third face using the same corner.

Analytic surfaces are measured exactly here (plane, cylinder, cone, sphere).
A B-spline surface is reported as "not measured" rather than guessed at.
"""

import math
import os
import sys
from importlib.machinery import SourceFileLoader

_here = os.path.dirname(os.path.abspath(__file__))
voc = SourceFileLoader(
    "vertex_on_curve",
    os.path.join(_here, "vertex-on-curve.py")).load_module()


def dist_to_surface(ents, sid, p):
    kind, args = ents[sid]
    r = voc.refs(args)
    if kind == "PLANE":
        origin, axis, _ = voc.placement(ents, r[0])
        n = voc.unit(axis)
        return abs(voc.dot(voc.sub(p, origin), n)), "plane"
    if kind in ("CYLINDRICAL_SURFACE", "CONICAL_SURFACE", "SPHERICAL_SURFACE"):
        origin, axis, _ = voc.placement(ents, r[0])
        n = voc.unit(axis)
        tail = voc.floats(args.rsplit(",", 2)[-2] + " " + args.rsplit(",", 2)[-1])
        rel = voc.sub(p, origin)
        along = voc.dot(rel, n)
        across = voc.norm(voc.sub(rel, [x * along for x in n]))
        if kind == "CYLINDRICAL_SURFACE":
            return abs(across - tail[-1]), "cylinder"
        if kind == "SPHERICAL_SURFACE":
            return abs(voc.norm(rel) - tail[-1]), "sphere"
        rad, half = tail[-2], tail[-1]
        want = rad + along * math.tan(half)
        # distance to the cone, measured along its own normal
        return abs(across - want) * math.cos(half), "cone"
    return None, kind.lower()


def face_map(ents):
    """vertex id -> [(face id, surface id)]"""
    oriented = {}
    for i, (kind, args) in ents.items():
        if kind == "ORIENTED_EDGE":
            r = voc.refs(args)
            if r:
                oriented.setdefault(i, r[0])
    loops = {}
    for i, (kind, args) in ents.items():
        if kind == "EDGE_LOOP":
            loops[i] = voc.refs(args)
    bounds = {}
    for i, (kind, args) in ents.items():
        if kind.endswith("FACE_BOUND") or kind.endswith("FACE_OUTER_BOUND"):
            r = voc.refs(args)
            if r:
                bounds[i] = r[0]
    out = {}
    for i, (kind, args) in ents.items():
        if kind != "ADVANCED_FACE":
            continue
        r = voc.refs(args)
        surf = r[-1]
        for b in r[:-1]:
            lp = bounds.get(b)
            if lp is None:
                continue
            for o in loops.get(lp, []):
                ec = oriented.get(o)
                if ec is None or ec not in ents:
                    continue
                er = voc.refs(ents[ec][1])
                for v in er[:2]:
                    out.setdefault(v, set()).add((i, surf))
    return out


def main():
    path = sys.argv[1]
    ents = voc.parse(path)
    owners = face_map(ents)
    targets = []
    if len(sys.argv) > 2:
        want = [float(x) for x in sys.argv[2:5]]
        for v, fs in owners.items():
            p = voc.point(ents, voc.refs(ents[v][1])[0])
            if math.dist(p, want) < 1e-4:
                targets.append(v)
        for v in targets:
            p = voc.point(ents, voc.refs(ents[v][1])[0])
            print("vertex #%d at (%.6f %.6f %.6f), used by %d faces"
                  % (v, p[0], p[1], p[2], len(owners[v])))
            for f, s in sorted(owners[v]):
                d, kind = dist_to_surface(ents, s, p)
                print("   face #%-6d %-24s %s"
                      % (f, kind, "off by %.3e" % d if d is not None else "not measured"))
        return 0

    worst = []
    for v, fs in owners.items():
        p = voc.point(ents, voc.refs(ents[v][1])[0])
        for f, s in fs:
            d, kind = dist_to_surface(ents, s, p)
            if d is None:
                continue
            if d > 1e-6 * max(1.0, voc.norm(p)):
                worst.append((d, v, f, kind, p))
    worst.sort(reverse=True)
    print("%s: %d vertex/face pairs where the vertex is off the face's own surface" % (path, len(worst)))
    seen = set()
    for d, v, f, kind, p in worst:
        if v in seen:
            continue
        seen.add(v)
        others = len(owners[v])
        print("  %.6f  vertex #%-6d on a %-9s face #%-6d  (%d faces at it)  (%.4f %.4f %.4f)"
              % (d, v, kind, f, others, p[0], p[1], p[2]))
        if len(seen) >= 15:
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
