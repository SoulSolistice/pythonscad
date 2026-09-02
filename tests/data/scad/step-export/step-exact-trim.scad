// A cylinder whose rim is notched, so the wall is trimmed but exactly.
//
// The band recogniser writes a wall as a run of facets between two rims, and
// it needs both rims whole. Notch the top one and it has nothing to work
// with - but the wall itself is untouched, every corner of every facet still
// sits on the cylinder the model declared, and there is nothing approximate
// about writing it as that cylinder.
//
// That is the distinction this fixture exists to hold. Recognising a trimmed
// quadric used to be gated behind the approximation flag as a whole, on the
// grounds that a trim's vertices are not all on the surface. Some are and some
// are not, and which is a question the mesh answers: a boolean puts its new
// vertices on the facet planes rather than on the ideal surface, so it is the
// fringe where the cut landed that strays, and the interior does not. A region
// whose every corner is on the surface to 1e-7 asserts nothing the mesh does
// not already state, and it belongs in the exact pass.
//
// The notches are 3 wide against a chord of 1.96, so each one crosses a vertex
// and eats into the two facets either side. Those facets are the fringe: their
// cut corners are at radius 9.85, not 10, and they stay planar. The sixteen
// facets nowhere near a notch are written as the cylinder.
//
// EXPECT: 1 analytic surface available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 2 trimmed quadrics written as one face each, replacing 16 facets (exactly on their surface)
// EXPECT-NOT: within their tessellation band
// ROUNDTRIP: Cylinder=8 Plane=50
// RADII: Cylinder=10
//
// Nothing here is approximated, so the kernel has no slack to take up. This is
// the assertion the rest of the fixture is scaffolding for: every edge lies on
// both faces it bounds, and a kernel that has to widen a tolerance to sew the
// file is telling us it does not. It is what an exact pass is *for*, and it is
// checked nowhere else, because until this face existed the exact pass only
// ever wrote bands - whose bounds are rim circles and rulings, and so were
// exact whether anyone had thought about it or not.
//
// 1e-7 is OpenCASCADE's own floor, Precision::Confusion, and it reports that
// for a shape it did not have to open up at all. There is no smaller number to
// ask for, and it is the first fixture here to ask for any.
// TOLERANCE: 1e-07
//
// The volume is derived. The mesh is a 32-gonal prism, area n*r^2*sin(2*pi/n)/2
// over h=20, which is 6242.890305, less eight notches. A notch is the box
// x in [7,13], y in [-1.5,1.5] cut over the 3 of its height that is inside the
// solid; theta = 0 is a vertex, so within |y| <= 1.5 the prism's boundary is
// two straight edges running from (9.852263, -1.5) through (10, 0) and back
// out, and the area between them and x = 7 is 8.778394. Eight of those over a
// depth of 3 remove 210.681464, leaving a mesh of 6032.208840.
//
// Writing sixteen facets as the cylinder then puts back the circular segment
// each of them cuts off: r^2*(theta - sin theta)/2 = 0.062961 per unit height,
// over 16 facets of height 20, so 20.147502. The export is 6052.356342, and
// OpenCASCADE measuring the file agrees to the six figures it prints - which is
// what says the derivation is right rather than merely repeatable.
// VOLUME: 6052.356342 +/- 0.001
$fn = 32;
difference() {
	cylinder(r = 10, h = 20);
	for (i = [0:7]) rotate([0, 0, i * 45]) translate([10, 0, 20]) cube([6, 3, 6], center = true);
}
