#!/usr/bin/env python3
"""Where the conics cut by a chord plane come from.

PromoteCurves reports conics written "by a plane nothing declares - of which N
are facets of something curved, so the conic is the wrong curve". This finds
each of them in the file - an ELLIPSE, HYPERBOLA or PARABOLA edge between a
PLANE face and a CYLINDRICAL or CONICAL one, where every corner of the plane
face lies on that same quadric, so the plane is a chord facet of it - matches
the plane face back to its mesh facet by centroid, and says what the claim
ledger and the existence table record for that facet: who claimed it, what
became of the claim and why, and whether the model has a face there at all.

    python conic-chord-origin.py <file.stp> <file.ledger.tsv>

The ledger and its `.existence.tsv` are what `step-dev.sh measure` writes beside
each export (OPENSCAD_STEP_LEDGER). No kernel in the path.
"""

import collections
import csv
import importlib.util
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("voc", os.path.join(HERE, "vertex-on-curve.py"))
voc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(voc)
parse, refs, floats, point, placement = voc.parse, voc.refs, voc.floats, voc.point, voc.placement
sub, dot, norm, unit = voc.sub, voc.dot, voc.norm, voc.unit

CONICS = {"ELLIPSE", "HYPERBOLA", "PARABOLA"}


def face_parts(ents, face_id):
    """The surface id and every EDGE_CURVE id of an ADVANCED_FACE, in order."""
    kind, args = ents[face_id]
    r = refs(args)
    surface = r[-1]
    edges = []
    for b in r[:-1]:
        bk, bargs = ents[b]
        if bk not in ("FACE_OUTER_BOUND", "FACE_BOUND"):
            continue
        loop = refs(bargs)[0]
        for oe in refs(ents[loop][1]):
            ok, oargs = ents[oe]
            if ok == "ORIENTED_EDGE":
                edges.append(refs(oargs)[-1])
    return surface, edges


def edge_ends(ents, e):
    r = refs(ents[e][1])
    return [point(ents, refs(ents[r[0]][1])[0]), point(ents, refs(ents[r[1]][1])[0])], r[2]


def curve_kind(ents, c):
    kind, args = ents[c]
    if kind == "SURFACE_CURVE":
        return curve_kind(ents, refs(args)[0])
    return kind


def quadric_normal(ents, s, p):
    """The quadric's unit normal at (or nearest to) p, in closed form."""
    kind, args = ents[s]
    origin, axis, _ = placement(ents, refs(args)[0])
    axis = unit(axis)
    rel = sub(p, origin)
    h = dot(rel, axis)
    radial = unit([rel[i] - axis[i] * h for i in range(3)])
    if kind == "CONICAL_SURFACE":
        half = floats(args.split(",", 2)[2])[1]
        return unit([radial[i] * math.cos(half) - axis[i] * math.sin(half) for i in range(3)])
    return radial


def plane_normal(ents, s):
    kind, args = ents[s]
    return unit(placement(ents, refs(args)[0])[1])


def off_quadric(ents, s, p):
    kind, args = ents[s]
    origin, axis, _ = placement(ents, refs(args)[0])
    axis = unit(axis)
    vals = floats(args.split(",", 2)[2]) if args.count(",") >= 2 else []
    rel = sub(p, origin)
    h = dot(rel, axis)
    radial = norm([rel[i] - axis[i] * h for i in range(3)])
    if kind == "CYLINDRICAL_SURFACE":
        return abs(radial - vals[0])
    if kind == "CONICAL_SURFACE":
        r0, half = vals[0], vals[1]
        return abs(radial - (r0 + h * math.tan(half))) * math.cos(half)
    return math.inf


def main(stp, ledger):
    ents = parse(stp)
    faces = [i for i, (k, _) in ents.items() if k == "ADVANCED_FACE"]
    faces_of_edge = collections.defaultdict(list)
    parts = {}
    for f in faces:
        parts[f] = face_parts(ents, f)
        for e in parts[f][1]:
            faces_of_edge[e].append(f)

    # the facets, by centroid, from the ledger and the existence table
    centroid, claims = {}, collections.defaultdict(list)
    with open(ledger, encoding="utf-8", newline="") as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            centroid[r["facet"]] = (float(r["x"]), float(r["y"]), float(r["z"]))
            claims[r["facet"]].append(r)
    verdict = {}
    ex = ledger + ".existence.tsv"
    if os.path.exists(ex):
        with open(ex, encoding="utf-8", newline="") as fh:
            for r in csv.DictReader(fh, delimiter="\t"):
                verdict[r["facet"]] = (r["verdict"], r["made_on"], r["by"], r["open"])
                centroid.setdefault(r["facet"], (float(r["x"]), float(r["y"]), float(r["z"])))

    found = collections.Counter()
    rows = []
    for e, fs in faces_of_edge.items():
        ends, curve = edge_ends(ents, e)
        if curve_kind(ents, curve) not in CONICS or len(fs) != 2:
            continue
        kinds = [ents[parts[f][0]][0] for f in fs]
        if "PLANE" not in kinds or not any(k in ("CYLINDRICAL_SURFACE", "CONICAL_SURFACE") for k in kinds):
            continue
        plane_face = fs[kinds.index("PLANE")]
        quad = parts[fs[1 - kinds.index("PLANE")]][0]
        corners = []
        for pe in parts[plane_face][1]:
            corners += edge_ends(ents, pe)[0]
        # What the exporter's planeIsFacetOfCurved measures - the plane's normal
        # within 30 degrees of the quadric's at the edge - and what would make
        # the plane a chord facet of it exactly: every corner on the quadric.
        mid = [(ends[0][i] + ends[1][i]) / 2 for i in range(3)]
        angle = math.degrees(math.acos(min(1.0, abs(dot(plane_normal(ents, parts[plane_face][0]),
                                                            quadric_normal(ents, quad, mid))))))
        if angle >= 30.0:
            continue  # the exporter does not count it either
        worst = max(off_quadric(ents, quad, c) for c in corners)
        on_all = worst <= 1e-6 * max(1.0, max(norm(c) for c in corners))
        c = [sum(p[i] for p in corners) / len(corners) for i in range(3)]
        facet = min(centroid, key=lambda k: norm(sub(centroid[k], c))) if centroid else "?"
        gap = norm(sub(centroid[facet], c)) if centroid else math.inf
        v = verdict.get(facet, ("-", "-", "-", "-"))
        cl = claims.get(facet, [])
        lost = [x for x in cl if x["fate"] != "honoured"]
        why = "; ".join(sorted({f"{x['by']} {x['evidence']} {x['fate']}: {x['why']}" for x in lost})) or "no claim"
        shape = "all corners on it" if on_all else f"corners off it"
        found[(shape, v[0], why)] += 1
        rows.append((facet, gap, v, why, angle, worst, len(corners) // 2))

    print(f"{len(rows)} conic edges between a quadric face and a plane within 30 degrees of tangent to it")
    for (shape, ver, why), n in found.most_common():
        print(f"{n:5d}  {shape:18s} existence {ver:10s}  {why}")
    if "-v" in sys.argv:
        for facet, gap, v, why, angle, worst, nc in sorted(rows, key=lambda r: r[4]):
            print(f"  facet {facet} (matched {gap:.2e} off, {nc} corners)  {angle:6.2f} deg  "
                  f"worst corner {worst:.3e} off  existence {v}  {why}")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "-v"]
    main(args[0], args[1])
