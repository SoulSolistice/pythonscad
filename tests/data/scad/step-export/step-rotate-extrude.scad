// A profile of straight segments revolved about the axis.
//
// This is the fixture for `rotate_extrude` declaring its own surfaces. Half of
// that item is free: revolving a *line segment* produces exactly the band the
// recogniser already handles - a cylinder where the segment is parallel to the
// axis, a frustum where it is tilted - so there is no emission work in it at
// all, only the record. The other half - an arc in the profile and the
// TOROIDAL_SURFACE it sweeps - is step-rounded-profile.scad, and it needed the
// profile to carry its arcs. This fixture stays deliberately straight-edged, so
// that what it measures is the segment case on its own.
//
// The profile is a stepped tube, chosen so that one fixture covers all three
// kinds of edge and the rim case that only appears between two curved faces:
//
//   (8,0)-(10,0)    at one height          a flat annulus, declares nothing
//   (10,0)-(10,6)   parallel to the axis   a cylinder, r=10
//   (10,6)-(12,10)  tilted                 a frustum, rims r=10 and r=12
//   (12,10)-(12,16) parallel to the axis   a cylinder, r=12
//   (12,16)-(8,16)  at one height          a flat annulus, declares nothing
//   (8,16)-(8,0)    parallel to the axis   the bore, r=8
//
// Expected: four analytic faces - the r=8 bore, the r=10 wall, the cone between
// them, and the r=12 wall. The rim at z=6 and the rim at z=10 each bound two
// *curved* faces and nothing else, so they are written once and shared, the
// case step-chamfered-cylinder introduced. The two annuli and the two end faces
// stay planar.
//
// A twist, a helical v, or a Python profile_func would make each station a
// different profile, and none of this would be declared - which is correct, and
// is why a screw thread built that way stays faceted.
//
// Measured, and asserted by the driver: what the exporter has to report for
// the above to have happened. A silently faceted export is still a valid one,
// so validity alone cannot see a recogniser that has stopped recognising.
// EXPECT: 3 analytic surfaces available (3 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
// EXPECT: 4 surfaces recognised (0 toroidal, 0 spherical, 1 conical, 0 partial), 128 facets replaced
//
// pi times the integral of (r_out^2 - 64) dz over the profile: 216 for the
// straight r=10 run, 229 1/3 for the taper from 10 to 12, 480 for the r=12
// run, so pi*925 1/3.
// ROUNDTRIP: Cone=1 Cylinder=3 Plane=2
// VOLUME: 2907.0203975
$fn = 32;
rotate_extrude()
  polygon(points = [[8, 0], [10, 0], [10, 6], [12, 10], [12, 16], [8, 16]]);
