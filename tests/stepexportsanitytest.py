#!/usr/bin/env python3

# STEP export sanity checker
#
# Exports the input file to STEP and validates the result (see validatestep.py
# for the list of checks and the defect each of them guards against).
#
# A fixture can state what the exporter is expected to report about it, by
# writing EXPECT: lines into its own comment or docstring (see expectations()
# below). Those are checked against the analytic run. Without them the only
# thing this script asserts about the analytic export is that it is valid,
# which a silently faceted export also is - that is how a regression that
# dropped every Bezier patch of the fillet fixture went unnoticed while the
# test stayed green.
#
# The export is run a second time under a locale which uses a comma as decimal
# separator, if the machine has one. openscad.cc calls setlocale(LC_ALL, ""),
# so the number formatting in the exporter has to be independent of it; the two
# exports have to come out identical. Without a suitable locale installed that
# half of the test is skipped.
#
# Usage: <script> <inputfile> --openscad=<executable-path> [<openscad args>] tmpfilebasename

import argparse
import locale
import os
import re
import subprocess
import sys

from steproundtrip import roundtripSTEP
from validatestep import validateSTEP

# locales which conventionally use a comma radix, cheapest first
LOCALE_CANDIDATES = [
    "de_DE.UTF-8", "de_DE.utf8", "de_DE",
    "fr_FR.UTF-8", "fr_FR.utf8", "fr_FR",
    "es_ES.UTF-8", "it_IT.UTF-8", "nl_NL.UTF-8", "pt_BR.UTF-8",
    "pl_PL.UTF-8", "ru_RU.UTF-8", "sv_SE.UTF-8", "cs_CZ.UTF-8",
]


