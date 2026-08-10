#!/usr/bin/env python3

# STEP export sanity checker
#
# Exports the input file to STEP and validates the result (see validatestep.py
# for the list of checks and the defect each of them guards against).
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
import subprocess
import sys

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
    subprocess.check_call(cmd, env=env)
    if not os.path.exists(outfile):
        failquit("no STEP file was written to " + outfile)


def normalized(path):
    """File content without the header line carrying the export timestamp."""
    with open(path, encoding="utf-8", errors="replace") as f:
        return [line for line in f.read().splitlines() if not line.startswith("FILE_NAME")]


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

if ok:
    os.unlink(stepfile)
else:
    print("keeping %s for inspection" % stepfile, file=sys.stderr)

sys.exit(0 if ok else 1)
