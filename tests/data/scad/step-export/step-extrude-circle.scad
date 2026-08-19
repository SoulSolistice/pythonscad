// A cylinder nobody had to declare: linear_extrude of a circle.
//
// The mesh is a ring of 32 quads, which is equally the mesh of a 32 sided prism.
// Nothing measurable tells them apart - that is the ambiguity the declaration
// channel exists for, and step-declare.scad is the same body with the statement
// made by hand. Here the circle makes it itself: it records its radius as an
// Arc2d on the profile, and the extrusion turns that into the cylinder it swept.
//
// The refusals are the interesting half, and each has its own fixture beside
// this one: a twist or an uneven scale sweeps a helicoid or a general ruled
// surface, an oblique v sweeps an oblique cylinder whose circular section is not
// perpendicular to its axis, and a sheared profile plane has no circle in it to
// begin with. None of those is a CYLINDRICAL_SURFACE and none is declared.
//
// EXPECT: 1 analytic surface available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 1 surface recognised (0 toroidal, 0 spherical, 0 conical, 0 partial), 32 facets replaced
$fn = 32;
linear_extrude(height = 20) circle(r = 10);
