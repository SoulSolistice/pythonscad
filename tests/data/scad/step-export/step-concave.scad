// An L shaped prism. The top and bottom faces merge into a concave polygon, so
// the surface normal cannot be taken from the cross product of the first two
// edges of the loop - at a reflex corner that points into the body and turns
// the face inside out.
linear_extrude(height = 5)
  polygon([[0, 0], [20, 0], [20, 5], [5, 5], [5, 20], [0, 20]]);
