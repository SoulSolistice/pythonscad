// A cylinder standing on a chamfer, built the way a printed part is: the hull
// of the wall with a smaller copy of itself.
//
// This is the fixture for two things which only work together.
//
// The hull has to carry the two cylinders its children declared, or the wall
// has no intent behind it and stays faceted however exactly it fits. And the
// rim between the wall and the chamfer is the complete bound of neither face,
// so it can only be collapsed as a rim *shared* by two recognised bands - one
// CIRCLE used by the cylinder and by the cone, once in each direction.
//
// The frustum also has no analytic record of its own: a hull declares the two
// cylinders, never the cone between them, so it is accepted because both of
// its rims match one.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: 2 analytic surfaces available (2 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 2 surfaces recognised (0 toroidal, 0 spherical, 1 conical, 0 partial), 64 facets replaced
//
// One original (the hull), two declared cylinders, and only one of them is
// still here: the r = 8 disc's wall is 0.01 tall and inside the hull's
// profile, so none of it survives. What does survive is the hull's bottom cap,
// whose every corner is on r = 8 - and a presence test asking corners alone
// counts that cap as the disc's wall. It did, once, while ownership was being
// moved from measurement to identity. The cap lies wholly on a declared plane,
// which is the surface it is a facet of. See doc/step-export-wip.md, open
// item 31.
// EXPECT: provenance maps 1 of 1 originals onto 1 of 2 declared surfaces
//
// The hull's profile is (r=8, z=0) to (r=10, z=2) to (r=10, z=20) - the small
// cylinder's top rim at (8, 0.01) is inside that line, so it contributes no
// vertex. Frustum (pi*2/3)(64 + 80 + 100) plus cylinder pi*100*18.
// ROUNDTRIP: Cone=1 Cylinder=1 Plane=2
// VOLUME: 6165.8991814
$fn = 32;
hull() {
  translate([0, 0, 2]) cylinder(h = 18, r = 10);
  cylinder(h = 0.01, r = 8);
}
