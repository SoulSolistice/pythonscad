// A hole in a face the corner placement fans into triangles.
//
// step-cap-meets-wall's part, with a blind bore of radius 0.2 drilled radially
// into its first cylinder at 70 degrees below the x axis. The 64-gon's facet
// there - between -73.125 and -67.5 degrees - straddles the line where the
// second cylinder's wall crosses the first one's (x = 3, y = -9.5394), so it is
// one of the two facets no cylinder face takes; its junction corners move onto
// that crossing and bend it, and it is written as triangles on the planes their
// corners are on. The bore puts a hole in it.
//
// The hole was fanned twice (open item 39, R24). The corner placement judged
// every valid loop as a face, holes included: the bore's ring, bent by the same
// moves, was fanned into fourteen triangles of its own, covering the hole a
// second time against the face whose triangles already bound it - every edge of
// the ring used twice the same way, seventeen breaks of the B-rep's contract, a
// shell no kernel reads as the solid. A hole is a bound of its face, and is
// judged with it.
//
// And the ring was read as a facet (R25). Named for the first cylinder, as its
// face is, and lying on the bore that cut it, it was reported as a facet whose
// position contradicts its name, in both tiers - a ring lies on whatever cut it,
// and that says nothing about the surface of the face it bounds.
//
// The volume is step-cap-meets-wall's bracket less the bore, each end of it by
// its own rule. The smooth solid loses the circular bore from radius 8 to the
// cylinder r = 10: the integral over the disc of sqrt(100 - y^2) - 8, which is
// 2 pi r^2 - pi r^4 / 80 = 0.2512646. The mesh loses the 16-gon's prism, area
// 8 r^2 sin(pi/8) = 0.1224587, from radius 8 to the facet's plane, which is
// 10 cos(pi/64) from the axis and tilted 0.3125 degrees from square to the
// bore, so 10 cos(pi/64) / cos(0.3125 deg) = 9.9881031 along it at the
// section's centroid: 0.2434605. So 4891.296892 - 0.2512646 = 4891.045627 and
// 4881.063273 - 0.2434605 = 4880.819812, and the export lies between.
//
// VOLUME: 4885.932720 +/- 5.112907
// VOLUME-APPROX: 4885.932720 +/- 5.112907
//
// The known defects of step-cap-meets-wall, which this part carries unchanged:
// a D0 region left to the tessellation at the exact tier, and facets named by
// their maker for a declared surface not written on it (open item 34). Tolerated
// by name; the test fails the day either is no longer raised.
// KNOWN-EXPORT-ERROR: of a D0 region are left to the tessellation
// KNOWN-EXPORT-ERROR: facets named by their maker for a declared surface are not written on it
//
// R25 is not fixed yet: the ring reported as a facet whose position
// contradicts its name, in both tiers. Tolerated by name; the test fails the
// day it is no longer raised.
// KNOWN-EXPORT-ERROR: position contradicts the name

$fn = 64;
difference() {
  intersection() {
    union() {
      cylinder(r = 10, h = 20);
      translate([0, 0, 20]) cylinder(r1 = 10, r2 = 4, h = 8);
    }
    translate([6, 0, -1]) cylinder(r = 10, h = 40);
  }
  rotate([0, 0, -70]) translate([8, 0, 10]) rotate([0, 90, 0]) cylinder(r = 0.2, h = 4, $fn = 16);
}
