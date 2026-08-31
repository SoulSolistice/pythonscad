"""The declaration channel for geometry no primitive can name.

A helical ridge, built the way examples/step_test/bayonet_container_v1-2.scad
builds its hose thread: polyhedron() over a computed point list. There is no
declare_cylinder for this - the surface has no closed form and no name - so the
other half of the channel is used instead. declare_grid() hands over the one
thing the generator has and the mesh loses: the *order* the points were swept
in.

Why that is the thing worth carrying is measured, not assumed. Fitting a surface
to a run of facets needs to know which follows which along the sweep, and a mesh
straight from a generator still says so - every interior vertex of a swept quad
grid sits at valence 6. After a boolean has trimmed it the valence spreads and
the ordering is gone: the bayonet's thread measures 36% regular, at which point
no fitter can be pointed at it. See uncoveredRegions() in AnalyticFeatures.

This model is the ridge fused onto a wall, which is the case that matters,
because a sweep standing alone is not what any real model exports. The union
cuts the ridge along its base, and the numbers below say exactly what that costs:

  the ridge alone                              356 claimed whole,   0 cut
  fused onto the wall, by declared points       63 claimed whole, 237 cut
  fused onto the wall, by the surface          349 claimed whole, 184 cut

The middle line is what declaring the grid alone achieved, and it was not
enough: only the facets the boolean never touched were claimed, because
membership was a lookup of the points the generator emitted.

The last line is the grid used as a *surface*. The stations are interpolated by
a cubic B-spline along the sweep, ruled across the profile - cubic where the
sweep curves and linear across, because the profile is a polyline whose corners
the model means to keep - and membership becomes a projection onto it.

The tolerance that makes that work is the interesting part, and it cannot be a
constant. The declared sweep is smooth; the mesh is its tessellation; a boolean
cuts the *tessellation*, so the vertices it creates lie on facets, standing off
the smooth surface by up to the sagitta of a station. At 1e-7 every one is
refused - projection alone moved 63 to 66. So the tolerance is the grid's own
tessellation band, the widest the interpolant departs from the flat facets
covering it: 0.1290 here, printed on every export because it is the number being
trusted.

Claiming 349 rather than 356 is the honest ceiling: the seven are where the
union actually removed surface, and the 184 cut across it are the facets the
trim left straddling the ridge's edge.

Those 348 then have to be one *face*, which is a stricter thing than 348 claims,
and being claimed by position is not enough to be on the sweep. Every corner of
this ridge's two end caps is a point the generator emitted, and so is every
corner of a facet the boolean retriangulated across two stations - and their
middles are off the surface. Asking whether the facet's centroid lies on the
sweep asks about the facet rather than about its corners, and refuses 49 of
them. What is left is 299 facets forming one sheet with one boundary, split into
282 runs of at most 7 mesh edges with a single neighbouring face behind each,
none unresolved.

The profile here is declared closed, so the sweep is a tube and its surface is
closed across v, and the claimed facets lie over all four of the profile's
spans: the region closes around the profile. A face on a surface written as an
open rectangle cannot be bounded across its own seam, so the region is **cut**
into two arcs of two spans each, and each arc is written as its own face - 300
facets replaced by two.

Cutting rather than seaming is a choice about what the operation has to survive.
Writing the surface as closed across v and carrying a seam edge round the loop
is how a full cylindrical band is written, and it works when the region is the
whole tube. Here the region is whatever the boolean left of one, with a trim
boundary meeting the seam wherever it happens to. The cut does not depend on the
shape of that boundary at all: each arc of spans is a sheet in its own right,
and the cut runs along mesh edges the two arcs already share, so the neighbours
are asked for nothing. The cost is one extra face, and it is stated on export
rather than left to be discovered in the file.

That the surface covers the closing strip at all is recent: the fourth side of
this four sided ridge, from the last column back to the first, is named by no
column of the net, and without it the sweep had a surface over three of its
sides. Facets on the fourth could be claimed by position, because the generator
emitted their corners, and never by projection - precisely the half a boolean
destroys.

OpenCASCADE reads the result back as one solid of one shell. Its volume is 0.15%
under the faceted export's, which is the smooth surface departing from the
chords: spread over the ridge's roughly 1280 square units of surface that is an
average normal displacement of about 0.02, against a tessellation band of 0.1290
- inside what the mesh leaves open, which is the whole claim being made.
"""
# EXPECT: 2 analytic surfaces available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 swept grid)
# EXPECT: a declared 60x4 cubic sweep claims 349 facets whole, 184 cut across it, within its tessellation band of 0.1290
# EXPECT: 49 facets have every corner on the sweep and their middle off it
# EXPECT: a sweep closing around its profile was cut into 2 faces, so that no face crosses the surface's seam
# EXPECT: 2 declared sweeps cover 300 facets over 1 boundary cycle, split into 401 runs of up to 8 mesh edges, 0 unresolved
# EXPECT-NOT: written as one face each
# APPROX: 2 declared sweeps written as one face each, replacing 300 facets
# EXPECT: its facets lie over 2 of the profile's 4 spans - the region is a strip, whose boundary stays inside the surface's rectangle
#
# What a kernel makes of each export: the declared sweep survives as a surface, not as the facets it claimed.
# Validity says the file is well formed; only this says the surface
# survived as one. A fit read back as the planes it replaced would
# pass every other check in this fixture.
# ROUNDTRIP: Plane=534
# ROUNDTRIP-APPROX: BSplineSurface=2 Plane=234
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
ridge = polyhedron(points=pts, faces=faces).declare_grid(points=grid, closed=True)
(ridge | cylinder(r=19.2, h=14, fn=96)).show()
