"""The same 20 quadrics, on a box that is not a cube and not axis aligned.

step-fillet.py measures a cube centred on the origin, which is the case where
every quantity the quadric recovery has to produce is also a coordinate axis or
a round number - so a recogniser that only ever worked on axis aligned models
would pass it, and that is a plausible way for this code to be wrong. The net
is read in world coordinates and nothing in quadricOfPatch may assume a frame.

This box is 14 x 9 x 6 and turned through three angles that share no factor.
Everything about the answer is the same: twelve edge strips are exact cylinder
quadrants, eight corners exact sphere octants, no B-spline is written, and the
file is 26 faces.

What makes it a measurement rather than a repetition is where the spheres land.
A filleted box is the Minkowski sum of the box shrunk by 2r with a sphere of
radius r, so the eight centres have to be the corners of a
12.4 x 7.4 x 4.4 box - and they are, to nine decimal places, at radius exactly
0.8, after a rotation that leaves no edge on an axis.

  centre to centre distances   4.4, 7.4, 12.4 (edges), 8.6092973, 13.157507363,
                               14.440221605 (face diagonals), 15.09569475 (body)

The refusal side of this feature has no fixture, and the reason is worth
recording rather than leaving as a gap: every fillet whose corners are *not*
sphere octants is a fillet on a non right dihedral, and FilletNode produces a
non manifold mesh for those today - a hexagonal prism and a sheared cube both
export as open shells with the analytic path switched off entirely. So there is
currently no model which both refuses the quadric and exports at all. See
doc/step-export-status.md.
"""
# EXPECT: 20 Bezier patches cover 756 facets
# EXPECT: 20 of 20 patches are exactly quadrics - 12 cylindrical, 8 spherical
# ROUNDTRIP: Cylinder=12 Sphere=8 Plane=6 BSplineSurface=0
# EXPECT: written as 26 faces instead of 762
# EXPECT-NOT: left faceted
from pythonscad import *

cube([14, 9, 6], center=True).rotate([25, 40, -15]).fillet(0.8, fn=10).show()
