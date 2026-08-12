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
// Expected at $fn = 32: num_rings = 16, so 480 quads plus two caps, 482 faces.
// 8 analytic surfaces available, 15 surfaces recognised of which 14 are conical
// and one - the equatorial band - is cylindrical, none partial, 480 facets
// replaced. 17 faces out of 482.
$fn = 32;
sphere(r = 10);
