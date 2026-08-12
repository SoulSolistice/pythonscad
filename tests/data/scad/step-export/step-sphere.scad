// A sphere, which collapses into a stack of exact cones.
//
// A sphere is not a band and is not going to become one: its facets span many
// rings rather than two rims, so the strip walk cannot describe it and a
// SPHERICAL_SURFACE would need a grower of its own. But the mesh *between* two
// consecutive rings is a band - a frustum whose rims are those two circles -
// and a cone is accepted when both of its rims match a declared cylinder. So
// declaring the rings collapses the whole sphere with no new recogniser work,
// exactly as a torus does.
//
// Two things about OpenSCAD's sphere make this easier than the roadmap assumed:
//
//   - It has **no poles**. The rings sit at phi = 180(i+0.5)/num_rings, so the
//     first and last are ordinary circles closed by a flat cap rather than a
//     fan of triangles meeting at a point. Every band's outer rim is therefore
//     the complete bound of one face, which is the easiest rim case there is.
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
// 9 analytic surfaces available (8 cylindrical, 1 spherical, 0 toroidal), and
// 1 surface recognised - spherical, not conical, not partial - with 480 facets
// replaced. 3 faces out of 482: the zone and the two caps.
//
// If the report says 15 surfaces and 14 conical, the merge did not happen and
// this is the cone stack again.
$fn = 32;
sphere(r = 10);
