#!/usr/bin/env python3
"""Compare two `scripts/step-dev.sh measure` runs by content.

The oracle for a change that claims to change no behaviour. The exporter is not
byte-deterministic - entity numbering moves run to run - so files are never
diffed. Per export, three things must be identical:

  report   the export's own log, less wall-clock timings and the ledger's own
           "written to" line
  census   how many of each STEP entity type the file holds
  ledger   the claim ledger dump, row by row after sorting (the claims are data;
           the order a pass happened to record them in is not)

    measure-compare.py <run a> <run b> [--show N]

Exit status 0 only when every export agrees. Prints each difference with the
first few differing lines, so a failure says what moved, not only that
something did. Needs nothing but the standard library.
"""
import argparse
import difflib
import re
import sys
from collections import Counter
from pathlib import Path

SKIP = re.compile(r"Total rendering time|written to .*ledger")
ENTITY = re.compile(r"^#\d+\s*=\s*([A-Z_0-9]+)", re.M)


def report(path):
    if not path.exists():
        return None
    return [ln for ln in path.read_text(encoding="utf-8", errors="replace").splitlines() if not SKIP.search(ln)]


def census(path):
    return Counter(ENTITY.findall(path.read_text(encoding="utf-8", errors="replace")))


def ledger(path):
    if not path.exists():
        return None
    return sorted(path.read_text(encoding="utf-8", errors="replace").splitlines())


def first_differences(a, b, n):
    # An ordered diff, not a set difference: a line moved or printed twice is a
    # difference too, and a set comparison shows it as nothing at all - which
    # is how a duplicated report line first surfaced here as an empty REPORT.
    out = []
    for line in difflib.unified_diff(a, b, lineterm="", n=0):
        if line.startswith(("---", "+++", "@@")):
            continue
        out.append("  " + line[:200])
        if len(out) >= 2 * n:
            break
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--show", type=int, default=3, help="differing lines to print per difference")
    args = ap.parse_args()
    ra, rb = Path(args.a), Path(args.b)
    exports = sorted(ra.glob("*/*.stp"))
    if not exports:
        print(f"no exports under {ra}", file=sys.stderr)
        return 2
    bad = 0
    for stp in exports:
        rel = stp.relative_to(ra)
        other = rb / rel
        base, obase = stp.with_suffix(""), other.with_suffix("")
        if not other.exists():
            print(f"MISSING  {rel}")
            bad += 1
            continue
        ca, cb = census(stp), census(other)
        if ca != cb:
            bad += 1
            moved = sorted(k for k in set(ca) | set(cb) if ca[k] != cb[k])
            print(f"ENTITIES {rel}: " + ", ".join(f"{k} {ca[k]}->{cb[k]}" for k in moved[:8]))
        la, lb = report(base.with_suffix(".log")), report(obase.with_suffix(".log"))
        if la != lb:
            bad += 1
            print(f"REPORT   {rel}")
            print("\n".join(first_differences(la or [], lb or [], args.show)))
        ga, gb = ledger(base.with_suffix(".ledger.tsv")), ledger(obase.with_suffix(".ledger.tsv"))
        if ga != gb:
            bad += 1
            print(f"LEDGER   {rel}" + ("" if ga and gb else " (missing on one side)"))
            print("\n".join(first_differences(ga or [], gb or [], args.show)))
        sa = ledger(Path(str(base) + ".ledger.tsv.surfaces.tsv"))
        sb = ledger(Path(str(obase) + ".ledger.tsv.surfaces.tsv"))
        if sa != sb:
            bad += 1
            print(f"SURFACES {rel}")
            print("\n".join(first_differences(sa or [], sb or [], args.show)))
    print(f"compared {len(exports)} exports: {bad} differences")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
