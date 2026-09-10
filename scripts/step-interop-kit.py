#!/usr/bin/env python3
"""Build a STEP interoperability kit for a third-party CAD kernel.

The exporter's analytic path is validated against OpenCASCADE only, and OCCT is
one kernel with one set of opinions. The failure that motivated this work was
seen in SOLIDWORKS. This script produces the files needed to get a second
opinion, and - more importantly - produces a *control* beside every one of them.

For each coupon it writes two STEP files:

    <name>-analytic.stp   the analytic path, the thing under test
    <name>-faceted.stp    the same model with the analytic path off

The control is the whole method. A coupon that fails to import proves nothing on
its own: the target system might dislike the model, the units, the tolerance, or
this exporter's faceted output, none of which is news. A coupon whose analytic
export fails *while its faceted control imports cleanly* isolates the defect to
the analytic entity that coupon exists to exercise.

Usage:
    python3 scripts/step-interop-kit.py \\
        --binary build/staging/pythonscad.exe \\
        --outdir build/interop-kit

The binary must come from a staging directory that can actually run; a freshly
linked build/pythonscad.exe cannot (see CLAUDE.md, "Running the built binary").
"""

import argparse
import csv
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# name, source, what it exercises, why it is risky in a foreign kernel
COUPONS = [
    ("c01-cylinder", "tests/data/scad/step-export/step-declare.scad",
     "CYLINDRICAL_SURFACE, CIRCLE",
     "Baseline quadric. If this fails nothing below is interpretable."),
    ("c02-partial-cylinder", "tests/data/scad/step-export/step-partial-cylinder.scad",
     "4 partial CYLINDRICAL_SURFACE",
     "A quadric trimmed short of its seam."),
    ("c03-cone", "tests/data/scad/step-export/step-cone-primitive.scad",
     "CONICAL_SURFACE",
     "Half-angle and apex placement; a cone degenerates at its apex."),
    ("c04-sphere", "tests/data/scad/step-export/step-sphere.scad",
     "one SPHERICAL_SURFACE closed on itself, TWO edges, no PLANE",
     "Riskier than it was, and riskier than c07. Since 2026-09-08 a whole "
     "sphere is written closed at its poles rather than capped with two discs, "
     "so the face has no rim at all: one bound, one seam meridian used once in "
     "either direction, and the two poles are VERTEX_POINTs where the surface "
     "is degenerate. That is a **two** edge face, where c07's octants are three "
     "and the note there already says foreign importers routinely reject or "
     "silently repair a 3-edge face. OpenCASCADE writes the same solid as a "
     "VERTEX_LOOP with no edges at all and reads ours back as one Sphere with "
     "two degenerate edges; whether SOLIDWORKS sews it is the question. The "
     "faceted control is the same ball as 482 planes, so a solid there and a "
     "surface body here isolates it to the closure."),
    ("c05-torus", "tests/data/scad/step-export/step-torus.scad",
     "TOROIDAL_SURFACE, whole",
     "Two closed seams and no rim."),
    ("c06-partial-torus", "tests/data/scad/step-export/step-rounded-profile.scad",
     "4 partial TOROIDAL_SURFACE",
     "Rim circles of latitude plus one seam along the tube."),
    ("c07-fillet-quadrics", "tests/data/pythonscad-step-export/step-fillet.py",
     "12 CYLINDRICAL_SURFACE, 8 SPHERICAL_SURFACE, 24 CIRCLE",
     "The sphere octants carry THREE edges - the fourth side is the pole, where "
     "the patch is degenerate. Foreign importers routinely reject or silently "
     "repair a 3-edge face."),
    ("c08-fillet-oblique", "tests/data/pythonscad-step-export/step-fillet-oblique.py",
     "as c07, nothing axis aligned",
     "AXIS2_PLACEMENT_3D precision when no direction is a unit axis."),
    ("c09-rational-bspline", "tests/data/pythonscad-step-export/step-fillet-refusals.py",
     "24 RATIONAL_B_SPLINE_SURFACE complex instances",
     "HIGHEST RISK. A rational surface is written as an ISO 10303-21 complex "
     "instance, whose sub-entity records must appear in a prescribed order. That "
     "is the exact class of defect F7 was, and OCCT is more forgiving of it than "
     "most commercial importers."),
    ("c10-bspline-text", "tests/data/scad/step-export/step-extrude-text.scad",
     "32 B_SPLINE_SURFACE_WITH_KNOTS",
     "Non-rational splines with uniform knots - the easy spline case."),
    ("c11-swept-grid", "tests/data/pythonscad-step-export/step-declare-grid.py",
     "a declared sweep as one B-spline face",
     "General (non-uniform) knot vectors, and a large control net."),
    ("c12-approximated", "tests/data/scad/step-export/step-approximate-report.scad",
     "4 swept-grid faces from the approximation pass",
     "The approximation pass, which needs step-approximate-surfaces as well."),
    ("c13-oblique-trim", "tests/data/scad/step-export/step-oblique-trim.scad",
     "ELLIPSE bounding a CYLINDRICAL_SURFACE",
     "HIGHEST RISK of the new entities, because it is the only entity kind this "
     "exporter has never shown a commercial reader. A cylinder cut by a tilted "
     "plane is bounded by an ellipse, and it is written as a plain 3D ELLIPSE "
     "with no pcurve - which OpenCASCADE reads back to 2e-6 of the radius, but "
     "which a stricter importer may insist on having a parameterisation for."),
    ("c14-declared-cone", "tests/data/scad/step-export/step-declare-cone.scad",
     "CONICAL_SURFACE from declare_cone, sharing a rim with a cylinder",
     "A cone and a cylinder meeting at one CIRCLE used by both, where the cone "
     "came from a declaration rather than from two matching rims. c03 covers a "
     "cone standing alone; this covers the joint."),
    ("c15-bored-cylinder", "tests/data/scad/step-export/step-bored-cylinder.scad",
     "CYLINDRICAL_SURFACE with holes, and 80 SURFACE_CURVE / 160 PCURVE",
     "Structurally new: every other quadric face here is bounded by circles and "
     "arcs, and this one is bounded by the mesh's own polyline because its trim "
     "is a quartic no STEP curve can state. It may also carry more than one "
     "FACE_BOUND - a quadric with a hole in it - which some importers only "
     "expect on a PLANE. "
     "Since the trim work of 2026-09-07 this is also the kit's carrier for a "
     "genuinely new entity class: every crossing curve is written as a "
     "SURFACE_CURVE with a PCURVE on each of the two surfaces it lies on, 80 "
     "and 160 of them here. c13's ELLIPSE is written with no pcurve at all, so "
     "this is the opposite bet - that a reader prefers being told the "
     "parameterisation - and no commercial reader has seen either."),
    ("c16-bored-cone", "tests/data/scad/step-export/step-bored-cone.scad",
     "as c15 on a taper: 80 SURFACE_CURVE / 160 PCURVE, on a cone",
     "As c15 on a taper, so the bore's own trim runs on a cone - and the "
     "pcurve of a curve on a cone is the one whose parameterisation is easiest "
     "to get wrong, because the radius varies along the axis."),
    ("c17-cylinder-cross", "tests/data/scad/step-export/step-cylinder-cross.scad",
     "8 CYLINDRICAL_SURFACE, 8 ELLIPSE, 4 LINE, no PLANE at all",
     "Two equal cylinders crossing at right angles, trimmed to the curve where "
     "they actually cross. Three things here are new to a commercial reader at "
     "once: a closed shell containing no planar face whatsoever, faces bounded "
     "by ELLIPSE *arcs* rather than whole conics, and a region that pinches to "
     "a point at theta = 0 and 180 - so those faces carry three edges, with the "
     "fourth side closing on itself. It is also the one coupon whose correct "
     "volume is known in closed form, the Steinmetz 16*r^3/3 = 5333.33333, so "
     "what SOLIDWORKS makes of it can be measured and not merely counted."),
    ("r01-lid10", "examples/step_test/lid10.scad",
     "real part: cylinders, cones, circles",
     "A real model, and the one whose committed export was finding F1."),
    ("r02-bayonet", "examples/step_test/bayonet_container_v1-2.scad",
     "real part: 1693 faces, 14 surfaces of revolution",
     "Scale, and a helical thread that stays faceted by design."),
]

