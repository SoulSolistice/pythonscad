// A roof over a square frame, by the straight skeleton: every face a declared
// plane.
//
// Both roof methods raise a point by its distance to the nearest feature of the
// profile, so the face over a straight edge is the plane through that edge
// rising at slope 1 away from it - the model's own plane, known before any
// skeleton is built. Until 2026-09-25 roof() declared nothing, and every face
// was a plane fitted to its own corners (doc/step-export-wip.md, *Inferred where
// a declaration was possible*, item 10).
// ENABLE: roof
//
// Declared, from the profile: the floor, and one plane over each of the eight
// edges - four outside, four round the hole. The straight skeleton meets the
// hole's reflex corners with planes too, so that is every face.
// EXPECT: 0 analytic surfaces available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier), and 9 declared planes
// EXPECT: 9 declared planes: 9 written as a face, 0 read to identify a surface without one standing on them, 0 with no mesh left on them, 0 unexplained
// EXPECT: 0 on a surface the approximation fitted, 0 left to the tessellation
// ROUNDTRIP: Plane=9
//
// Derived: in s = max(|x|, |y|), the frame runs from s = 3 to s = 10, the
// perimeter at s is 8s, and the roof stands at the nearer edge's distance,
// min(10 - s, s - 3). So the volume is the integral of 8s (s - 3) from 3 to 6.5
// plus that of 8s (10 - s) from 6.5 to 10: 261 1/3 + 375 2/3.
// VOLUME: 637
roof(method = "straight") difference() {
  square(20, center = true);
  square(6, center = true);
}
