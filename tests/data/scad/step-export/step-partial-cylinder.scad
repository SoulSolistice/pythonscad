// A cylindrical wall interrupted by four ribs.
//
// This is the fixture for the partial cylinder: no arc of the wall closes on
// itself, so none of them can be written as a periodic face bounded by two
// full circles. Each arc has to come out as a CYLINDRICAL_SURFACE bounded by
// an arc at either rim and the band's two straight end edges, and the runs of
// edges those arcs replace sit inside the loops of the top and bottom face -
// which is the part that has to leave the shell watertight.
//
// The ribs also have flat quad side faces welded to the wall, which is what
// caught the band walk running off the surface it started on: it crossed a
// vertical edge into the rib, through it, and back into the next arc, and the
// ring then failed to fit anything at all. A band is grown only through facets
// which sit on the surface the seed does.
//
// The ribs run the full height on purpose. That keeps both rims of every arc
// bordering a single face (the top or the bottom disc), which is the case the
// exporter handles; a rib stopping short would leave a rim bordering one face
// per facet, and the arc is then correctly left faceted.
$fn = 32;
union() {
  cylinder(h = 20, r = 10);
  for (i = [0:3]) {
    rotate([0, 0, i * 90]) translate([8, -2, 0]) cube([4, 4, 20]);
  }
}
