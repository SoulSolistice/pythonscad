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
