#!/usr/bin/env python3
"""The same model under other vertex numbers must export the same file.

Open item 37 of doc/step-export-wip.md: a change that only perturbed the order
in which Manifold hands over its triangles and vertices - same triangles, same
positions - moved points on 27 of 120 exports. A closed band's seam turned to
another vertex of its ring, a boundary cycle started somewhere else, and every
fit that samples from where its cycle starts moved thousands of control points
and the solid with them (OCCT volume by up to 5.5e-5 relative on the bayonet).
The exporter's arbitrary choices followed the numbering and not the model -
checklist A7, order is not policy.

This exports each fixture twice per tier: once as it is, and once with
OPENSCAD_STEP_PERMUTE set, under which export_step.cc renumbers every vertex,
reorders every triangle and turns every triangle to start at another corner
before recognition sees the mesh (`permutedNumbering`). Nothing about the model
changes, so nothing in the file may:

  * the entity census - how many of each STEP entity - is the same;
  * every CARTESIAN_POINT of one file has a point of the other at the same
    place, to machine precision, and the other way round - a point is written
    at the shortest decimal that round-trips its double, so the one allowance
    is the double's own rounding, DBL_EPSILON times its magnitude, and no
    chosen number;
  * OCCT reads the same volume from both, to the rounding of summing one
    contribution per face (n_faces x DBL_EPSILON relative). Skipped, and said
    so, where OCP is not installed;
  * and, since nothing may differ, the DATA section is the same byte for byte:
    every entity under the same id. The points and the volume say whether the
    solid moved; this says whether anything is still written in an order the
    model does not fix - the numbering, or an address (checklist F9).

It derives nothing from a kernel: both sides are this exporter's, and the
assertion is that they are one.
"""

import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import steproundtrip  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EPS = sys.float_info.epsilon

TIERS = {
    "exact": ["--enable=step-analytic-surfaces"],
    "approx": ["--enable=step-analytic-surfaces", "--enable=step-approximate-surfaces"],
}

# A bore (a closed band's seam), the band family (seams, runs and fits across
# every kind of band), the extrusion refusals (the exact-tier export whose
# volume moved on 2026-09-26) and a declared grid (a fit sampled from where its
# cycle starts).
FIXTURES = [
    "tests/data/scad/step-export/step-bore.scad",
    "tests/data/scad/step-export/step-band-family.scad",
    "tests/data/scad/step-export/step-extrude-refusals.scad",
    "tests/data/scad/step-export/step-declare-grid-scad.scad",
]

# Two numberings, so that one that happens to agree with the plain run on some
# choice does not carry the test.
SEEDS = ["1", "2"]

POINT = re.compile(r"CARTESIAN_POINT\('[^']*',\s*\(([^)]*)\)\)")
ENTITY = re.compile(r"^#\d+\s*=\s*([A-Z_0-9]+)\s*\(", re.M)


def export(binary, source, target, flags, seed):
    env = dict(os.environ)
    env.pop("OPENSCAD_STEP_PERMUTE", None)
    if seed is not None:
        env["OPENSCAD_STEP_PERMUTE"] = seed
    cmd = [binary, source, "-o", target, "--trust-python"] + flags
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT, env=env)
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    data = text[text.find("DATA;"):]
    points = [tuple(float(c) for c in m.group(1).split(",")) for m in POINT.finditer(data)]
    census = collections.Counter(ENTITY.findall(data))
    return points, census, data


def first_difference(a, b):
    for i, (x, y) in enumerate(zip(a.splitlines(), b.splitlines())):
        if x != y:
            return "line %d: %s | %s" % (i + 1, x[:120], y[:120])
    return "one file is longer"


def unmatched(a, b):
    """Points of `a` with no point of `b` within one rounding of their magnitude,
    each with how far its nearest point of `b` is."""
    exact = collections.Counter(b)
    rest = []
    for p in a:
        if exact[p] > 0:
            exact[p] -= 1
        else:
            rest.append(p)
    if not rest:
        return []
    # A point is matched only against points of its own dimension: a pcurve's
    # are 2D, in a surface's parameters, and a model point's 3D.
    # Against what `b` has left over; where nothing of a dimension is left - the
    # counts differ - against all of it.
    left, whole = collections.defaultdict(list), collections.defaultdict(list)
    for q, n in exact.items():
        left[len(q)].extend([q] * n)
    for q in b:
        whole[len(q)].append(q)
    pools = {dim: left[dim] or whole[dim] for dim in whole}
    out = []
    try:
        import numpy
        arrays = {dim: numpy.array(pool) for dim, pool in pools.items()}
        for p in rest:
            pool = pools.get(len(p))
            if not pool:
                out.append((float("inf"), p, p))
                continue
            d2 = ((arrays[len(p)] - numpy.array(p)) ** 2).sum(axis=1)
            i = int(d2.argmin())
            out.append((float(d2[i]) ** 0.5, p, pool[i]))
    except ImportError:
        for p in rest:
            pool = pools.get(len(p))
            if not pool:
                out.append((float("inf"), p, p))
                continue
            best = min(pool, key=lambda q: sum((x - y) ** 2 for x, y in zip(p, q)))
            out.append((sum((x - y) ** 2 for x, y in zip(p, best)) ** 0.5, p, best))
    moved = []
    for d, p, q in out:
        scale = max(max(abs(x) for x in p), max(abs(y) for y in q))
        if d > EPS * scale:
            moved.append((d, p))
    return sorted(moved, reverse=True)


