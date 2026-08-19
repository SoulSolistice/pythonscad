// A frustum drawn as one primitive rather than as a hull.
//
// This is the fixture for the asymmetry that `cylinder(r1, r2)` used to lose
// to `hull()` of two coaxial cylinders. The hull declares the two cylinders it
// was given, and an exporter accepts a cone when both of its rims match a
// declared cylinder - so the workaround exported as a CONICAL_SURFACE while
// the primitive that draws the same shape exported as facets.
//
// The primitive now declares the circle at each of its rims, which is the same
// statement of intent made by one primitive instead of two, so the two
// constructions leave identical provenance and export identically. Nothing in
// the recogniser changed for this.
//
// Expected: one CONICAL_SURFACE for the wall, and the two discs. Both of its
// rims are the complete bound of a disc, which is the easiest of the three rim
// cases - step-chamfered-cylinder covers the shared one.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: 2 analytic surfaces available (2 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 1 surface recognised (0 toroidal, 0 spherical, 1 conical, 0 partial), 32 facets replaced
$fn = 32;
cylinder(h = 10, r1 = 8, r2 = 12);
