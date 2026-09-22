// A declared cylinder whose region is larger than the band that writes it.
//
// This fixture exists because of a regression that shipped green. On
// 2026-09-22 a region rule (R0 of doc/step-rungs.md) made the band pass give
// a region up when the surface covered more than the ring, in the hope the
// trimmed-quadric pass would write the whole of it. On lid10's *exact* tier
// that pass then refused - "the surface is exact but some of it is bounded off
// itself, and half a surface is worse than none" - and 56 facets that had been
// an exact conical face came out as planes. Cone faces 9 to 8, LINE 3466 to
// 3633, on a run that passed 56 of 56.
//
// Nothing saw it. lid10 is not a fixture, the flagship checks run with both
// feature flags, and no fixture asserted the exact tier's analytic coverage on
// a model where a region rule fires. This fixture is that assertion.
//
// **What it does and does not catch, mutation-checked against the parked
// attempt rather than assumed.** Rebuilt with
// `claude/step-region-r0-optimistic-parked` in the tree, this model exports
// identically and the test still passes: that rule does not fire here, because
// the two facets the cut clips have vertices on a *chord* of the wall, up to
// the sagitta inside the true cylinder, so at 1e-7 they are not on the declared
// surface and the region is exactly the ring. What this fixture guards is the
// *class* - a declared quadric written as an analytic face at the exact tier,
// with facets of the same surface beside it, so that anything which surrenders
// the band turns twenty facets into twenty planes and the count says so. The
// companion `step-region-pocket.scad` is the model where a region rule does
// fire, and it carries a derived volume.
//
// What it is: a 32-gon cylinder with one flat milled down its whole height.
// Everything below follows from r = 12, x = 6 and $fn = 32, and from nothing
// measured off the mesh.
//
//   the flat meets the cylinder where cos(theta) = 6/12, so at theta = +/-60
//   a facet spans 360/32 = 11.25 degrees, and 60/11.25 = 5.333 - NOT a facet
//   boundary, which is the whole point: the cut falls *inside* a facet at each
//   end
//   facets wholly removed: those between -60 and +60, which is 5 either side
//   of 0, so 10
//   facets clipped: the two the cut falls inside
//   facets left whole: 32 - 10 - 2 = 20
//
// So one partial band of 20, and two facets of the same declared cylinder that
// the band cannot hold because they are no longer quads.
// EXPECT: 1 surface recognised (0 toroidal, 0 spherical, 0 conical, 1 partial), 20 facets replaced
//
// The exact tier therefore writes six faces. One CYLINDRICAL_SURFACE over the
// band, and five planes: the bottom cap, the top cap, the flat - and the two
// clipped facets, which are **facets of the declared cylinder written as chord
// planes**. Those two are a defect and this fixture states it rather than
// hiding it: the region is not claimed whole. The day it is, this line becomes
// Plane=3 and the test fails, which is how that fix announces itself.
//
// ROUNDTRIP: is exhaustive by design - it has to account for every surface in
// the file - and that is what makes it the guard here. A change that surrenders
// the band does not merely lose a cylinder, it turns twenty facets into twenty
// planes, and the plane count is what says so.
// ROUNDTRIP: Cylinder=1 Plane=5
//
// Two circles, and they are the model's: the band's two rims, each an arc of
// r = 12 at z = 0 and z = 30. Everything else on the boundary is straight -
// the flat's edges, the rulings at either end of the arc, the cap polygons.
// EDGES: Circle=2
//
// With the approximation flag the corner placement moves 46 corners onto the
// exact cylinder, by at most 0.0582. Two of the planes beside the band would be
// bent by that move, so the writer fans each into triangles rather than letting
// a planar face stop being planar - which is a deliberate mechanism, not a loss:
// APPROX: 2 faces that keep a plane fanned into triangles to follow them, adding 2 faces
// APPROX: approximation found nothing left to fit
//
// so the plane count goes 5 to 7 and the cylinder count does not move. A rising
// plane count is not by itself a regression; this is what one looks like when it
// is the placement doing its job, and it is worth having a fixture that says so.
// ROUNDTRIP-APPROX: Cylinder=1 Plane=7
// EDGES-APPROX: Circle=2
//
// **No VOLUME, and the reason is the point of the fixture.** The exported solid
// is the exact cylinder over twenty facets and a chord plane over two, on caps
// that are 32-gon segments. That is neither the polygon's volume nor the
// circle's but a mixture, so any figure written here would have to be captured
// from the exporter or integrated off the tessellation - and a number taken
// from the faceted input is wrong however it is obtained. See
// doc/step-export-development.md, *Derived, not captured*. When the region is
// claimed whole and the wall is one exact face, a volume becomes derivable and
// belongs here.
$fn = 32;
difference() {
  cylinder(h = 30, r = 12);
  translate([6, -20, -1]) cube([20, 40, 32]);
}
