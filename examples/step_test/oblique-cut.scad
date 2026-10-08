// A cylinder cut by a plane at an angle: the first case past one cylindrical
// grid for the boundary evaluated from the model's declarations
// (src/geometry/model/Boundary.h, doc/step-export-wip.md item 42).
//
// cylinder(r = 10, h = 20), less everything above the plane through (0, 0, 10)
// tilted 30 degrees about x: z = 10 + y tan 30, which runs from 4.23 to 15.77
// across the wall - inside its height all round. The model's boundary is three
// faces - the wall between the bottom circle and the cut's ellipse, the bottom
// disk, and the cut, an ellipse with semi-axes 10 / cos 30 = 11.547 and 10 -
// with two edges, each a whole conic, and material on the axis.
//
// The cube's front edge lies exactly in the bottom's plane, 20 along -m from
// (0, 0, 10) at z = 10 - 20 sin 30 = 0: three declared planes share a line
// there, outside the cylinder - which the evaluation has to read as no edge,
// not as a point it cannot decide.
//
// The model's volume: the plane passes through the axis and the cut is
// symmetric across it, so the mean height over the disk is 10:
//
//     pi * 10^2 * 10 = 1000 pi = 3141.592654

difference() {
	cylinder(r = 10, h = 20, $fn = 48);
	translate([0, 0, 10]) rotate([30, 0, 0]) translate([-20, -20, 0]) cube(40);
}
