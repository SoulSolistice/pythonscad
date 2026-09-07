"""Two declared sweeps at different resolutions, which is what tells a bound from a value.

Every other fixture here declares at most one grid, and a model with one grid
cannot tell a figure that means "the band" from one that means "the last band I
happened to see". This one declares two, an order of magnitude apart, and the
difference becomes a sentence that contradicts itself.

The two ridges are the same helix at 12 stations and at 48. A cubic through 12
stations of a helix departs from the chords joining them by far more than one
through 48 does - the sagitta of a station falls as the square of the spacing -
so the coarse sweep publishes a tessellation band of 0.6519 and the fine one
0.0657. Neither is wrong; they are two different surfaces stating their own
resolutions, which is exactly what a band is for.

What that catches: the corner placement judges each junction corner against the
band of *its own* sweep, and then reports one figure for all of them. Written as
the last band seen rather than the widest, the line came out as

    520 of those move no further than their sweep's own tessellation band, the
    widest of which is 0.0657, by at most 0.2243

which cannot be true of anything: a corner does not move 0.2243 while moving no
further than 0.0657. With one sweep the two forms are identical and no fixture
could see the difference. The same shape of defect sat in the sweep's own
"against an allowance of" figure, and is asserted below for the same reason.

The numbers are the model's. 520 junction corners is what the two unions leave;
the two bands are properties of the two grids and are printed by the exporter
that fitted them. What this fixture asserts is not their values but that the
report does not contradict itself about them - report_contradictions() in
stepexportsanitytest.py checks that automatically, and this is the model that
makes it fire.
"""
# EXPECT: 3 analytic surfaces available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 2 swept grid), and 2 declared planes
# The two bands, an order of magnitude apart, which is the whole point of the model.
# EXPECT: a declared 12x4 cubic sweep claims 60 facets whole, 144 cut across it, within its tessellation band of 0.6519
# EXPECT: a declared 48x4 cubic sweep claims 237 facets whole, 146 cut across it, within its tessellation band of 0.0657
# And the bound that has to be the wider of the two, not whichever was seen last.
# EXPECT: their sweep's own tessellation band, the widest of which is 0.6519
from pythonscad import *
import math


def ridge(rows, R, pitch, turns, depth, zoff):
    pts, faces = [], []
    for i in range(rows):
        t = i / (rows - 1)
        a = 2 * math.pi * turns * t
        z = zoff + pitch * turns * t
        for dr, dz in ((0.6, -1.2), (-depth, -0.4), (-depth, 0.4), (0.6, 1.2)):
            pts.append([(R + dr) * math.cos(a), (R + dr) * math.sin(a), z + dz])
    for i in range(rows - 1):
        for j in range(4):
            a0 = i * 4 + j
            b0 = i * 4 + (j + 1) % 4
            c0 = (i + 1) * 4 + (j + 1) % 4
            d0 = (i + 1) * 4 + j
            faces += [[a0, c0, b0], [a0, d0, c0]]
    faces += [[0, 1, 2, 3],
              [(rows - 1) * 4 + 3, (rows - 1) * 4 + 2, (rows - 1) * 4 + 1, (rows - 1) * 4]]
    grid = [[pts[i * 4 + j] for j in range(4)] for i in range(rows)]
    return polyhedron(points=pts, faces=faces).declare_grid(points=grid, closed=True)


coarse = ridge(rows=12, R=20.0, pitch=6.0, turns=0.75, depth=1.0, zoff=1.0)
fine = ridge(rows=48, R=20.0, pitch=6.0, turns=0.75, depth=1.0, zoff=9.0)
(coarse | fine | cylinder(r=19.2, h=16, fn=96)).show()
