// A torus, which is not a TOROIDAL_SURFACE and does not need to be.
//
// rotate_extrude() meshes a circular profile as a grid: one ring per profile
// edge, and every quad of that grid has its four corners at two radii and two
// heights. That is exactly what a frustum band looks like, and since a straight
// profile edge now declares the circle at each of its ends, every ring has both
// of its rims declared. So a torus already collapses - into a *stack of exact
// cones*, each one passing through the mesh vertices with zero residual and
// sharing its rim circle with the ring above and below.
//
// At $fn = 32 that is 1024 facets down to 32 faces. A real TOROIDAL_SURFACE
// would be 1 face; the remaining factor of 32 is what that item is now worth,
// against the factor of 32 already collected here for nothing.
//
// This fixture exists mainly because it is the first thing to chain more than
// three closed bands through shared rims - the bayonet's longest chain is a
// wall on a chamfer on a wall. The seam pass is a single pass over the bands:
// each one takes whichever of its rims another band has already settled and
// derives the other along a ruling, which is consistent only while the chain
// does not fork. Thirty-two rings closing back on themselves is the first real
// test of that. A fork shows up as "EDGE_LOOP does not close", because the seam
// line would end on a different vertex from the one the shared CIRCLE starts
// at.
//
// Expected: 17 analytic surfaces available - the profile's 32 radii, which
// repeat in pairs about the widest and narrowest points - and 32 surfaces
// recognised, all conical, none partial, 1024 facets replaced.
$fn = 32;
rotate_extrude()
  translate([10, 0]) circle(3);
