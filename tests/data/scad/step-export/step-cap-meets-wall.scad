// A corner where one solid's cap meets another's wall, and a rim whose
// neighbour is fanned. Nine lines, and before the two fixes below it exported
// as three shells.
//
// It was registered WILL_FAIL when it was added, for a third defect neither of
// those fixes touched - two corners 3.6e-03 and 4.5e-03 off the analytic faces
// they bound - and ctest duly reported it as a *failure* on the day that stopped
// being true. What fixed it is the placement that puts a corner where the
// surfaces of the two analytic faces it bounds cross. See open items 3 and 18
// of doc/step-export-wip.md.
//
// Two defects meet in it, and neither is visible in the exact tier - that one is
// a clean solid. Both are the approximation pass's.
//
// **A corner is not where its two owners cross.** Provenance is per original
// solid: the first cylinder's original owns its wall, and its *cap* facets
// belong to that same original without being on it. So a vertex on the lens rim
// at z = 0 - where the second cylinder's wall meets the first one's bottom
// plane - is named by two walls, and the pair placement put it where those two
// walls cross, on the line x = 3, y = +/-9.5394. Twenty-six corners moved by up
// to 11.0267 on a part whose radius is 10, and the shell came apart along them.
// The other eighty-eight junctions here are real, sit on both their owners to
// 0.0120 - the 64-gon's own sagitta, 10(1 - cos(pi/64)) - and move at most
// 0.0192.
//
// **A rim cannot be handed to a face that will be fanned.** The cone's top rim
// at z = 28 is one CIRCLE on the cone's side; the cap across it is bent by the
// corner moves and so is fanned into triangles, and a fan writes one straight
// line per triangle side and never reads the substitution. That left the arc on
// one face and twenty-four chords on the other: twenty-five edges used by one
// face. The plane sections have guarded against this since item 11's root cause
// A was closed for them; the rims had not. Given up as an arc, the rim's chords
// are shared edges again, and the per-edge promotion then writes each of them as
// the arc it is - 62 arcs become 86, and the shell closes.
//
// The volume is derived twice, and the export has to lie between the two. Every
// cross section is the intersection of two circles - the part's own of radius
// a(z), and the second cylinder's of radius 10 at distance 6 - so the smooth
// solid is
//
//   20 * lens(10) + (1/0.75) * integral of lens(a) da from 4 to 10
//
// with lens(a) the standard two-circle overlap, which comes to 4891.296892. The
// mesh is the same integral over OpenSCAD's own inscribed 64-gons rather than
// circles, which comes to 4881.063273 and agrees with what the mesher reports to
// six decimals. An exported face is either the smooth surface or the mesh's
// chords, and chords are inscribed, so the solid cannot lie outside that
// bracket. It reads 4890.790271 with both flags, near the smooth end because
// most of this boundary is analytic, and 4885.633285 in the exact tier.
//
// VOLUME: 4886.180083 +/- 5.116809
// VOLUME-APPROX: 4886.180083 +/- 5.116809
//
// **What it reads now.** One solid, 4891.033991 with both flags and 4885.633285
// in the exact tier, worst kernel slack 0.007757, and every corner of every
// analytic face on the surface that face is written on. The approximation figure
// is 0.0054% under the smooth solid, which is the boundary that is still chords
// rather than the curve the two surfaces cross along.

$fn = 64;
intersection() {
  union() {
    cylinder(r = 10, h = 20);
    translate([0, 0, 20]) cylinder(r1 = 10, r2 = 4, h = 8);
  }
  translate([6, 0, -1]) cylinder(r = 10, h = 40);
}
