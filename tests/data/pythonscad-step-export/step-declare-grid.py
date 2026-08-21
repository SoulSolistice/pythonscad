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

  the ridge alone                     356 facets claimed whole, 0 cut
  the ridge fused onto the wall        63 claimed whole, 237 cut across

The 63 are the facets the boolean never touched, and they are claimed exactly -
membership is by position, so a corner the generator emitted matches and one the
boolean created does not. That is the correct answer rather than a limitation:
a trimmed facet is genuinely no longer a facet of the pure sweep.

It is also not yet a useful one, and the fixture records that honestly. Those
237 facets still *lie on* the swept surface - the trim changed their boundary,
not the surface underneath - so claiming them needs the record to answer "is
this point on the sweep" for points the generator never emitted. That means
evaluating the grid as a surface and projecting onto it, rather than looking the
point up. The grid is what makes that possible; it is not what does it.
"""
# EXPECT: 2 analytic surfaces available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 swept grid)
# EXPECT: a declared 60x4 sweep claims 63 facets whole, and 237 more are cut across it
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
