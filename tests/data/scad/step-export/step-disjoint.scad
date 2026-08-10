// Two bodies which do not touch. A CLOSED_SHELL has to be a single connected
// shell, so these have to end up as one MANIFOLD_SOLID_BREP each instead of
// being stuffed into one shell that can never close.
cube([5, 5, 5]);
translate([20, 0, 0]) cube([5, 5, 5]);
