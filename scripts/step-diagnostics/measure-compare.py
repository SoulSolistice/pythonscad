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

    measure-compare.py <run a> <run b> [--show N] [--relabel-originals]

--relabel-originals renumbers each export's original ids by rank before
comparing, in the report ("original N"), the ledger's origin column and the
surfaces table's owners and witnesses. An original id is a label Manifold mints
from a global counter, so a change that mints one more id anywhere - a Manifold
built once more on the way - shifts every later label without changing what any
original is or owns. With the flag, that relabelling compares equal and nothing
else does; without it, it is a difference like any other.

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

# Wall-clock time, the ledger's own path, and the geometry cache's byte count -
# memory accounting, which moves whenever a geometry carries a field more, and
# says nothing about what was exported.
SKIP = re.compile(r"Total rendering time|written to .*ledger|Geometry cache size in bytes")
ORIGINAL = re.compile(r"\boriginal (\d+)")
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


def original_ids(report_lines, ledger_lines, surface_lines):
    """Every original id an export mentions, by rank: the renumbering
    --relabel-originals applies."""
    ids = set()
    for ln in report_lines or []:
        ids.update(int(m) for m in ORIGINAL.findall(ln))
    for ln in (ledger_lines or [])[1:]:
        cols = ln.split("	")
        if len(cols) > 1 and cols[1].lstrip("-").isdigit():
            ids.add(int(cols[1]))
    for ln in (surface_lines or [])[1:]:
        for col in ln.split("	")[-2:]:
            ids.update(int(x) for x in col.split(",") if x.isdigit())
    return {i: r for r, i in enumerate(sorted(i for i in ids if i >= 0))}


def relabel(kind, lines, rank):
    if lines is None:
        return None
    out = []
    for ln in lines:
        if kind == "report":
            ln = ORIGINAL.sub(lambda m: "original #%d" % rank.get(int(m.group(1)), -1), ln)
        elif kind == "ledger":
            cols = ln.split("	")
            if len(cols) > 1 and cols[1].isdigit():
                cols[1] = "#%d" % rank.get(int(cols[1]), -1)
            ln = "	".join(cols)
        elif kind == "surfaces":
            cols = ln.split("	")
            for k in (len(cols) - 2, len(cols) - 1):
                if k > 0 and cols[k] != "-" and all(x.isdigit() for x in cols[k].split(",")):
                    cols[k] = ",".join("#%d" % rank.get(int(x), -1) for x in cols[k].split(","))
            ln = "	".join(cols)
        out.append(ln)
    return sorted(out) if kind != "report" else out


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
    ap.add_argument("--relabel-originals", action="store_true",
                    help="compare original ids by rank, not by the label Manifold minted")
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
        ga, gb = ledger(base.with_suffix(".ledger.tsv")), ledger(obase.with_suffix(".ledger.tsv"))
        sa = ledger(Path(str(base) + ".ledger.tsv.surfaces.tsv"))
        sb = ledger(Path(str(obase) + ".ledger.tsv.surfaces.tsv"))
        if args.relabel_originals:
            rank_a, rank_b = original_ids(la, ga, sa), original_ids(lb, gb, sb)
            la, lb = relabel("report", la, rank_a), relabel("report", lb, rank_b)
            ga, gb = relabel("ledger", ga, rank_a), relabel("ledger", gb, rank_b)
            sa, sb = relabel("surfaces", sa, rank_a), relabel("surfaces", sb, rank_b)
        if la != lb:
            bad += 1
            print(f"REPORT   {rel}")
            print("\n".join(first_differences(la or [], lb or [], args.show)))
        if ga != gb:
            bad += 1
            print(f"LEDGER   {rel}" + ("" if ga and gb else " (missing on one side)"))
            print("\n".join(first_differences(ga or [], gb or [], args.show)))
        if sa != sb:
            bad += 1
            print(f"SURFACES {rel}")
            print("\n".join(first_differences(sa or [], sb or [], args.show)))
    print(f"compared {len(exports)} exports: {bad} differences")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
