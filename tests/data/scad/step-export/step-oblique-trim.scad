// A cylinder cut off at an angle, so its rim is an ellipse.
//
// The band model this exporter recognises cylinders with is two rims at
// constant height along the axis, and every gate in it says so: the axis comes
// from crossing two chords, which assumes chords lie in planes perpendicular to
// it; the probe fits a circle to each rim; and every wall vertex has to sit at
// one of two heights. A trim which is not perpendicular to the axis fails all
// three, and it failed them *silently* - three separate rejections with no
// reason recorded, so the report said only that one declared cylinder had gone
// out as facets, and not why.
//
// Nothing about such a cut stops the wall being a cylinder. Every vertex of it
// still lies on the true surface, exactly, because the vertices of the cut are
// where the exact plane meets the prism's exact rulings. What stops being true
// is only that the rim is at one height, and the rim is then a conic instead of
// a circle: radius r cut at a plane whose normal makes cos t with the axis
// gives an ellipse with semi-axes r/cos t and r. At 20 degrees on r=10 that is
// 10.6417777 and 10, which is what the ELLIPSE in the export reads.
//
// So the axis is taken from the rulings instead, where a cylinder states it
// exactly and with no pairing; the probe assumes a cylinder unless the far rim
// is flat enough to fit a circle and make it a cone; and the vertices off the
// flat rim have to be coplanar rather than level. A cone is still refused - cut
// one off-axis and the rim has no single radius, so the ellipse written here
// would be the wrong curve rather than an imprecise one - and so is a partial
// turn, which would need an elliptical arc and the arc machinery is written for
// circles.
//
// EXPECT: 1 analytic surface available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 1 surface recognised (0 toroidal, 0 spherical, 0 conical, 0 partial), 32 facets replaced
//
// What a kernel makes of it. Validity would pass on the faceted export too, so
// the surface census is what says the cylinder survived as one surface rather
// than as the 32 planes it replaced.
// ROUNDTRIP: Cylinder=1 Plane=2
// RADII: Cylinder=10
//
// And the rim survives as the conic it was written as, not resampled into
// a spline or split into arcs. This is the line that would notice a kernel
// quietly rebuilding the trim it was handed.
// EDGES: Circle=1 Ellipse=1 Line=1
//
// The volume, worked out from the model rather than read off the kernel: the
// exact solid is a cylinder of radius 10 whose mean height is 26 - 10/cos 20,
// because the tilt term integrates to nothing over a disc. That is
// pi*100*15.3582222752 = 4824.92783.
//
// The tolerance is the one place in this suite that needs more than the
// kernel's own 1e-6, and it is a stated property rather than slack. This trim
// goes out as a plain 3D ELLIPSE with no pcurve, so the reader re-derives the
// parameterisation on the cylinder instead of reading one, and lands about
// 2e-6 of the size away - it measures 4824.915473. The same displacement is
// measurable on OpenCASCADE's own export of the same solid: 5026.548244 with
// its pcurves, 5026.558839 with the edges repointed at the plain curve. So the
// bound is set at 0.02, which is far tighter than the tessellation band and
// far too tight to hide a wrong ellipse - halving a semi-axis moves this by
// hundreds.
// VOLUME: 4824.92783 +/- 0.02
//
// And the volume says the surface is the *right* one. The exact solid is a
// cylinder of radius 10 whose mean height is 26 - 10/cos 20 = 15.3582223, so
// pi*100*that = 4824.9153; OpenCASCADE reads 4824.915473 back. The mesh's own
// volume is 4793.98, short by the chord deficit of a 32-gon against its circle
// - 0.993558 of the area, measured 0.99356. The fit gives that back and claims
// nothing further: it is the tessellation band, not an invention.
$fn = 32;
difference() {
	cylinder(r = 10, h = 20);
	translate([0, 0, 26]) rotate([20, 0, 0]) cube([60, 60, 20], center = true);
}
