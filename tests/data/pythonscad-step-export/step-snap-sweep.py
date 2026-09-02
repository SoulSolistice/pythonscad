"""A declared sweep cut by a plane, which is the case the ladder's fourth rung is for.

Every other sweep fixture cuts the ridge with a *declared cylinder*, and that
makes each cut vertex the property of two surfaces at once. This one cuts it
with a cube, which declares nothing, so provenance names exactly one owner at
every junction vertex - and that is the only configuration in which the exporter
moves a vertex at all.

The rungs, in the order they are tried, and what each of them does here:

    1  already on every surface that owns it     133 vertices, the untouched ridge
    2  provably on one of them                   none, by rung 1 having taken them
    3  a quadric owns it                         none: there is no quadric to own one
    4  one owner, a declared sweep               34 vertices, slid onto the sweep

A boolean cuts the *mesh* of the ridge, so the vertex it makes lands on the
chord between two stations rather than on the curve through them, up to a
sagitta off the surface the model meant. It cannot be fitted back - see
doc/step-export-status.md §20 - but it can be moved, and the direction is not
free: it lies on the cube's plane too, and every planar facet around it needs it
to stay there. So it slides *within* that plane onto the sweep, which is a point
of the intersection of the two exact surfaces and where the trim between them
belonged in the first place.

The assertions below are structural rather than numeric wherever they can be,
because most of the counts here are the mesher's business:

    two for 0, more for 0, none for 0     Derived. A cube declares no surface, so
                                          no vertex of this model can have a
                                          second owner. If that line ever reads
                                          otherwise the ownership mapping has
                                          started naming surfaces the model does
                                          not have.
    and 0 a quadric owns                  Derived, the same way. It is rung 3
                                          being *inapplicable* rather than
                                          passive, which is what makes rung 4
                                          reachable at all.
    slid onto the surface it was cut from  The rung firing. Deliberately without
                                          its count: how many vertices the
                                          boolean happens to make is tessellation
                                          detail, and pinning it would fail on
                                          any change to $fn that changes nothing
                                          about the rung.

And one thing this fixture asserts by *not* saying it: the analytic run must not
mention the slide. The ladder runs under step-approximate-surfaces only, because
after rung 3 the only vertices left to move belong to a declared sweep and a
declared sweep is written under approximation alone. The exact tier's input is
left exactly as it was, and EXPECT-NOT is what holds that.

No VOLUME. The two runs measure 265.71 and 282.73, six per cent apart, and both
are honest: the ridge is 1.6 wide and its tessellation band is 0.1290, so a
surface anywhere in that band moves the volume of a ribbon this thin by about
that much. It is the band being large *relative to the feature*, not a fit
straying - the same figure on the reference lid is 0.005%. A number that can be
argued either way is not an assertion, and the round trip census below is what
says the surface survived.
"""
# EXPECT: 1 analytic surface available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 swept grid)
# EXPECT: two for 0, more for 0, none for 0
# EXPECT-NOT: slid onto the surface it was cut from
#
# The end caps are why membership cannot be a question about corners alone.
# Both of them have every corner on the sweep - they are quads of the declared
# net - and their middles are a millimetre off it, because they cross the sweep
# rather than lying along it. The exporter refuses them on the centroid.
# EXPECT: facets have every corner on the sweep and their middle off it
#
# APPROX: slid onto the surface it was cut from
# APPROX: and 0 a quadric owns
# APPROX: 1 declared sweep written as one face each
#
# What a kernel makes of each export. The analytic run has nothing to recognise -
# a declared sweep is a fit, and fits are the approximation pass's business - so
# it is planes throughout, and saying so is what would catch a sweep leaking into
# the exact tier.
# ROUNDTRIP: Plane=223
# ROUNDTRIP-APPROX: BSplineSurface=1 Plane=45
from pythonscad import *
import math

rows, cols = 60, 4
R, pitch, turns = 20.0, 6.0, 1.5
pts, faces = [], []
for i in range(rows):
    t = i / (rows - 1)
    a = 2 * math.pi * turns * t
    z = pitch * turns * t
    for dr, dz in ((0.6, -1.2), (-1.0, -0.4), (-1.0, 0.4), (0.6, 1.2)):
        pts.append([(R + dr) * math.cos(a), (R + dr) * math.sin(a), z + dz])
for i in range(rows - 1):
    for j in range(cols):
        a0 = i * cols + j
        b0 = i * cols + (j + 1) % cols
        c0 = (i + 1) * cols + (j + 1) % cols
        d0 = (i + 1) * cols + j
        faces += [[a0, c0, b0], [a0, d0, c0]]
faces += [[0, 1, 2, 3],
          [(rows - 1) * cols + 3, (rows - 1) * cols + 2, (rows - 1) * cols + 1, (rows - 1) * cols]]

grid = [[pts[i * cols + j] for j in range(cols)] for i in range(rows)]
ridge = polyhedron(points=pts, faces=faces).declare_grid(points=grid, closed=False)
# A plane, and nothing else. The cube is the cutter and declares no surface, so
# every vertex it makes has one owner - which is the whole point of the fixture.
(ridge - cube([80, 80, 80], center=True).translate([0, 0, 45.0])).show()
