// A tapered extrusion of a circle, which sweeps a cone.
//
// The taper idiom - a draft angle, a lead-in, a tapered boss - is as common as
// the straight one, and it was the case that showed the arc channel needed to
// know what `scale` does rather than only whether it was uniform. Declaring the
// circle's radius alone says "cylinder r=10" of a body that is a frustum: the
// fit rejects it, so the export stayed correct and stayed faceted, and the
// report said neither "nothing was declared" nor "something was recognised".
//
// A cone states its intent here the way every other cone in this codebase does:
// by declaring the circle at each of its two ends, which the band pass accepts
// as a frustum when both rims match. So this comes out identical to
// cylinder(h=20, r1=10, r2=5) - three faces, one CONICAL_SURFACE - which is the
// point, since the two are the same solid written two ways.
//
// Two refusals belong to this fixture and are exercised beside it rather than
// in it, because both are correct and produce a valid faceted body:
//
//   an off-centre arc under a taper. `scale` is taken about the profile origin,
//   so such an arc slides sideways as it shrinks and sweeps an *oblique* cone -
//   exact, describable, and not a CONICAL_SURFACE. Nothing is declared.
//
//   scale = 0. The top rim is an apex rather than a circle, so there is no
//   second rim to declare and the body stays faceted - exactly what
//   cylinder(r2 = 0) does, the two paths agreeing by construction.
//
// EXPECT: 2 analytic surfaces available (2 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 1 surface recognised (0 toroidal, 0 spherical, 1 conical, 0 partial), 32 facets replaced
//
// scale=0.5 on r=10 over h=20 is the frustum r1=10, r2=5:
// (pi*20/3)(100 + 50 + 25).
// ROUNDTRIP: Cone=1 Plane=2
// VOLUME: 3665.1914292
$fn = 32;
linear_extrude(height = 20, scale = 0.5) circle(r = 10);
