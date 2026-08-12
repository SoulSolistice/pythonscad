// A cylinder that does not go all the way round.
//
// `cylinder(angle = ...)` used to declare nothing, on the reasoning that a pie
// slice has flat sides which are not part of the cylinder. That is true and it
// was the wrong place to act on it: the flat sides run through the axis, so
// they fit no cylinder and the recogniser discards them on the fit by itself.
// Excluding the whole primitive threw the curved wall away with them.
//
// The exclusion also predates partial cylinders. When it was written a band had
// to close on itself to be written at all, so a 90 degree wall was of no use
// even if it had been declared; that has not been true for two rounds.
//
// Expected: one CYLINDRICAL_SURFACE bounded by an arc at either rim and the
// band's two straight ends, and no analytic face for either flat side. At
// $fn = 32 the wall is 30 facets - the ring is generated with one extra vertex
// on the axis and one quad to each flat side.
$fn = 32;
cylinder(h = 10, r = 8, angle = 90);
