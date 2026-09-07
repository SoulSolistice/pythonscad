// Two equal cylinders crossing at right angles: an intersection curve with a
// closed form, which is what this fixture is for.
//
// Everywhere else in this suite a trim boundary is the mesh's own polyline, and
// there is nothing to compare it against - a boolean's cut through a tessellated
// solid has no analytic description, so "the boundary is where it should be" is
// not a statement any fixture could check. Here it does have one, and every
// figure below follows from it rather than from an export.
//
// The model. cylinder() runs along z, so the first is x^2 + y^2 = r^2, and
// rotate([90, 0, 0]) carries z onto -y, so the second is x^2 + z^2 = r^2. They
// meet where y^2 = z^2: the two planes z = y and z = -y. In each of those the
// section of either cylinder is an *ellipse* - semi-axis r across it, along x,
// and r/cos(45) = r*sqrt(2) down its slope, so 10 and 14.14213562 here. Those
// two ellipses are the whole of the intersection.
//
// EXPECT: 2 analytic surfaces available (2 cylindrical, 0 spherical, 0 toroidal, 0 Bezier), and 4 declared planes
//
// No plane survives: each cylinder's caps sit at |z| = 20 and the other reaches
// only to 10, so both are cut away entirely. That is what makes this a closed
// shell of nothing but quadrics, and a clean test of a boundary.
//
// How many faces. On the z-axis cylinder the surviving region is
// |z| <= r*|sin theta|, which pinches to a point at theta = 0 and 180. So what
// survives is two lobes of exactly 180 degrees, four lobes over the two
// cylinders, and each lobe is written as two faces - 180 degrees is the widest
// a region can be before a face on an open rectangle becomes ambiguous. Eight
// faces, and they come out as the quadrants: theta 0-90, 90-180, 180-270,
// 270-360, thirty-three boundary vertices each.
// EXPECT: 8 trimmed quadrics written as one face each, replacing 128 facets
//
// 128 facets is the same number the refusing export used to write as 128 planes,
// and it is the model's: each cylinder is a 64-gon, and |z| <= r*|sin theta| is
// a single interval of z for every one of the 64 columns, so every column
// survives as one merged strip and none is cut in two. 64 from each cylinder.
//
// What bounds a face. Each quadrant is bounded above by z = +r*sin theta and
// below by z = -r*sin theta - one arc of each ellipse - and those two meet at
// the pinch, theta = 0 or 180, where the region closes to a point on its own.
// The only other side is the ruling at theta = 90 or 270 where the lobe was
// halved. Three edges, not four, and the reason is that a plane section climbs
// as it goes round: two of them cross, where two rims at constant height never
// could. validatestep.py knows that shape.
//
// So: two ellipses, each cut into four arcs by the two pinches and the two
// quadrant splits, is 8 arcs; two rulings on each cylinder is 4 lines; 12
// distinct edges, every one of them shared by exactly two of the eight faces.
// ROUNDTRIP: Cylinder=8
// EDGES: Ellipse=8 Line=4
//
// And the figure none of the above can fake. The solid two equal cylinders cut
// from each other is the Steinmetz solid, whose volume is exactly 16*r^3/3 -
// no pi in it, which is the pleasant surprise of the shape. 16*1000/3 is
// 5333.33333, and OpenCASCADE measures the exported solid at 5333.333262: 1.3e-8
// relative, which is the kernel's own integration and not the model's.
//
// The number is what makes this fixture worth having. Every other expectation
// here is a census of what this exporter wrote, so it locks the behaviour in but
// cannot say it was ever right; this one was worked out from r alone and the
// kernel that measures it has no access to the arithmetic that predicted it.
// When the boundary was the mesh's chords the same export measured 5320.49 -
// 12.8 low, 0.24 per cent, the chords cutting inside the ellipse all the way
// round both curves. Nothing in the census moved when that was fixed except the
// surface names; the volume is the only line that knew the difference.
// VOLUME: 5333.33333
//
// The approximation flag has nothing left to approximate here.
// ROUNDTRIP-APPROX: Cylinder=8
// VOLUME-APPROX: 5333.33333
$fn = 64;
r = 10;
h = 40;
intersection() {
	cylinder(r = r, h = h, center = true);
	rotate([90, 0, 0]) cylinder(r = r, h = h, center = true);
}
