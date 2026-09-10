// step-band-family.scad with three more knobs, for the controls that isolate
// what makes a coupon of that family faulty in SOLIDWORKS.
//
// At WALL = 1, TAPER = 1, PITCH = 12 this is step-band-family.scad exactly, and
// "exactly" was checked rather than assumed: at FN = 32 the two exports differ
// in 187 entities of 11641, all of them an EDGE_CURVE naming one of two
// duplicate LINEs, or a permuted CARTESIAN_POINT or DIRECTION. Every referenced
// curve is geometrically identical, and the same difference appears between two
// runs of the *same* file - the exporter's entity numbering is not
// deterministic, though its geometry is.
//
// This lives here rather than in tests/data/scad/step-export/ deliberately: that
// directory is globbed into the test suite, so a file added to it is a fixture
// and needs derived expectations and a cmake configure. This is an instrument.
//
// See doc/step-export-wip.md open item 1, and the refuted claims in
// doc/step-export-development.md, for what
// each corner of the cube is for. In short:
//
//   WALL = 0   removes the boolean - and is NOT a usable control, because it
//              also changes the topology to a periodic face. Recorded because
//              it cost a session to find out.
//   TAPER = 0  removes the run-out, and makes the standalone ridge's volume
//              derivable in closed form by Pappus.
//   RUNOUT     the length of the run-out as a fraction of the sweep, which the
//              shipped model writes as the literal 0.2 in
//              f = min(1, t/0.2, (1-t)/0.2). Walking it with everything else
//              fixed is what turns "tapered or not" from a switch into a
//              series - and the taper is the only correlate of code 17 still
//              standing after the run of 2026-09-09. TAPER = 0 is the RUNOUT = 0
//              end of that walk; RUNOUT is ignored when TAPER is 0.
//   PITCH      moves the coupon across the alignment line with FN held fixed,
//              which is the intervention that separates "the tessellations do
//              not divide into one another" from "this particular FN".
FN     = 32;
TAPER  = 1;
WALL   = 1;
RUNOUT = 0.2;
// How far the run-out is allowed to taper, as a fraction of full depth. Zero -
// the shipped model - takes the crest all the way back to the bore's own radius,
// so the two surfaces are *tangent* at each end of the sweep by construction:
// f = 0 gives dr = -ridgeDepth*0 = 0, and radius + 0 is exactly the bore.
// FLOOR > 0 stops the taper short, so the ridge crosses the bore transversally
// at every station.
//
// This was built to test the theory that the tangency is what makes 131 of the
// 142 chords on the shipped coupon, and it **refuted** it: FLOOR 0 gives 142
// chords and FLOOR 0.25 gives 144. The chords do concentrate in the run-in and
// run-out fifths, but not because the ends are tangent. What actually makes
// them is the 64-iteration cap on the alternating projection that places the
// corners, which reaches only crossings above 29.98 degrees - see open item 3
// of doc/step-export-wip.md for the derivation and the cap sweep.
//
// Kept because a refuted control is still a control: it holds the tangency
// fixed while the crossing angle moves, which is what let the two be told
// apart.
FLOOR  = 0;
// PITCH is the intervention: turns = height/PITCH, and the ridge takes
// round(FN*turns) stations over turns*360 degrees, so the ridge's angular step
// equals the wall's facet step exactly when FN*turns is an integer.  At
// height = 40 that is pitch 12 with FN a multiple of three, or pitch 10 with
// any FN divisible by 4.  Changing PITCH alone moves a coupon across that line
// without touching FN, which is what separates alignment from tessellation.

ridgeDepth = 2;
crestWidth = 3;
rootWidth  = 8;
back       = 0.3;
radius     = 20;
height     = 40;
PITCH      = 12;
pitch      = PITCH;
turns      = height/pitch;

module ridge() {
	steps = max(24, round(FN*turns));
	rows = [
		for (i = [0 : steps])
		let (t = i/steps,
		     a = 360*turns*t,
		     z = rootWidth/2 + (height - rootWidth)*t,
		     f = TAPER ? max(FLOOR, min(1, t/RUNOUT, (1 - t)/RUNOUT)) : 1)
		[ for (p = [[back, -rootWidth/2], [-ridgeDepth*f, -crestWidth/2],
		            [-ridgeDepth*f, crestWidth/2], [back, rootWidth/2]])
			[(radius + p[0])*cos(a), (radius + p[0])*sin(a), z + p[1]] ]
	];
	points = [ for (row = rows) for (p = row) p ];
	np = 4;
	faces = concat(
		[ for (i = [0 : steps - 1]) for (j = [0 : np - 1]) for (k = [0, 1])
			let (a0 = i*np + j,
			     b0 = i*np + (j + 1)%np,
			     c0 = (i + 1)*np + (j + 1)%np,
			     d0 = (i + 1)*np + j)
			k == 0 ? [a0, c0, b0] : [a0, d0, c0] ],
		[ [0, 1, 2, 3] ],
		[ [steps*np + 3, steps*np + 2, steps*np + 1, steps*np] ]
	);
	declare_grid(points = rows, closed = true)
	polyhedron(points = points, faces = faces, convexity = 8);
}

if (WALL) {
	union() {
		difference() {
			cylinder(r = radius + 3, h = height, $fn = FN);
			translate([0, 0, -1]) cylinder(r = radius, h = height + 2, $fn = FN);
		}
		ridge();
	}
} else {
	ridge();
}
