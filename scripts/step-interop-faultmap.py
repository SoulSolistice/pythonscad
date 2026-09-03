#!/usr/bin/env python3
"""Map the faces a CAD system calls faulty back onto the geometry we wrote.

`scripts/step-interop-solidworks.ps1 -FaultDetail <tsv>` writes one row per
faulty entity: what kind of surface it is, its area, and where its bounding box
centres. This reads that beside the STEP file it came from and says which of
*our* faces those are - by surface type, by area, and by how they sit relative to
the analytic faces in the export.

Why it exists. On the bayonet, three variants that differ only in coordinates
are ordered by SOLIDWORKS as 1, 0 and 83 faulty faces, and **every** geometric
measure available from our side orders them the other way or not at all: the
83-fault file has seven times fewer off-surface boundary vertices than the
1-fault file, no slivers, no off-plane faces, no vertex off its edge, and
identical topology. See doc/step-interop-validation.md. When nothing you can
measure explains a verdict, the next move is to ask the system which entities it
means rather than to keep proposing mechanisms.

Matching is by bounding-box centre, to a tolerance, because that is all the two
sides share - SOLIDWORKS' face ordering is its own. A row that matches nothing is
reported rather than dropped; a centre that matches several is reported as
ambiguous. Neither is silently resolved.

Usage:

    python3 scripts/step-interop-faultmap.py --tsv build/interop-kit/bayonet-faults.tsv \\
        --step build/interop-kit/r02-bayonet-analytic.stp
"""

import argparse
import collections
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tests"))


def our_faces(path):
    """Every face we wrote: kind, area, bounding-box centre."""
    from OCP.STEPControl import STEPControl_Reader
    from OCP.IFSelect import IFSelect_ReturnStatus
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopAbs import TopAbs_FACE
    from OCP.TopoDS import TopoDS
    from OCP.BRepAdaptor import BRepAdaptor_Surface
    from OCP.BRepGProp import BRepGProp
    from OCP.GProp import GProp_GProps
    from OCP.Bnd import Bnd_Box
    from OCP.BRepBndLib import BRepBndLib

    reader = STEPControl_Reader()
    if reader.ReadFile(os.path.abspath(path)) != IFSelect_ReturnStatus.IFSelect_RetDone:
        return []
    reader.TransferRoots()
    shape = reader.OneShape()
    out = []
    exp = TopExp_Explorer(shape, TopAbs_FACE)
    while exp.More():
        face = TopoDS.Face_s(exp.Current())
        kind = str(BRepAdaptor_Surface(face).GetType()).rsplit("_", 1)[-1]
        props = GProp_GProps()
        BRepGProp.SurfaceProperties_s(face, props)
        box = Bnd_Box()
        BRepBndLib.Add_s(face, box)
        xa, ya, za, xb, yb, zb = box.Get()
        out.append({
            "kind": kind, "area": props.Mass(),
            "centre": ((xa + xb) / 2, (ya + yb) / 2, (za + zb) / 2),
        })
        exp.Next()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tsv", required=True, help="the -FaultDetail file")
    ap.add_argument("--step", required=True, help="the STEP file it was produced from")
    ap.add_argument("--tol", type=float, default=0.05,
                    help="how close two bounding-box centres must be to be the same face (mm)")
    args = ap.parse_args()

    ours = our_faces(args.step)
    if not ours:
        print("could not read %s" % args.step, file=sys.stderr)
        return 2

    rows = []
    with open(args.tsv, newline="", encoding="utf-8-sig") as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            if row.get("entity") == "face":
                rows.append(row)
    if not rows:
        print("no faulty faces in %s" % args.tsv)
        return 0

    by_kind = collections.Counter()
    by_our_kind = collections.Counter()
    codes = collections.Counter()
    unmatched = ambiguous = 0
    matched_areas = []
    for row in rows:
        by_kind[row["kind"]] += 1
        for c in (row.get("codes") or "").split("/"):
            if c:
                codes[c] += 1
        try:
            cx, cy, cz = (float(v) for v in row["centre_mm"].split(";"))
        except (ValueError, KeyError):
            unmatched += 1
            continue
        near = [f for f in ours
                if abs(f["centre"][0] - cx) < args.tol
                and abs(f["centre"][1] - cy) < args.tol
                and abs(f["centre"][2] - cz) < args.tol]
        if not near:
            unmatched += 1
        elif len(near) > 1:
            ambiguous += 1
        else:
            by_our_kind[near[0]["kind"]] += 1
            matched_areas.append(near[0]["area"])

    print("%d faulty faces in %s\n" % (len(rows), os.path.basename(args.tsv)))
    print("as SOLIDWORKS classifies them:")
    for k, n in by_kind.most_common():
        print("   %-12s %d" % (k, n))
    print("\nerror codes:")
    for c, n in codes.most_common():
        print("   %-6s %d" % (c, n))
    print("\nmatched onto the faces we wrote (bounding-box centre, tol %.3f mm):" % args.tol)
    for k, n in by_our_kind.most_common():
        print("   %-16s %d" % (k, n))
    if matched_areas:
        matched_areas.sort()
        print("   area of the matched faces: min %.5f  median %.5f  max %.5f"
              % (matched_areas[0], matched_areas[len(matched_areas) // 2], matched_areas[-1]))
    if unmatched or ambiguous:
        print("\n   %d matched nothing, %d matched more than one - reported, not resolved"
              % (unmatched, ambiguous))
    return 0


if __name__ == "__main__":
    sys.exit(main())
