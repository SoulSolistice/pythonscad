// A bored cylinder. The top and the bottom face are annuli, so this is the
// fixture for the hole handling:
//   - the hole loop has to become an inner FACE_BOUND of the ring face, not a
//     face of its own (which shows up as a membrane spanning the bore)
//   - the ring face has to keep exactly one FACE_OUTER_BOUND
// The rounded coordinates also make this the fixture for the number
// formatting: on a locale with a comma radix a coordinate such as 7.0710678
// would be written as "7,0710678" and split into two arguments.
$fn = 16;
difference() {
  cylinder(h = 10, r = 10);
  translate([0, 0, -1]) cylinder(h = 12, r = 4);
}
