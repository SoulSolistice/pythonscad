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
// facets - and leaves the other fifty-two to the trimmed-quadric pass.
//
// That pass now takes ten of them, and it is the *bore* it writes, not the wall.
// The wall is the 32-gon here, so each of its facets is a flat plane, and a
// plane cuts the bore cylinder in an ellipse - exact, and written as one. The
// mesh edges where the bore meets a wall facet are chords of that ellipse; the
// ellipse replaces them, the wall facet on the other side is handed the same
// edge, and the bore's regions stop being bounded off themselves.
// EXPECT: 10 trimmed quadrics written as one face each, replacing 24 facets
//
// So the census moves by arithmetic, not by measurement: each replaced facet was
// one PLANE, so 54 - 24 = 30 remain, and the two partial cylinders the band pass
// already wrote are joined by the ten new ones for 12.
//
// And the ellipses are the model's, down to their axes. The wall's facet normals
// lie at 5.625 + 11.25k degrees from x; the bore runs along y; so a facet plane
// meets the bore at a tilt whose cosine is |sin| of that angle, and the section
// has the bore's own radius 4 across it and 4/|sin| along it. Only three of
// those angles reach the bore at all, and the file carries exactly their three
// values - 4/sin(61.875) = 4.5356 on four arcs, 4/sin(73.125) = 4.1800 on
// eight, 4/sin(84.375) = 4.0194 on eight. Twenty arcs, which is what the export
// reports, and every minor semi-axis is 4 because a plane section of a cylinder
// is as wide as the cylinder however it is tilted - validatestep.py checks that
// one on every ellipse in every export.
// EXPECT: 20 plane sections written as the conic it is
// EDGES: Ellipse=20
//
// Provenance, reported and gating nothing. Manifold keeps an id per run of
// triangles through a boolean chain - it is what makes colour survive a
// difference() - and this says what survived. Two originals here, the wall and
// the bore, which is the whole model. It is asserted because a channel nothing
// checks is a channel that quietly stops working, and because the number is
// derivable by looking at the model rather than at the exporter.
// EXPECT: the mesh comes from 2 original solids over 288 facets
// EXPECT: 2 analytic surfaces available (2 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 2 surfaces recognised (0 toroidal, 0 spherical, 0 conical, 2 partial), 20 facets replaced
// ROUNDTRIP: Cylinder=12 Plane=30
//
// With the approximation flag the trimmed-quadric path takes the rest. It
// claims facets by distance to the axis rather than by walking rings, and
// bounds the face with the mesh's own polyline - the same bound the faceted
// faces around it already use, so the shell closes edge for edge. Eight
// cylinder faces rather than two because a face written on an open rectangle
// cannot wrap, so each surface is cut at its seam.
// Six faces, not four. The bore cuts the wall into separate regions and a
// face cannot be in two places, so each region is its own face; the two the
// count gained were being carried as inner bounds of the others, which is
// what an inner bound is not for. ROUNDTRIP-APPROX does not move, because
// OpenCASCADE was splitting them on read and counting eight all along - what
// changed is that the file now says what the kernel was already making of it.
// APPROX: 6 trimmed quadrics written as one face each, replacing 52 facets
// APPROX: approximation found nothing left to fit
// ROUNDTRIP-APPROX: Cylinder=8 Plane=2
//
// And in the approximation tier the opening is neither chords nor sections any
// more: it is the curve itself. Once the corner placement has put the junction
// vertices on both cylinders, every edge between two of them is an arc of the
// quartic where the two cross, and the exporter writes that - a Bezier fitted to
// the true curve found from the two *declarations*, held to 1e-7, and derived
// identically by both faces because both start from the same two declarations.
// APPROX: 80 edges written as the curve where two declared surfaces cross
// EDGES-APPROX: BSplineCurve=80
//
// Eighty is the model's, and it ties two report lines together: the provenance
// pass finds exactly 80 junction vertices owned by two surfaces, the bore opens
// on the wall in two closed loops, and a closed loop through n vertices has n
// edges. One curve per junction edge.
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
// And there is nothing left for the tolerance to cover. Every face is the exact
// cylinder, every boundary segment around one at constant height is an arc of
// it, and the opening is the quartic. The approximation export measures
//
//     derived                       5298.405619
//     chords                        5301.57       short by 3.16
//     plane sections on one surface 5298.921138   over by 0.5155
//     the curve where they cross    5298.405620   over by 1e-06
//
// Two parts in 1e13, on a solid whose trim curve ISO 10303 has no entity for.
// The quartic is still approximated - a Bezier through it, degree raised until
// the fit is inside 1e-7 - but it approximates the *true* curve rather than the
// mesh, which is the whole of the difference.
//
// So the window comes off. It used to be +/- 5.0, bounded by the bore's own
// tessellation - a chord of a 32-gon on r=4 lies at most 4*(1-cos(pi/32)) =
// 0.0193 inside the true circle, over about 460 square units of bore, so no more
// than 4.4 could be lost that way. None of that applies to a trim which is the
// curve. What is left is the default 1e-6 relative, 0.0053, and the export is
// inside it by four thousand times.
//
// The deficit used to read 1.87 rather than 3.16, which was the smaller number
// for the worse reason - the outer rim was inscribed too, and its error had the
// opposite sign and very nearly cancelled the bore's. That is the reason a
// volume window is a poor way to lock in a boundary and EDGES-APPROX is a good
// one: two errors of opposite sign look like accuracy.
// VOLUME-APPROX: 5298.405619
$fn = 32;
difference() {
	cylinder(r = 10, h = 20);
	translate([0, 0, 10]) rotate([90, 0, 0]) cylinder(r = 4, h = 60, center = true);
}
