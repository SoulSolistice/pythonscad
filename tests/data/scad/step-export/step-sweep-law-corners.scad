// A declared sweep whose laws turn corners, fused to a bored wall.
//
// step-band-family's ridge, stated with declare_sweep instead of declare_grid:
// the profile fades in over the first fifth of the sweep and out over the last,
// each a law linear in t, so the profile law turns a corner at t = 0.2 and at
// t = 0.8. There the sweep's surface is only C0 across its whole profile - a
// knot of full multiplicity in its rendering - and a face spanning that crease
// is a face whose own surface is not G1. SOLIDWORKS's Import Diagnostics lists
// every such face as faulty ("general geometry problem"), while every check its
// API offers passes it (build/sw-kit6..13, 2026-09-26). So the sweep is written
// as one face per stretch between corners, each carrying only its own stretch
// of the surface, and the face boundaries at the corners are the mesh's stations
// there - which is why the generator below puts a station at each corner.
//
// Where a crossing curve turns a corner - here where the fading ridge meets the
// bore - it ends at a vertex: a kernel wants every edge's curve G1, and a C0
// knot inside one edge is the same fault on a curve.
//
// Known systemic defect, the same as step-band-family's and on the same ridge
// and bore: on the CGAL backend only, a planar face of the faceted tier is
// written with a bound that leaves its plane by more than the mesh's own
// coplanarity allows. doc/step-export-wip.md, open item 33. Tolerated by name;
// the test fails the day it is no longer raised.
// KNOWN-EXPORT-ERROR: assert a plane one of their own bounds is not in
//
// EXPECT: a declared sweep turning a corner along its length was split there into 3 faces
// EXPECT-NOT: across a corner of its laws
// APPROX: a declared sweep turning a corner along its length was split there into 3 faces
// APPROX-NOT: across a corner of its laws
//
// The volume is derived from the model: the bored wall, pi (23^2 - 20^2) 40, and
// the part of the ridge inside the bore, swept - a helical sweep of a meridian
// section P(t) encloses omega * integral over t of integral over P(t) of rho dA,
// the flux of the section through the meridian plane, omega = 2 pi turns. The
// section below rho = 20 is the trapezoid between the crest, depth 2f, and where
// the flanks cross the bore; f(t) is the fade. Integrated by Gauss-Legendre over
// each stretch between corners, where the integrand is smooth: 19509.346883.
FN = 32;

ridgeDepth = 2;
crestWidth = 3;
rootWidth  = 8;
back       = 0.3;
radius     = 20;
height     = 40;
pitch      = 12;
turns      = height/pitch;
lead       = 0.2;

profileAt = function (f) [[back, -rootWidth/2], [-ridgeDepth*f, -crestWidth/2],
                          [-ridgeDepth*f, crestWidth/2], [back, rootWidth/2]];
fAt = function (t) max(0, min(1, t/lead, (1 - t)/lead));

module ridge() {
	steps = max(24, round(FN*turns));
	uniform = [ for (i = [0 : steps]) i/steps ];
	// A station at each corner of the laws, among the uniform ones.
	ts = [ for (t = uniform) if (t < lead) t, lead,
	       for (t = uniform) if (t > lead && t < 1 - lead) t, 1 - lead,
	       for (t = uniform) if (t > 1 - lead) t ];
	np = 4;
	rows = [
		for (t = ts)
		let (a = 360*turns*t, z = rootWidth/2 + (height - rootWidth)*t)
		[ for (p = profileAt(fAt(t)))
			[(radius + p[0])*cos(a), (radius + p[0])*sin(a), z + p[1]] ]
	];
	n = len(ts) - 1;
	points = [ for (row = rows) for (p = row) p ];
	faces = concat(
		[ for (i = [0 : n - 1]) for (j = [0 : np - 1]) for (k = [0, 1])
			let (a0 = i*np + j,
			     b0 = i*np + (j + 1)%np,
			     c0 = (i + 1)*np + (j + 1)%np,
			     d0 = (i + 1)*np + j)
			k == 0 ? [a0, c0, b0] : [a0, d0, c0] ],
		[ [0, 1, 2, 3] ],
		[ [n*np + 3, n*np + 2, n*np + 1, n*np] ]
	);
	declare_sweep(profile = [[0, profileAt(0)], [lead, profileAt(1)], [1 - lead, profileAt(1)],
	                         [1, profileAt(0)]],
	              radius = radius, pitch = (height - rootWidth)/turns, turns = turns,
	              origin = [0, 0, rootWidth/2], stations = n + 1)
	polyhedron(points = points, faces = faces, convexity = 8);
}

union() {
	difference() {
		cylinder(r = radius + 3, h = height, $fn = FN);
		translate([0, 0, -1]) cylinder(r = radius, h = height + 2, $fn = FN);
	}
	ridge();
}
