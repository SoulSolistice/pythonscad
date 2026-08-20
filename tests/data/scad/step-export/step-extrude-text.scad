// An extruded glyph, whose walls are the font's own Bezier curves.
//
// This is the arc channel's other half, and the case that showed one channel was
// not enough: a glyph outline is not made of arcs. FreeType hands text() its
// contours as lines and quadratic or cubic Beziers - FreetypeRenderer's conic_to
// and cubic_to - and DrawingCallback samples them into points one line after
// holding the control points, which is exactly what circle() used to do with its
// radius. Bezier2d records them where they are known.
//
// Extruded, such a segment sweeps a patch of degree (n, 1) whose control net is
// the segment's own control points at each end of the sweep. That is exact, not
// fitted, because a Bezier and a linear sweep are both affine in their control
// points - and it needed no recogniser work at all, because BezierPatchSurface
// takes a general degree and recogniseBezierPatches accepts any declared patch.
// The machinery came in for fillet(); a glyph wall is the same shape of thing.
//
// S rather than a rounder letter on purpose: it is all curve and no counter, so
// every one of its 32 patches is written and the count is the whole answer.
// step-extrude-text-counter.scad is the letter with a hole in it, which is the
// case that found a real gap in the recogniser.
//
// The font is named explicitly so the outline is the same wherever this runs;
// the test suite ships Liberation in tests/data/ttf.
//
// EXPECT: 32 analytic surfaces available (0 cylindrical, 0 spherical, 0 toroidal, 32 Bezier)
// EXPECT: 32 Bezier patches cover 64 facets
// EXPECT: 0 unresolved
// EXPECT: written as 36 faces instead of 68
// CANONICAL: spline=32
$fn = 8;
linear_extrude(height = 2) text("S", size = 20, font = "Liberation Sans");
