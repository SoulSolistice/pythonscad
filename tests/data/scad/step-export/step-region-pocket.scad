// A declared cylinder with a pocket cut along its own facet boundaries, whose
// volume the model fixes in closed form - and which the exporter misses.
//
// **Registered WILL_FAIL.** It fails today by 5.4398 mm3, and that is the
// defect rather than the test being wrong. Read it beside
// `step-region-beyond-the-band.scad`, which guards the same surface's face
// count; this one states what the solid *is*.
//
// Why this shape, and every choice in it is load bearing. The pocket is cut
// with `rotate_extrude(angle = 45)`, so at $fn = 32 it takes 45/11.25 = 4
// segments and its two radial end faces are planes **through the axis**, which
// fall exactly on the wall's vertex columns at 0 and 45 degrees. That is the
// whole trick: a cut anywhere else puts its new vertices on a *chord* of the
// wall, up to the sagitta inside the true cylinder, and they are then not on
// the declared surface at all. Cutting along vertex columns leaves every new
// vertex at exactly r = 12, and the pocket's floor and ceiling at z = 10 and
// z = 20 add vertices on the *rulings*, which are also exactly on it.
//
// So the wall ends up as 26 full-height facets plus 8 short ones above and
// below the pocket, every one of them exactly on the declared cylinder, and the
// short ones in nobody's band - a band is a ring between two rims and these are
// not in that ring. The region a declaration covers is larger than the form
// that writes it. See doc/step-rungs.md, R0.
//
// **The volume is derived, and it is elementary.** The solid is a cylinder less
// an annular sector: the pocket removes 6 <= r <= 12 over 45 degrees and 10 <=
// z <= 20, and nothing else touches the body.
//
//     cylinder     pi * 12^2 * 30                    = 4320 pi
//     pocket       (45/360) * pi * (12^2 - 6^2) * 10 =  135 pi
//     solid        4320 pi - 135 pi = 4185 pi        = 13147.565255
//
// That figure is the model's arithmetic and owes nothing to the tessellation -
// which is the point, because a number taken off the faceted input would be
// wrong however carefully it were taken. It is reachable only if **both**
// declared cylinders are written whole: the wall at r = 12 and the pocket's
// inner face at r = 6. The outer r = 20 is entirely outside the body and
// contributes no face.
// VOLUME: 13147.565255
//
// And the declaration channel is pinned beside it, because a volume that is
// wrong for want of a *declaration* is a different defect from one that is
// wrong for want of a claim, and this fixture is about the second. Three
// cylinders: the wall at r = 12 and the pocket's two walls at r = 6 and r = 20.
// EXPECT: 3 analytic surfaces available (3 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
//
// Measured 2026-09-22, both tiers export **13142.125430** - short by 5.4398,
// 4.1e-4 - because part of each declared cylinder is written as chord planes
// which sit inside the surface they stand for. The exporter reads back as
// Cylinder 4, Plane 8.
//
// And it is not a hypothesis that claiming the region whole fixes it. The
// parked attempt on `claude/step-region-r0-optimistic-parked` gives up the band
// and lets the trimmed-quadric pass cover the region; on this model its exact
// tier exports **13147.565255**, the derived figure to 2.8e-8 relative, as
// Cylinder 3, Plane 6 with 74 exact circles instead of 8. That attempt was
// reverted for surrendering a closed form elsewhere - see
// doc/step-recognition-design.md, *R0, attempted and reverted* - so the number
// here stays wrong until R0 is done properly. The day it is, ctest reports this
// as a failure, which is how the fix announces itself.
$fn = 32;
difference() {
  cylinder(h = 30, r = 12);
  translate([0, 0, 10]) rotate_extrude(angle = 45) polygon([[6,0],[20,0],[20,10],[6,10]]);
}
