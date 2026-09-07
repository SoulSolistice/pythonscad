// A cone whose base rim is cut away, which is what a declaration is for.
//
// The exporter can recognise a cone from its two rims: find a declared circle
// at either end and the wall between them is conical. That recognition is
// conditioned on both rims surviving whatever the model does next, and a
// boolean is exactly what removes one. Here the cut takes the base rim, and an
// apex is not a rim - there is no circle there to declare, only a point - so
// after the cut this solid has no pair of rims left to be recognised from.
//
// Before CylinderNode declared the cone itself, that was fatal: sixty-six
// planes, the whole taper lost, on a shape whose generator knew exactly what
// surface it was making. A ConeSurface is a radius and a slope and needs no
// second rim, so it survives the cut that the rims do not.
// EXPECT: 1 analytic surface available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 conical)
//
// The exact tier no longer declines it, and what changed is worth stating: the
// cut edge is a plane section of the cone taken at 12 degrees, which is less
// than the cone's own half angle of atan(10/20) = 26.57, so the section closes
// and is an ellipse. That ellipse is now written as one, and the face across it
// - the cube's, whose plane the model declares - is handed the same edge.
//
// The other half of the change is not about sections at all. The taper's other
// boundaries are its *generators*, straight lines running to the apex, and a
// generator lies exactly on the cone. They were being refused because the test
// asked whether an edge ran parallel to the axis, which is a cylinder's answer;
// a cone's rulings converge. Both together, all four regions are writable.
// EXPECT: 4 trimmed quadrics written as one face each, replacing 30 facets
// EXPECT: 2 plane sections written as the conic it is - 2 on a plane the model declared
//
// Two sections, not four, and both declared: the cut is one plane and the cube
// declares it, so however many faces the taper is split into they share one
// ellipse, cut into the two arcs that survive above z = 0.
//
// Where the taper's lower boundary is the ellipse and where it is the base rim
// is derived. On the cone, radius R(z) = 10 - z/2, the cut keeps
//     z >= (1.566998 + 2.12557 sin(theta)) / (1 + 0.1062785 sin(theta))
// which is zero at sin(theta) = -0.737207, so for theta between 227.52 and
// 312.48 degrees the cut passes below z = 0 and the base rim bounds the taper
// instead. That is why two of the four faces are bounded by circle arcs and two
// by an ellipse arc - and why the two faces that reach the apex have three edges
// each: an arc and two generators meeting at a point where the surface itself
// closes.
// ROUNDTRIP: Cone=4 Plane=4
// EDGES: Circle=6 Ellipse=2 Line=11
//
// Why four faces rather than three - the region runs all the way round the axis
// and is cut where it must be - is the exporter's business and is deliberately
// not explained here. Stating a reason for it would be inventing one. If that
// changes, this line moves and the reason is worked out then.
//
// Derived. The kept solid is the cone r=10, h=20 above the cube's top face,
// which after rotate([12,0,0]) and translate([0,0,-1.5]) is the plane through
// (0, -3sin12, 3cos12 - 1.5) with normal (0, -sin12, cos12). Integrating the
// area of each z-slice - a disc of radius 10(1 - z/20) cut by y < ycut(z) -
// over z in [0,20] gives 1661.646512. The same integral with a 32-gon in place
// of the disc gives 1650.966482, which is what the mesh measures, so the
// integral is checked against something the exporter had no hand in.
//
// The approximation tier now reads 1661.646504 - eight parts in a million of a
// millimetre cubed from the ideal solid, where before this it was 1.94 short.
// So the window comes off: the default 1e-6 relative is 0.0017, and being
// inside that is the assertion that the exported solid *is* the intended one.
// VOLUME-APPROX: 1661.646512
//
// APPROX: 2 trimmed quadrics written as one face each, replacing 32 facets
// ROUNDTRIP-APPROX: Cone=2 Plane=2
//
// The exact tier reads 1660.807437, 0.84 short, and the shortfall is named
// rather than tolerated: its corner placement does not run, so the two points
// where the cut crosses z = 0 sit on a chord of the base 32-gon at radius
// 9.9645 instead of on the rim at 10. It is not asserted here, because a figure
// that is neither the ideal solid nor the mesh's is a figure only this
// exporter's current behaviour explains - which is what
// doc/step-export-testing.md means by captured. When that placement reaches the
// exact tier the two tiers agree and VOLUME: joins VOLUME-APPROX: above.
$fn = 32;
difference() {
	cylinder(r1 = 10, r2 = 0, h = 20);
	translate([0, 0, -1.5]) rotate([12, 0, 0]) cube([60, 60, 6], center = true);
}