# Coupons whose interesting geometry only exists with the approximation pass:
# a declared sweep is a fit rather than an exact surface, and so is a quadric
# claimed by distance to its axis. Both real parts are here because both now
# declare their thread - exported with the analytic flag alone, lid10 comes
# out at 1985 faces and shows a CAD system none of this work.
# The band family: one model, one feature, at a sweep of tessellations.
#
# Every other coupon here answers "does this import"; this one answers "up to
# what tessellation band does this importer sew". That is the question the
# SOLIDWORKS run of 2026-09-01 could not answer, because every candidate cause
# was tried on one part at one tessellation. A fitted face is bounded by the
# mesh's own polyline - the faceted faces around it have to close against it
# edge for edge - so its boundary sags off the surface by up to a station's
# sagitta, and a kernel either grants that slack or does not.
#
# `-D FN=` is the whole difference between the members. Everything else about
# the model is fixed, so the band is the only thing that moves.
BAND_FAMILY = [24, 32, 48, 64, 96]
for _i, _fn in enumerate(BAND_FAMILY):
    COUPONS.append((
        "f%02d-band-fn%03d" % (_i + 1, _fn),
        "tests/data/scad/step-export/step-band-family.scad",
        "a declared sweep at $fn=%d" % _fn,
        "One member of the band family; read the members together, not alone. "
        "What matters is the fn at which the importer stops making a solid.",
        ["-D", "FN=%d" % _fn],
    ))

