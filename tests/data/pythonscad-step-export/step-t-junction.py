# A T-junction, and the sliver that holds it shut.
#
# The lid in examples/step_test arrives at the exporter as a manifold mesh -
# Manifold reports NoError, validatestl passes, every edge used twice - and
# still exported as a shell with 48 edges used by one face only. The 26 faces
# responsible have no area at all: three distinct collinear points, no two
# coincident, the longest edge exactly the sum of the other two. That is how a
# mesh stitches a vertex which sits in the interior of another face's edge.
#
# Refusing to write such a face is right, because a face with no normal has no
# surface to be written on. Dropping it and stopping there is not: the
# neighbour still spans the whole edge, the vertex in the middle belongs to no
# face, and every edge the sliver carried is left unpaired.
#
# This is that shape at its smallest, built by hand so no backend can tidy it
# away first - a union or a difference which would produce it gets cleaned up
# before the exporter ever sees it, which is why the fixture is a polyhedron.
#
#   vertex 8 sits halfway along the cube's top-front edge
#   the front face runs 5 -> 8 -> 4, stopping at it
#   the top face runs 4 -> 5, one edge across the whole span
#   face [4, 8, 5] has no area, and is the only reason edge 4-5 is used twice
#
# Drop that face without putting vertex 8 back and the cube is not closed.
# EXPECT: skipped 1 degenerated face - 0 collapsed to fewer than three distinct points, 1 had no area
# EXPECT: welded 1 T-junction vertex back into 2 edges
# EXPECT-NOT: had no neighbouring edge to weld into
#
# A unit cube. The extra vertex at (0.5, 0, 1) is a T-junction on one edge and
# adds no volume, so anything other than exactly 1 means the sliver weld moved
# the solid rather than only its topology.
# ROUNDTRIP: Plane=6
# VOLUME: 1

from openscad import *

points = [
    (0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
    (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1),
    (0.5, 0, 1),
]
faces = [
    [0, 3, 2, 1],
    [4, 5, 6, 7],
    [0, 1, 5, 8, 4],
    [1, 2, 6, 5],
    [2, 3, 7, 6],
    [3, 0, 4, 7],
    [4, 8, 5],
]

# OpenSCAD orders a polyhedron's face clockwise seen from outside, so the
# right-hand normal of each face points *into* the solid. Written the other way
# round the cube exports inside out - which is exactly what it did until
# validatestep.py learned to measure the volume a shell encloses, and read this
# unit cube as -1.
faces = [f[::-1] for f in faces]

show(polyhedron(points=points, faces=faces))
