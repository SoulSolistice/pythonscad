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
// channels at once, so the profile became the same points written by hand. That
// stopped being silent too: `polygon()` names its edges, straight lines - which
// is the truth about a polygon - and since 2026-09-29 an extrusion names each
// wall facet by the plane its edge sweeps (open item 40), so the walls were
// 32 planes by their maker's word, and a declaration around them contradicted
// it. A declaration fills where the generators said nothing; it does not
// overrule one that spoke. So the wall is now the one generator that says
// nothing about its faces, `polyhedron()`, over the same points: the mesh is the
// same prism, and the declaration below is once again the only thing that
// could have produced a cylinder.
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
//
// pi*10^2*20, the true cylinder the declaration asked for. The mesh is a
// 32-gon prism of 6238.7, so this number is also what says the declaration
// was acted on.
// ROUNDTRIP: Cylinder=1 Plane=2
// VOLUME: 6283.1853072
ring = [for (i = [0 : 31]) [10 * cos(i * 360 / 32), 10 * sin(i * 360 / 32)]];
declare_cylinder(r = 10, faces = "wall")
  polyhedron(
    points = concat([for (p = ring) [p[0], p[1], 0]], [for (p = ring) [p[0], p[1], 20]]),
    faces = concat(
      [[for (i = [0 : 31]) i]],                             // bottom, clockwise seen from below
      [[for (i = [31 : -1 : 0]) i + 32]],                   // top, clockwise seen from above
      [for (i = [0 : 31]) [i, i + 32, (i + 1) % 32 + 32, (i + 1) % 32]]),  // the 32 walls
    // Which faces the cylinder is: the walls, by label - the model's word per
    // face, where the corners alone cannot tell a wall from the caps, whose
    // corners are on the circle too (checklist E8).
    labels = concat(["", ""], [for (i = [0 : 31]) "wall"]),
    convexity = 2);
