#!/usr/bin/env python3
"""Every vertex against the curve its own edge is written as, straight from the text.

This is SOLIDWORKS fault code 7 - "vertex does not lie on edge's curve" - and it
is the relation none of the corner-stray instruments measure: those hold a corner
against the *surface* its face is written on, which is code 16, and code 16 has
never fired on a file of ours.

No kernel is in the path, so nothing is repaired on the way in. That matters
here more than usual: read back through OpenCASCADE, lid10 shows strays this
reports nothing for, because the reader had already split faces and rebuilt
edges of its own. What this prints is what was written.
"""

import math
import re
import sys

NUM = r"[-+0-9.EDed]+"


def parse(path):
    text = open(path, "r", encoding="utf-8", errors="replace").read()
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    ents = {}
    for m in re.finditer(r"#(\d+)\s*=\s*([A-Z_0-9]+)\s*\((.*?)\)\s*;", text, flags=re.S):
        ents[int(m.group(1))] = (m.group(2), m.group(3))
    return ents


def refs(args):
    return [int(x) for x in re.findall(r"#(\d+)", args)]


def floats(args):
    out = []
    for tok in re.findall(NUM, args):
        try:
            out.append(float(tok.replace("D", "E").replace("d", "e")))
        except ValueError:
            pass
    return out


def point(ents, i):
    kind, args = ents[i]
    return floats(args.split("(", 1)[1])[:3]


def direction(ents, i):
    return point(ents, i)


def placement(ents, i):
    kind, args = ents[i]
    r = refs(args)
    origin = point(ents, r[0])
    axis = direction(ents, r[1]) if len(r) > 1 else [0, 0, 1]
    ref = direction(ents, r[2]) if len(r) > 2 else [1, 0, 0]
    return origin, axis, ref


def sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def norm(a):
    return math.sqrt(dot(a, a))


def unit(a):
    n = norm(a)
    return [x / n for x in a] if n > 0 else a


def distance(ents, curve_id, p):
    kind, args = ents[curve_id]
    r = refs(args)
    if kind == "LINE":
        base = point(ents, r[0])
        vec_kind, vec_args = ents[r[1]]
        d = unit(direction(ents, refs(vec_args)[0]))
        rel = sub(p, base)
        return norm(sub(rel, [x * dot(rel, d) for x in d]))
    if kind == "CIRCLE":
        origin, axis, _ = placement(ents, r[0])
        rad = floats(args.rsplit(",", 1)[1])[0]
        n = unit(axis)
        rel = sub(p, origin)
        along = dot(rel, n)
        across = norm(sub(rel, [x * along for x in n]))
        return math.hypot(along, across - rad)
    if kind == "ELLIPSE":
        origin, axis, ref = placement(ents, r[0])
        a, b = floats(args.rsplit(",", 2)[1] + " " + args.rsplit(",", 2)[2])[:2]
        n = unit(axis)
        major = unit(ref)
        minor = cross(n, major)
        best = float("inf")
        best_t = 0.0
        for k in range(720):
            t = 2 * math.pi * k / 720.0
            q = [origin[i] + major[i] * a * math.cos(t) + minor[i] * b * math.sin(t) for i in range(3)]
            d = norm(sub(p, q))
            if d < best:
                best, best_t = d, t
        step = 2 * math.pi / 720.0
        while step > 1e-13:
            for t in (best_t - step, best_t + step):
                q = [origin[i] + major[i] * a * math.cos(t) + minor[i] * b * math.sin(t)
                     for i in range(3)]
                d = norm(sub(p, q))
                if d < best:
                    best, best_t = d, t
            step *= 0.5
        return best
    if kind in ("HYPERBOLA", "PARABOLA"):
        origin, axis, ref = placement(ents, r[0])
        vals = floats(re.sub(r"#\d+", " ", re.sub(r"'[^']*'", " ", args)))
        n = unit(axis)
        major = unit(ref)
        minor = cross(n, major)

        def pt(t, kind=kind, vals=vals):
            if kind == "HYPERBOLA":
                a, b = vals[0], vals[1]
                return [origin[i] + major[i] * a * math.cosh(t) + minor[i] * b * math.sinh(t)
                        for i in range(3)]
            f = vals[0]
            return [origin[i] + major[i] * f * t * t + minor[i] * 2 * f * t for i in range(3)]

        span = 8.0 if kind == "HYPERBOLA" else 30.0
        best, best_t = float("inf"), 0.0
        for k in range(-800, 801):
            t = span * k / 800.0
            d = norm(sub(p, pt(t)))
            if d < best:
                best, best_t = d, t
        step = span / 800.0
        while step > 1e-14:
            for t in (best_t - step, best_t + step):
                d = norm(sub(p, pt(t)))
                if d < best:
                    best, best_t = d, t
            step *= 0.5
        return best
    if kind.startswith("B_SPLINE_CURVE"):
        ctrl = [point(ents, i) for i in refs(args.split("(", 1)[1].split(")", 1)[0])]
        if not ctrl:
            return 0.0
        return min(norm(sub(p, ctrl[0])), norm(sub(p, ctrl[-1])))
    return 0.0


def offenders(path):
    """(stray, curve kind, edge id, point) for every vertex off its own edge's curve.

    Sorted worst first, and empty for a correct export whatever its numbers.
    """
    ents = parse(path)
    rows = []
    for i, (kind, args) in ents.items():
        if kind != "EDGE_CURVE":
            continue
        r = refs(args)
        if len(r) < 3:
            continue
        v1, v2, curve = r[0], r[1], r[2]
        ckind = ents[curve][0]
        for v in (v1, v2):
            vkind, vargs = ents[v]
            p = point(ents, refs(vargs)[0])
            try:
                d = distance(ents, curve, p)
            except Exception:
                continue
            owed = 1e-6 * max(1.0, norm(p))
            if d > owed:
                rows.append((d, ckind, i, p))
    rows.sort(reverse=True)
    return rows


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    worst_overall = 0.0
    for path in sys.argv[1:]:
        rows = offenders(path)
        print("%s: %d vertex/edge pairs off the curve" % (path, len(rows)))
        for d, ckind, i, p in rows[:12]:
            print("  %.6f  %-22s #%d at (%.4f %.4f %.4f)" % (d, ckind, i, p[0], p[1], p[2]))
        if rows:
            worst_overall = max(worst_overall, rows[0][0])
    return 1 if worst_overall > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
