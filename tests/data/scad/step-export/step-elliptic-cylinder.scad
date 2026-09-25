// A cylinder under an uneven scale: an elliptic cylinder, declared and written.
//
// A circular cylinder under any affine map that is not a similarity is the
// elliptic cylinder through the image of its circle - the images of its axis and
// of two perpendicular radii are the sweep and two conjugate semi-diameters of a
// section. Until 2026-09-25 the record was dropped instead (Surface::transform
// had nothing to become), and so were the planes of both caps, though a plane
// stays a plane under every affine map. Now the cylinder becomes an
// EllipticCylinderSurface (Surface::transformed), the planes move with the map,
// and the wall is written as the SURFACE_OF_LINEAR_EXTRUSION of its ELLIPSE.
//
// Declared, from the model: the elliptic cylinder of semi-axes 10 and 5, and the
// two cap planes.
// EXPECT: 1 analytic surface available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 elliptic), and 2 declared planes
//
// Every wall facet is on it at machine precision - the scale moves each vertex
// onto the image of the circle it was on - so every one is decided by the model.
// EXPECT: what decided each written facet: 32 D0, 0 D1, 0 D2, 0 D3, 0 D4
// And the rims are the planes' sections of it, ellipses, written as such.
// EXPECT: on a plane the model declared, 0 on one taken from the mesh
//
// No ROUNDTRIP: line yet. The wall is one surface and one face, but the quadric
// pass cuts a region that goes all the way round in two at a seam - an edge the
// model does not have (checklist D1, doc/step-export-wip.md) - so the file has
// two; stating either count here would be wrong or would pin the defect.
//
// Derived: pi a b h = pi * 10 * 5 * 10.
// VOLUME: 1570.7963268
$fn = 32;
scale([2, 1, 1]) cylinder(r = 5, h = 10);
