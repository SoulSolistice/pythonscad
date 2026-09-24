// A rounded profile that goes through a 2D boolean after its offset, turned
// into four tori - and each torus on its declaration at machine precision.
//
// offset(r = 2) records one exact arc per rounded corner (ClipperUtils.cc,
// applyOffset) and computes the corner's points on it - and then Clipper rounds
// every coordinate to its integer grid, 2^-27 wide, so the points arrive up to
// half a cell off the arc they were computed on. Asked whether a point is on its
// declared torus at machine precision, which is all D0 admits
// (src/geometry/recognition/Exact.h), those points say no: measured 2026-09-24
// on step-rounded-profile, its four tori came out as 32 cones the moment the
// question stopped allowing a relative 1e-7. The records were right and the
// points were not.
//
// The repair is at the origin: Polygon2d::placeOnArcs puts a point back on its
// arc where the grid moved it, by the closed form. applyOffset calls it; so must
// every 2D boolean, because a boolean sends the points through the same grid
// again. step-rounded-profile covers the first. This fixture is the second: the
// notch is a difference() applied after the offset, and it rounds the corner
// points a second time.
//
// The model. step-rounded-profile's section - a 6 x 10 rectangle at x = 10,
// rounded by 2 - with a notch 4 high cut in from the outside at z = 3, turned
// about z. The notch touches no rounded corner: those span z in [-2, 0] and
// [10, 12], and it spans [3, 7].
//
// Declarations: step-rounded-profile's 18 cylinders and 2 planes, and the
// notch's own three edges - the vertical one at r = 12 a cylinder, the two
// horizontal ones the planes z = 3 and z = 7. The outer wall at r = 18 is cut in
// two by the notch and is still one cylinder.
// EXPECT: 23 analytic surfaces available (19 cylindrical, 0 spherical, 4 toroidal, 0 Bezier), and 4 declared planes
//
// Recognised: the four tori, of 8 x 32 = 256 facets each; the bore at r = 8;
// the outer wall at r = 18, now two bands, below and above the notch; and the
// notch's own wall at r = 12 - four cylindrical bands of 32. 1024 + 128.
// EXPECT: 8 surfaces recognised (4 toroidal, 0 spherical, 0 conical, 0 partial), 1152 facets replaced
// APPROX: 8 surfaces recognised (4 toroidal, 0 spherical, 0 conical, 0 partial), 1152 facets replaced
// ROUNDTRIP: Cylinder=4 Plane=4 Torus=4
// ROUNDTRIP-APPROX: Cylinder=4 Plane=4 Torus=4
//
// The volume is Pappus's, as step-rounded-profile's is: that section turns to
// 2 pi (1612 + 52 pi), and the notch removes the ring 12 <= r <= 18 over a
// height of 4, pi (18^2 - 12^2) 4 = 720 pi.
//     2 pi 1612 + 104 pi^2 - 720 pi = 8892.9868623
// VOLUME: 8892.9868623
// VOLUME-APPROX: 8892.9868623
$fn = 32;
rotate_extrude() difference() {
  offset(r = 2) translate([10, 0]) square([6, 10]);
  translate([12, 3]) square([10, 4]);
}
