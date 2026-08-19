// Baseline for the STEP exporter: a single closed body without holes.
// Guards the vertex and edge sharing - every edge of the shell has to be used
// by exactly two faces, once in each direction.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: no analytic surfaces were declared
cube([10, 20, 30]);
