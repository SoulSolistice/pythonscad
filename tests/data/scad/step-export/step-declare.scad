// A wall the model says is a cylinder, from a generator that cannot.
//
// The profile is written out as a polygon, so nothing upstream knows it is a
// circle - which is the point. Its mesh is a cylinder in every measurable
// respect and is also, exactly, the mesh of a 32 sided prism: the same vertices
// and the same facets, and no examination of the result tells them apart.
// `cylinder()` gets written as a CYLINDRICAL_SURFACE only because the primitive
// said what it drew.
//
// It was `circle()` here until circles started carrying their radius into the
// extrusion (see step-extrude-circle.scad). That made this fixture measure two
// channels at once, so the profile became the same points written by hand: the
// mesh is identical to the letter, and the declaration below is once again the
// only thing that could have produced it.
//
// This is the whole of item 5 in miniature. A swept thread or a ramp exists
// only as a polyhedron the user built, and there is no generator anywhere in
// the pipeline that could declare it; the declaration has to come from the
// model. `declare_cylinder` is how it does, and everything downstream is
// already in place - the record is held in world coordinates, so a transform
// above this node moves it with the geometry, and a later boolean or hull
// carries it the way it carries a primitive's own.
//
// The record is a hint and nothing more. The exporter re-checks it against the
// mesh and against the topology before acting, so declaring a cylinder that is
// not there costs one rejected candidate rather than a wrong file. Declaring
// r = 9 below would leave this body faceted and valid.
//
// Expected: 1 analytic surface available, 1 surface recognised, none conical,
// none partial, 32 facets replaced, 34 faces down to 3.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: 1 analytic surface available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 1 surface recognised (0 toroidal, 0 spherical, 0 conical, 0 partial), 32 facets replaced
$fn = 32;
declare_cylinder(r = 10)
  linear_extrude(height = 20)
    polygon([for (i = [0 : 31]) [10 * cos(i * 360 / 32), 10 * sin(i * 360 / 32)]]);
