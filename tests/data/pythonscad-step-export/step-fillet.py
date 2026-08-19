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

The three counts below are what the exporter has to report for that to have
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
# EXPECT-NOT: left faceted
from pythonscad import *

cube(10, center=True).fillet(1, fn=12).show()
