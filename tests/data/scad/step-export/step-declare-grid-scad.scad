// The declaration channel, from OpenSCAD rather than from Python.
//
// step-declare-grid.py exercises the same channel through the Python API. This
// one exists because the capability ledger claims model-level declaration in
// *both* languages, and until declare_grid was registered as a builtin that was
// true of declare_cylinder, declare_sphere and declare_torus but not of the one
// case that needs it most: geometry no primitive can name.
//
// A helical ridge fused onto a wall, built the way
// examples/step_test/bayonet_container_v1-2.scad builds its hose thread -
// polyhedron() over a computed point list. The union cuts the ridge along its
// base, which is what destroys the grid regularity a fitter would otherwise
// recover from the mesh; the declaration carries the ordering across that cut.
//
// Unlike its siblings this declaration is not exact. declare_cylinder names a
// surface with a closed form; declare_grid hands over the order the points were
// swept in and the exporter fits a cubic B-spline along the sweep, ruled across
// the profile. So it is written only under step-approximate-surfaces as well as
// step-analytic-surfaces, and only where the fit stays inside the band the
// model's own tessellation already leaves open. Both of those are asserted
// below: the EXPECT line holds under the analytic flag alone, where the sweep is
// recognised and then left faceted, and the APPROX lines hold when the
// approximation flag lets it be written.
//
// EXPECT: 3 analytic surfaces available (2 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 swept grid)
// EXPECT: a declared 33x4 cubic sweep claims 178 facets whole, 67 cut across it, within its tessellation band of 0.6000
// EXPECT: 1 declared sweep left faceted - 0 wrap the surface's seam, 1 await the approximation flag
// APPROX: 1 declared sweep written as one face each, replacing 160 facets
// APPROX-NOT: await the approximation flag
//
// And what a real kernel makes of each. The analytic export leaves the sweep
// faceted, so OpenCASCADE sees the wall's outer cylinder and planes; with the
// approximation flag the sweep survives as a surface rather than as the 160
// planes it would otherwise be, which is the whole claim and is worth asserting
// rather than inferring from a face count.
// ROUNDTRIP: Cylinder=1 Plane=244
//
// The two walls go out as well, and not as facets. They are declared
// cylinders the band pass could not write - the ridge cut into one leaves a
// hole in it, and a band is two rims at a constant height with no notion of
// a hole. The trimmed-quadric path claims them by distance to the axis and
// bounds them with the mesh's own polyline, which is what the faceted faces
// around them close against. Nine cylinder faces rather than two because a
// face written on an open rectangle cannot wrap, so each wall is cut at the
// seam.
// Eight faces over the same 76 facets: the wall the ridge is fused to is cut
// into that many separate regions, and each is its own face now rather than
// an inner bound of the largest. The cylinder count in ROUNDTRIP-APPROX did not
// move for it, since it was already counting what OpenCASCADE split ours into.
// APPROX: 8 trimmed quadrics written as one face each, replacing 76 facets
// How far the fitted surface strays *between* the stations it was
// interpolated through, which is the only place it can. A cubic passes
// through its data exactly, so measuring at the data says nothing; what
// moves is the surface between, and it moves most at the ends of a sweep
// where uniform sampling is thinnest against geometry changing fastest.
// Captured rather than derived - there is no closed form for it - and
// pinned because it is what a change to the fit would move. The figure
// that matters is that it is below the band beside it.
// EXPECT: the fitted sweep passes within 0.1873 of the middle of every facet it claims, against a tessellation band of 0.6000
//
// Twenty-two planes, where the mesh's own bottom, top, two end caps and four
// pieces of bore facet would be eight. The extra fourteen are the corner
// placement, and they are derivable from the two tessellations meeting at the
// junction rather than from what the exporter reports.
//
// Why the bore facets are bent at all. cylinder(r = 20) at $fn = 32 is a
// 32-gon: its facet planes stand at the inradius 20*cos(pi/32) = 19.90369,
// while the surface the exporter writes those facets' corners on has radius
// 20. So a corner the union put in the middle of a facet - and every corner
// along the ridge's trim is one - sits 20 - 20*cos(pi/32) = 0.09631 inside the
// cylinder it is written on. That figure is this fixture's whole defect: it is
// the 9.63e-02 corner-off p95 recorded in doc/step-corner-exactness.md, to
// three digits, and nothing else in the model is off by anything like it.
// Putting such a corner on the cylinder moves it out by that 0.09631 and takes
// the facet's polygon out of its own plane, so the polygon is fanned into
// triangles, each carrying the plane its own three corners lie on.
//
// Four polygons are fanned, and which four follows from where the ridge
// crosses the bore. It climbs 540 degrees, so it crosses the facet spanning
// 45 to 56.25 degrees twice - at a = 45 and again at a = 405 - and crosses the
// mirror facet at 123.75 to 135 twice as well. What survives of a facet is the
// strip below the first crossing and the strip between the two, on each side:
// four pieces, of 6, 7, 7 and 6 corners. A fan of an n-gon is n-2 triangles, so
// 26 corners become 4 + 5 + 5 + 4 = 18 faces where there were 4, and 8 - 4 + 18
// = 22.
//
// The corner counts are the two meshes' resolutions against each other. A bore
// facet spans 360/32 = 11.25 degrees; the ridge is 33 stations over 540, one
// every 16.875 degrees, and each station span carries the one diagonal its
// quad is split along. A trim line crossing a facet therefore takes a vertex
// where it enters and where it leaves, plus one for every ridge edge crossing
// inside - a station and a diagonal where a station falls strictly within the
// facet, the diagonal alone where a station lands on the facet's own edge, as
// station 24 does at a = 405 exactly. That is 4 vertices and 3. The lower
// pieces add the facet's own two corners on the rim at z = 0 and z = 24, for
// 2 + 4 = 6; the middle pieces are bounded by two trim lines, for 4 + 3 = 7.
//
// The other four planes have nowhere to move and stay whole. The bottom and
// top are annuli whose corners are the bore's own polygon vertices, already at
// r = 20 on the cylinder exactly - 33 rather than 64 corners each, because the
// outer wall is written as a cylinder and its rim is one circle with one seam
// vertex rather than 32 chords. The ridge's two end caps lie at a = 0 and
// a = 180, both vertex lines of the 32-gon, where the facet plane meets the
// cylinder and there is no 0.09631 to make up; each is the profile trapezoid
// clipped at the bore, (1.2 + 2.88)/2 * 3 = 6.12 square units.
// ROUNDTRIP-APPROX: BSplineSurface=1 Cylinder=9 Plane=22

