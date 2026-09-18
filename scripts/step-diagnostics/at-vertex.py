#!/usr/bin/env python3
"""What we wrote around one point: the vertex, its edges, and the faces using them.

Text only, no kernel: this is what the reader was handed, before anything healed
it.
"""

import re
import sys


def parse(path):
    text = open(path, "r", encoding="utf-8", errors="replace").read()
    ents = {}
    for m in re.finditer(r"#(\d+)\s*=\s*([A-Z_0-9]+)\s*\((.*?)\)\s*;", text, flags=re.S):
        ents[int(m.group(1))] = (m.group(2), m.group(3))
    return ents


def refs(a):
    return [int(x) for x in re.findall(r"#(\d+)", a)]


def nums(a):
    return [float(x) for x in re.findall(r"[-+]?[0-9]*\.?[0-9]+(?:[EeDd][-+]?[0-9]+)?", a)]


def main():
    path, x, y, z = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
    tol = float(sys.argv[5]) if len(sys.argv) > 5 else 1e-6
    ents = parse(path)

    # points at that place, and the vertices on them
    pts = set()
    for i, (kind, args) in ents.items():
        if kind != "CARTESIAN_POINT":
            continue
        v = nums(args.split("(", 1)[1])[:3]
        if len(v) == 3 and abs(v[0] - x) <= tol and abs(v[1] - y) <= tol and abs(v[2] - z) <= tol:
            pts.add(i)
    verts = {i for i, (k, a) in ents.items() if k == "VERTEX_POINT" and set(refs(a)) & pts}
    print("points there: %s   vertices on them: %s" % (sorted(pts), sorted(verts)))

    edges = {}
    for i, (kind, args) in ents.items():
        if kind != "EDGE_CURVE":
            continue
        r = refs(args)
        if len(r) >= 3 and (r[0] in verts or r[1] in verts):
            edges[i] = r

    # who uses each edge, and on what surface
    oriented = {}
    for i, (kind, args) in ents.items():
        if kind == "ORIENTED_EDGE":
            r = refs(args)
            if r:
                oriented.setdefault(r[0], []).append(i)
    loops = {}
    for i, (kind, args) in ents.items():
        if kind == "EDGE_LOOP":
            for o in refs(args):
                loops.setdefault(o, []).append(i)
    bounds = {}
    for i, (kind, args) in ents.items():
        if kind.endswith("FACE_BOUND") or kind.endswith("FACE_OUTER_BOUND"):
            for lp in refs(args):
                bounds.setdefault(lp, []).append(i)
    faces = {}
    for i, (kind, args) in ents.items():
        if kind == "ADVANCED_FACE":
            r = refs(args)
            for b in r[:-1]:
                faces.setdefault(b, []).append((i, r[-1]))

    print("\nedges at that vertex: %d" % len(edges))
    for e, r in sorted(edges.items()):
        ckind, cargs = ents[r[2]]
        base = ckind
        if ckind == "SURFACE_CURVE":
            cr = refs(cargs)
            base = "%s on %s" % (ents[cr[0]][0], ",".join(ents[q][0] for q in cr[1:]))
        users = []
        for o in oriented.get(e, []):
            for lp in loops.get(o, []):
                for b in bounds.get(lp, []):
                    for f, surf in faces.get(b, []):
                        users.append("#%d %s" % (f, ents[surf][0]))
        p1 = nums(ents[refs(ents[r[0]][1])[0]][1].split("(", 1)[1])[:3]
        p2 = nums(ents[refs(ents[r[1]][1])[0]][1].split("(", 1)[1])[:3]
        print("  #%-6d %-28s (%.4f %.4f %.4f) -> (%.4f %.4f %.4f)"
              % (e, base, p1[0], p1[1], p1[2], p2[0], p2[1], p2[2]))
        print("          used by %d: %s" % (len(users), ", ".join(users) if users else "nobody"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
