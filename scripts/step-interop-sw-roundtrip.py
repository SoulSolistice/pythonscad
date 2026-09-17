#!/usr/bin/env python3
"""Compare an interop coupon with what SOLIDWORKS wrote back out of it.

`scripts/step-interop-solidworks.ps1 -RoundTrip` imports each coupon and saves
it straight back out as `<coupon>_SW.STEP`. This reads both halves of that pair
with OpenCASCADE and says what changed.

Why this exists, when the PowerShell driver already records a body type and a
face count: a body type is a verdict and this is evidence. "SOLIDWORKS made a
solid" does not say whether it kept the B-spline the coupon is about, and
`doc/step-export-development.md` has already had to record one case where it
made a solid by *healing* the file - reading our faces, discarding what it did
not like, and sewing the rest. A re-export says exactly which surfaces survived
that, in the same census the rest of the suite is written in.

It is the same argument as `check_bound_enclosure()` and one level further out:
a kernel agreeing with us proves nothing on its own, because agreement can be
what its repair pass produced. What it wrote back is not an opinion.

**Two comparisons, and running both is the method** - see
`doc/step-export-development.md`, *How to interrogate a reader*:

  ours -> its round trip        what it made of what we wrote. Kind-level, with
                                OpenCASCADE reading both, because the two files
                                are written by different writers and only the
                                census and the volume are comparable.

  its round trip -> its healed  what its *repair* changed, and nothing else:
  round trip                    same writer, same settings, same session, one
                                repaired. Exact, and geometric rather than by
                                id, through `reader-diff.py`. Written by
                                `-RoundTrip -Heal`, and reported here whenever
                                the second file is present.

The second is the one that answers questions the first cannot. On `f04` it found
that "General geometry problem" - a fault no API check reports at all - is 215
curves re-fitted by at most 9.583e-05, with no vertex, no surface and no topology
touched.

Three things this can find that nothing upstream of it can:

  substitution   The coupon's B_SPLINE_SURFACE comes back as a fan of planes.
                 The import "succeeded" and the analytic path bought nothing.
  degradation    A cylinder comes back as a B-spline, or a circle as a spline
                 curve. The surface survived as geometry and not as intent, so
                 downstream feature recognition has nothing to find.
  drift          The volume moves. Within the coupon's tessellation band that is
                 the fit standing off the chords and is expected; outside it,
                 something was repaired rather than read.

Usage:

    python3 scripts/step-interop-sw-roundtrip.py --kitdir build/interop-kit
"""

import argparse
import csv
import importlib.util
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tests"))

try:
    import steproundtrip as srt
except ImportError as exc:  # pragma: no cover - the message is the point
    print("cannot import tests/steproundtrip.py: %s" % exc, file=sys.stderr)
    sys.exit(2)


def unbounded(box):
    """Whether a bounding box says the shape runs off to infinity.

    OpenCASCADE gives an unbounded surface the box +/-1e100 rather than
    refusing it, and every measurement downstream of that is meaningless while
    still being a number. The SOLIDWORKS re-export this script was first run
    against is exactly that: OCCT reads it as one solid of three shells, and the
    box around it is +/-1e100, so its "volume" came out at 2.16e168.

    1e12 rather than 1e100 because the point is to catch a model that is not a
    model, and no part this exporter will ever write is a trillion units across.
    """
    return any(abs(c) > 1e12 for c in box.Get())


