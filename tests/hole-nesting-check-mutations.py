"""Prove check_hole_nesting refuses what it is there to refuse, and nothing else.

A hole has to sit directly inside the outer bound of the face carrying it; a
loop of the same plane lying between the two means the hole belongs to that
face instead. "The same plane" is the whole question, and the check answers it
by a key made of the plane's normal and offset - which has to be one key for a
plane whichever way a loop on it winds, and a different key for any other
plane. It took the sign of each component of the normal on its own until
2026-09-30, so a plane and its mirror image in a coordinate plane shared a key:
the two facets of step-hole-in-a-fanned-facet at y = +9.53 and y = -9.53 were
each the intervening loop of the other's hole (open item 39).

Each case below is a face with a hole and a second face that encloses the hole
when both are seen along one axis. It lies between them on the same plane -
one aligned with an axis, and the diagonal x = y, aligned with none and with
two components of its normal equal and opposite - or on a mirror plane or a
parallel one, where it lies between nothing. The second face always winds the
other way, so a key that told the two windings apart would miss the first two.

Run from the repository root: python3 tests/hole-nesting-check-mutations.py
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validatestep import parse_step, check_hole_nesting


class File:
    """A STEP file of planar faces, entity by entity."""

    def __init__(self):
        self.lines = []
        self.n = 0

    def add(self, text):
        self.n += 1
        self.lines.append("#%d = %s;" % (self.n, text))
        return self.n

    def loop(self, pts):
        vs = [self.add("VERTEX_POINT('',#%d)" % self.add("CARTESIAN_POINT('',(%r,%r,%r))" % p)) for p in pts]
        oes = []
        for k in range(len(vs)):
            origin = self.add("CARTESIAN_POINT('',(0.,0.,0.))")
            vector = self.add("VECTOR('',#%d,1.)" % self.add("DIRECTION('',(1.,0.,0.))"))
            line = self.add("LINE('',#%d,#%d)" % (origin, vector))
            edge = self.add("EDGE_CURVE('',#%d,#%d,#%d,.T.)" % (vs[k], vs[(k + 1) % len(vs)], line))
            oes.append(self.add("ORIENTED_EDGE('',*,*,#%d,.T.)" % edge))
        return self.add("EDGE_LOOP('',(%s))" % ",".join("#%d" % o for o in oes))

    def face(self, outer, holes=()):
        # The PLANE's own placement is not read by the check - it asks the loops.
        placement = self.add("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)" % (
            self.add("CARTESIAN_POINT('',(0.,0.,0.))"), self.add("DIRECTION('',(0.,0.,1.))"),
            self.add("DIRECTION('',(1.,0.,0.))")))
        plane = self.add("PLANE('',#%d)" % placement)
        bounds = [self.add("FACE_OUTER_BOUND('',#%d,.T.)" % self.loop(outer))]
        bounds += [self.add("FACE_BOUND('',#%d,.T.)" % self.loop(h)) for h in holes]
        return self.add("ADVANCED_FACE('',(%s),#%d,.T.)" % (",".join("#%d" % b for b in bounds), plane))

    def text(self):
        return "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\n" + "\n".join(self.lines) + "\nENDSEC;\nEND-ISO-10303-21;\n"


def square(lo, hi, ccw=True):
    """A square [lo, hi]^2 in the (a, b) coordinates of a plane."""
    pts = [(lo, lo), (hi, lo), (hi, hi), (lo, hi)]
    return pts if ccw else pts[::-1]


def on_z(z):
    return lambda pts: [(float(a), float(b), float(z)) for a, b in pts]


def on_y(y):
    return lambda pts: [(float(a), float(y), float(b)) for a, b in pts]


def on_diagonal(pts):
    # the plane x = y, spanned by (1, 1, 0) / sqrt(2) and z
    return [(a / 2 ** 0.5, a / 2 ** 0.5, float(b)) for a, b in pts]


def problems_of(face_plane, other_plane):
    """A face 0..10 with a hole 4..6 on `face_plane`, and a face 2..8 wound the
    other way on `other_plane`."""
    f = File()
    f.face(face_plane(square(0, 10)), [face_plane(square(4, 6, ccw=False))])
    f.face(other_plane(square(2, 8, ccw=False)))
    fd, path = tempfile.mkstemp(suffix=".stp")
    try:
        with os.fdopen(fd, "w", newline="") as out:
            out.write(f.text())
        entities, _ = parse_step(path)
    finally:
        os.remove(path)
    problems = []
    check_hole_nesting(entities, problems)
    return problems


failures = []


def run(name, face_plane, other_plane, want):
    got = "rejected" if problems_of(face_plane, other_plane) else "accepted"
    print("%-44s %s" % (name, got))
    if got != want:
        failures.append("%s: %s, expected %s" % (name, got, want))


run("intervening face on the same plane", on_z(0), on_z(0), "rejected")
run("intervening face on the plane x = y", on_diagonal, on_diagonal, "rejected")
run("the same face on the mirror plane", on_y(3), on_y(-3), "accepted")
run("the same face on a parallel plane", on_z(0), on_z(1), "accepted")

print()
if failures:
    for f in failures:
        print("FAIL " + f)
    sys.exit(1)
print("all hole-nesting mutations behaved as expected")
