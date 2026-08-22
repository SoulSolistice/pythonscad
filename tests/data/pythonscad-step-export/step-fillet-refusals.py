"""A fillet whose corners are not sphere octants, which must stay splines.

The companion to step-fillet.py, and the fixture that could not be written
until the fillet itself was fixed. A hexagonal prism's vertical edges meet its
end faces at right angles but each other at 120 degrees, so its twelve corners
are blends rather than octants of anything. Those twelve have to stay splines,
and the file has to say so: no SPHERICAL_SURFACE anywhere on this shape.

This is the direction the quadric recovery fails in, and the reason it needs a
fixture at all. A patch wrongly *refused* costs a nicer entity and nothing else
- the export stays valid and the shape stays right. A patch wrongly *accepted*
writes a surface the mesh is not on, and nothing downstream would notice: the
shell still closes, every edge is still used twice, and the file still
validates. Only the count says which happened.

The eighteen edge strips are the interesting part, and what happens to them
changed. A strip along a straight edge at constant radius is an exact cylinder
quadrant at *any* dihedral, so all eighteen qualify on their own. They used to
be refused anyway: each shares its end rail with a corner that is not a quadric,
and one EDGE_CURVE cannot be a CIRCLE for the face on one side and a spline for
the face on the other, so the classification withdrew the strip. OCCT then
reported six of the thirty splines as exact cylinders of radius sqrt(3) - a
third party naming precisely what that rule had thrown away.

The rule was asking the wrong object. Whether a boundary is a circle is a
property of the *run*, and runCircle answers it from the declared control net,
weights and all - not from whether the face beside it happens to be a quadric.
When both patches meeting at a run describe the same circle, a CIRCLE lies on
both surfaces exactly and either may be bounded by it. So the six vertical edge
strips are now written as CYLINDRICAL_SURFACE, and OCCT's canonical census comes
back with twenty four splines and nothing hidden among them.

The other twelve strips are the ones along the prism's top and bottom rims.
They stay splines because their rails do not come back as one common circle, and
that is the curve giving way rather than the face - which is the whole point of
moving the question.

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
# EXPECT: 6 of 30 patches are exactly quadrics - 6 cylindrical, 0 spherical
# ROUNDTRIP: BSplineSurface=24 Cylinder=6 Plane=8
# EXPECT: written as 38 faces instead of 722
# EXPECT-NOT: left faceted
# CANONICAL: spline=24
# RADII: Cylinder=1.73205
from pythonscad import *

cylinder(r=10, h=10, fn=6).fillet(1, fn=8).show()
