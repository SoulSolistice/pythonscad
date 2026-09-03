// A cone whose base rim is cut away, which is what a declaration is for.
//
// The exporter can recognise a cone from its two rims: find a declared circle
// at either end and the wall between them is conical. That recognition is
// conditioned on both rims surviving whatever the model does next, and a
// boolean is exactly what removes one. Here the cut takes the base rim, and an
// apex is not a rim - there is no circle there to declare, only a point - so
// after the cut this solid has no pair of rims left to be recognised from.
//
// Before CylinderNode declared the cone itself, that was fatal: sixty-six
// planes, the whole taper lost, on a shape whose generator knew exactly what
// surface it was making. A ConeSurface is a radius and a slope and needs no
// second rim, so it survives the cut that the rims do not.
// EXPECT: 1 analytic surface available (0 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 conical)
//
// The exact tier still declines it. The cut edge is a plane section of the cone
// taken at an angle, so it is an ellipse, and the boundary this tier is willing
// to write runs either along the axis or around it at one height - an ellipse
// is neither. Half a surface being worse than none, it leaves the whole taper
// faceted rather than write the part it can bound.
// EXPECT: 4 trimmed quadric regions left faceted
// ROUNDTRIP: Plane=34
//
// With the approximation flag the boundary may be the mesh's own polyline, and
// then the taper is two faces - two rather than one because a face on an open
// rectangle cannot wrap, so the cone is cut at a seam. The two planes are the
// cut itself and the sliver of the original base that the tilted cut left
// behind, the tilt being what stops it taking the base off cleanly.
// APPROX: 2 trimmed quadrics written as one face each, replacing 32 facets
// ROUNDTRIP-APPROX: Cone=2 Plane=2
//
// Derived. The kept solid is the cone r=10, h=20 above the cube's top face,
// which after rotate([12,0,0]) and translate([0,0,-1.5]) is the plane through
// (0, -3sin12, 3cos12 - 1.5) with normal (0, -sin12, cos12). Integrating the
// area of each z-slice - a disc of radius 10(1 - z/20) cut by y < ycut(z) -
// over z in [0,20] gives 1661.646512.
//
// The analytic export reads 1659.706, short by 1.94, and the faceted one 1650.966,
// short by 10.7: the taper is now the true cone and only its boundary is still
// chords. The window has to admit that 1.94 and exclude a real change - boring
// the cone at r=10.1 instead moves this by 33.
// VOLUME-APPROX: 1661.646512 +/- 6.0
$fn = 32;
difference() {
	cylinder(r1 = 10, r2 = 0, h = 20);
	translate([0, 0, -1.5]) rotate([12, 0, 0]) cube([60, 60, 6], center = true);
}
