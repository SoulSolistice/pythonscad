// A cone bored across, so the bore's own trim runs on a tapered wall.
//
// step-bored-cylinder is the same shape on a cylinder. This one is here because
// the surface the bore is cut into is a *cone*, which changes what has to be
// written on both sides: the wall's rims are still circles so the band pass can
// take the arc-bounded parts of it as conical faces, but the bore's ends are
// where it meets a taper, and neither is a plane section of anything.
//
// The exact pass takes twenty facets - the two arcs of the wall the bore did not
// interrupt - and leaves fifty-four planes.
// EXPECT: 3 analytic surfaces available (3 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 2 surfaces recognised (0 toroidal, 0 spherical, 2 conical, 2 partial), 20 facets replaced
// ROUNDTRIP: Cone=2 Plane=54
//
// With the approximation flag the trimmed-quadric path takes the bore, which is
// a cylinder whose two rims are the curve where it meets the taper - the case
// the band model cannot express at all. Two faces rather than one because a
// face on an open rectangle cannot wrap, so the bore is cut at its seam.
// APPROX: 2 trimmed quadrics written as one face each, replacing 32 facets
// ROUNDTRIP-APPROX: Cone=2 Cylinder=2 Plane=22
//
// What stays: two ten-facet strips of wall either side of the mouth, which the
// report calls too thin to tell - every vertex of them is on their boundary, so
// there is no interior to measure a grid or an axis from.
// APPROX: 2 smooth regions left faceted, 20 facets in all
//
// The volume is derived. The frustum is (pi*20/3)(12^2 + 12*8 + 8^2) =
// 6366.961111. The bore through it is not elementary - the wall it crosses is
// tapered, so its half width follows R(z) = 12 - 0.2z - and integrating
// 2*sqrt(R(z)^2 - x^2) over the bore's disc gives 984.757269, leaving
// 5382.203842.
//
// The tolerance is for the boundary rather than the surface, as in
// step-bored-cylinder: every face is the exact quadric, but the bore is bounded
// by chords instead of by the curve where it meets the cone, and the export
// reads 5372.245. That deficit of 10 over the roughly 500 square units the
// trimmed faces cover is a mean displacement of 0.02, against a band of 0.0567.
// Boring at r=4.1 instead would move this by 50.
// VOLUME-APPROX: 5382.203842 +/- 12.0
$fn = 32;
difference() {
	cylinder(r1 = 12, r2 = 8, h = 20);
	translate([0, 0, 10]) rotate([90, 0, 0]) cylinder(r = 4, h = 60, center = true);
}
