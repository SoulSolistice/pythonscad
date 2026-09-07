// A sphere is closed at its poles, and the poles come from the declaration.
//
// Sibling of step-sphere, which is the same shape at the origin with the
// default ring count. This one differs on every axis that could be hiding an
// assumption:
//
//   - it is **off the origin**, so a pole taken as (0, 0, +/-r) instead of as
//     centre + r*axis lands 13 mm away and the seam is not on the sphere;
//   - it has **$fn = 24**, so num_rings = 12 rather than 16 - eleven bands to
//     merge, not fifteen - and the outermost ring sits at 180/24 = 7.5 degrees
//     off the axis rather than 5.625;
//   - its **radius is 5**, so a pole distance carried over from the other
//     fixture's r = 10 is off by a factor of two.
//
// None of those change the answer, which is the point: the exported solid is
// the sphere the model declared, whatever the tessellation did.
//
// EXPECT: 7 analytic surfaces available (6 cylindrical, 1 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 1 surface recognised (0 toroidal, 1 spherical, 0 conical, 0 partial), 266 facets replaced
//
// num_rings is (num_fragments + 1) / 2 = 12, so the rings sit at
// phi = 180(i + 0.5)/12 and their radii pair up about the equator: 6 distinct
// circles, declared as cylinders, plus the sphere itself is 7 records. 266
// facets is 11 bands of 24 quads, which is 264, plus the two polar caps - they
// are replaced by this face too, which is the whole point of the fixture.
//
// One face and nothing else. A sphere closed on itself has no rim to bound it
// and no cap to close it: it is bounded by its seam meridian alone, used once
// in either direction, and the poles are where the two usages meet. OCCT
// inserts the zero length edge at either pole itself, which is what the two
// degenerate edges are.
// ROUNDTRIP: Sphere=1
// EDGES: Circle=1 degenerate=2
// RADII: Sphere=5
//
// The approximation flag changes nothing here either: all 266 facets are
// covered by the analytic pass, so nothing is left uncovered to fit.
// ROUNDTRIP-APPROX: Sphere=1
// EDGES-APPROX: Circle=1 degenerate=2
// VOLUME-APPROX: 523.5987756
//
// (4/3)*pi*r^3 at r = 5, exactly, and derived from nothing but that. The
// tessellation does not appear in it: this is the assertion that the export is
// the declared sphere rather than the mesh's inscribed polyhedron with its
// poles sliced off, and the two differ by 0.0035% - small, and not noise.
// VOLUME: 523.5987756
$fn = 24;
translate([3, -7, 11]) sphere(r = 5);