def failquit(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


def find_comma_locale():
    """Return the name of an installed locale whose decimal separator is a comma."""
    previous = locale.setlocale(locale.LC_NUMERIC)
    try:
        for name in LOCALE_CANDIDATES:
            try:
                locale.setlocale(locale.LC_NUMERIC, name)
            except locale.Error:
                continue
            if locale.localeconv()["decimal_point"] == ",":
                return name
    finally:
        try:
            locale.setlocale(locale.LC_NUMERIC, previous)
        except locale.Error:
            pass
    return None


def export(openscad, inputfile, outfile, extra_args, env=None):
    cmd = [openscad, inputfile, "-o", outfile] + extra_args
    print("Running PythonSCAD:", " ".join(cmd), file=sys.stderr)
    sys.stderr.flush()
    # Capture the output rather than letting check_call raise: what PythonSCAD
    # printed is the whole diagnosis when an export fails, and a traceback of
    # this script hides it.
    proc = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = proc.stdout.decode("utf-8", "replace").strip()
    if output:
        print(output, file=sys.stderr)
    if proc.returncode != 0:
        failquit("PythonSCAD exited with status %d without exporting %s" % (proc.returncode, outfile))
    if not os.path.exists(outfile):
        failquit("PythonSCAD reported success but wrote no file to " + outfile)
    return output


def normalized(path):
    """File content without the header line carrying the export timestamp."""
    with open(path, encoding="utf-8", errors="replace") as f:
        return [line for line in f.read().splitlines() if not line.startswith("FILE_NAME")]


def expectations(path):
    """The EXPECT:/EXPECT-NOT: lines a fixture states about itself.

    A fixture documents what the exporter should make of it in prose anyway;
    these turn one of those sentences into an assertion. The text after the
    marker is matched as a substring against everything the analytic run
    printed, so a fixture quotes the exporter's own line verbatim:

        # EXPECT: 20 Bezier patches cover 1100 facets
        # EXPECT-NOT: left faceted

    Whitespace inside the expected text is normalised, so a long line may be
    wrapped in the fixture. Fixtures which state nothing are not weakened by
    this - they are checked exactly as before."""
    want, unwanted = [], []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(r"EXPECT(-NOT)?:\s*(.+?)\s*$", line)
            if m:
                (unwanted if m.group(1) else want).append(" ".join(m.group(2).split()))
    return want, unwanted


def roundtrip_expectations(path):
    """The ROUNDTRIP: line a fixture states about what a CAD kernel reads.

    validatestep.py says the file is well formed by this project's own lights;
    this says OpenCASCADE agrees, and in particular that it reads a
    CYLINDRICAL_SURFACE *as a cylinder* rather than merely parsing it. The names
    are OCCT's own, from BRepAdaptor_Surface::GetType:

        # ROUNDTRIP: Cylinder=12 Sphere=8 Plane=6

    A fixture which states nothing still gets the round trip, just without the
    surface census assertion - reading as a closed, valid, positive volume solid
    is checked for every fixture either way."""
    wanted = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(r"ROUNDTRIP:\s*(.+?)\s*$", line)
            if not m:
                continue
            for token in m.group(1).split():
                key, _, value = token.partition("=")
                if value.isdigit():
                    wanted[key] = int(value)
    return wanted


def keyed_expectations(path, marker, cast=int):
    """A fixture line of `Key=value` tokens under `marker`."""
    wanted = {}
    pattern = re.compile(r"%s:\s*(.+?)\s*$" % marker)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pattern.search(line)
            if not m:
                continue
            for token in m.group(1).split():
                key, _, value = token.partition("=")
                try:
                    wanted[key] = cast(value)
                except ValueError:
                    pass
    return wanted


def canonical_expectations(path):
    """The CANONICAL: line a fixture states about its B-spline faces.

    The cross check on our own recogniser, running in the direction that finds a
    missed opportunity rather than a wrong answer: OpenCASCADE is asked, of every
    face we wrote as a spline, whether it is exactly a quadric after all.

        # CANONICAL: Cylinder=6 spline=24

    "spline" is the count OCCT agrees are genuinely not canonical. A fixture
    which states this is asserting both halves - that we did not miss more than
    we know about, and that we did not silently start missing something we used
    to catch, which is what a regression in quadricOfPatch would look like."""
    wanted = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(r"CANONICAL:\s*(.+?)\s*$", line)
            if not m:
                continue
            for token in m.group(1).split():
                key, _, value = token.partition("=")
                if value.isdigit():
                    wanted[key] = int(value)
    return wanted


def check_roundtrip(path, stepfile, what, expect_surfaces=None, expect_canonical=None,
                    expect_edges=None, expect_radii=None):
    """Read the export back with a real CAD kernel. Skipped when OCCT is absent.

    Returns True when the round trip passed or could not be run, so a machine
    without OpenCASCADE behaves exactly as it did before this check existed."""
    result, report = roundtripSTEP(
        stepfile,
        expect_surfaces=expect_surfaces,
        expect_canonical=expect_canonical,
        expect_edges=expect_edges,
        expect_radii=expect_radii,
    )
    if result is None:
        print("note: " + report[0], file=sys.stderr)
        return True
    for line in report:
        print("round trip (%s): %s" % (what, line), file=sys.stderr)
    if not result:
        print("the %s export does not survive a round trip through OpenCASCADE" % what,
              file=sys.stderr)
    return result


def check_expectations(path, output):
    """True when the analytic run reported what the fixture says it should."""
    want, unwanted = expectations(path)
    if not want and not unwanted:
        print(
            "note: %s states no EXPECT: line, so the analytic export is only "
            "checked for validity" % os.path.basename(path),
            file=sys.stderr,
        )
        return True
    flat = " ".join(output.split())
    missing = [text for text in want if text not in flat]
    present = [text for text in unwanted if text in flat]
    for text in missing:
        print("the analytic export never reported: " + text, file=sys.stderr)
    for text in present:
        print("the analytic export reported what %s rules out: %s" % (os.path.basename(path), text),
              file=sys.stderr)
    if missing or present:
        print(
            "%s states what the exporter should make of it; the run above did "
            "something else. An export can be perfectly valid and still have "
            "silently stopped recognising anything." % os.path.basename(path),
            file=sys.stderr,
        )
        return False
    return True


parser = argparse.ArgumentParser()
parser.add_argument("--openscad", required=True, help="Specify OpenSCAD executable.")
args, remaining_args = parser.parse_known_args()

inputfile = remaining_args[0]  # Can be .scad file or a file to be imported
stepfile = remaining_args[-1] + ".stp"
remaining_args = remaining_args[1:-1]  # Passed on to the OpenSCAD executable

if not os.path.exists(inputfile):
    failquit("cant find input file named: " + inputfile)
if not os.path.exists(args.openscad):
    failquit("cant find openscad executable named: " + args.openscad)

export(args.openscad, inputfile, stepfile, remaining_args)
ok = validateSTEP(stepfile)

# The faceted export is the baseline every fixture has to clear: whatever the
# analytic path does or does not recognise, the plain triangle mesh has to come
# back out of a CAD kernel as a closed solid.
if ok:
    ok = check_roundtrip(inputfile, stepfile, "faceted")

# Re-export with a comma radix locale and compare, so a number formatted
# through LC_NUMERIC cannot slip back in.
comma_locale = find_comma_locale()
if not comma_locale:
    print(
        "note: no locale with a comma decimal separator is installed, "
        "skipping the locale independence check",
        file=sys.stderr,
    )
elif ok:
    localefile = stepfile.replace(".stp", "-locale.stp")
    env = dict(os.environ)
    env["LC_ALL"] = comma_locale
    env["LC_NUMERIC"] = comma_locale
    print("Re-exporting under LC_ALL=%s" % comma_locale, file=sys.stderr)
    export(args.openscad, inputfile, localefile, remaining_args, env=env)
    if not validateSTEP(localefile):
        print(
            "the export written under %s is not valid; number formatting still "
            "depends on LC_NUMERIC" % comma_locale,
            file=sys.stderr,
        )
        ok = False
    elif normalized(localefile) != normalized(stepfile):
        print(
            "the export written under %s differs from the one written under the "
            "default locale" % comma_locale,
            file=sys.stderr,
        )
        ok = False
    if ok:
        os.unlink(localefile)


def surfaces_available(output):
    """The surface declarations that reached the exporter, from its own report.

    This is the declaration channel, and it is a different quantity from the
    surfaces actually written: a declaration is only a hint, and the exporter
    accepts it only where it also finds an exact fit and a topology that can
    take the substitution."""
    m = re.search(r"(\d+) analytic surfaces? available", output)
    return int(m.group(1)) if m else 0


# Export once more with the analytic geometry turned on. Every check in
# validatestep.py applies to that file too - a cylinder written as a
# CYLINDRICAL_SURFACE still has to leave the shell watertight, its rims still
# have to be used once in each direction - and the surface checks only have
# anything to look at here.
if ok:
    analyticfile = stepfile.replace(".stp", "-analytic.stp")
    analytic_flag = "--enable=step-analytic-surfaces"
    print("Re-exporting with " + analytic_flag, file=sys.stderr)
    output = export(args.openscad, inputfile, analyticfile, remaining_args + [analytic_flag])
    if not validateSTEP(analyticfile):
        print("the analytic export is not valid", file=sys.stderr)
        ok = False
    elif not check_expectations(inputfile, output):
        ok = False
    elif not check_roundtrip(
        inputfile,
        analyticfile,
        "analytic",
        roundtrip_expectations(inputfile) or None,
        canonical_expectations(inputfile) or None,
        keyed_expectations(inputfile, "EDGES") or None,
        keyed_expectations(inputfile, "RADII", float) or None,
    ):
        ok = False
    else:
        declared = surfaces_available(output)
        os.unlink(analyticfile)

        # Once more on the CGAL backend. A surface declaration rides along with
        # the geometry it describes, and each backend has its own representation
        # to carry it: on this one a boolean converts both operands to Nef
        # polyhedra, which had nowhere to put them and dropped every record on
        # the way in. That was invisible - both files validate, and an export
        # with no analytic surfaces looks exactly like a model that declared
        # none.
        #
        # What is compared is how many declarations *reached* the exporter, not
        # how many it wrote. Those are different questions, and only the first
        # one is about the channel. The backends mesh differently, so the same
        # model can offer identical declarations and still have a different
        # number of them accepted: a Nef boolean splits wall loops at the seams
        # where its operands met, and step-nested-rings comes out of it as 17
        # arcs where Manifold gives 5 closed rings. Requiring the two to write
        # the same surfaces would be asserting that the two backends produce the
        # same mesh, which they do not and need not.
        if declared:
            cgalfile = stepfile.replace(".stp", "-cgal.stp")
            print("Re-exporting with " + analytic_flag + " --backend=CGAL", file=sys.stderr)
            output = export(
                args.openscad,
                inputfile,
                cgalfile,
                remaining_args + [analytic_flag, "--backend=CGAL"],
            )
            carried = surfaces_available(output)
            if not validateSTEP(cgalfile):
                print("the analytic export on the CGAL backend is not valid", file=sys.stderr)
                ok = False
            elif carried != declared:
                print(
                    "%d surface declarations reached the exporter on the Manifold backend "
                    "and %d on CGAL: they did not survive the conversion to a Nef polyhedron"
                    % (declared, carried),
                    file=sys.stderr,
                )
                ok = False
            else:
                os.unlink(cgalfile)

if ok:
    os.unlink(stepfile)
else:
    print("keeping %s for inspection" % stepfile, file=sys.stderr)

sys.exit(0 if ok else 1)
