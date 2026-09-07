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
// And there the ellipse count is derived rather than measured. The bore is a
// 32-gon and the approximation tier claims the whole opening, so every one of
// its 32 facet planes cuts the wall in an ellipse and every one of them is used.
// The exact tier writes 20 of the same 32 - the ones bounding a region it will
// write at all - which is a count of what it claims, not of the model.
// EDGES-APPROX: Ellipse=32
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
// exact cylinder, and since the boundary segments that run around one at a
// constant height are written as arcs of it, the outer wall is now bounded
// exactly: its rim is the true circle rather than a 32-gon inscribed in it.
// What is left is the bore's opening, and it is no longer chords: the wall's 32
// facet planes each cut the bore in an ellipse, so the opening is a chain of 32
// elliptical arcs, every one of them exactly on the bore. The quartic still has
// no entity to be written as - the chain is inscribed in it, at the wall's own
// sagitta of 10*(1-cos(pi/32)) = 0.0482 - so the bore still removes slightly too
// little, but the solid now reads 5298.92 against the derived 5298.41: short by
// 0.52 where it was short by 3.16.
//
// The bound on that is the bore's own tessellation. A chord of a 32-gon on r=4
// lies at most 4*(1-cos(pi/32)) = 0.0193 inside the true circle, and the bore's
// faces cover about 460 square units, so no more than 460*0.0193/2 = 4.4 can be
// lost this way. Five is the bound rounded up. It is a bound and not a
// measurement, so it stays where it is now that the deficit has fallen well
// inside it; what locks in the improvement is EDGES-APPROX above, which says the
// opening is arcs. And it is still far too tight to hide a wrong radius: boring
// at r=4.1 instead moves this by 25.
//
// The deficit used to read 1.87 rather than 3.16, which was the smaller number
// for the worse reason - the outer rim was inscribed too, and its error had the
// opposite sign and very nearly cancelled the bore's.
// VOLUME-APPROX: 5298.405619 +/- 5.0
$fn = 32;
difference() {
	cylinder(r = 10, h = 20);
	translate([0, 0, 10]) rotate([90, 0, 0]) cylinder(r = 4, h = 60, center = true);
}
