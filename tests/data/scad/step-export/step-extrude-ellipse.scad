// An ellipse drawn in 2D and extruded: the record survives the transform.
//
// scale([2, 1]) takes circle(r = 2) to the ellipse of semi-axes 4 and 2, and an
// affine map takes a circle to an ellipse exactly - the images of its centre and
// of two perpendicular radii. Until 2026-09-25 Polygon2d::transform dropped the
// circle's record there, since it was not a circle any more, and the extruded
// wall was facets. Now the record becomes an Ellipse2d, the chords are named its
// chords, and the extrusion sweeps it into an elliptic cylinder
// (SweptSurfaces.cc, extrudeConic): a circle is the ellipse whose semi-diameters
// are two perpendicular radii, so it is one derivation for both.
// EXPECT: 1 analytic surface available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 elliptic), and 2 declared planes
// EXPECT: what decided each written facet: 32 D0, 0 D1, 0 D2, 0 D3, 0 D4
// EXPECT: 1 face round its axis between two rims closed by a seam along a ruling
// ROUNDTRIP: Plane=2 SurfaceOfExtrusion=1
//
// Derived: pi a b h = pi * 4 * 2 * 20.
// VOLUME: 502.6548246
$fn = 32;
linear_extrude(height = 20) scale([2, 1]) circle(r = 2);
