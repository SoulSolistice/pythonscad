// A tapered extrusion of a circle, which sweeps a cone.
//
// The taper idiom - a draft angle, a lead-in, a tapered boss - is as common as
// the straight one, and it was the case that showed the arc channel needed to
// know what `scale` does rather than only whether it was uniform. Declaring the
// circle's radius alone says "cylinder r=10" of a body that is a frustum: the
// fit rejects it, so the export stayed correct and stayed faceted, and the
// report said neither "nothing was declared" nor "something was recognised".
//
// Since 2026-09-25 the extrusion declares the cone itself, as a face, and the
// circle at each of its two ends as evidence - the way rotate_extrude has
// declared a sloped edge's cone since 2026-09-24, and cylinder(r1, r2) always
// has. Until then it declared the two circles alone, the band pass recognised
// the frustum from them, and every facet was held on the tessellation's
// evidence (D3) though the model stated the surface exactly. It is what the
// sweep gives: every station of an even taper is a similarity, so the circle
// stays a circle, its centre travels along the normal, and its radius changes
// linearly - the right cone through its two end circles. So this comes out
// identical to cylinder(h=20, r1=10, r2=5) - three faces, one CONICAL_SURFACE -
// which is the point, since the two are the same solid written two ways.
//
// An off-centre arc under a taper is refused by the same fact, not a rule of
// its own: `scale` is taken about the profile origin, so such an arc's centre
// slides sideways as it shrinks, travels off the normal, and sweeps an
// *oblique* cone - exact, describable, and not a CONICAL_SURFACE.
//
// EXPECT: 3 analytic surfaces available (2 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 conical)
// EXPECT: 1 surface recognised (0 toroidal, 0 spherical, 1 conical, 0 partial), 32 facets replaced
// EXPECT: what decided each written facet: 32 D0, 0 D1, 0 D2, 0 D3, 0 D4
//
// scale=0.5 on r=10 over h=20 is the frustum r1=10, r2=5:
// (pi*20/3)(100 + 50 + 25).
// ROUNDTRIP: Cone=1 Plane=2
// VOLUME: 3665.1914292
$fn = 32;
linear_extrude(height = 20, scale = 0.5) circle(r = 10);
