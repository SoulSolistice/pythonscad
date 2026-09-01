// A cone whose far rim is only where a boolean cut it.
//
// The recogniser could always *fit* a frustum. What it had to check the fit
// against was a rule that both rims match a declared cylinder - on the argument
// that a hull() of two coaxial cylinders declares those two and never the cone
// between them, which is true and is what step-chamfered-cylinder asserts.
//
// That rule cannot state this shape. The union declares r=10 and r=2, being the
// cone primitive's own two ends; the intersection then cuts the cone off at
// z=14, where its radius is 6 - a number no primitive ever named. One surviving
// rim is declared and the other is a trim, and asking a trim to declare itself
// is asking the wrong thing of it. The three chamfers left faceted on lid10 are
// this shape, which is why the fixture is this shape.
//
// So the cone says what it is. `declare_cone` is exact, like its cylinder,
// sphere and torus siblings and unlike `declare_grid`: it names a surface
// rather than handing over an ordering to fit, so it needs no approximation
// flag and asserts nothing the mesh does not already satisfy exactly.
//
// The cascade is the point. Declaring *one* cone recovers *two* surfaces: the
// cylinder below it was itself being refused with "the rim borders one face per
// facet", because the rim it shares with the chamfer bordered 32 separate
// chamfer facets rather than one face. Once the chamfer is a band that rim is a
// shared rim, and both halves go out together. Undeclared, this model exports
// 66 faces and not one analytic surface.
// EXPECT: 5 analytic surfaces available (4 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 conical)
// EXPECT: 2 surfaces recognised (0 toroidal, 0 spherical, 1 conical, 0 partial), 64 facets replaced
// EXPECT-NOT: left faceted
//
// What a kernel makes of it: four faces, where the faceted export has 66.
// ROUNDTRIP: Cone=1 Cylinder=1 Plane=2
// RADII: Cylinder=10
// VOLUME: 3962.5955337
//
// And the volume says it is the *right* cone, not merely a cone. The exact
// solid is a cylinder of r=10 and h=10 under a frustum of h=4 running r=10 to
// r=6, so pi*100*10 + (pi*4/3)(100 + 60 + 36) = 3141.59265 + 821.00288 =
// 3962.59553; OpenCASCADE reads back 3962.595534. The mesh measures 3937.18,
// short by the chord deficit of a 32-gon, which is the tessellation band the
// fit hands back and nothing more.
$fn = 32;
declare_cone(r1 = 10, r2 = 2, h = 8, center = [0, 0, 10])
intersection() {
	union() {
		cylinder(r = 10, h = 10);
		translate([0, 0, 10]) cylinder(r1 = 10, r2 = 2, h = 8);
	}
	cylinder(r = 100, h = 14);
}
