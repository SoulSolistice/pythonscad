// Baseline for the STEP exporter: a single closed body without holes.
// Guards the vertex and edge sharing - every edge of the shell has to be used
// by exactly two faces, once in each direction.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// Nothing *curved* is declared, which is the assertion: there is no recogniser
// work to do here, so a recogniser that stopped working could not hide behind
// this fixture. The six planes are the box's own faces, which cube() states
// since PlaneSurface existed - a flat face is a thing a model can mean, and the
// corner placement needs to tell one from a chord of some tessellation. Six of
// them for six faces, and they are counted apart from the curved surfaces
// because nothing is ever recognised *onto* a plane.
// EXPECT: 0 analytic surfaces available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier), and 6 declared planes
//
// A box, so the volume is the box: 10*20*30 = 6000, and it is here because a
// fixture whose answer is arithmetic anyone can do is the one that proves the
// check itself works.
// ROUNDTRIP: Plane=6
// VOLUME: 6000
cube([10, 20, 30]);
