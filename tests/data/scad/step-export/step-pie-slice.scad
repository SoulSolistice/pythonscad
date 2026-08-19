// A cylinder that does not go all the way round.
//
// `cylinder(angle = ...)` used to declare no surface, on the reasoning that a
// pie slice has flat sides which are not part of the cylinder. That is true and
// it was the wrong place to act on it: the flat sides run through the axis, so
// they fit no cylinder and the recogniser discards them on the fit by itself.
// Excluding the whole primitive threw the curved wall away with them.
//
// The exclusion also predates partial cylinders. When it was written a band had
// to close on itself to be written at all, so a 90 degree wall was of no use
// even if it had been declared; that has not been true for two rounds.
//
// This fixture also guards the parse. `angle` was in cylinder()'s parameter
// list and was then dropped on the floor, so a .scad file asking for 90 degrees
// silently got 360 - only the Python binding ever set it. The first version of
// this fixture therefore passed while testing nothing, and the giveaway was in
// the exporter's own report: a pie slice whose wall comes out as a *closed*
// band is not a pie slice.
//
// Expected: 5 faces - one CYLINDRICAL_SURFACE bounded by an arc at either rim
// and the band's two straight ends, the two flat sides, and the two pie shaped
// end faces. At $fn = 32 the ring is generated with one extra vertex on the
// axis, so of its 33 side quads one at each end is flat and 31 are collapsed.
// The report has to say *1 partial*; a closed band means `angle` was ignored
// again.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: 1 analytic surface available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 1 surface recognised (0 toroidal, 0 spherical, 0 conical, 1 partial), 31 facets replaced
$fn = 32;
cylinder(h = 10, r = 8, angle = 90);
