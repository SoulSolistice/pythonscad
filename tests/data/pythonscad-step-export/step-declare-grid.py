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
  fused onto the wall, by the surface          348 claimed whole, 185 cut

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
tessellation band, the widest the interpolant departs from the chords through
the points it was built from: 0.0660 here, printed on every export because it is
the number being trusted.

Claiming 348 rather than 356 is the honest ceiling: the eight are where the
union actually removed surface, and the 185 cut across it are the facets the
trim left straddling the ridge's edge.
"""
# EXPECT: 2 analytic surfaces available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 swept grid)
# EXPECT: a declared 60x4 sweep claims 348 facets whole, 185 cut across it, within its tessellation band of 0.0660
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
