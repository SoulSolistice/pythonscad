// A wall the model says is a cylinder, from a generator that does not.
//
// `linear_extrude` declares nothing. Extruding a circle gives a mesh that is a
// cylinder in every measurable respect and is also, exactly, the mesh of a
// 32 sided prism - the two are the same vertices and the same facets, and no
// examination of the result tells them apart. `cylinder()` gets written as a
// CYLINDRICAL_SURFACE only because the primitive said what it drew.
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
$fn = 32;
declare_cylinder(r = 10)
  linear_extrude(height = 20) circle(r = 10);
