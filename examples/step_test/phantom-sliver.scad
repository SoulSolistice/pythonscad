// A cutter nearer a surface than that surface's tessellation sags: slivers the
// model does not have survive in the mesh, every face of them made on a
// declared surface. lid10 in eight lines - its bayonet cutters sit `eps` = 0.1
// inside a socket whose chords sag 0.107 - and the case the existence design
// exists for (doc/step-recognition-design.md, "Existence is a D0 question
// too"; checklist B10).
//
// The body is a cylinder r = 20 with a bore r = 15 through it at $fn = 24,
// whose chords sag 15 (1 - cos 7.5 degrees) = 0.128 at their midpoints. From
// z = 10 up, a quarter slot is cut from r = 14.9 out to r = 17 - its inner
// wall at $fn = 96, so it sags only 0.008 and stays near 14.9 all round. The
// model's slot starts inside the bore's air; the mesh's does not everywhere:
// near each chord's midpoint the bore reaches only 14.872 and a sliver between
// it and 14.9 is left standing. Its outer wall is made on the r = 14.9
// cylinder, its inner wall on the bore's, its top on the body's cap - and the
// model has none of them, since everything within r = 15 is the bore's.
//
// The exporter asks the model (recognition/Existence.h): every point of those
// facets, taken onto the surface it was made on, lies inside the bore, so the
// model's solid has no face there. They are phantoms, and while the file writes
// the mesh's slivers rather than the model's solid that is an EXPORT-ERROR,
// named here so the day phantoms are written as the model's solid announces
// itself:
// KNOWN-EXPORT-ERROR: made on a declared surface where the model's solid has no face
//
// And a second, older defect this coupon also carries: the bore's D0 region is
// split between a band's ring and a trimmed patch, on a boundary the model
// does not have (checklist B3) - R0's grant, parked until existence lands:
// KNOWN-EXPORT-ERROR: D0 region is split on a boundary the model does not have
//
// The declarations, read off the model: the body, the bore, the slot's slice
// and its inner cutter are four cylinders; their caps are eight planes, of
// which the bore's top and the slice's top coincide at z = 21.
// EXPECT: 4 analytic surfaces available (4 cylindrical, 0 spherical, 0 toroidal, 0 Bezier), and 7 declared planes
//
// The model's volume is elementary, and not yet asserted - the exact tier
// writes the bore partly as chords, which sit inside it, and the slivers; it
// is the figure the file must reach once both are the model's:
//
//     body   pi * 20^2 * 20                         = 8000 pi
//     bore   pi * 15^2 * 20                         = 4500 pi
//     slot   (90/360) * pi * (17^2 - 15^2) * 10     =  160 pi
//     solid  3340 pi                                = 10492.919463
//
// Measured 2026-09-27: the exact tier exports 10514.507466, the approximation
// tier 10488.521393 with a cylinder face's corner 0.103 off its surface.
//
// **A specimen, not yet a fixture.** Every fixture's approximation tier is
// round-tripped, and a corner off the face it bounds fails that - rightly, and
// no directive may excuse it. This coupon's corner sits where the bore's chord
// meets the phantom sliver: exactly the cut-corner defect the existence design
// is for (steps 4 to 7). It moves to tests/data/scad/step-export/ the day its
// approximation tier survives the round trip, with its VOLUME asserted then;
// until that day the detection it drives is held by
// src/geometry/recognition/existence_test.cc.

difference() {
	cylinder(r = 20, h = 20, $fn = 24);
	translate([0, 0, -1]) cylinder(r = 15, h = 22, $fn = 24);
	translate([0, 0, 10]) difference() {
		cylinder(r = 17, h = 11, angle = 90, $fn = 96);
		translate([0, 0, -1]) cylinder(r = 14.9, h = 13, $fn = 96);
	}
}
