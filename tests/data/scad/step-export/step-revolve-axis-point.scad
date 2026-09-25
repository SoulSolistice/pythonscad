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
// And since 2026-09-25 the cone is declared. A segment reaching the axis sweeps
// a cone whose apex is on it - a wall like any other - and rotate_extrude
// refused it, first because the fan left the shell open and then because
// nobody took the refusal out once the weld had closed it (doc/step-export-wip.md,
// *Inferred where a declaration was possible*, item 4). The sweep table declares
// it now as it declares every straight edge's surface - the conic through the
// circles its two ends sweep, which with one end on the axis is a cone with its
// apex there: a face, anchored at the rim, r = 10 at z = 0. That rim is
// declared too, as evidence, as every vertex off the axis is: the circle it
// sweeps is real whatever the band pass can make of it.
// EXPECT: 2 analytic surfaces available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 conical)
//
// One plane, and it is the model's: of the profile triangle's three edges, the
// one from [0,0] to [10,0] lies at a single height and sweeps a flat disc, the
// one from [0,20] back to [0,0] is the axis itself and sweeps nothing, and the
// sloped one is the cone this fixture is about. A full turn has no end caps. So
// exactly one plane is declared, and the disc is the face written on it.
// EXPECT: and 1 declared plane
// EXPECT: 1 planar face written on a plane the model declared
//
// Thirty-two facets of wall, every one on the cone its maker declared with all
// three corners on it - the apex included, which the weld made one vertex - so
// every one decided by the model. A degenerate facet at the apex would be a
// thirty-third, or a wall facet left to the tessellation.
// EXPECT: what decided each written facet: 32 D0, 0 D1, 0 D2, 0 D3, 0 D4
//
// The kernel reads the cone and the disc, and nothing else: one face for the
// cone, bounded by its base circle alone. It wraps the axis, and the quadric
// pass cuts a wrapping region in two - which here would put an edge the model
// does not have between two faces of one surface, since a region containing
// the apex is a disc and needs no cut. It does need a seam: the surface is
// periodic round the axis, so a face covering the whole turn is bounded in its
// own parameters only by a loop that also runs up a generator to the apex and
// back. Without it OCCT read the file anyway, by inventing the seam - which is
// the reader's repair, not the file being right (worst tolerance 1e-5 then, 0
// with it).
// EXPECT: 1 cone face closed round its apex by a seam up a generator
// ROUNDTRIP: Cone=1 Plane=1
//
// Not yet right, and recorded rather than asserted: the base circle is written
// as 32 arcs, one per facet it bounded - 32 vertices on a circle the model has
// none on. A band writes its rim as one CIRCLE; this face comes through the
// quadric pass, which writes a boundary segment by segment.
//
// Derived, and exactly: the analytic export is the cone itself, (1/3) pi r^2 h
// = pi * 100 * 20 / 3. Until the cone was declared this line stated the mesh -
// a pyramid on a regular 32-gon, 2080.9634348 - because the export was the mesh.
// VOLUME: 2094.3951024
$fn = 32;
rotate_extrude() polygon([[0, 0], [10, 0], [0, 20]]);
