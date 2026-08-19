// An arc in a revolved profile, which sweeps a torus.
//
// This is the other half of item 1, and it needed the arc channel to be
// possible at all: a rounded profile revolved gives *exactly* the mesh a
// polygonal one gives, so no fit can tell them apart, and the only thing that
// knows the corner was round is offset() at the moment it rounded it.
//
// Each of the four corners of the rounded rectangle sweeps a quarter of a torus:
// closed round the axis, open along the tube. So the face is bounded like any
// other ring - two rim circles of latitude and one seam, an arc of the tube,
// used once in either direction - and not like a complete torus, which has no
// rims at all and is bounded by its own two closed seams. step-torus.scad is
// that one. Both shapes are checked in validatestep.py.
//
// Eight faces, from 1088 facets. Declaring only the profile's vertices - all a
// polygon can offer - gives 36 faces instead: the arcs come out as 32 exact
// cones, which is valid and was what this exported before.
//
// EXPECT: 4 toroidal
// EXPECT: 6 surfaces recognised (4 toroidal, 0 spherical, 0 conical, 0 partial), 1088 facets replaced
$fn = 32;
rotate_extrude() offset(r = 2) translate([10, 0]) square([6, 10]);