APPROX = {"c11-swept-grid", "c12-approximated", "c15-bored-cylinder",
          "c16-bored-cone", "r01-lid10", "r02-bayonet"}
APPROX |= {"f%02d-band-fn%03d" % (i + 1, fn) for i, fn in enumerate(BAND_FAMILY)}

# What the analytic export's face count has to be, where that number follows
# from the model rather than from a run.
#
# The kit has always printed the count and never checked it, so a coupon whose
# claim quietly changed looked identical to one that had not - and the first
# thing to notice was a CAD system, twenty minutes and a licence later. These
# are the cheap half of that: they cost one export each and they fail in the
# terminal.
#
# Derived, and only where derivable. A count captured from a run locks in
# whatever the exporter did last, which is the circularity
# doc/step-export-development.md exists to prevent; a coupon absent from this
# table simply reports its count as before. Adding one means working the number
# out from the model and writing the reason beside it.
EXPECT_FACES = {
    "c04-sphere": (1, "a sphere closed on itself is bounded by its seam alone: one "
                      "face, and no plane anywhere"),
    "c05-torus": (1, "a complete torus is closed in both directions, so it is one "
                     "face bounded by its own two seams"),
    "c07-fillet-quadrics": (26, "a filleted cube is 6 planes, 12 edge cylinders and "
                                "8 corner sphere octants"),
    "c17-cylinder-cross": (8, "each cylinder keeps two 180 degree lobes and each lobe "
                              "is written as two faces; no cap survives"),
}

CENSUS_KINDS = [
    "PLANE", "CYLINDRICAL_SURFACE", "CONICAL_SURFACE", "SPHERICAL_SURFACE",
    "TOROIDAL_SURFACE", "B_SPLINE_SURFACE_WITH_KNOTS",
    "RATIONAL_B_SPLINE_SURFACE", "B_SPLINE_CURVE_WITH_KNOTS", "CIRCLE",
    "ADVANCED_FACE", "CLOSED_SHELL",
]


def parameter_set(source):
    """Return ['-p', file, '-P', set] when the model ships a customizer set.

    The two real parts are the same .scad rendered as different components -
    lid10.json selects the lid, bayonet_container_v1-2.json the base. Exporting
    either without its parameter set silently gives the default component, so
    both coupons come out nearly identical and neither is the part the rest of
    the documentation measured.
    """
    stem = os.path.splitext(source)[0]
    cfg = stem + ".json"
    if not os.path.exists(cfg):
        return []
    try:
        import json
        with open(cfg) as fh:
            sets = json.load(fh).get("parameterSets", {})
    except (OSError, ValueError):
        return []
    if not sets:
        return []
    return ["-p", cfg, "-P", sorted(sets)[0]]


BAND_RE = re.compile(r"tessellation band of ([0-9.]+)")