$fn = 32;

radius   = 20;
pitch    = 12;
turns    = 1.5;
depth    = 3;
crest    = 1.2;
root     = 4;
back     = 2;
wall     = 2.5;
height   = 24;

steps  = 32;
np     = 4;
zStart = root / 2;
span   = turns * pitch;

// (dr, dz) offsets from the wall: positive dr reaches back into it so the ridge
// stays fused, negative dr protrudes.
profile = [[back, -root / 2], [-depth, -crest / 2], [-depth, crest / 2], [back, root / 2]];

// The same points twice, in the two shapes the two consumers want: rows for the
// declaration, flat for the polyhedron. They are generated from one expression
// so they cannot disagree - and if they ever did, the exporter would simply not
// claim the facets rather than write a wrong surface.
rows = [
	for (i = [0 : steps])
	let (t = i / steps, a = 360 * turns * t, z = zStart + span * t)
	[ for (p = profile) [(radius + p[0]) * cos(a), (radius + p[0]) * sin(a), z + p[1]] ]
];

points = [for (row = rows) for (p = row) p];

faces = concat(
	[ for (i = [0 : steps - 1]) for (j = [0 : np - 1]) for (k = [0, 1])
		let (a0 = i * np + j,
		     b0 = i * np + (j + 1) % np,
		     c0 = (i + 1) * np + (j + 1) % np,
		     d0 = (i + 1) * np + j)
		k == 0 ? [a0, c0, b0] : [a0, d0, c0] ],
	[ [0, 1, 2, 3] ],
	[ [steps * np + 3, steps * np + 2, steps * np + 1, steps * np] ]
);

union() {
	difference() {
		cylinder(r = radius + wall, h = height);
		translate([0, 0, -1]) cylinder(r = radius, h = height + 2);
	}
	declare_grid(points = rows, closed = true) {
		polyhedron(points = points, faces = faces, convexity = 8);
	}
}
