// A glyph with a counter, which is a hole in the letter's cap face.
//
// The patch recogniser skipped hole loops when working out what each patch
// boundary borders, so a run along the inside of an O found one user instead of
// two and stayed UNRESOLVED - and a patch with an unresolved boundary is
// dropped. This letter kept eight of its nineteen patches and wrote eleven
// B-splines, which is most of the alphabet quietly half-collapsing: O, A, B, D,
// P, R, e, o and the rest all have one.
//
// A patch's *facets* are always outer bounds - a hole is not a sheet of anything
// - but what a patch borders may perfectly well be one. The band path had always
// used every valid loop for that lookup and the emitter already substitutes into
// a hole loop, so the patch path was simply inconsistent with both.
//
// EXPECT: 19 analytic surfaces available (0 cylindrical, 0 spherical, 0 toroidal, 19 Bezier)
// EXPECT: 19 Bezier patches cover 38 facets
// EXPECT: 0 unresolved
// EXPECT: written as 23 faces instead of 42
$fn = 8;
linear_extrude(height = 2) text("O", size = 20, font = "Liberation Sans");
