// One declared sweep, at whatever tessellation the caller asks for.
//
// This is not a pass/fail fixture. It is the model behind a *family* of interop
// coupons, and it exists because of what the SOLIDWORKS run in
// doc/step-interop-validation.md could and could not establish.
//
// It could establish that every small coupon passes and that a real part failed
// - imported as a surface body rather than a solid, its faces read and then not
// sewn. It could not establish why, because every candidate mechanism was tried
// on one model at one tessellation: cutting the face up, coarsening the mesh,
// refining it. What is missing is the same geometry at a *range* of
// tessellations, so the question stops being "does this file import" and starts
// being "up to what band does this kernel sew".
//
// The band is the quantity under suspicion. A fitted surface is bounded by the
// mesh's own polyline, because the faceted faces around it have to close
// against it edge for edge; those chords sag off the surface they bound by up
// to the sagitta of a station, which is what the exporter reports as the
// tessellation band. Every other kind of face this exporter writes is bounded
// by curves that lie on it exactly. A kernel with a tighter sewing tolerance
// than that sag has nothing to sew, and healing it is expensive - which is why
// a failing file takes minutes to open where a passing one is instant.
//
// FN is the knob, overridden per coupon with -D. Everything else is fixed so
// that the band is the only thing that moves.
FN = 32;

ridgeDepth = 2;
crestWidth = 3;
rootWidth  = 8;
back       = 0.3;
radius     = 20;
height     = 40;
pitch      = 12;
turns      = height/pitch;

module ridge() {
	steps = max(24, round(FN*turns));
	rows = [
		for (i = [0 : steps])
		let (t = i/steps,
		     a = 360*turns*t,
		     z = rootWidth/2 + (height - rootWidth)*t,
		     f = max(0, min(1, t/0.2, (1 - t)/0.2)))
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

// The ridge fused to a wall, which is what makes the sweep a *trimmed* one -
// the boolean cuts it where it meets the bore, exactly as a real thread is cut.
//
// Order matters, and getting it wrong is quiet. The wall is bored *first* and
// the ridge added after, which is how a real thread is made: bore, then lay the
// thread onto the bore's wall. Boring afterwards instead cuts the ridge back
// off again - the first version of this model did that and reported `a declared
// sweep claims 0 facets whole`, having exported four faces and no error.
union() {
	difference() {
		cylinder(r = radius + 3, h = height, $fn = FN);
		translate([0, 0, -1]) cylinder(r = radius, h = height + 2, $fn = FN);
	}
	ridge();
}
