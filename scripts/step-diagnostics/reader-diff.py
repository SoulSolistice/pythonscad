#!/usr/bin/env python3
"""What changed between two STEP files whose entity ids do not correspond.

Written for one question in particular: *what did the reader actually do to our
file?* A CAD system renumbers every entity when it saves, so a text diff of its
output against ours - or of its output before and after a repair - is noise. This
matches by geometry instead:

  * **faces** by their surface's kind and parameters, and by the set of corner
    coordinates their bounds visit, so a face that moved, changed surface or
    disappeared is named;
  * **curves** by their control points, centre, axes and radii, so a curve that
    was re-fitted is named and its displacement measured.

Two ways to use it, and the second is the one that isolates:

    reader-diff.py ours.stp theirs_SW.STEP
        what the reader made of what we wrote.

    reader-diff.py theirs_SW.STEP theirs_SW_healed.STEP
        what its *repair* changed, and nothing else: same writer, same settings,
        same session, one repaired. On f04 on 2026-09-17 that answered a question
        no fault list could - every face matched on surface and corners, the
        census was identical, and 215 B-spline curves had been re-fitted by a
        median of 4.281e-07 and at most 9.583e-05 with both endpoints unmoved. So
        "General geometry problem" is a curve a ten-thousandth of a millimetre off
        the surface it bounds, and nothing structural at all.

`scripts/step-interop-solidworks.ps1 -RoundTrip -Heal` writes both files.

No kernel is in the path, so nothing is repaired on the way in.
"""

import math
import os
import re
import sys
from importlib.machinery import SourceFileLoader

_here = os.path.dirname(os.path.abspath(__file__))
voc = SourceFileLoader("vertex_on_curve", os.path.join(_here, "vertex-on-curve.py")).load_module()

Q = 6  # coordinates are rounded to this many decimals before matching


def _bare(args):
    """The numeric arguments of an entity, with references and labels removed.

    '#7169' is an id, not a parameter, and leaving it in makes every plane unique
    to the file it came from.
    """
    return voc.floats(re.sub(r"#\d+", " ", re.sub(r"'[^']*'", " ", args)))


def read(path):
    ents = voc.parse(path)
    oriented = {i: voc.refs(a)[0] for i, (k, a) in ents.items()
                if k == "ORIENTED_EDGE" and voc.refs(a)}
    loops = {i: voc.refs(a) for i, (k, a) in ents.items() if k == "EDGE_LOOP"}
    bounds = {i: voc.refs(a)[0] for i, (k, a) in ents.items()
              if (k.endswith("FACE_BOUND") or k.endswith("FACE_OUTER_BOUND")) and voc.refs(a)}
    return ents, oriented, loops, bounds


def faces(path):
    ents, oriented, loops, bounds = read(path)

    def vpoint(v):
        return voc.point(ents, voc.refs(ents[v][1])[0])

    out = {}
    for i, (k, a) in ents.items():
        if k != "ADVANCED_FACE":
            continue
        r = voc.refs(a)
        skind, sargs = ents[r[-1]]
        nums = []
        sr = voc.refs(sargs)
        if sr and sr[0] in ents and ents[sr[0]][0] == "AXIS2_PLACEMENT_3D":
            o, ax, rf = voc.placement(ents, sr[0])
            nums += [round(x, Q) for x in o + ax + rf]
        nums += [round(x, Q) for x in _bare(sargs)]
        corners = set()
        for b in r[:-1]:
            lp = bounds.get(b)
            if lp is None:
                continue
            for o in loops.get(lp, []):
                ec = oriented.get(o)
                if ec is None or ec not in ents:
                    continue
                for v in voc.refs(ents[ec][1])[:2]:
                    try:
                        corners.add(tuple(round(x, Q) for x in vpoint(v)))
                    except Exception:
                        pass
        out.setdefault((skind, tuple(nums)), set()).update(corners)
    return out


def curves(path):
    ents = voc.parse(path)
    out = []
    for i, (k, a) in ents.items():
        try:
            if k.startswith("B_SPLINE_CURVE"):
                ids = voc.refs(a.split("(", 1)[1].split(")", 1)[0])
                pts = [voc.point(ents, j) for j in ids]
                if len(pts) >= 2:
                    out.append(("bspline", i, pts))
            elif k in ("CIRCLE", "ELLIPSE", "HYPERBOLA", "PARABOLA"):
                o, ax, rf = voc.placement(ents, voc.refs(a)[0])
                out.append((k.lower(), i, [o, ax, rf] + [[v, 0, 0] for v in _bare(a)]))
            elif k == "LINE":
                r = voc.refs(a)
                base = voc.point(ents, r[0])
                d = voc.point(ents, voc.refs(ents[r[1]][1])[0])
                out.append(("line", i, [base, d]))
        except Exception:
            pass
    return out


def key_of(kind, pts):
    return (kind, tuple(tuple(round(x, 9) for x in p) for p in pts))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    a_path, b_path = sys.argv[1], sys.argv[2]
    A, B = faces(a_path), faces(b_path)
    only_a = [k for k in A if k not in B]
    only_b = [k for k in B if k not in A]
    moved = [(k, len(A[k] - B[k]), len(B[k] - A[k])) for k in A if k in B and A[k] != B[k]]

    print("%s  ->  %s" % (os.path.basename(a_path), os.path.basename(b_path)))
    print("faces: %d / %d;  only in the first: %d;  only in the second: %d;  corners differ: %d"
          % (len(A), len(B), len(only_a), len(only_b), len(moved)))
    for k in only_a[:6]:
        print("   only in the first:  %-26s %s" % (k[0], str(k[1])[:90]))
    for k in only_b[:6]:
        print("   only in the second: %-26s %s" % (k[0], str(k[1])[:90]))
    for k, na, nb in moved[:6]:
        print("   corners differ:     %-26s %d gone, %d new" % (k[0], na, nb))

    ca, cb = curves(a_path), curves(b_path)
    ka = {}
    for kind, i, pts in ca:
        ka.setdefault(key_of(kind, pts), []).append(i)
    kb = {}
    for kind, i, pts in cb:
        kb.setdefault(key_of(kind, pts), []).append(i)
    lost = [(kind, i, pts) for kind, i, pts in ca if key_of(kind, pts) not in kb]
    gained = [(kind, i, pts) for kind, i, pts in cb if key_of(kind, pts) not in ka]
    print("curves: %d / %d;  replaced: %d;  new: %d" % (len(ca), len(cb), len(lost), len(gained)))

    # How far each replaced curve moved: match it to the nearest new one by ends.
    rows = []
    for kind, i, pa in lost:
        best = None
        for kind2, j, pb in gained:
            if kind2 != kind or len(pb) != len(pa):
                continue
            d = math.dist(pa[0], pb[0]) + math.dist(pa[-1], pb[-1])
            if best is None or d < best[0]:
                best = (d, j, pb)
        if best is None:
            continue
        d, j, pb = best
        rows.append((max(math.dist(x, y) for x, y in zip(pa, pb)), d / 2, kind, i, j))
    if rows:
        rows.sort(reverse=True)
        vals = sorted(r[0] for r in rows)
        print("   displacement of a replaced curve against its nearest replacement:")
        print("      n=%d  median %.3e  max %.3e" % (len(vals), vals[len(vals) // 2], vals[-1]))
        for worst, ends, kind, i, j in rows[:5]:
            print("      %-8s worst %.6f  ends %.6f  #%d -> #%d" % (kind, worst, ends, i, j))
    return 0


if __name__ == "__main__":
    sys.exit(main())
