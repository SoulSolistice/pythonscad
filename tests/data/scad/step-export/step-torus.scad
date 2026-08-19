// A torus, written as one TOROIDAL_SURFACE.
//
// rotate_extrude() meshes a circular profile as a grid: one ring per profile
// edge, and every quad of that grid has its four corners at two radii and two
// heights. That is exactly what a frustum band looks like, and since a straight
// profile edge declares the circle at each of its ends, every ring has both of
// its rims declared. So even with nothing else a torus collapses - into a stack
// of exact cones, each passing through the mesh vertices with zero residual and
// sharing its rim circle with the ring above and below. At $fn = 32 that alone
// is 1024 facets down to 32 faces.
//
// The stack is then merged into one surface, by the same pass that merges a
// sphere. The difference is only that this run has no ends: every rim is
// shared, so there is nothing to keep, and the face is bounded by its own two
// seams instead - the circle the tube's centre traces and the circle of the
// tube itself, each closed, each through the one vertex where they cross, each
// used once in either direction. That is the same four edge loop a periodic
// cylinder uses, so the doubly periodic case needs a second seam and not a new
// shape, which is what the roadmap expected it to need.
//
// It needs a declaration of its own. The ring circles say "cone" thirty-two
// times over and only a TorusSurface says the thirty-two were one surface -
// which rotate_extrude knows because the circle told it. The profile carries the
// circle as an Arc2d and the revolve turns it into the torus it sweeps, the same
// path a rounded corner takes in step-rounded-profile.scad.
//
// It used to be read off the node tree instead - walk down through the
// transforms and see whether the child is a CircleNode - because a 2D outline
// carried vertices, a winding flag and a colour and no record of having been
// round. That worked for a bare circle and stopped at the first difference() or
// rotation in the way. The channel does not.
//
// This is also the first thing to chain more than three closed bands through
// shared rims; the bayonet's longest chain is a wall on a chamfer on a wall.
//
// Expected: 18 analytic surfaces available - the profile's 32 radii, which
// repeat in pairs about the widest and narrowest points and so give 17, plus
// the torus - and 1 surface recognised, toroidal, none conical, none partial,
// 1024 facets replaced. One face, because a torus is closed and has no others.
//
// If the report says 32 conical, the torus was not declared and this is the
// cone stack again.
$fn = 32;
rotate_extrude()
  translate([10, 0]) circle(3);
