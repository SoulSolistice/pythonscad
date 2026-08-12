// A cylinder standing on a chamfer, built the way a printed part is: the hull
// of the wall with a smaller copy of itself.
//
// This is the fixture for two things which only work together.
//
// The hull has to carry the two cylinders its children declared, or the wall
// has no intent behind it and stays faceted however exactly it fits. And the
// rim between the wall and the chamfer is the complete bound of neither face,
// so it can only be collapsed as a rim *shared* by two recognised bands - one
// CIRCLE used by the cylinder and by the cone, once in each direction.
//
// The frustum also has no analytic record of its own: a hull declares the two
// cylinders, never the cone between them, so it is accepted because both of
// its rims match one.
$fn = 32;
hull() {
  translate([0, 0, 2]) cylinder(h = 18, r = 10);
  cylinder(h = 0.01, r = 8);
}
