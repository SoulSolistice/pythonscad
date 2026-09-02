#!/usr/bin/env python3
"""Put three measurements of every coupon beside each other and flag the disagreements.

`doc/step-interop-validation.md`, step 5 of the per-file procedure, says to
compare a CAD system's volume three ways: against the coupon's own faceted
control, against the exact value where the model has one, and against
OpenCASCADE's answer - "a disagreement between two kernels on the same file is a
much sharper finding than either number alone". Nothing automated that, so it
was done by hand for a few files and not at all for the rest.

It is worth automating because a coupon can pass every other criterion and still
be wrong. On the 2026-09-02 run `c06-partial-torus-analytic` imported as a solid
with the right face count and no errors, and SOLIDWORKS measured its volume 14%
below both OpenCASCADE and the fixture's own hand-derived figure. Body type and
face count called that a pass.

Three columns, and the point is which of them can disagree:

  derived    what the model's own arithmetic says, read from the fixture's
             `// VOLUME:` directive. Available only where a closed form exists,
             which is the minority - see doc/step-export-testing.md on why a
             number that can only be got by running the exporter is not one.
  OCCT       OpenCASCADE reading the exported file.
  CAD        what the target system measured, from solidworks-results.csv.

A faceted control is the calibration: the two kernels should agree on it to
several digits, because there is nothing in it but planes. Where they agree on
the control and disagree on the analytic file, the disagreement is about the
analytic surfaces and nothing else - not units, not settings, not tolerance.

Usage:

    python3 scripts/step-interop-crosscheck.py --kitdir build/interop-kit
"""

import argparse
import csv
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, HERE)

try:
    import steproundtrip as srt
except ImportError as exc:  # pragma: no cover
    print("cannot import tests/steproundtrip.py: %s" % exc, file=sys.stderr)
    sys.exit(2)


def derived_volumes():
    """Coupon name -> the volume its source fixture derives, where it states one."""
    out = {}
    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "kit", os.path.join(HERE, "step-interop-kit.py"))
        kit = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(kit)
        coupons = kit.COUPONS
    except Exception as exc:
        print("could not read the coupon table: %s" % exc, file=sys.stderr)
        return out
    for entry in coupons:
        name, src = entry[0], entry[1]
        path = os.path.join(ROOT, src)
        if not os.path.exists(path):
            continue
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        # The same directive the sanity test reads. Tolerance suffix ignored:
        # this is a cross-check, not the assertion.
        m = re.search(r"(?<![-\w])VOLUME:\s*([0-9.eE+-]+)", text)
        if m:
            try:
                out[name] = float(m.group(1))
            except ValueError:
                pass
    return out


def occt_volume(path):
    """OpenCASCADE's volume, or None when the shape is not one that has one."""
    from OCP.STEPControl import STEPControl_Reader
    from OCP.IFSelect import IFSelect_ReturnStatus
    from OCP.BRepGProp import BRepGProp
    from OCP.GProp import GProp_GProps
    from OCP.TopAbs import TopAbs_SOLID
    from OCP.Bnd import Bnd_Box
    from OCP.BRepBndLib import BRepBndLib

    reader = STEPControl_Reader()
    if reader.ReadFile(os.path.abspath(path)) != IFSelect_ReturnStatus.IFSelect_RetDone:
        return None
    if reader.TransferRoots() < 1:
        return None
    shape = reader.OneShape()
    if shape.IsNull() or srt._count(shape, TopAbs_SOLID) < 1:
        return None
    box = Bnd_Box()
    BRepBndLib.Add_s(shape, box)
    if box.IsVoid() or any(abs(c) > 1e12 for c in box.Get()):
        return None
    props = GProp_GProps()
    BRepGProp.VolumeProperties_s(shape, props)
    return props.Mass()


def rel(a, b):
    return None if not a else (b - a) / abs(a)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kitdir", default="build/interop-kit")
    ap.add_argument("--results", help="the CAD system's CSV "
                                      "(default <kitdir>/solidworks-results.csv)")
    ap.add_argument("--tolerance", type=float, default=0.005,
                    help="relative disagreement worth flagging (default 0.5%%)")
    ap.add_argument("--out", help="CSV to write (default <kitdir>/crosscheck.csv)")
    args = ap.parse_args()

    kitdir = args.kitdir if os.path.isabs(args.kitdir) else os.path.join(ROOT, args.kitdir)
    results = args.results or os.path.join(kitdir, "solidworks-results.csv")
    if not srt.available():
        print("OpenCASCADE is not installed - pip install cadquery-ocp", file=sys.stderr)
        return 2

    cad = {}
    if os.path.exists(results):
        with open(results, newline="", encoding="utf-8-sig") as fh:
            for row in csv.DictReader(fh):
                try:
                    cad[row["file"]] = (float(row.get("volume_mm3") or 0),
                                        row.get("body_type", ""), row.get("note", ""))
                except ValueError:
                    pass
    else:
        print("no CAD results at %s - showing OCCT against derived only" % results,
              file=sys.stderr)

    derived = derived_volumes()
    rows, flagged = [], []
    for name in sorted(os.listdir(kitdir)):
        if not name.endswith(".stp"):
            continue
        stem = name[:-4]
        coupon = stem.rsplit("-", 1)[0]
        mode = stem.rsplit("-", 1)[-1]
        occt = occt_volume(os.path.join(kitdir, name))
        cadvol, body, note = cad.get(name, (None, "", ""))
        dv = derived.get(coupon) if mode == "analytic" else None
        d_cad = rel(occt, cadvol) if (occt and cadvol) else None
        d_der = rel(dv, occt) if (dv and occt) else None
        rows.append({
            "file": name, "body_type": body,
            "derived": "%.6f" % dv if dv else "",
            "occt": "%.6f" % occt if occt else "",
            "cad": "%.6f" % cadvol if cadvol else "",
            "cad_vs_occt_pct": "%.4f" % (100 * d_cad) if d_cad is not None else "",
            "occt_vs_derived_pct": "%.6f" % (100 * d_der) if d_der is not None else "",
            "note": note,
        })
        if d_cad is not None and abs(d_cad) > args.tolerance:
            flagged.append((name, occt, cadvol, 100 * d_cad, body))

    out = args.out or os.path.join(kitdir, "crosscheck.csv")
    with open(out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    if flagged:
        print("The CAD system and OpenCASCADE disagree on these, by more than %.2f%%:\n"
              % (100 * args.tolerance))
        print("%-38s %14s %14s %9s  %s" % ("file", "OCCT", "CAD", "diff", "body"))
        for name, occt, cadvol, pct, body in flagged:
            print("%-38s %14.4f %14.4f %8.2f%%  %s" % (name, occt, cadvol, pct, body))
        print("\nA faceted control in that list means something basic is wrong - units or\n"
              "settings. An analytic file alone in it means the disagreement is about the\n"
              "analytic surfaces, and the coupon passed every other criterion anyway.")
    else:
        print("no disagreement beyond %.2f%%" % (100 * args.tolerance))
    print("\n%d files, results: %s" % (len(rows), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