# How much of a declared sweep was actually written as a surface.
#
# "faults=0" over a sweep that is half faceted is a much weaker result than it
# looks, and doc/step-export-development.md records one read as though it were
# not - twice, once each way. A facet leaves the claim two ways: refused as an
# outlier, and *cut across* by the boolean, and the second is much the larger.
# So the kit records both numbers beside every file, and any fault count read
# out of the CAD system has this column to be read against.
CLAIM_RE = re.compile(r"sweep claims (\d+) facets whole, (\d+) cut across it")
SPANS_RE = re.compile(r"facets lie over (\d+) of the profile's (\d+) spans")


def band_of(stderr):
    """The tessellation band the exporter reported, which is the model's own.

    This is the one number that says how far a fitted surface is *entitled* to
    sit from the mesh it was fitted to - the sagitta of the model's own stations
    - so it is what the slack a kernel has to grant should be measured against.
    Captured from the exporter rather than derived here because the exporter
    computes it from the declared grid; the derivation lives in
    GridSurface::tessellationBand.

    A faceted export reports none and needs none: planes through mesh vertices
    are exact, so its entitlement is zero and any slack at all is a defect."""
    bands = [float(m) for m in BAND_RE.findall(stderr or "")]
    return max(bands) if bands else 0.0


def claim_of(stderr):
    """Facets the declared sweeps claimed whole, cut across, and as a share.

    Summed over every declared sweep in the model, because a model may declare
    more than one and the question - how much of what was declared came out as
    surface - is about the model rather than about any single declaration.
    """
    pairs = [(int(a), int(b)) for a, b in CLAIM_RE.findall(stderr or "")]
    whole = sum(a for a, _ in pairs)
    cut = sum(b for _, b in pairs)
    spans = SPANS_RE.findall(stderr or "")
    covered = "; ".join("%s/%s" % (a, b) for a, b in spans)
    return whole, cut, covered


def export(binary, source, target, analytic, approx, extra=()):
    args = [binary, source, "-o", target, "--trust-python"]
    args += parameter_set(source)
    args += list(extra)
    if analytic:
        args.append("--enable=step-analytic-surfaces")
        if approx:
            args.append("--enable=step-approximate-surfaces")
    proc = subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
    # Both streams, not just stderr. The exporter's report - the tessellation
    # band, the sweep's claim, everything band_of() and claim_of() read - comes
    # out on *stdout*, so reading stderr alone silently reports nothing: the
    # `band` column has been 0.000000 for every file of every kit ever
    # generated, and nothing noticed because a zero there reads as "no declared
    # sweep in this coupon" for the twenty coupons that have none.
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def _unused():  # pragma: no cover
    pass


