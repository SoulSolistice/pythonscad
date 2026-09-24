// A closed form the exact tier writes must not fall to planes at the
// approximation tier. doc/step-rungs.md: *a closed form may be exchanged, never
// surrendered*; the approximation tier is the exact tier plus what spending the
// tessellation band adds, never less.
//
// This is lid10's bayonet channel in thirty lines - open item 30. There a
// channel cut as a polyhedron at 1.5 degree pitch, declared as the cylinder it
// lies on, meets an entry slot cut from the same cylinder at the part's $fn.
// The exact tier wrote the channel's wall as one cylindrical face; the
// approximation tier admitted the slot's coarse chords to the same claim by
// the tessellation band, measured the region's worst corner against the fine
// channel's band, and declined all of it - 188 facets on lid10, 39 here.
//
// The model. A solid cylinder r = 36; from it, an entry slot - the sector 0 to
// 20 degrees of a cylinder r = 30 at $fn = 24, open from z = 8 upward - and a
// channel from 10 to 70 degrees, between r = 24 and r = 30, 40 stations of 1.5
// degrees, its roof falling by 3 over its length so that no ring describes it
// (a band's far rim at one height is exactly what it does not have). The
// channel declares both of its walls; the slot's cylinder declares its own.
//
// Declarations, one record per geometric surface: cylinders r = 36, r = 30 (the
// slot's and the channel's outer wall are one surface) and r = 24; planes
// z = 0 (the body's bottom and the prism's, one plane), z = 30, z = 8, z = 38,
// z = 35 and the prism's three sides.
// EXPECT: 3 analytic surfaces available (3 cylindrical, 0 spherical, 0 toroidal, 0 Bezier), and 8 declared planes
//
// What is on each declared cylinder, from the model alone:
//
//   r = 36   the body's 24 facets, uncut - the slot and the channel stay inside
//            r = 30 - a band between two circular rims
//   r = 30   the channel's 40 stations, less the one over 14.5 to 16 degrees:
//            the slot's $fn = 24 vertex at 15 degrees is on r = 30, outside the
//            channel's chord there (30 cos 0.75 = 29.9974), so the slot cuts
//            that facet. 39 whole facets, every corner a point the channel's
//            generator emitted on r = 30
//   r = 24   the channel's inner wall from 20 degrees on, where the slot's
//            sector stops removing everything inside r = 30: 20 degrees falls
//            inside station 6 (10 + 6.67 x 1.5), so 33 whole facets
//
// Every one of those 96 facets is made by a solid that declared the surface it
// lies on, with every corner on it - decided by the declarations alone.
// EXPECT: 3 trimmed quadrics written as one face each, replacing 72 facets
// EXPECT: what decided each written facet: 96 D0, 0 D1, 0 D2, 0 D3, 0 D4
//
// And the same 96 at the approximation tier, whatever the band adds beside
// them: the sanity test holds every export's approximation tier against its
// exact tier facet by facet (`surrendered` in tests/stepexportsanitytest.py),
// and this is the fixture that failed it.
// APPROX: 96 D0, 0 D1, 0 D2, 0 D3
//
// No VOLUME: the slot's and the channel's walls are cut against each other's
// tessellation, so the solid is a mixture of an exact cylinder and 24-gon and
// 240-gon chords, and no figure for it is derivable from the model.
//
// Known systemic defect: on the CGAL backend only, a planar face is written
// with a bound that leaves its plane by more than the mesh's own coplanarity
// allows - raised by the exporter about its own output and never read by this
// test until 2026-09-24. Not yet chased: doc/step-export-wip.md, open item
// 33. Tolerated by name; the test fails the day it is no longer raised.
// KNOWN-EXPORT-ERROR: assert a plane one of their own bounds is not in
$fn = 24;
R = 30; Ri = 24; H = 30;
z0 = 8; h = 8; ramp = 3;
a0 = 10; span = 60; steps = 40;
points = [
  for (i = [0 : steps])
  let (th = a0 + span*i/steps, hh = h - ramp*i/steps)
  for (q = [[R, 0], [Ri, 0], [Ri, hh], [R, hh]])
    [q[0]*cos(th), q[0]*sin(th), z0 + q[1]]
];
faces = concat(
  [ for (i = [0 : steps - 1]) for (j = [0 : 3]) for (k = [0, 1])
    let (a = i*4 + j, b = i*4 + (j + 1)%4, c = (i + 1)*4 + (j + 1)%4, d = (i + 1)*4 + j)
    k == 0 ? [a, c, b] : [a, d, c] ],
  [[0, 1, 2, 3]], [[steps*4 + 3, steps*4 + 2, steps*4 + 1, steps*4]]);
difference() {
  cylinder(r = R + 6, h = H);
  intersection() {
    translate([0, 0, z0]) cylinder(r = R, h = H);
    linear_extrude(H + 5) polygon([[0, 0], [2*R, 0], [2*R*cos(20), 2*R*sin(20)]]);
  }
  declare_cylinder(r = R) declare_cylinder(r = Ri)
  polyhedron(points = points, faces = faces, convexity = 8);
}
