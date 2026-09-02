"""A filleted cube, which for a long time did not export as a solid at all.

Every filleted body PythonSCAD produced was non-manifold. The rail where an edge
strip meets a corner patch is computed twice - once by the strip as
p + e_fa - 2f*e_fa + f^2*(e_fa + e_fb), once by the corner as
center + mat * Bezier(...) - and the two landed one unit in the last place
apart, so the mesh got two vertices where it needed one. This cube came out
with 48 quadrilateral holes, one at every place a strip end meets a corner, and
SolidWorks imported it as loose surfaces rather than a solid. The validator's
"edge used by only one face" check is what guards it now, and it is the reason
this fixture exists even before any of the fillet's surfaces can be written.

It is also the fixture for the Bezier patches themselves. A cube has twelve
filleted edges and eight corners, so at fn = 12:

  20 analytic surfaces available, all Bezier - one per strip, one per corner
  1100 of the 1106 facets covered by them; the six left over are the flat faces
  48 curved boundary runs - 8 corners x 3 rails, 12 strips x 2 - over 528 mesh
     edges, and 24 straight ones, which are single edges already
  26 faces once they are written: 20 patches and the 6 flat faces

The corner patch is the whole reason the count is worth having. It is (N-1)^2
triangles against a strip's N-1 quads, so it grows quadratically and the strips
linearly: at fn = 24 a cube is 4232 corner triangles against 276 strip quads,
and collapsing only the strips would be six per cent of the model.

Since the rails went rational those 20 patches are not merely splines that
happen to fit: an edge strip is an exact quadrant of a cylinder and a corner an
exact octant of a sphere, and a cube is the case where every one of them
qualifies. So the file this writes has no B-spline in it at all - 12
CYLINDRICAL_SURFACE, 8 SPHERICAL_SURFACE, 6 PLANE, bounded by CIRCLEs and LINEs,
which is entity for entity what SolidWorks writes for the same part. The eight
sphere centres come out at (+-4, +-4, +-4) with radius exactly 1, which is the
Minkowski truth for a 1 mm fillet on a 10 cube rather than something fitted.

The quadric count is the assertion that guards it, and it fails in the direction
that matters: a patch wrongly *accepted* as a quadric writes a surface the mesh
is not on, and the count is the only line that says which way each patch went.

The four counts below are what the exporter has to report for that to have
happened; the driver checks them against the analytic run. Every one of them
survives a run that recognises nothing at all - the file still validates, it is
just faceted - and that is not hypothetical: the patch boundary was evaluated
with the polynomial basis for a while after the patches went rational, which
dropped all 20 of them and wrote 1106 faces, and this test stayed green through
it. "left faceted" is the exporter's word for a patch it gave up on, and on
this model it should never have cause to say it.
"""
# EXPECT: 20 Bezier patches cover 1100 facets
# EXPECT: 48 of 48 shared seams agree between the two patches meeting there
# EXPECT: written as 26 faces instead of 1106
# EXPECT: 20 of 20 patches are exactly quadrics - 12 cylindrical, 8 spherical
# ROUNDTRIP: Cylinder=12 Sphere=8 Plane=6 BSplineSurface=0
# RADII: Cylinder=1 Sphere=1
# EDGES: Circle=24 Line=24 degenerate=8
# EXPECT-NOT: left faceted
#
# A filleted cube is the Minkowski sum of cube(10-2r) with a ball of r, so it
# comes apart into the pieces the fillet is made of: 8^3 for the core, 6*1*8^2
# for the six slabs, 3*pi*1^2*8 for the twelve quarter-cylinders, and
# (4/3)*pi*1^3 for the eight corner octants. 512 + 384 + 24*pi + (4/3)*pi.
# VOLUME: 975.5870137
from pythonscad import *

cube(10, center=True).fillet(1, fn=12).show()
