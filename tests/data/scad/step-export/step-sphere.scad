// A sphere, which collapses into a stack of exact cones and then closes.
//
// A sphere is not a band and is not going to become one: its facets span many
// rings rather than two rims, so the strip walk cannot describe it and a
// SPHERICAL_SURFACE would need a grower of its own. But the mesh *between* two
// consecutive rings is a band - a frustum whose rims are those two circles -
// and a cone is accepted when both of its rims match a declared cylinder. So
// declaring the rings collapses the whole sphere with no new recogniser work,
// exactly as a torus does.
//
// Two things about OpenSCAD's sphere shape the pass:
//
//   - It has **no pole vertex**. The rings sit at phi = 180(i+0.5)/num_rings,
//     so the first and last are ordinary circles and the mesh closes each end
//     with a flat disc rather than a fan of triangles meeting at a point.
//   - The ring radii repeat in pairs about the equator, so 16 rings give 8
//     distinct records, and the two rings straddling the equator have the same
//     radius - that band is a cylinder, not a cone.
//
// The rings are then *merged*. A sphere is a stack of bands, so the zone is the
// maximal run of them joined at shared rims whose vertices all lie on the
// declared sphere - which makes one SPHERICAL_SURFACE out of all 15 without any
// grower of its own. Flooding across edges instead does not work and this
// fixture is why: the caps have every vertex on the sphere too, with the same
// sag as any ring quad, so a geometric test cannot tell them apart. Only the
// structure can, and the band pass has already worked it out.
//
// The seam is the one genuinely new thing. A periodic face is closed by a seam
// which has to lie *on* the surface; up a cylinder that is a straight ruling,
// but over a sphere it is a meridian, and the straight line between the same
// two vertices sags 0.048 mm off a radius 10 sphere - five thousand times the
// modelling tolerance. So the seam here is an arc of a great circle.
//
// Expected at $fn = 32: num_rings = 16, so 480 quads plus two caps, 482 faces.
// 9 analytic surfaces available (8 cylindrical, 1 spherical, 0 toroidal, 0 Bezier),
// 1 surface recognised - spherical, not conical, not partial - with 482 facets
// replaced: the 480 quads and the two caps the closure absorbs.
//
// If the report says 15 surfaces and 14 conical, the merge did not happen and
// this is the cone stack again.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: 9 analytic surfaces available (8 cylindrical, 1 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 1 surface recognised (0 toroidal, 1 spherical, 0 conical, 0 partial), 482 facets replaced
//
// **The caps are the tessellation's, not the model's.** This fixture used to
// assert 4188.6447513 - the sphere less two spherical caps of height
// 10*(1 - cos 5.625) - on the argument that an OpenSCAD sphere legitimately
// ends in a flat disc at either pole and the export was being faithful to the
// mesh. It was faithful to the mesh and wrong about the model: `sphere()`
// declares a whole sphere and carries no latitude bound, so a disc at the pole
// is an artefact of how many rings the tessellation chose, and writing it
// exported a solid 0.0035% short with two planar faces the model never asked
// for. What the export owes is the declared sphere.
//
// So: one face, no planes, and (4/3)*pi*r^3 exactly. A sphere closed on itself
// has no rim to bound it - it is bounded by its seam meridian alone, used once
// in either direction, with the poles where the two usages meet. The two
// degenerate edges are the zero length ones OCCT inserts at those poles itself.
// ROUNDTRIP: Sphere=1
// EDGES: Circle=1 degenerate=2
// RADII: Sphere=10
//
// And the same again with the approximation flag, which every fixture is now
// exported under. Nothing changes and that is the assertion: the analytic pass
// has already covered all 482 facets, so there is no uncovered region left for
// a fit to be attempted on, and a tier that invented one here would be fitting
// a surface to facets that already have theirs.
// ROUNDTRIP-APPROX: Sphere=1
// EDGES-APPROX: Circle=1 degenerate=2
// VOLUME-APPROX: 4188.7902048
//
// (4/3)*pi*1000, derived from the declaration and from nothing the exporter
// printed. The tessellation does not appear in it, which is the point: this is
// the assertion that separates the declared sphere from the mesh's inscribed
// polyhedron with its poles sliced off. A face census cannot make that
// separation - both read as one Sphere beside some planes - so the volume is
// the check that matters here.
// VOLUME: 4188.7902048
$fn = 32;
sphere(r = 10);
