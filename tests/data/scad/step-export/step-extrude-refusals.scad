// The extrusions that must *not* be declared, in one body.
//
// Every other fixture here guards a surface that should be written. This one
// guards the opposite, and it exists because that is the direction the arc
// channel fails in: a record is a claim about the mesh, and a wrong claim is
// invisible. `linear_extrude(scale = 0.5)` declared "cylinder r = 10" of a body
// that is a frustum for exactly as long as nobody asserted the report - the fit
// rejected it, so the export stayed valid and stayed faceted, and the run said
// neither "nothing was declared" nor "something was recognised". See
// step-tapered-extrude.scad, which is that case once it was understood.
//
// Four sweeps of a circle, one of them in no vocabulary this exporter writes:
//
//   twist      turns every station - but a circle turned about its own centre
//              is itself, and this one is centred on the twist axis. So the
//              wall is the r = 4 cylinder, exactly, every vertex of every
//              slice on it, and since 2026-09-25 it is declared: the sweep's
//              circle stays a circle and its centre travels along the normal.
//              Off the axis, the centre would turn with the twist and sweep a
//              helix - which is how a screw thread is built, and refused.
//   scale=[..] uneven, so the circle becomes an ellipse on the way up and the
//              sweep is a general ruled surface.
//   scale([..]) an ellipse before it is swept at all. Polygon2d::transform
//              tests the matrix for a similarity rather than trusting the
//              caller, and since 2026-09-25 turns the circle's record into the
//              ellipse's where it is not one - so the sweep is an elliptic
//              cylinder, declared.
//   v=[..]     an oblique cylinder: a real surface, exactly describable, and not
//              a CYLINDRICAL_SURFACE - its circular section is not perpendicular
//              to its axis. Since 2026-09-25 it is declared as what it is: every
//              station the same circle, swept along v, which is an elliptic
//              cylinder whose section across v is an ellipse, written as the
//              SURFACE_OF_LINEAR_EXTRUSION of it.
//
// The bodies are set apart so each is its own shell and a stray declaration
// cannot be absorbed by a neighbour. What is asserted is the *availability*
// line: three curved surfaces reach the exporter - the twisted body's cylinder,
// and the elliptic cylinders of the sheared and the oblique ones - and none for
// the uneven taper, which is stronger than nothing else being written and is the
// property that actually broke.
//
// The zeros are the assertion, and they say it more exactly than "nothing
// else was declared" would. Planes *are* declared here - a linear extrusion's
// two caps are planar whatever its twist, scale or v do, because every station
// is the profile moved within its own plane. None of that is a claim about a
// curved surface.
// EXPECT: 3 analytic surfaces available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 2 elliptic)
//
// And all three walls are written on them: every vertex of every slice of the
// twisted body is on its cylinder, however many slices the mesher takes, and
// every vertex of the other two is the ellipse moved along the sweep. Those two
// go all the way round between their caps, and are closed by a seam each.
// EXPECT: 3 declared surfaces: 3 written as a face
// EXPECT: 2 faces round their axis between two rims closed by a seam along a ruling
//
// And the plane channel, pinned since 2026-09-25. Until then the third body - an
// ellipse whose arc record the shear drops on the way in - declared one plane
// per tessellation chord: with the record gone, nothing could tell a chord from
// a straight edge, so its 32 chords were measured straight, and the count was
// the mesher's. Now circle() names its edges as chords of its arc, and the shear
// carries both the name and the record - an ellipse now - so no body declares a
// straight wall. What is left falls out of the model: the
// near caps of all four at z = 0, the far caps of the first three at z = 20, and
// the oblique one's far cap, 20 along v = [6, 0, 20], at z = 400 / sqrt(436).
// EXPECT: and 3 declared planes
// EXPECT: 3 declared planes: 3 written as a face, 0 read to identify a surface without one standing on them, 0 with no mesh left on them, 0 unexplained
//
// The census is exhaustive, and for a refusal fixture that is the whole point:
// the assertion is not the number of planes, which is the mesher's business and
// was read off a run - it is that one Cylinder, the twisted wall, two surfaces
// of extrusion, the sheared and the oblique walls, and no Cone, Sphere, Torus or
// BSplineSurface appears beside them. A recogniser that started accepting the
// uneven taper would have to put the surface somewhere, and there is nowhere
// here for it to go unnoticed. The planes are 2152 less the 32 facets of each of
// those three walls, which are one face each now. No VOLUME: line: the uneven
// taper is a faceted export, so the figure would be the mesh's own and has no
// closed form to check it against.
// ROUNDTRIP: Cylinder=1 SurfaceOfExtrusion=2 Plane=2056
$fn = 32;
linear_extrude(height = 20, twist = 90) circle(r = 4);
translate([20, 0, 0]) linear_extrude(height = 20, scale = [0.5, 1]) circle(r = 4);
translate([40, 0, 0]) linear_extrude(height = 20) scale([2, 1]) circle(r = 2);
translate([60, 0, 0]) linear_extrude(height = 20, v = [6, 0, 20]) circle(r = 4);
