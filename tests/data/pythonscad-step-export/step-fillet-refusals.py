"""A fillet whose corners are not sphere octants, which must stay splines.

The companion to step-fillet.py, and the fixture that could not be written
until the fillet itself was fixed. A hexagonal prism's vertical edges meet its
end faces at right angles but each other at 120 degrees, so its twelve corners
are blends rather than octants of anything. Every one of the thirty patches is
therefore a genuine spline, and the file has to say so: no CYLINDRICAL_SURFACE,
no SPHERICAL_SURFACE, thirty B_SPLINE_SURFACE_WITH_KNOTS.

This is the direction the quadric recovery fails in, and the reason it needs a
fixture at all. A patch wrongly *refused* costs a nicer entity and nothing else
- the export stays valid and the shape stays right. A patch wrongly *accepted*
writes a surface the mesh is not on, and nothing downstream would notice: the
shell still closes, every edge is still used twice, and the file still
validates. Only the count says which happened.

Note what the eighteen edge strips do here, because it is not obvious. A strip
along a straight edge at constant radius is an exact cylinder quadrant at *any*
dihedral, so all eighteen qualify on their own. They are refused anyway, because
each shares its end rail with a corner that does not, and one EDGE_CURVE cannot
be a CIRCLE for the face on one side and a spline for the face on the other.
That is the fixed point in the classification doing its job, and this model is
the only one in the suite that exercises it.

It is also the regression guard for the fillet bug that made this shape
exportable in the first place. bezier_patch() measured its Bezier weights in a
frame built by pretending the corner's three directions were perpendicular;
cos(theta/2) is not affine invariant, so once that frame was sheared onto the
real directions the corner drew an ellipse where the strips meeting it drew
circles. The two came apart between their shared endpoints and left a lens
shaped hole at every corner - 168 edges used by one face, on this exact model.
The validator's closed-shell check is what holds that fixed, so this fixture
fails loudly if the weights ever go back into the wrong frame.
"""
# EXPECT: 30 Bezier patches cover 714 facets
# EXPECT: 72 of 72 shared seams agree between the two patches meeting there
# EXPECT: those runs border 0 whole faces, 36 stretches of a face, 72 other patches, 0 unresolved
# EXPECT: 0 of 30 patches are exactly quadrics - 0 cylindrical, 0 spherical
# EXPECT: written as 38 faces instead of 722
# EXPECT-NOT: left faceted
from pythonscad import *

cylinder(r=10, h=10, fn=6).fillet(1, fn=8).show()