def census(path):
    """Count the STEP entities that decide whether an importer copes."""
    try:
        with open(path, "r", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return {}
    return {k: len(re.findall(r"(?<![A-Z_])" + k + r"\s*\(", text))
            for k in CENSUS_KINDS}


def validate(path):
    proc = subprocess.run(
        [sys.executable, os.path.join(ROOT, "tests", "validatestep.py"), path],
        capture_output=True, text=True)
    return proc.returncode == 0


def main():
    ap = argparse.ArgumentParser(
        description="Generate a STEP interop kit with a faceted control per coupon.")
    ap.add_argument("--binary", required=True,
                    help="pythonscad executable, from a staging dir that can run")
    ap.add_argument("--outdir", default="build/interop-kit")
    ap.add_argument("--only", metavar="REGEX",
                    help="restrict the kit to coupons whose name matches, for "
                         "putting one question in front of an importer without "
                         "asking a tester to open forty files")
    args = ap.parse_args()
    only = re.compile(args.only) if args.only else None

    # Resolve the binary before handing it to subprocess. Every export runs with
    # cwd=ROOT, and on Windows CreateProcess resolves a relative executable
    # against the *parent's* directory rather than that one - so the usage this
    # script documents, `--binary build/staging/pythonscad.exe`, dies with
    # "cannot find the file specified" from anywhere but the repository root,
    # and says nothing about which file it could not find.
    binary = args.binary if os.path.isabs(args.binary) else os.path.abspath(args.binary)
    if not os.path.exists(binary):
        sys.exit("no such binary: %s" % binary)

    outdir = args.outdir if os.path.isabs(args.outdir) else os.path.join(ROOT, args.outdir)
    os.makedirs(outdir, exist_ok=True)

    rows = []
    face_mismatch = []
    for entry in COUPONS:
        name, src, exercises, risk = entry[:4]
        if only and not only.search(name):
            continue
        extra = entry[4] if len(entry) > 4 else []
        srcpath = os.path.join(ROOT, src)
        if not os.path.exists(srcpath):
            print("SKIP %-22s source missing: %s" % (name, src))
            continue
        for mode in ("analytic", "faceted"):
            target = os.path.join(outdir, "%s-%s.stp" % (name, mode))
            rc, err = export(binary, srcpath, target,
                             analytic=(mode == "analytic"),
                             approx=(name in APPROX), extra=extra)
            band = band_of(err)
            if rc != 0 or not os.path.exists(target):
                print("FAIL %-22s %-8s export rc=%s" % (name, mode, rc))
                for line in err.strip().splitlines()[-3:]:
                    print("       " + line)
                continue
            ok = validate(target)
            c = census(target)
            faces = c.get("ADVANCED_FACE", 0)
            note = "validator ok" if ok else "VALIDATOR FAILED"
            want = EXPECT_FACES.get(name) if mode == "analytic" else None
            if want is not None and faces != want[0]:
                note = "FACES %d, EXPECTED %d - %s" % (faces, want[0], want[1])
                face_mismatch.append("%s: %d faces, expected %d (%s)"
                                     % (name, faces, want[0], want[1]))
            whole, cut, spans = claim_of(err)
            claim = ("  sweep claims %d whole + %d cut = %.1f%% whole"
                     % (whole, cut, 100.0 * whole / (whole + cut))) if whole + cut else ""
            print("%-4s %-22s %-8s %5d faces  %s%s" % (
                "ok" if ok and (want is None or faces == want[0]) else "BAD",
                name, mode, faces, note, claim))
            rows.append({
                "coupon": name,
                "mode": mode,
                "file": os.path.basename(target),
                "exercises": exercises if mode == "analytic" else "control",
                "risk": risk if mode == "analytic" else "",
                "validator": "ok" if ok else "FAILED",
                # What this file is entitled to be off by. See band_of().
                "band": "%.6f" % band,
                # How much of the declaration reached the file. See claim_of().
                "sweep_whole": whole,
                "sweep_cut": cut,
                "sweep_pct_whole": ("%.1f" % (100.0 * whole / (whole + cut))
                                    if whole + cut else ""),
                "sweep_spans": spans,
                "faces": c.get("ADVANCED_FACE", 0),
                "shells": c.get("CLOSED_SHELL", 0),
                "plane": c.get("PLANE", 0),
                "cylinder": c.get("CYLINDRICAL_SURFACE", 0),
                "cone": c.get("CONICAL_SURFACE", 0),
                "sphere": c.get("SPHERICAL_SURFACE", 0),
                "torus": c.get("TOROIDAL_SURFACE", 0),
                "bspline_surf": c.get("B_SPLINE_SURFACE_WITH_KNOTS", 0),
                "rational": c.get("RATIONAL_B_SPLINE_SURFACE", 0),
                "circle": c.get("CIRCLE", 0),
                # Filled in by hand, in the target CAD system. See
                # doc/step-export-development.md for what each one means.
                "cad_body_type": "",
                "cad_import_errors": "",
                "cad_faces": "",
                "cad_volume": "",
                "cad_area": "",
                "cad_check_entity": "",
                "cad_verdict": "",
            })

    if not rows:
        print("nothing generated", file=sys.stderr)
        return 1

    csvpath = os.path.join(outdir, "results.csv")
    with open(csvpath, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print("\nkit: %d files in %s" % (len(rows), outdir))
    print("checklist: %s  (the cad_* columns are yours to fill in)" % csvpath)
    if face_mismatch:
        print("")
        print("the claim is not what these coupons say it should be:")
        for line in face_mismatch:
            print("  " + line)
        print("")
        print("Either the export changed or the expectation is wrong, and the second is")
        print("worth considering first. Nothing past this point is worth running until")
        print("it is settled: a CAD system will happily import the wrong solid.")
        return 1
    checked = sum(1 for r in rows
                  if r["mode"] == "analytic" and r["coupon"] in EXPECT_FACES)
    print("face count as derived on %d of %d coupons" % (checked, len(COUPONS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
