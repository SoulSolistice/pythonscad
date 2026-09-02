// The rounded box, which is the commonest curved profile there is.
//
// offset(r=) rounds each corner with an arc of exactly that radius: Clipper puts
// the join's points on the circle, on a grid of 2^-27 of a unit, three orders
// finer than the tolerance the recogniser fits with. So the profile carries four
// arcs, one per corner of the square, and the extrusion sweeps four quarter
// cylinders.
//
// Quarter, not whole - so all four are *partial* bands, each bounded by two arcs
// of its rims rather than by closed circles, which is the case step-partial-
// cylinder introduced. Ten faces: four cylinders and six planes, against the
// sixty-six this was before the profile carried anything.
//
// EXPECT: 4 analytic surfaces available (4 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 4 surfaces recognised (0 toroidal, 0 spherical, 0 conical, 4 partial), 60 facets replaced
//
// offset(r=3) adds the perimeter times r and one full disc of r:
// (20*30 + 2*(20+30)*3 + pi*3^2) * 10 = (600 + 300 + 9*pi) * 10.
// ROUNDTRIP: Cylinder=4 Plane=6
// VOLUME: 9282.7433388
$fn = 60;
linear_extrude(height = 10) offset(r = 3) square([20, 30], center = true);
