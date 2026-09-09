"""Edge lengths and curve kinds straight out of the STEP text, no kernel.

Every other instrument here goes through OpenCASCADE, which repairs on read -
it widens edge tolerances by up to a tenth of a millimetre and says nothing.
This one parses the file, so what it reports is what was written.

Per file: the distance between each EDGE_CURVE's two vertex points, split by
the kind of curve the edge is written on, plus how many of those edges are
short.  A sliver on a planar facet is harmless; a sliver bounding a face whose
surface curves through it is where a kernel runs out of room.
"""
import os
import re
import sys

NUM = re.compile(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?")


def load(path):
    ents = {}
    buf = ""
    for line in open(path, errors="replace"):
        buf += line.strip()
        if buf.endswith(";"):
            m = re.match(r"#(\d+)\s*=\s*(.*);$", buf)
            if m:
                ents[int(m.group(1))] = m.group(2)
            buf = ""
    return ents


def main():
    print("%-34s %7s %6s %9s %9s %6s %6s %6s  %s"
          % ("file", "open", "closed", "shortest", "median", "<1e-2", "<1e-3",
             "<1e-4", "kinds of the ten shortest"))
    for path in sys.argv[1:]:
        ents = load(path)
        pts = {}
        for k, v in ents.items():
            if v.startswith("CARTESIAN_POINT"):
                n = NUM.findall(v[v.index("(", 15):])
                if len(n) >= 3:
                    pts[k] = tuple(float(x) for x in n[:3])
        vtx = {}
        for k, v in ents.items():
            if v.startswith("VERTEX_POINT"):
                r = re.findall(r"#(\d+)", v)
                if r:
                    vtx[k] = pts.get(int(r[0]))
        rows = []
        closed = 0
        for k, v in ents.items():
            if not v.startswith("EDGE_CURVE"):
                continue
            r = [int(x) for x in re.findall(r"#(\d+)", v)]
            if len(r) < 3:
                continue
            a, b, c = vtx.get(r[0]), vtx.get(r[1]), ents.get(r[2], "")
            if a is None or b is None:
                continue
            # A closed edge - a full circle bounded by one vertex used twice -
            # has its two ends in the same place by construction and would sit
            # at the bottom of every list, hiding the slivers that matter.
            if r[0] == r[1] or a == b:
                closed += 1
                continue
            d = sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5
            rows.append((d, c.split("(")[0]))
        rows.sort()
        n = len(rows)
        med = rows[n // 2][0] if n else 0.0
        kinds = ",".join(sorted({t for _, t in rows[:10]}))
        print("%-34s %7d %6d %9.6f %9.4f %6d %6d %6d  %s"
              % (os.path.basename(path), n, closed, rows[0][0] if n else 0, med,
                 sum(1 for d, _ in rows if d < 1e-2),
                 sum(1 for d, _ in rows if d < 1e-3),
                 sum(1 for d, _ in rows if d < 1e-4), kinds))


if __name__ == "__main__":
    main()