def read(path):
    """Surface census, volume and face count, or None if OCCT will not read it."""
    from OCP.STEPControl import STEPControl_Reader
    from OCP.IFSelect import IFSelect_ReturnStatus
    from OCP.BRepGProp import BRepGProp
    from OCP.GProp import GProp_GProps
    from OCP.TopAbs import TopAbs_FACE, TopAbs_SHELL, TopAbs_SOLID
    from OCP.Bnd import Bnd_Box
    from OCP.BRepBndLib import BRepBndLib

    reader = STEPControl_Reader()
    if reader.ReadFile(os.path.abspath(path)) != IFSelect_ReturnStatus.IFSelect_RetDone:
        return None
    if reader.TransferRoots() < 1:
        return None
    shape = reader.OneShape()
    if shape.IsNull():
        return None
    solids = srt._count(shape, TopAbs_SOLID)
    # A volume is only usable if it is one. VolumeProperties integrates the
    # divergence over the faces and will return whatever the arithmetic gives
    # when they do not enclose anything - on the first file this was ever run
    # against, a SOLIDWORKS re-export that OCCT reads as a solid, it returned
    # 2.16e168, and the first draft of this script duly reported a drift of
    # 9e167 per cent. Checking for a solid is not enough, because that file has
    # one.
    #
    # What catches it is the one bound a volume cannot escape: its own bounding
    # box. A shape whose "volume" exceeds the box around it has not been
    # measured, it has been mis-integrated, and saying so is more use than a
    # number nobody can read.
    volume = None
    if solids:
        props = GProp_GProps()
        BRepGProp.VolumeProperties_s(shape, props)
        v = props.Mass()
        box = Bnd_Box()
        BRepBndLib.Add_s(shape, box)
        if not box.IsVoid() and not unbounded(box):
            xa, ya, za, xb, yb, zb = box.Get()
            bbox = abs((xb - xa) * (yb - ya) * (zb - za))
            volume = v if abs(v) <= bbox * 1.001 else None
    box = Bnd_Box()
    BRepBndLib.Add_s(shape, box)
    return {
        "surfaces": srt._surface_census(shape),
        "edges": srt.edge_census(shape),
        "faces": srt._count(shape, TopAbs_FACE),
        "shells": srt._count(shape, TopAbs_SHELL),
        "solids": solids,
        "volume": volume,
        "unbounded": (not box.IsVoid()) and unbounded(box),
    }


def delta(before, after):
    """One line per surface kind that changed, plus the volume."""
    lines = []
    kinds = sorted(set(before["surfaces"]) | set(after["surfaces"]))
    for k in kinds:
        b, a = before["surfaces"].get(k, 0), after["surfaces"].get(k, 0)
        if b != a:
            lines.append("    %-22s %5d -> %-5d" % (k, b, a))
    for k in ("solids", "shells"):
        if before[k] != after[k]:
            lines.append("    %-22s %5d -> %-5d" % (k, before[k], after[k]))
    if after["unbounded"] and not before["unbounded"]:
        lines.append("    %-22s bounded -> unbounded, it contains an infinite surface"
                     % "extent")
    if before["volume"] and after["volume"]:
        moved = (after["volume"] - before["volume"]) / abs(before["volume"])
        lines.append("    %-22s %.6f -> %.6f  (%+.4f%%)"
                     % ("volume", before["volume"], after["volume"], 100 * moved))
    elif before["volume"] and after["volume"] is None:
        why = ("the shape is unbounded" if after["unbounded"]
               else "it exceeds its own bounding box")
        lines.append("    %-22s %.6f -> unmeasurable, %s" % ("volume", before["volume"], why))
    return lines


def verdict(before, after):
    """What the pair says, in one word, on the terms this kit is written in.

    Deliberately blunt and deliberately not a pass/fail: whether a change is
    acceptable depends on the coupon's band, which lives in the fixture rather
    than here. This says what *kind* of change it is and leaves the judgement to
    the reader, which is what the cad_verdict column is for."""
    if after is None:
        return "unreadable"
    analytic = ("BSplineSurface", "Cylinder", "Cone", "Sphere", "Torus")
    lost = [k for k in analytic if after["surfaces"].get(k, 0) < before["surfaces"].get(k, 0)]
    if after["solids"] < 1 <= before["solids"]:
        return "not-a-solid"
    if after["unbounded"] and not before["unbounded"]:
        return "unbounded"
    if before["volume"] is not None and after["volume"] is None:
        return "unmeasurable"
    if lost:
        return "lost:" + ",".join(lost)
    if after["surfaces"].get("BSplineSurface", 0) > before["surfaces"].get("BSplineSurface", 0):
        return "degraded-to-spline"
    return "kept"


