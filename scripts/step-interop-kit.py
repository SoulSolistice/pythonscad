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
     "SPHERICAL_SURFACE, whole",
     "Both poles are parametric singularities."),
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
     "CYLINDRICAL_SURFACE bounded entirely by LINE, with holes",
     "Structurally new: every other quadric face here is bounded by circles and "
     "arcs, and this one is bounded by the mesh's own polyline because its trim "
     "is a quartic no STEP curve can state. It may also carry more than one "
     "FACE_BOUND - a quadric with a hole in it - which some importers only "
     "expect on a PLANE."),
    ("c16-bored-cone", "tests/data/scad/step-export/step-bored-cone.scad",
     "CONICAL_SURFACE and CYLINDRICAL_SURFACE, both polyline-bounded",
     "As c15 on a taper, so the bore's own trim runs on a cone."),
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
APPROX = {"c12-approximated", "c15-bored-cylinder", "c16-bored-cone",
          "r01-lid10", "r02-bayonet"}

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


def export(binary, source, target, analytic, approx):
    args = [binary, source, "-o", target, "--trust-python"]
    args += parameter_set(source)
    if analytic:
        args.append("--enable=step-analytic-surfaces")
        if approx:
            args.append("--enable=step-approximate-surfaces")
    proc = subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
    return proc.returncode, (proc.stderr or "")


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
    args = ap.parse_args()

    outdir = args.outdir if os.path.isabs(args.outdir) else os.path.join(ROOT, args.outdir)
    os.makedirs(outdir, exist_ok=True)

    rows = []
    for name, src, exercises, risk in COUPONS:
        srcpath = os.path.join(ROOT, src)
        if not os.path.exists(srcpath):
            print("SKIP %-22s source missing: %s" % (name, src))
            continue
        for mode in ("analytic", "faceted"):
            target = os.path.join(outdir, "%s-%s.stp" % (name, mode))
            rc, err = export(args.binary, srcpath, target,
                             analytic=(mode == "analytic"),
                             approx=(name in APPROX))
            if rc != 0 or not os.path.exists(target):
                print("FAIL %-22s %-8s export rc=%s" % (name, mode, rc))
                for line in err.strip().splitlines()[-3:]:
                    print("       " + line)
                continue
            ok = validate(target)
            c = census(target)
            print("%-4s %-22s %-8s %5d faces  %s" % (
                "ok" if ok else "BAD", name, mode, c.get("ADVANCED_FACE", 0),
                "validator ok" if ok else "VALIDATOR FAILED"))
            rows.append({
                "coupon": name,
                "mode": mode,
                "file": os.path.basename(target),
                "exercises": exercises if mode == "analytic" else "control",
                "risk": risk if mode == "analytic" else "",
                "validator": "ok" if ok else "FAILED",
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
                # doc/step-interop-validation.md for what each one means.
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
