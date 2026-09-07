"""step-declare-grid.py reflected, so the ridge meets the cap the declaration does not anchor.

The same helical ridge fused onto the same wall, reflected in z, and with the
wall built so that its own record is anchored at the *far* end. That single
difference is the whole fixture, and it exists because the corner placement asks
a declaration whether a plane is a face of the model - so how the model happens
to have declared itself must not decide the answer.

`cylinder()` pushes one CylinderSurface for r1 == r2, anchored at z1; a second
would be folded into it, coaxial cylinders of equal radius being the same
surface. So a cylinder's record names one of its two rims and there is no plane
declaration to name the other. A placement that vouches for a plane by matching
a declaration's anchor therefore protects the cap at z1 and not the cap at z2 -
and this model is the second case: the wall is translated so its anchor lies at
z = -14 while the ridge crosses the cap at z = 0.

The defect that would hide here is not a wrong number but a flat face quietly
destroyed. A corner of the cap is owned by the wall cylinder and by the ridge's
sweep; placed where those two cross it leaves the cap's plane, and a 95-corner
face cannot then be written as one plane, so it is fanned into 93 triangles -
three of them off level. Every corner in the file is then exactly on the surface
it is written on, and a corner census reads 6.3e-16 and calls it done. Only the
face count dissents. Verified by restoring the anchor-matching test: this model
comes out Plane=96 while step-declare-grid.py, whose ridge meets the anchored
cap, stays at Plane=4.

Every number below is step-declare-grid.py's own, and that is the point rather
than a coincidence: a reflection is an isometry, so the solid, its tessellation,
its claims and its face counts are all identical, and the only thing this fixture
changes is which of the two caps the ridge arrives at. Any number here that
differs from its twin's is the bug this fixture is for. See that file for the
derivations; they are not repeated.
"""
# EXPECT: 2 analytic surfaces available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 swept grid)
# EXPECT: a declared 60x4 cubic sweep claims 349 facets whole, 184 cut across it, within its tessellation band of 0.1290
# EXPECT-NOT: declared sweeps written as one face each
# APPROX: 2 declared sweeps written as one face each, replacing 299 facets
# APPROX: 5 trimmed quadrics written as one face each, replacing 231 facets
#
# The cap survives as one face. Written as the count rather than as prose
# because the failure is silent in every other measure this suite takes: the
# fanned export is valid, closed, and exact at every corner.
# APPROX: 2 corners are placed on a plane a declaration vouches for crossed with their exact owner
#
# The kernel's view, identical to the twin's. Plane=4 is the assertion that
# matters: bottom cap, top cap, and the ridge's two end caps. Fanning the cap
# takes it to 96.
#
# The exact tier also writes seventy-four pieces of the bore, and the sweep it
# left faceted is what makes them writable. Each facet of the ridge is a flat
# plane, and a plane cuts the bore cylinder in an ellipse; the mesh edges where
# the bore meets a ridge facet are chords of that ellipse, the ellipse replaces
# them, and the ridge facet across it is handed the same edge.
# EXPECT: 74 trimmed quadrics written as one face each, replacing 79 facets
#
# Seventy-four arcs, and all seventy-four are *congruent* - semi-axes 21.4672 and
# 19.2. That is the model speaking rather than the mesh: the ridge is a helix of
# constant pitch carrying a constant profile, so every one of its facets presents
# the bore the same plane, rotated and raised. And 19.2 is the bore's own radius,
# straight out of cylinder(r=19.2) below, because a plane section of a cylinder
# is as wide as the cylinder however it is tilted.
# EXPECT: 74 plane sections written as the conic it is
# EDGES: Ellipse=74
#
# So the census moves by arithmetic: each replaced facet was one PLANE, so
# 534 - 79 = 455 remain, and the seventy-four new faces are all bore, so all
# cylinders.
# ROUNDTRIP: Cylinder=74 Plane=455
# ROUNDTRIP-APPROX: BSplineSurface=2 Cylinder=5 Plane=4
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
        pts.append([(R + dr) * math.cos(a), (R + dr) * math.sin(a), -(z + dz)])
# Reflected, so the winding of every face reverses with it.
for i in range(rows - 1):
    for j in range(cols):
        a0 = i * cols + j
        b0 = i * cols + (j + 1) % cols
        c0 = (i + 1) * cols + (j + 1) % cols
        d0 = (i + 1) * cols + j
        faces += [[a0, b0, c0], [a0, c0, d0]]
faces += [[3, 2, 1, 0],
          [(rows - 1) * cols, (rows - 1) * cols + 1, (rows - 1) * cols + 2, (rows - 1) * cols + 3]]

grid = [[pts[i * cols + j] for j in range(cols)] for i in range(rows)]
ridge = polyhedron(points=pts, faces=faces).declare_grid(points=grid, closed=True)
# Translated rather than mirrored: mirroring the whole part would carry the
# wall's anchor along with it and leave the ridge at the anchored cap again,
# which is the case the twin already covers.
(ridge | cylinder(r=19.2, h=14, fn=96).translate([0, 0, -14])).show()
