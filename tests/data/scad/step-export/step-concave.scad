// An L shaped prism. The top and bottom faces merge into a concave polygon, so
// the surface normal cannot be taken from the cross product of the first two
// edges of the loop - at a reflex corner that points into the body and turns
// the face inside out.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: no analytic surfaces were declared
//
// The L is 175 square units by the shoelace formula, extruded 5, so 875. A
// concave outline is where a normal taken from the first two edges of a loop
// points the wrong way, and an inverted face would show up here as a volume
// that is not this one.
// ROUNDTRIP: Plane=8
// VOLUME: 875
linear_extrude(height = 5)
  polygon([[0, 0], [20, 0], [20, 5], [5, 5], [5, 20], [0, 20]]);
