// A hull of two half cylinders on one axis: the chamfered lead-in a bayonet lug
// is built as, where each operand is an arc - a ring less half-spaces through
// its axis - and not a whole solid of revolution.
//
// The hull of whole coaxial solids of revolution states the cone it makes
// (declareHullOfRevolution, step-chamfered-cylinder). Of sectors it stated
// nothing, so the cone between the two rims had no record and the band pass
// named it after a rim cylinder it is not on (open item 39, R29: four chamfer
// faces of the bayonet). Over one wedge of at most half a turn the hull's
// boundary is the hull of the whole solids' profiles turned, so the hull states
// that cone, read from each operand's solid tree: a cylinder less a box that
// removes one side of the plane x = 0 over all of it.
//
// What is declared: the two cylinders, r 10 and r 8, and the cone the hull
// makes between the rim r 8 at z 0 and the rim r 10 at z 2 - the profile's
// edge from (8, 0) to (10, 2).
// EXPECT: 3 analytic surfaces available (2 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 conical)
//
// The solid: the profile (0,0) (8,0) (10,2) (10,6) (0,6) turned through half a
// turn - a cone, the r 10 cylinder, the planes z 0 and z 6, and the plane x 0
// the cut leaves.
// ROUNDTRIP: Cone=1 Cylinder=1 Plane=3
//
// Its volume. Every face is written on its surface, so the export is the
// smooth solid: half of the cylinder r 10 from z 2 to 6 and the frustum r 8 to
// 10 from z 0 to 2, (400 pi + 2 pi (64 + 80 + 100) / 3) / 2 = 844 pi / 3. (The
// mesh, on 32-gons each scaled in area by (32 / 2 pi) sin(2 pi / 32), holds
// 878.166570 - which a faceted export would measure instead.)
// VOLUME: 883.8347332
// VOLUME-APPROX: 883.8347332

$fn = 32;
module half(r, h) difference() {
  cylinder(r = r, h = h);
  translate([-2 * r, -r - 1, -1]) cube([2 * r, 2 * r + 2, h + 2]);
}
hull() {
  translate([0, 0, 2]) half(10, 4);
  half(8, 4);
}
