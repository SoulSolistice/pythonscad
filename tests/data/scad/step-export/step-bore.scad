// A bored cylinder. The top and the bottom face are annuli, so this is the
// fixture for the hole handling:
//   - the hole loop has to become an inner FACE_BOUND of the ring face, not a
//     face of its own (which shows up as a membrane spanning the bore)
//   - the ring face has to keep exactly one FACE_OUTER_BOUND
// The rounded coordinates also make this the fixture for the number
// formatting: on a locale with a comma radix a coordinate such as 7.0710678
// would be written as "7,0710678" and split into two arguments.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: 2 analytic surfaces available (2 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 2 surfaces recognised (0 toroidal, 0 spherical, 0 conical, 0 partial), 32 facets replaced
//
// Both walls are exact, so the bore takes exactly its own area away:
// pi*(10^2 - 4^2)*10 = 840*pi. A bore written at the wrong radius, or as the
// 16-gon it is in the mesh, lands somewhere else.
// ROUNDTRIP: Cylinder=2 Plane=2
// VOLUME: 2638.9378290
$fn = 16;
difference() {
  cylinder(h = 10, r = 10);
  translate([0, 0, -1]) cylinder(h = 12, r = 4);
}