def repair_delta(swfile, healed):
    """What the reader's own repair changed, measured against its own output.

    Deliberately not through OpenCASCADE: both files come from the same writer,
    so they can be compared exactly, and the only thing in the way is that every
    entity is renumbered on each save. `reader-diff.py` matches by geometry
    instead - faces by surface and corners, curves by their control points.
    """
    try:
        spec = importlib.util.spec_from_file_location(
            "reader_diff", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                        "step-diagnostics", "reader-diff.py"))
        rd = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(rd)
    except Exception as exc:
        return ["    repair: cannot load reader-diff.py (%s)" % exc]

    out = []
    try:
        A, B = rd.faces(swfile), rd.faces(healed)
        gone = [k for k in A if k not in B]
        new = [k for k in B if k not in A]
        moved = [k for k in A if k in B and A[k] != B[k]]
        ca, cb = rd.curves(swfile), rd.curves(healed)
        ka = {rd.key_of(kind, pts) for kind, _, pts in ca}
        kb = {rd.key_of(kind, pts) for kind, _, pts in cb}
        replaced = [(kind, i, pts) for kind, i, pts in ca if rd.key_of(kind, pts) not in kb]
        arrived = [(kind, i, pts) for kind, i, pts in cb if rd.key_of(kind, pts) not in ka]
        worst = 0.0
        for kind, _, pa in replaced:
            best = None
            for kind2, _, pb in arrived:
                if kind2 != kind or len(pb) != len(pa):
                    continue
                d = math.dist(pa[0], pb[0]) + math.dist(pa[-1], pb[-1])
                if best is None or d < best[0]:
                    best = (d, pb)
            if best is not None:
                worst = max(worst, max(math.dist(x, y) for x, y in zip(pa, best[1])))
        if not gone and not new and not moved and not replaced:
            out.append("    repair: changed nothing")
        else:
            out.append("    repair: %d faces gone, %d new, %d with corners moved; "
                       "%d curves replaced, worst %.3e"
                       % (len(gone), len(new), len(moved), len(replaced), worst))
            if not gone and not new and not moved and replaced:
                out.append("            no vertex, no surface and no topology - a re-fit only")
    except Exception as exc:
        out.append("    repair: comparison failed (%s)" % exc)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kitdir", default="build/interop-kit")
    ap.add_argument("--out", help="CSV to write (default <kitdir>/sw-roundtrip.csv)")
    args = ap.parse_args()

    kitdir = args.kitdir if os.path.isabs(args.kitdir) else os.path.join(ROOT, args.kitdir)
    if not srt.available():
        print("OpenCASCADE is not installed - pip install cadquery-ocp", file=sys.stderr)
        return 2

    pairs = []
    for name in sorted(os.listdir(kitdir)):
        if not name.endswith("_SW.STEP"):
            continue
        original = os.path.join(kitdir, name[: -len("_SW.STEP")] + ".stp")
        healed = os.path.join(kitdir, name[: -len("_SW.STEP")] + "_SW_healed.STEP")
        if os.path.exists(original):
            pairs.append((original, os.path.join(kitdir, name),
                          healed if os.path.exists(healed) else None))
    if not pairs:
        print("no <coupon>_SW.STEP files in %s - run the PowerShell driver with "
              "-RoundTrip first" % kitdir, file=sys.stderr)
        return 1

    rows = []
    for original, swfile, healed in pairs:
        before = read(original)
        after = read(swfile)
        coupon = os.path.basename(original)[:-4]
        if before is None:
            print("%-34s SKIP - OCCT cannot read our own export" % coupon)
            continue
        v = verdict(before, after)
        print("%-34s %s" % (coupon, v))
        if after is not None:
            for line in delta(before, after):
                print(line)
        if healed is not None:
            for line in repair_delta(swfile, healed):
                print(line)
        rows.append({
            "coupon": coupon,
            "verdict": v,
            "faces_ours": before["faces"],
            "faces_sw": after["faces"] if after else "",
            "shells_ours": before["shells"],
            "shells_sw": after["shells"] if after else "",
            "volume_ours": "%.6f" % before["volume"] if before["volume"] else "",
            "volume_sw": ("%.6f" % after["volume"]
                          if after and after["volume"] is not None else ""),
            "surfaces_ours": " ".join("%s=%d" % kv for kv in sorted(before["surfaces"].items())),
            "surfaces_sw": (" ".join("%s=%d" % kv for kv in sorted(after["surfaces"].items()))
                            if after else ""),
        })

    out = args.out or os.path.join(kitdir, "sw-roundtrip.csv")
    with open(out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print("\n%d pairs, results: %s" % (len(rows), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
