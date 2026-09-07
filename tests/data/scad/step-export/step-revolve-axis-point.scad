// A profile with a point on the axis, which is where rotate_extrude used to
// leave degenerate facets behind.
//
// Every revolved profile point becomes one vertex per angular step. A point on
// the axis is the exception: all of its copies are the same point in space, and
// emitting them separately gives each ring of the fan a triangle with two
// coincident corners - zero area, no normal, and a shell that is closed only in
// the sense that its holes are infinitely thin. Nothing downstream can fit a
// surface through that, so a turned cone would export as one plane per facet
// plus the slivers between them. The generator welds them instead.
//
// The regression this guards is not subtle once it is measured: on a profile
// like a wine glass's the same defect took the export from 42 faces to 2616.
//
// No *curved* surface is declared here - rotate_extrude declares the rims of a
// sloped segment, and a segment that reaches the axis has only one rim - so this
// is the faceted path throughout, which is the point. It asserts the mesh, not
// the recognition.
// EXPECT: 0 analytic surfaces available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
//
// One plane, and it is the model's: of the profile triangle's three edges, the
// one from [0,0] to [10,0] lies at a single height and sweeps a flat disc, the
// one from [0,20] back to [0,0] is the axis itself and sweeps nothing, and the
// sloped one is the cone this fixture is about. A full turn has no end caps. So
// exactly one plane is declared, and the disc is the face written on it.
// EXPECT: and 1 declared plane
// EXPECT: 1 planar face written on a plane the model declared
//
// Thirty-two facets of wall and one base, and nothing else: no sliver, no
// duplicate, no second fan. Any degenerate facet at the apex shows up here as a
// face over thirty-three.
// ROUNDTRIP: Plane=33
//
// Derived, and exactly: the mesh of a turned cone is a pyramid on a regular
// 32-gon, so it is (1/3)*A*h with A = (1/2)*32*10^2*sin(2*pi/32) = 312.1445152.
// That gives 312.1445152 * 20 / 3 = 2080.9634348. A mesh carrying degenerate
// facets still measures this, which is why the face count above is the
// assertion that does the work and this one only pins the shape.
// VOLUME: 2080.963435 +/- 0.001
$fn = 32;
rotate_extrude() polygon([[0, 0], [10, 0], [0, 20]]);
