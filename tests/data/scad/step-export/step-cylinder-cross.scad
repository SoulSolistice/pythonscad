// Two equal cylinders crossing at right angles: an intersection curve with a
// closed form, which is what this fixture is for.
//
// Everywhere else in this suite a trim boundary is the mesh's own polyline, and
// there is nothing to compare it against - a boolean's cut through a tessellated
// solid has no analytic description, so "the boundary is where it should be" is
// not a statement any fixture could check. Here it does have one. Two cylinders
// of equal radius whose axes cross at right angles meet along two *ellipses*,
// semi-axes r and r*sqrt(2), lying in the two planes x = z and x = -z. That is
// the curve the two faces should be trimmed to, written out, and it can be
// asserted rather than believed.
//
// What the export gets right already: the vertices. Every one of the 126
// junction vertices lies on both cylinders to 1e-15 - the boolean puts them
// where the two vertex-columns cross, which is on the true ellipse - and the
// placement reports moving them by 0.0000 because they are there to begin with.
// The corner invariant in steproundtrip.py checks that on every export, so the
// ellipse is tested at every one of its vertices without a directive naming it.
//
// EXPECT: 2 analytic surfaces available (2 cylindrical, 0 spherical, 0 toroidal, 0 Bezier), and 4 declared planes
//
// And what the exact tier does with it, which is the whole fixture: it refuses,
// and says why in the one sentence this work has been circling for a week.
// EXPECT: 8 trimmed quadric regions left faceted - the surface is exact but some of it is bounded off itself
//
// The corners are on the cylinder. The *chords between them* are not: a straight
// edge lies on a quadric only if it runs along the axis or round it at constant
// height, and a chord of an ellipse does neither. So every one of the eight
// regions is a piece of exact cylinder bounded by a polyline that leaves it, and
// the exact tier will not write a face whose own boundary is off its surface.
// That is the correct answer to the question as posed, and it is why this model
// comes back as 128 planes rather than 8 cylinders.
// 128 planes, and that number is the model's: each cylinder is a 64-gon, and on
// the z-axis one the surviving region is |z| <= r*|sin theta| - a single
// interval of z for every one of the 64 columns, so every column survives as one
// merged strip and none is cut in two. 64 faces from each cylinder is 128.
// ROUNDTRIP: Plane=128
//
// Under the approximation flag the same eight regions are written anyway, which
// is what that flag licenses - the boundary is off the surface by less than the
// tessellation leaves open, so it is written and said to be approximate.
// ROUNDTRIP-APPROX: Cylinder=8
//
// Eight faces for two cylinders, of which the model derives six and the exporter
// the other two. |z| <= r*|sin theta| pinches to a point at theta = 0 and 180,
// so what survives on each cylinder is two lobes of exactly 180 degrees: four
// lobes over the two cylinders. Each lobe is then written as *two* faces, and
// they come out as the four quadrants - measured, theta 0-90, 90-180, 180-270,
// 270-360, thirty-three vertices each.
//
// Why a lobe is halved is the exporter's business and is deliberately not
// explained here: 180 degrees is the widest a region can be before a face on an
// open rectangle becomes ambiguous, which makes the halving plausible and not
// derived. Stating a reason for it would be inventing one. If that behaviour
// changes this line moves, and the reason should be worked out then rather than
// guessed now.
//
// No planes survive: each cylinder's caps sit at |z| = 20 and the other cylinder
// reaches only to 10, so both are cut away entirely. That is what makes this a
// closed shell of nothing but quadrics, and a clean test of a boundary.
//
// The volume is deliberately not asserted, and the omission is the fixture.
//
// The solid this model means is the Steinmetz solid, whose volume is exactly
// 16*r^3/3 = 5333.3333 - no pi in it, which is the pleasant surprise of the
// shape and makes it about as derived as a figure here can be. The approximation
// export measures 5329.049: 4.28 low, or 0.08%. That deficit is the trim, the
// chords cutting inside the ellipse they approximate, all the way round both
// curves.
//
// So VOLUME: 5333.3333 is what this fixture should assert, and it is left out
// until the trim is the ellipse rather than its chords - at which point the
// exact tier stops refusing too, and ROUNDTRIP becomes Cylinder=8 with no planes
// at all. Asserting the 5329.049 the exporter produces today would pin the
// defect in place, which is what doc/step-export-testing.md means by captured
// rather than derived.
$fn = 64;
r = 10;
h = 40;
intersection() {
	cylinder(r = r, h = h, center = true);
	rotate([90, 0, 0]) cylinder(r = r, h = h, center = true);
}
