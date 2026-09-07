// Two bodies which do not touch. A CLOSED_SHELL has to be a single connected
// shell, so these have to end up as one MANIFOLD_SOLID_BREP each instead of
// being stuffed into one shell that can never close.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// Nothing curved is declared, as with step-cube. The plane count is eight
// rather than twelve, and that is this fixture's own arithmetic: the two boxes
// have the same extent in y and z and differ only along x, so their y = 0,
// y = 5, z = 0 and z = 5 faces lie in the same four planes and are one record
// each - Surface::sameAs compares a plane by the plane, not by the point it was
// anchored at. Four x planes at 0, 5, 20 and 25 and four shared ones is eight.
// EXPECT: 0 analytic surfaces available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier), and 8 declared planes
//
// Two 5mm cubes, 125 each. The figure is what says both bodies survived: lose
// one and the kernel still reads a valid solid, of half the volume.
// ROUNDTRIP: Plane=12
// VOLUME: 250
cube([5, 5, 5]);
translate([20, 0, 0]) cube([5, 5, 5]);
