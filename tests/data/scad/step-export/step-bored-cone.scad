// A cone bored across, so the bore's own trim runs on a tapered wall.
//
// step-bored-cylinder is the same shape on a cylinder. This one is here because
// the surface the bore is cut into is a *cone*, which changes what has to be
// written on both sides: the wall's rims are still circles so the band pass can
// take the arc-bounded parts of it as conical faces, but the bore's ends are
// where it meets a taper, and neither is a plane section of anything.
//
// The exact pass takes twenty facets - the two arcs of the wall the bore did not
// interrupt - and leaves fifty-four planes.
// Four records: a circle at each rim of the frustum, the cone between them, and
// the bore's own cylinder. The cone is declared rather than left to be
// recognised from its two rims - see doc/step-export.md, *The rule: declare
// first, recognise second*.
// EXPECT: 4 analytic surfaces available (3 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 conical)
// EXPECT: 2 surfaces recognised (0 toroidal, 0 spherical, 2 conical, 2 partial), 20 facets replaced
//
// The exact tier now writes the bore as well, and for the same reason as in
// step-bored-cylinder: the taper is the faceted one here, so each of its facets
// is a flat plane, and a plane cuts the bore *cylinder* in an ellipse. The mesh
// edges where the bore meets a taper facet are chords of that ellipse; the
// ellipse replaces them and the taper facet across it is handed the same edge.
// EXPECT: 10 trimmed quadrics written as one face each, replacing 24 facets
// EXPECT: 20 plane sections written as the conic it is
//
// The census moves by arithmetic: each replaced facet was one PLANE, so
// 54 - 24 = 30 remain, and the ten new faces are all the bore, so they are
// cylinders and the two conical band faces are untouched.
// ROUNDTRIP: Cone=2 Cylinder=10 Plane=30
// EDGES: Ellipse=20
//
// With the approximation flag the trimmed-quadric path takes the bore - a
// cylinder whose two rims are the curve where it meets the taper, the case the
// band model cannot express at all - and the four quadrants of wall around the
// two mouths, which are cone and could not be band-written because a mouth is
// not a plane section. Six faces: two for the bore, cut at its seam because a
// face on an open rectangle cannot wrap, and one per mouth quadrant.
//
// Six and not two is the whole of what declaring the cone bought here. Before
// it, the wall's four quadrants had no declared cone to be measured against -
// only the two rim circles, and a rim is what the bore takes away - so they
// stayed as fifty-four planes.
// APPROX: 6 trimmed quadrics written as one face each, replacing 52 facets
// APPROX: approximation found nothing left to fit
// ROUNDTRIP-APPROX: Cone=6 Cylinder=2 Plane=2
//
// In the approximation tier the opening is neither chords nor plane sections: it
// is the curve itself. A cone and a cylinder crossing meet in a quartic, which
// ISO 10303 has no entity for, so it is written as a Bezier fitted to the true
// curve found from the two *declarations* - held to 1e-7, and derived identically
// by both faces because both start from the same two declarations.
// APPROX: 80 edges written as the curve where two declared surfaces cross
// APPROX: 80 of those carry a pcurve on each of the two surfaces
// EDGES-APPROX: BSplineCurve=80
//
// Eighty ties two report lines together: the provenance pass finds exactly 80
// junction vertices owned by two surfaces, the bore opens on the taper in two
// closed loops, and a closed loop through n vertices has n edges.
//
// Cone=6 is also the assertion that guards the membership test. A facet that
// spans a hole in a surface has every corner on that surface and no interior on
// it: the bore is walled by one quad per angular step running its whole length,
// and all four corners of each sit on the mouth curves, which are sections of
// the cone. Claiming those for the cone gives Cone=12 Cylinder=4 - a census
// that looks *better* - on a solid whose interior is wrong by half. See
// recogniseQuadricPatches, where the interior of a facet is sampled and not
// only its corners.
//
// The volume is derived. The frustum is (pi*20/3)(12^2 + 12*8 + 8^2) =
// 6366.961111. The bore through it is not elementary - the wall it crosses is
// tapered, so its half width follows R(z) = 12 - 0.2z - and integrating
// 2*sqrt(R(z)^2 - x^2) over the bore's disc gives 984.757269, leaving
// 5382.203842.
//
// And there is nothing left for a window to cover. Every face is the exact
// quadric and the bore's mouth is the quartic, so the export reads 5382.205081
// against the derived 5382.203842 - over by 0.00124, where it used to be over by
// 3.2 with the mouth bounded by chords. That is 2.3e-07 relative, inside the
// default 1e-6 the harness applies when no window is given, so the +/- 12.0 goes
// and the assertion becomes four times stricter than the thing it is measuring.
// VOLUME-APPROX: 5382.203842
$fn = 32;
difference() {
	cylinder(r1 = 12, r2 = 8, h = 20);
	translate([0, 0, 10]) rotate([90, 0, 0]) cylinder(r = 4, h = 60, center = true);
}
