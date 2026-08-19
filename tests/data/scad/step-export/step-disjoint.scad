// Two bodies which do not touch. A CLOSED_SHELL has to be a single connected
// shell, so these have to end up as one MANIFOLD_SOLID_BREP each instead of
// being stuffed into one shell that can never close.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: no analytic surfaces were declared
cube([5, 5, 5]);
translate([20, 0, 0]) cube([5, 5, 5]);
