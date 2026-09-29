// The same frame roofed by the Voronoi diagram, which declares cones the
// exporter cannot yet write.
//
// The Voronoi roof raises a point by its Euclidean distance to the profile, so
// over each straight edge it is the plane step-roof-straight has - and over each
// reflex corner, where the nearest feature is the corner itself, it is the right
// cone with its apex there, axis up, slope 1. Four of them here, one at each
// corner of the hole. All thirteen are declared, and every facet of the four
// cones is on its cone at machine precision. That made it a D0 region until
// 2026-09-29; it is not one now, because roof() names none of its facets and
// corners on a surface are not a name (checklist E8).
// ENABLE: roof
// EXPECT: 4 analytic surfaces available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 4 conical), and 9 declared planes
// EXPECT: 9 declared planes: 9 written as a face
//
// And the cones are left to the tessellation. Each is bounded where it meets
// the plane beside it, and a plane at the cone's own slope cuts it in a
// parabola; the writer knows plane sections that are ellipses and nothing else,
// so the boundary stays a run of chords off the surface and the face is
// refused. The declaration is right and the writer is short. It was carried
// here as a KNOWN-EXPORT-ERROR - "facets of a D0 region are left to the
// tessellation" - until 2026-09-29, when the facets stopped being D0 ground:
// the defect is the same, the error cannot see it without a name. It comes back
// the day roof() names its facets (doc/step-export-wip.md, item 40), and goes
// the day parabolic sections are written.
//
// No ROUNDTRIP: or VOLUME: line: the corners are faceted, so both would state
// the mesh.
roof(method = "voronoi") difference() {
  square(20, center = true);
  square(6, center = true);
}
