#!/usr/bin/env python3
"""The `v` a face's own pcurves run between - does its loop cross the surface's seam?

A face written on a surface closed in `v` may not have a boundary that crosses
the seam: the pcurve has nowhere to go but the long way round, sweeping the whole
parameter range instead of stepping across, and a reader then has to repair a
face whose boundary contradicts its own surface. OpenCASCADE repairs it by
building a seam edge nobody wrote and inflating the vertex tolerance there -
0.7159 and 1.08 respectively on lid10 - and everything that touches that seam is
faulty afterwards. See `doc/step-export-wip.md`, open item 15.

Run it over a face id, with no argument to list the candidates:

    python scripts/step-diagnostics/seam-straddle.py file.stp          # closed surfaces
    python scripts/step-diagnostics/seam-straddle.py file.stp 1217     # one face

No kernel is in the path, so what it prints is what was written.
"""

import math
import os
import re
import sys
from importlib.machinery import SourceFileLoader

_here = os.path.dirname(os.path.abspath(__file__))
voc = SourceFileLoader("vertex_on_curve", os.path.join(_here, "vertex-on-curve.py")).load_module()


def closed_in_v(ents, surf):
    """Whether every row of a B-spline surface's net starts and ends at one point."""
    kind, args = ents[surf]
    if not kind.startswith("B_SPLINE_SURFACE"):
        return False
    net = args[args.find("(("):]
    rows = [r for r in re.findall(r"\(([^()]*)\)", net) if "#" in r]
    if not rows:
        return False
    for r in rows:
        ids = voc.refs(r)
        if len(ids) < 2:
            return False
        a, b = voc.point(ents, ids[0]), voc.point(ents, ids[-1])
        if math.dist(a, b) > 1e-9:
            return False
    return True


def seam_point(ents, surf):
    net = ents[surf][1][ents[surf][1].find("(("):]
    rows = [r for r in re.findall(r"\(([^()]*)\)", net) if "#" in r]
    return voc.point(ents, voc.refs(rows[0])[0]) if rows else None


def pcurve_vs(ents, ec, surf):
    """The v values a shared edge's pcurve on this surface runs between."""
    er = voc.refs(ents[ec][1])
    if len(er) < 3 or ents[er[2]][0] != "SURFACE_CURVE":
        return ents[er[2]][0] if len(er) > 2 else "?", []
    cr = voc.refs(ents[er[2]][1])
    out = []
    for pc in cr[1:]:
        if pc not in ents or ents[pc][0] != "PCURVE":
            continue
        pr = voc.refs(ents[pc][1])
        if pr[0] != surf:
            continue
        for g in voc.refs(ents[pr[1]][1]):
            if g not in ents:
                continue
            gk, ga = ents[g]
            if gk == "LINE":
                lr = voc.refs(ga)
                p = voc.floats(ents[lr[0]][1].split("(", 1)[1])[:2]
                vr = voc.refs(ents[lr[1]][1])
                d = voc.floats(ents[vr[0]][1].split("(", 1)[1])[:2]
                ln = voc.floats(ents[lr[1]][1].rsplit(",", 1)[1])[0]
                out.append([p[1], p[1] + d[1] * ln])
            elif gk.startswith("B_SPLINE_CURVE"):
                ids = voc.refs(ga.split("(", 1)[1].split(")", 1)[0])
                pts = [voc.floats(ents[i][1].split("(", 1)[1])[:2] for i in ids]
                out.append([q[1] for q in pts])
    return ents[cr[0]][0], out


def crossings(ents, face):
    """How many of a face's pcurves run more than half the profile - the long way
    round a seam it should have stepped across."""
    r = voc.refs(ents[face][1])
    surf = r[-1]
    found = 0
    for b in r[:-1]:
        if b not in ents:
            continue
        lp = voc.refs(ents[b][1])[0]
        for o in voc.refs(ents[lp][1]):
            ec = voc.refs(ents[o][1])[0]
            _, runs = pcurve_vs(ents, ec, surf)
            for run in runs:
                if max(run) - min(run) > 0.5:
                    found += 1
    return found


def report(ents, face):
    r = voc.refs(ents[face][1])
    surf = r[-1]
    print("face #%d on #%d %s%s"
          % (face, surf, ents[surf][0], "  (closed in v)" if closed_in_v(ents, surf) else ""))
    sp = seam_point(ents, surf) if closed_in_v(ents, surf) else None
    if sp:
        print("  seam at (%.4f %.4f %.4f)" % (sp[0], sp[1], sp[2]))
    crossings = 0
    used = []
    for b in r[:-1]:
        lp = voc.refs(ents[b][1])[0]
        for o in voc.refs(ents[lp][1]):
            ec = voc.refs(ents[o][1])[0]
            base, runs = pcurve_vs(ents, ec, surf)
            text = "; ".join(" ".join("%.4f" % q for q in run) for run in runs)
            flag = ""
            for run in runs:
                used.extend(run)
                if max(run) - min(run) > 0.5:
                    flag = "   <- crosses the seam"
                    crossings += 1
            print("  edge #%-6d %-26s v %s%s" % (ec, base, text, flag))
    if used:
        print("  v used: %.4f to %.4f" % (min(used), max(used)))
    print("  crossings: %d" % crossings)
    return crossings


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    ents = voc.parse(sys.argv[1])
    if len(sys.argv) > 2:
        return 1 if report(ents, int(sys.argv[2])) else 0
    total = 0
    for i, (kind, args) in sorted(ents.items()):
        if kind != "ADVANCED_FACE":
            continue
        surf = voc.refs(args)[-1]
        if not closed_in_v(ents, surf):
            continue
        total += report(ents, i)
        print("")
    print("faces whose loop crosses their surface's seam: %d" % total)
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
