// A circle extruded obliquely: an elliptic cylinder, whose sections by the caps
// are the circle.
//
// linear_extrude(v = ...) carries every station of the profile along v, so the
// wall is the circle swept along a direction that is not its plane's normal. A
// circle swept is a cylinder over whatever section the sweep leaves it, and the
// section across v here is an ellipse, the circle foreshortened: major
// semi-axis 4, and minor 4 times the cosine of the angle between v and the
// normal. Until 2026-09-25 that was a
// refusal - "an oblique cylinder, not a CYLINDRICAL_SURFACE" - and the wall was
// facets. Now it is declared an EllipticCylinderSurface and written as the
// SURFACE_OF_LINEAR_EXTRUSION of that ellipse (SweptSurfaces.cc, extrudeArc).
// EXPECT: 1 analytic surface available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 elliptic), and 2 declared planes
//
// Every vertex of the wall is the circle moved along v, so every facet is on it
// exactly, and the wall goes all the way round between its caps: one face,
// closed by a seam, each rim the whole section its cap's plane cuts.
// EXPECT: what decided each written facet: 32 D0, 0 D1, 0 D2, 0 D3, 0 D4
// EXPECT: 1 face round its axis between two rims closed by a seam along a ruling
// EXPECT: 2 plane sections written as the conic it is - 2 on a plane the model declared
// ROUNDTRIP: Plane=2 SurfaceOfExtrusion=1
//
// Derived: the height is measured along v, so the far cap stands 20 * 20 /
// sqrt(436) above the near one, and the solid is the disc of radius 4 times that
// height - Cavalieri: 16 pi * 400 / sqrt(436).
// VOLUME: 962.9120067
$fn = 32;
linear_extrude(height = 20, v = [6, 0, 20]) circle(r = 4);
