// One declared surface must be written as one kind of face.
//
// This is the smallest model that reproduces the defect open item 25's work
// found on lid10 and the bayonet: a single declared surface of a single
// original solid, written partly as a band's wall and partly as a quadric
// patch, with the division decided by nothing but the order the recognition
// passes run in. The export says so:
//
//   1 declared surface is split between different kinds of recogniser, over 32
//   facets - a cylinder, surface 0, 32 facets (28 to a band's wall, 4 to a
//   quadric face) - 2 mesh edges have a differently-held facet on each side, so
//   this is one connected region cut in two by pass order - a band's wall from
//   original 1; a quadric face from original 1
//
// which is the same signature as lid10's cone 50 (56 to a band, 6 to a quadric,
// 2 edges across the divide, one original) at half the size and in ten lines.
//
// Why this shape. The chamfer makes the cylinder's wall end at something other
// than a clean rim, and the notch removes part of that ending over a few facets
// only. A band is a ring between two rims and has no shape for what is left, so
// it claims the ring and the remainder falls to whichever pass runs next. Any
// boolean that leaves a declared surface as "a ring plus a few facets" does
// this; the notch is simply the shortest way to write one.
//
// What this fixture does NOT assert is a volume. The boolean cuts the
// *tessellation* while the surface is written *analytically*, so the solid is a
// trim following 32-gon edges bounding an exact cylindrical face, and the two
// do not compose into closed-form arithmetic - measured, a segment formula
// leaves 4.88 mm3 unexplained on a simpler variant of this. That is also why
// lid10 carries no derived volume. The assertion this model is for is
// representation-independence: writing the surface one way or the other must
// not change the solid. See doc/step-recognition-design.md.
//
// Every facet its maker named for a declared surface is D0 by that heritage,
// whole or cut, and has to end in a face written on that surface (checklist
// B6, B7, E8). A facet a boolean cut is not in its surface's D0 region yet -
// the region holds whole facets only - so nothing proposes it at the exact
// tier, and at the approximation tier a statistic over the tessellation
// refuses it (C3). Measured 2026-09-29:
//   approximation tier, 2 facets (2 cut): 2 on cone 5 (a quadric face: fewer than three facets of
//     the surface were free to claim)
//   exact tier, 4 facets (4 cut): 2 on cone 5 (claimed by nothing for it); 2 on cylinder 0
//     (claimed by nothing for it)
// Tolerated by name until cut facets join their region and no statistic
// vetoes a named facet (doc/step-export-wip.md, open item 34); the test
// fails the day it is no longer raised.
// KNOWN-EXPORT-ERROR: facets named by their maker for a declared surface are not written on it
//
// Every EXPORT-WARNING this fixture raises, named, each with why it is known;
// any other fails the test, and so does a named one no run raises any more.
//
// One declared surface written partly by one recogniser and partly by
// another, the division decided by pass order - the split surface of open
// item 26, which R0 (item 29) has not reached here.
// KNOWN-EXPORT-WARNING: split between different kinds of recogniser
// KNOWN-EXPORT-WARNING: so this is one connected region cut in two by pass order
//
// The split warning is raised although its own detail line finds the pieces
// disconnected and two faces correct - a defect of the warning, not of the
// file (open item 43, W2).
// KNOWN-EXPORT-WARNING: so the pieces are disconnected and two faces are correct
//
// A plane fitted to facets does not survive the corner placement and its
// section falls back to chords; a declared plane would (open item 11).
// KNOWN-EXPORT-WARNING: agreed before the corners were placed
$fn = 32;
difference() {
  union() {
    cylinder(h = 10, r = 20);
    translate([0, 0, 10]) cylinder(h = 3, r1 = 20, r2 = 17);
  }
  translate([14, -4, 9]) cube([12, 8, 6]);
}
