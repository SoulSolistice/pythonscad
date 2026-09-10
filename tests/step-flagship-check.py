#!/usr/bin/env python3
"""Export the coupons this exporter's work is actually about, and check them.

The fixture suite globs `tests/data/scad/step-export/*.scad` and runs each at
whatever parameters the file sets. That leaves two gaps, and both of them have
now let a broken export past a green run:

  * **The band family is a family.** `step-band-family.scad` declares `FN = 32`
    and the fixture exports exactly that. The interop kit sweeps it over 24, 32,
    48, 64 and 96, and the tessellation is the whole point of the coupon - it is
    what decides how much of the sweep the recogniser claims, how the boolean
    cuts it, and how many corners the placement moves. On 2026-09-10 a change to
    the corner placement left `$fn` 32 valid and opened the shell at `$fn` 64,
    and `ctest -R 'export-step-|mutations'` passed 50 of 50.

  * **The reference parts are not fixtures at all.** `lid10` is the part this
    work exists for, and it is exercised only by hand and by the interop kit. The
    same change put two `VERTEX_POINT`s on the same coordinates in it, and again
    the suite said nothing.

Both were caught by `validatestep.py`, which the suite already owns - it was
simply never pointed at these files. So this is not a new check. It is the
checks that exist, run over the coupons that were missing.

What it deliberately does *not* do is assert face counts, volumes or censuses.
Those are expectations, they have to be **derived from the model** rather than
captured from a run (see `doc/step-export-development.md`), and a captured one
pins today's defects as tomorrow's contract. Coupons earn those one at a time,
as fixtures. What this asserts is only what is true of any correct export
whatever its numbers: the file parses, the shell closes, vertices are shared,
and a CAD kernel can read it back as a solid.
"""

import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from validatestep import validateSTEP  # noqa: E402
import steproundtrip  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The analytic path is behind two experimental flags and needs both to be
# exercised whole; without them the exporter writes facets and every check here
# passes for the wrong reason.
FLAGS = ["--enable=step-analytic-surfaces", "--enable=step-approximate-surfaces"]

# name, source, extra arguments. The band family at its own default is left out:
# the fixture suite already exports it.
COUPONS = [
    ("band-fn024", "tests/data/scad/step-export/step-band-family.scad", ["-D", "FN=24"]),
    ("band-fn048", "tests/data/scad/step-export/step-band-family.scad", ["-D", "FN=48"]),
    ("band-fn064", "tests/data/scad/step-export/step-band-family.scad", ["-D", "FN=64"]),
    ("band-fn096", "tests/data/scad/step-export/step-band-family.scad", ["-D", "FN=96"]),
    ("lid10", "examples/step_test/lid10.scad",
     ["-p", "examples/step_test/lid10.json", "-P", "New set 1"]),
    ("bayonet", "examples/step_test/bayonet_container_v1-2.scad",
     ["-p", "examples/step_test/bayonet_container_v1-2.json", "-P", "New set 1"]),
]


def export(binary, source, target, extra):
    cmd = [binary, source, "-o", target, "--trust-python"] + list(extra) + FLAGS
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


# Messages the exporter must never emit. `EXPORT-ERROR` is the exporter saying
# its own output is wrong - today the only one it can raise from this path is two
# faces contradicting each other about one edge, which opens the shell.
#
# The strict list is different in kind: these are reached today, and are here so
# that the day a fix stops them, something says so. See `--strict`.
STRICT_MUST_NOT_SAY = [
    ("corners are left where the mesh put them: moving them would bend",
     "the plane veto gave up the crossing-curve boundary"),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--openscad", required=True, help="path to the staged pythonscad binary")
    ap.add_argument("--strict", action="store_true",
                    help="also require the messages in STRICT_MUST_NOT_SAY to be absent. "
                         "Registered as a WILL_FAIL test: it is expected to fail today, and "
                         "when it starts passing ctest reports *that* as the failure, which "
                         "is how the win gets noticed instead of going quiet.")
    args, _ = ap.parse_known_args()
    binary = os.path.abspath(args.openscad)
    if not os.path.isfile(binary):
        print("no such binary: " + binary, file=sys.stderr)
        return 1

    if not steproundtrip.available():
        # Say so rather than skipping in silence: a green run that never read a
        # file back with a kernel is not the run this test exists to be.
        print("note: OCP is not installed, so the kernel round trip is skipped; "
              "install cadquery-ocp==7.8.1.1.post1 to have it run", file=sys.stderr)

    failures = []
    tmp = tempfile.mkdtemp(prefix="step-flagship-")
    for name, source, extra in COUPONS:
        if not os.path.isfile(os.path.join(ROOT, source)):
            print("SKIP %-12s (no %s)" % (name, source), file=sys.stderr)
            continue
        target = os.path.join(tmp, name + ".stp")
        code, output = export(binary, source, target, extra)
        if code != 0 or not os.path.isfile(target):
            print("FAIL %-12s export exited %d" % (name, code), file=sys.stderr)
            print(output, file=sys.stderr)
            failures.append(name)
            continue
        # The exporter's own verdict on its own output, and it outranks every
        # check below: a file it calls wrong is wrong whatever a validator makes
        # of it.
        errors = [ln for ln in output.splitlines() if "EXPORT-ERROR" in ln]
        if errors:
            print("FAIL %-12s the exporter reported an error" % name, file=sys.stderr)
            for ln in errors:
                print("       " + ln.strip(), file=sys.stderr)
            failures.append(name)
            continue
        if not validateSTEP(target):
            print("FAIL %-12s the export is not valid STEP" % name, file=sys.stderr)
            failures.append(name)
            continue
        if steproundtrip.available() and not steproundtrip.roundtripSTEP(target):
            print("FAIL %-12s a kernel could not read it back as a solid" % name, file=sys.stderr)
            failures.append(name)
            continue
        if args.strict:
            said = [why for needle, why in STRICT_MUST_NOT_SAY if needle in output]
            if said:
                print("FAIL %-12s %s" % (name, "; ".join(said)), file=sys.stderr)
                failures.append(name)
                continue
        print("ok   %-12s %s" % (name, os.path.basename(source)), file=sys.stderr)

    if failures:
        print("\n%d flagship coupon(s) failed: %s" % (len(failures), ", ".join(failures)),
              file=sys.stderr)
        return 1
    print("\nall flagship coupons export, validate and read back", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
