// A wall standing on a chamfer, where neither of them goes all the way round.
//
// This is the fixture for a rim shared between two *partial* bands: one arc
// bounding two curved faces, with no planar face anywhere along it.
//
// step-chamfered-cylinder covers the same joint for two closed bands, where
// the shared rim is a whole CIRCLE. That case was already handled, and the
// restriction that came with it - both bands must cover the full turn - is
// what left every bayonet lug faceted, because a lug is a wall on a chamfer on
// a wall and not one of the three closes on itself.
//
// Cutting a quadrant out of the chamfered cylinder reproduces exactly that
// shape with nothing else in it. The result has:
//
//   a CONICAL_SURFACE  270 degree arc, r=8 at z=0 to r=10 at z=2
//   a CYLINDRICAL_SURFACE  270 degree arc, r=10, z=2 to z=20
//
// and between them one arc at r=10, z=2, used by both, once in each
// direction. The far rim of each is a run of edges inside a planar face - the
// bottom disc and the top disc - which is the case that already worked, so
// anything this fixture catches is the shared arc itself.
//
// The cut is made with a cube rather than a wedge on purpose: at $fn = 32 the
// facet boundaries fall at multiples of 11.25 degrees, so both cut planes pass
// exactly through a vertex of every circle in the part. No facet is cut
// through its chord, so no trapezoid is left inside the wall and both arcs end
// on the true circle - see step-partial-cylinder for the case where that is
// not true.
$fn = 32;
difference() {
  hull() {
    translate([0, 0, 2]) cylinder(h = 18, r = 10);
    cylinder(h = 0.01, r = 8);
  }
  translate([0, 0, -1]) cube([20, 20, 30]);
}
