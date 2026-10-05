// A D-shaft: a cylinder with a flat milled at 0.9 of its radius.
//
// Where the flat meets the wall, the wall's normal is at 0.9 to the flat's. The
// corner placement used to tell a chord facet of a surface from a face of the
// model by that angle - a chord's normal is near the surface's - with cos(pi/6)
// in one pass and 0.5 in another, so this flat was a chord to both and a
// shallower one would have been a face to one and a chord to the other (open
// item 39, R4). Which it is was never an angle's to say. The flat is named for
// the plane its maker declared, the cube's x = 9 face, and the wall's facets
// for the cylinder: the flat is a face and the facets are chords, by their
// names, whatever the angle.
//
// Where each cap meets the flat's two edges, the cap, the flat and the cylinder
// cross: four corners where three exact things meet, placed where all three
// cross. Taken for a chord, the flat constrained nothing and none of the four
// was placed.
// APPROX: 4 corners where a declared surface meets two faces of the model are placed where all three cross
//
// The volume, bracketed as the other fixtures are. The smooth solid is the
// circle less the segment beyond x = 9, 100 acos(0.9) - 9 sqrt(19) = 5.872591,
// times 20: 6165.733493. The mesh is OpenSCAD's own 32-gon from angle 0, clipped
// at x = 9, times 20: 6130.948933. The export lies between.
// VOLUME: 6148.341213 +/- 17.392280
// VOLUME-APPROX: 6148.341213 +/- 17.392280
//
// The two facets the flat cuts are named for the cylinder and left as planes -
// cut facets are not in their surface's D0 region yet (open item 34), as on
// step-cap-meets-wall. Tolerated by name; the test fails the day it is no
// longer raised.
// KNOWN-EXPORT-ERROR: facets named by their maker for a declared surface are not written on it

$fn = 32;
difference() {
  cylinder(r = 10, h = 20);
  translate([9, -20, -1]) cube([10, 40, 22]);
}
