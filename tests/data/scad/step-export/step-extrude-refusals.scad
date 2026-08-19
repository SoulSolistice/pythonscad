// The extrusions that must *not* be declared, in one body.
//
// Every other fixture here guards a surface that should be written. This one
// guards the opposite, and it exists because that is the direction the arc
// channel fails in: a record is a claim about the mesh, and a wrong claim is
// invisible. `linear_extrude(scale = 0.5)` declared "cylinder r = 10" of a body
// that is a frustum for exactly as long as nobody asserted the report - the fit
// rejected it, so the export stayed valid and stayed faceted, and the run said
// neither "nothing was declared" nor "something was recognised". See
// step-tapered-extrude.scad, which is that case once it was understood.
//
// Four sweeps that are not in the quadric vocabulary, none of them declarable:
//
//   twist      a helicoid. Every station is a different profile in a different
//              place, which is also how a screw thread is built.
//   scale=[..] uneven, so the circle becomes an ellipse on the way up and the
//              sweep is a general ruled surface.
//   scale([..]) an ellipse before it is swept at all. The record is dropped by
//              Polygon2d::transform, which tests the matrix for a similarity
//              rather than trusting the caller.
//   v=[..]     an oblique cylinder: a real surface, exactly describable, and not
//              a CYLINDRICAL_SURFACE - its circular section is not perpendicular
//              to its axis. SURFACE_OF_LINEAR_EXTRUSION is what would write it.
//
// The bodies are set apart so each is its own shell and a stray declaration
// cannot be absorbed by a neighbour. What is asserted is the *availability*
// line: nothing reaches the exporter at all, which is stronger than nothing
// being written and is the property that actually broke.
//
// EXPECT: no analytic surfaces were declared
$fn = 32;
linear_extrude(height = 20, twist = 90) circle(r = 4);
translate([20, 0, 0]) linear_extrude(height = 20, scale = [0.5, 1]) circle(r = 4);
translate([40, 0, 0]) linear_extrude(height = 20) scale([2, 1]) circle(r = 2);
translate([60, 0, 0]) linear_extrude(height = 20, v = [6, 0, 20]) circle(r = 4);
