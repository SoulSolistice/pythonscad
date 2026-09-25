// A straight edge swept by an uneven scale: the bilinear patch through its four
// corners.
//
// A linear extrusion without a twist is an affine map in the sweep parameter,
// and an affine map sweeps a straight edge into the bilinear patch through the
// edge and its image at the far cap - a Bezier patch of degree (1, 1), exact. It
// is a plane exactly when the image is parallel to the edge: the scale sends the
// edge's direction u to (sx u_x, sy u_y), parallel to u when (sx - sy) u_x u_y =
// 0. Here sx = 0.5 and sy = 1, so the two edges along the axes stay planes and
// the hypotenuse, u = (-10, 10), does not. Until 2026-09-25 that wall declared
// nothing at all; see doc/step-export-wip.md, *Inferred where a declaration was
// possible*, item 7.
//
// Declared, from the model: the patch, and four planes - the two caps and the
// walls over the two edges along the axes.
// EXPECT: 1 analytic surface available (0 cylindrical, 0 spherical, 0 toroidal, 1 Bezier), and 4 declared planes
// EXPECT: 4 declared planes: 4 written as a face, 0 read to identify a surface without one standing on them, 0 with no mesh left on them, 0 unexplained
//
// And every facet on one of them: the uneven scale cuts the extrusion into
// slices, but every vertex of every slice is a station of the same affine map,
// so the hypotenuse wall's facets all lie on the patch, however many there are.
// EXPECT: 0 on a surface the approximation fitted, 0 left to the tessellation
//
// Written at D1, as every Bezier patch is today: membership in one is asked by
// projection, not in closed form - though a bilinear patch has one. See the wip.
//
// ROUNDTRIP: BSplineSurface=1 Plane=4
//
// Derived: the section at height z is the triangle with its x scaled by
// 1 - z/20, area 50 (1 - z/20), so the volume is 50 * (10 - 100/40) = 375.
// VOLUME: 375
linear_extrude(height = 10, scale = [0.5, 1]) polygon([[0, 0], [10, 0], [0, 10]]);