def volume(path):
    if not steproundtrip.available():
        return None, None
    reader = steproundtrip.STEPControl_Reader()
    if reader.ReadFile(path) != steproundtrip.IFSelect_ReturnStatus.IFSelect_RetDone:
        return None, None
    reader.TransferRoots()
    shape = reader.OneShape()
    props = steproundtrip.GProp_GProps()
    # The same rule steproundtrip.py reads a fixture's volume with.
    steproundtrip.BRepGProp.VolumeProperties_s(shape, props, 1e-9, False, False)
    return props.Mass(), steproundtrip._count(shape, steproundtrip.TopAbs_FACE)


def check(binary, fixture, tier, workdir):
    name = os.path.splitext(os.path.basename(fixture))[0]
    failures = []
    plain = os.path.join(workdir, "%s-%s.stp" % (name, tier))
    rc, log = export(binary, fixture, plain, TIERS[tier], None)
    if rc != 0 or not os.path.exists(plain):
        return ["%s %s: plain export failed (exit %d)\n%s" % (name, tier, rc, log[-2000:])]
    points, census, data = read(plain)
    vol, faces = volume(plain)
    for seed in SEEDS:
        label = "%s %s, numbering %s" % (name, tier, seed)
        other = os.path.join(workdir, "%s-%s-p%s.stp" % (name, tier, seed))
        rc, log = export(binary, fixture, other, TIERS[tier], seed)
        if rc != 0 or not os.path.exists(other):
            failures.append("%s: export failed (exit %d)\n%s" % (label, rc, log[-2000:]))
            continue
        opoints, ocensus, odata = read(other)
        if data != odata:
            failures.append("%s: the DATA section is not the same, byte for byte - first at %s"
                            % (label, first_difference(data, odata)))
        if census != ocensus:
            diff = {k: (census.get(k, 0), ocensus.get(k, 0)) for k in set(census) | set(ocensus)
                    if census.get(k, 0) != ocensus.get(k, 0)}
            failures.append("%s: entity census differs %s" % (label, sorted(diff.items())))
        gone = unmatched(points, opoints)
        new = unmatched(opoints, points)
        if gone or new:
            worst = max([d for d, _ in gone + new])
            failures.append("%s: %d of %d points moved (%d new), furthest %.3g, e.g. %s"
                            % (label, len(gone), len(points), len(new), worst,
                               (gone or new)[0][1]))
        ovol, _ = volume(other)
        if vol is None or ovol is None:
            print("%s: volume SKIPPED - OCP not installed or the file unreadable" % label)
            continue
        rel = abs(ovol - vol) / abs(vol) if vol else abs(ovol - vol)
        bound = faces * EPS
        print("%s: %d points, volume %.9f against %.9f (relative %.3g, bound %.3g)"
              % (label, len(points), vol, ovol, rel, bound))
        if rel > bound:
            failures.append("%s: OCCT volume %.12g against %.12g, relative %.3g over %d faces"
                            % (label, vol, ovol, rel, faces))
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--openscad", required=True)
    parser.add_argument("--keep", help="write the exports here and keep them")
    parser.add_argument("fixtures", nargs="*", help="override the fixture list")
    args = parser.parse_args()

    fixtures = args.fixtures or FIXTURES
    workdir = args.keep or tempfile.mkdtemp(prefix="step-numbering-")
    os.makedirs(workdir, exist_ok=True)
    failures = []
    for fixture in fixtures:
        for tier in TIERS:
            failures += check(args.openscad, fixture, tier, workdir)
    for f in failures:
        print("FAIL " + f)
    if failures:
        print("%d failure(s): the export is not the same under another numbering of the same mesh"
              % len(failures))
        return 1
    print("every export is the same under %d other numberings" % len(SEEDS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
