// Two concentric tubes on a common base. Their top faces are two annuli in the
// same plane, and the hole of the inner one (r=27) lies inside the outer bound
// of both of them.
//
// mergeTriangles() keeps the last enclosing loop it finds rather than the
// innermost, so a hole can be recorded against the face further out. The face
// it really belongs to is then written without its hole and seals the opening,
// which the CAD system shows as a membrane spanning the bore.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: 5 analytic surfaces available (5 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 5 surfaces recognised (0 toroidal, 0 spherical, 0 conical, 0 partial), 160 facets replaced
$fn = 32;
union() {
  cylinder(h = 2, r = 40);
  difference() { cylinder(h = 20, r = 30); cylinder(h = 20, r = 27); }
  difference() { cylinder(h = 20, r = 38); cylinder(h = 20, r = 35); }
}
