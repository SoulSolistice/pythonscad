// The same frame roofed by the Voronoi diagram, which declares cones the
// exporter cannot yet write.
//
// The Voronoi roof raises a point by its Euclidean distance to the profile, so
// over each straight edge it is the plane step-roof-straight has - and over each
// reflex corner, where the nearest feature is the corner itself, it is the right
// cone with its apex there, axis up, slope 1. Four of them here, one at each
// corner of the hole. All thirteen are declared, and every facet of the four
// cones is on its cone at machine precision - and named for it: the roof names
// each face for the record over the site whose Voronoi cell it is, a segment's
// by its edge's name, a corner's by the names of the edges into and out of it
// (checklist E8). So the cones are a D0 region. They were one until 2026-09-29,
// stopped being one when corners on a surface stopped counting as a name, and
// are one again since 2026-09-30, when roof() began naming its faces.
// ENABLE: roof
// EXPECT: 4 analytic surfaces available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 4 conical), and 9 declared planes
// EXPECT: 9 declared planes: 9 written as a face
// EXPECT: 25 facets named by their maker for a declared surface - D0 by heritage
//
// And at the exact tier the cones are left to the tessellation. Each is bounded
// where it meets the plane beside it, and a plane at the cone's own slope cuts
// it in a parabola; the writer knows plane sections that are ellipses and
// nothing else, so the boundary stays a run of chords off the surface and the
// face is refused. The declaration is right and the writer is short, and with
// the name the export says so: D0 ground left to the tessellation, and named
// facets not written on their surface. The approximation tier writes all 25.
// Tolerated by name; the test fails the day parabolic sections are written.
// KNOWN-EXPORT-ERROR: facets of a D0 region are left to the tessellation
// KNOWN-EXPORT-ERROR: facets named by their maker for a declared surface are not written on it
//
// No ROUNDTRIP: or VOLUME: line: the corners are faceted, so both would state
// the mesh.
//
// Every EXPORT-WARNING this fixture raises, named, each with why it is known;
// any other fails the test, and so does a named one no run raises any more.
//
// The census's side of the KNOWN-EXPORT-ERROR above: a declaration with mesh
// and no face written on it is one whose facets are not written on it.
// KNOWN-EXPORT-WARNING: a declaration that is not taken is a bug report
// KNOWN-EXPORT-WARNING: have mesh held under another surface's name
//
// Unexplained: vertices of the roof's approximation tier off their own
// edges' curves by up to 2.4 - SOLIDWORKS fault code 7 (open item 43, W5).
// KNOWN-EXPORT-WARNING: not on the curve its own edge is written as
roof(method = "voronoi") difference() {
  square(20, center = true);
  square(6, center = true);
}
