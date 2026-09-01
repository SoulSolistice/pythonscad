// A cylinder bored across, whose trim is a quartic.
//
// This is roadmap item 4's general case at its smallest. The wall and the bore
// are both declared cylinders and both are exactly right, so nothing is left to
// declare - what is missing is the curve where they meet. Two cylinders
// intersect in a quartic, and STEP has no entity for it: OpenCASCADE's own
// export of this solid writes the trim as a degree-7 B_SPLINE_CURVE_WITH_KNOTS
// of some thirty control points. So the general case is inherently an
// approximation, and it is gated as one.
//
// The band recogniser cannot write either face. A band *is* two rims at a
// constant height, and neither of these rims is: the bore's ends are where it
// meets the wall, and the wall is opened by a hole. Its exact pass therefore
// takes only the arc-bounded parts of the wall - two partial cylinders, twenty
// facets - and leaves the other fifty-two as planes.
//
// EXPECT: 2 analytic surfaces available (2 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 2 surfaces recognised (0 toroidal, 0 spherical, 0 conical, 2 partial), 20 facets replaced
// ROUNDTRIP: Cylinder=2 Plane=54
//
// With the approximation flag the trimmed-quadric path takes the rest. It
// claims facets by distance to the axis rather than by walking rings, and
// bounds the face with the mesh's own polyline - the same bound the faceted
// faces around it already use, so the shell closes edge for edge. Eight
// cylinder faces rather than two because a face written on an open rectangle
// cannot wrap, so each surface is cut at its seam.
// APPROX: 4 trimmed quadrics written as one face each, replacing 52 facets
// APPROX: approximation found nothing left to fit
// ROUNDTRIP-APPROX: Cylinder=8 Plane=2
//
// Why this is allowed to be approximate, measured rather than asserted. Where a
// ruling of the bore met a facet of the wall the vertex is exactly on the true
// bore; where a facet met an *edge* it is not. Of the eighty vertices near the
// bore radius, sixty-four are exact to 1e-9 and sixteen are off by at most
// 0.018804 - against that region's own tessellation band of 0.0193. The trim
// strays no further than the mesh already allows, which is the same licence the
// rest of the approximation pass runs on.
//
// The volume is derived, and it is not elementary: two perpendicular cylinders
// of *different* radii meet in an elliptic integral. For each x the bore fixes z
// over 2*sqrt(16-x^2) and the wall fixes y over 2*sqrt(100-x^2), so the shared
// volume is 8*int_0^4 sqrt((16-x^2)(100-x^2)) dx = 984.779688, and the solid is
// pi*100*20 - that = 5298.405619. OpenCASCADE, asked to build the same solid
// from its own primitives and measure it, says 5298.405182 - agreement to 8e-8,
// which is what says the figure is right rather than merely repeatable.
//
// The tolerance is for the boundary, not the surface. Every face here is the
// exact cylinder, but each is bounded by chords rather than by the quartic, so
// the solid is those cylinders trimmed by a polyline and reads 5296.533. That
// deficit of 1.87 over the roughly 500 square units the trimmed faces cover is
// a mean normal displacement of 0.004, well inside the 0.0193 band. It is far
// too tight to hide a wrong radius: boring at r=4.1 instead moves this by 25.
// VOLUME-APPROX: 5298.405619 +/- 2.0
$fn = 32;
difference() {
	cylinder(r = 10, h = 20);
	translate([0, 0, 10]) rotate([90, 0, 0]) cylinder(r = 4, h = 60, center = true);
}
