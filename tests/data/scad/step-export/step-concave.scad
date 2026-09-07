// An L shaped prism. The top and bottom faces merge into a concave polygon, so
// the surface normal cannot be taken from the cross product of the first two
// edges of the loop - at a reflex corner that points into the body and turns
// the face inside out.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
//
// The four zeros, not "nothing was declared": the extrusion's caps and its
// straight walls are planes and are declared as such. What must stay empty is
// the curved channel, and that is what the zeros say.
// EXPECT: 0 analytic surfaces available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
//
// The plane channel is pinned too, so that narrowing the line above did not
// leave it unwatched. Eight is the model's: the profile is a hexagon, so six
// walls, and a linear extrusion has two caps. Every one of the eight faces is
// written on one of them rather than on a plane fitted to its own corners, which
// is the whole of what a declaration buys for something already flat - the
// coefficients stop moving when a later pass touches a vertex.
// EXPECT: and 8 declared planes
// EXPECT: 8 planar faces written on a plane the model declared
//
// The L is 175 square units by the shoelace formula, extruded 5, so 875. A
// concave outline is where a normal taken from the first two edges of a loop
// points the wrong way, and an inverted face would show up here as a volume
// that is not this one.
// ROUNDTRIP: Plane=8
// VOLUME: 875
linear_extrude(height = 5)
  polygon([[0, 0], [20, 0], [20, 5], [5, 5], [5, 20], [0, 20]]);
