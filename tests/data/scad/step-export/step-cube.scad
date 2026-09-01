// Baseline for the STEP exporter: a single closed body without holes.
// Guards the vertex and edge sharing - every edge of the shell has to be used
// by exactly two faces, once in each direction.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: no analytic surfaces were declared
//
// A box, so the volume is the box: 10*20*30 = 6000, and it is here because a
// fixture whose answer is arithmetic anyone can do is the one that proves the
// check itself works.
// ROUNDTRIP: Plane=6
// VOLUME: 6000
cube([10, 20, 30]);
