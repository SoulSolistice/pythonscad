"""A declared sweep written as one face, which is what the channel was for.

The same helical ridge as step-declare-grid.py, fused onto the same wall, and
declared with an *open* profile rather than a closed one. That single word is
the difference between a region a face can be written on and one that cannot,
and it is worth stating why rather than treating it as a knob.

A profile declared closed makes the sweep a tube, and its surface closed across
v. The claimed facets then cover every span of the profile - the region goes all
the way round - so the face would have to be bounded across the surface's own
seam, which a surface written as an open rectangle cannot carry. Declared open,
the sweep is a ribbon: the strip from the last column back to the first is not
part of it, the claimed region is a sheet, and its boundary stays inside the
parameter rectangle where it can be written as it stands.

The facets on that excluded strip are the reason membership cannot be a question
about corners. Every corner of them is a point the generator emitted, so
position alone claims them - and their middles are nowhere near the declared
surface. Asking whether the facet's centroid lies on the sweep asks about the
facet instead, and here it refuses 108 of them.

What is written is one B_SPLINE_SURFACE_WITH_KNOTS face replacing 241 facets:
cubic along the sweep with the interior knots the chord lengths gave it, ruled
across the profile, bounded by the mesh's own straight edges.

Those edges are the design, not a compromise. The obvious move is to fit a curve
along each boundary run, the way a fillet's rails become arcs - but a fillet's
rail lies in the flat face beside it, and a trimmed sweep's boundary does not.
Its neighbours here are the planar facets the boolean left, so a curve on the
sweep lies in none of them, and a shared edge between the two can only be the
chord it already is. Taking the edges from the same map every other face uses is
both the simplest thing and the only one that keeps the shell closed: the
neighbour gives up nothing.

The face is written only under step-approximate-surfaces, and that gate is
honest rather than cautious. Every other analytic face this exporter writes
carries a surface the mesh lies on exactly. This one is matched to within the
grid's tessellation band - 0.1290 here - which is the model's own resolution and
not zero. OpenCASCADE reads the result back as one solid of one shell and puts
its volume 0.005% above the faceted export's, which is the smooth surface
standing off the chords by about that band, in the direction it should.
"""
# EXPECT: 2 analytic surfaces available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 swept grid)
# EXPECT: 108 facets have every corner on the sweep and their middle off it
# A claim is not always in one piece. The boolean that cuts the ridge leaves
# two separate regions of the same sweep, and each is its own face: written as
# one they would be one ADVANCED_FACE with two outer loops, the second labelled
# a hole in the first, which is not a face. The counts below say two sweeps and
# one boundary cycle each, where they used to say one sweep over two cycles.
#
# The ROUNDTRIP-APPROX line is unchanged, and that is the point of it. OpenCASCADE
# was already splitting these faces on read and reporting the split numbers, so
# what the exporter writes now is what a kernel was making of it all along.
# EXPECT: 2 declared sweeps cover 241 facets over 1 boundary cycle
# EXPECT: the region is a strip, whose boundary stays inside the surface's rectangle
# EXPECT-NOT: written as one face each
# APPROX: 2 declared sweeps written as one face each, replacing 241 facets
# How far the fitted surface strays *between* the stations it was
# interpolated through, which is the only place it can. A cubic passes
# through its data exactly, so measuring at the data says nothing; what
# moves is the surface between, and it moves most at the ends of a sweep
# where uniform sampling is thinnest against geometry changing fastest.
# Captured rather than derived - there is no closed form for it - and
# pinned because it is what a change to the fit would move. The figure
# that matters is that it is below the band beside it.
# EXPECT: the fitted sweep passes within 0.0609 of the middle of every facet it claims, against a tessellation band of 0.1290
# APPROX: 9 trimmed quadrics written as one face each, replacing 289 facets
#
# What a kernel makes of each export: the declared strip survives as a surface.
# Validity says the file is well formed; only this says the surface
# survived as one. A fit read back as the planes it replaced would
# pass every other check in this fixture.
# ROUNDTRIP: Plane=534
#
# The walls go out too, and not as facets. They are declared cylinders the
# band pass could not write: the ridge cut out of one leaves a hole in it, and
# a band is two rims at a constant height with no notion of a hole. The
# trimmed-quadric path claims them by distance to the axis instead and bounds
# them with the mesh's own polyline, which is what the faceted faces around
# them close against. So the census below is the whole part rather than the
# sweep alone - the several cylinder faces are one wall each side of the seam
# it is cut at, since a face written on an open rectangle cannot wrap.
# ROUNDTRIP-APPROX: BSplineSurface=2 Cylinder=9 Plane=4
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
(ridge | cylinder(r=19.2, h=14, fn=96)).show()
