# STEP export

Notes on the STEP exporter: the defects that were fixed and why they happened,
the checks that now guard them, and what a next round of work would look like.

Written over three sessions. The first started from "the STEP output has gaps
and degenerated faces" and ended with files SolidWorks reads as solids, with
full cylinders optionally written as real cylinders. The second started from
"the exporter recognised the inner circles but not the outer ones" and ended
with partial cylinders, cones, and rims shared between two curved faces - a
chamfered body now round trips through SolidWorks as a chamfered body. The third
was meant to start item 4 of the roadmap and instead measured it, found it was
worth fourteen faces of the part it was supposed to rescue, fixed the two much
smaller things the measurement turned up instead, and lifted the recogniser out
of the exporter so it can be reused and tested on its own. Its new fixture then
found two more defects in the validator on the first run.

*What generalises* and *What is actually left in the bayonet*, below, are the
parts worth reading before starting a fourth, and *What the blocked items
actually need* is where the ones that are not finished stand. *What a model can
do about it* is for the other audience - someone writing the SCAD rather than
the exporter, which on this part turns out to be the side with more leverage.

## Orientation

The exporter lives in four files:

| File | Role |
| --- | --- |
| `src/io/export_step.cc` | entry point: mesh to `StepKernel`, then the ISO-10303-21 header |
| `src/io/StepKernel.cc/.h` | the entity model, and turning recognised surfaces into entities |
| `src/geometry/AnalyticFeatures.cc/.h` | the recogniser: which facets were modelled as a surface of revolution |
| `src/io/import_step.cc` | the reader, useful as a round-trip oracle |

`AnalyticFeatures` knows nothing about STEP and is not reached through the
exporter — see *The recogniser is not part of the exporter* below.

The mesh arrives already merged: `export_step.cc` calls `mergeTriangles()`
(`src/geometry/GeometryEvaluator.cc`), which fuses coplanar triangles into
polygons and reports, per merged face, an outward normal and a parent index for
faces that are holes. Most of the exporter's difficulty comes from consuming
that output correctly.

## What was wrong

Each of these produced a file that a CAD system rejected or silently
misinterpreted.

| Defect | Why it happened | Fix |
| --- | --- | --- |
| Coordinates rounded to 6 significant digits | default `ostream <<` precision | `659ac1d` |
| `DIRECTION('',(0,0,1))` and `1e-07` written as REALs | ISO 10303-21 requires a decimal point and an upper case exponent | `659ac1d` |
| Every face carried its own copy of each vertex | no vertex or edge sharing, so neighbouring faces were never stitched | `659ac1d` |
| Face normals inverted or zero | plane taken from the cross product of the first two edges, which points inward at a reflex corner and collapses when the first three vertices are collinear | `659ac1d` |
| Zero-area faces and zero-length edges exported | no degeneracy filtering | `659ac1d` |
| Disconnected bodies in one `CLOSED_SHELL` | a shell that can never close | `659ac1d` |
| No units, no modelling tolerance, mandatory arguments missing | importers fall back to their own defaults | `659ac1d` |
| Comma decimal separator on a German locale | `snprintf` follows `LC_NUMERIC`, and `openscad.cc:778` calls `setlocale(LC_ALL, "")` | `a8595cd` |
| A membrane spanning every bore | holes attached to the wrong face | `a15fef9` |
| `-o part.stp` rejected; `export(part, "part.stp")` silently wrote STL | suffix resolution keyed on the format identifier, not the suffix | `cf9b593` |
| 94 edges used by one face, over 61 faces of one annulus | a loop whose winding disagreed with the mesh normal and which nothing encloses was *dropped*, and dropping a face opens the shell along every edge of it | see *The dropped loop* |

Three of these are worth understanding rather than just recording.

### The membrane

`mergeTriangles()` decides which face a hole belongs to by scanning the loops of
the plane and keeping the last one that contains it:

```cpp
// src/geometry/GeometryEvaluator.cc
if (pointInPolygon(vert, indices_sub[k], indices_sub[j][0])) {
  par = k;          // keeps the LAST enclosing loop, not the innermost
}
```

Where a plane holds concentric loops, that is the wrong face. The bayonet
container has two annular ledges at z=75, 75.1..75.7 and 77.5..78.1. The r=75.1
hole is inside *both* outer loops and ended up recorded against the r=78.1 face,
which came out with two holes while the r=75.7 face was written as a plain disc
— sealing the bore.

Two things make this a good example:

- **It is invisible to topological checks.** The hole edges are still used by
  exactly two faces, once in each direction, so the shell is watertight and
  every edge-pairing test passes. Only a nesting check finds it.
- **The first diagnosis was wrong.** `pointInPolygon()` also fails outright on
  concentric circular loops — its ray runs through a vertex of the outer loop —
  and that failure mode (`par` left at -1) was fixed first, in `4e54d44`. It was
  real, but it was not what produced this membrane. The distinguishing evidence
  was that the fix printed no warning on the affected model.

The exporter no longer trusts `faceParents` for holes. It re-runs the
containment search over the coplanar loops and takes the innermost enclosing
one, falling back to what `mergeTriangles()` recorded only when its own search
finds nothing.

### The dropped loop

`examples/step_test/lid10.stp` is an analytic export of the bayonet lid, and it
is not a closed shell: **94 edges are used by exactly one face**, over 61 faces,
every one of them in the annulus r = 78.20..78.31, z = 79.09..95.00. The five
hole nesting failures in the same file are in that same annulus. Nothing else in
the part is affected.

Three measurements narrow it to one line of code, and they are worth recording
because the shape of the reasoning applies to any missing-face report:

- **All 94 are `LINE`s, and all 94 single users are `PLANE` faces.** The eight
  analytic faces and twelve circles in the file are clean, so this is not the
  analytic path.
- **They are not a duplicate vertex crack.** For each open edge the nearest other
  open edge is 0.2 mm away, not 1e-16 - so the shell is not split along a seam of
  near coincident vertices the way every filleted body was (*Never choose the same
  thing twice*, above). Faces are absent, not doubled.
- **The exporter has exactly one path that removes a face after accepting it.**
  A merged loop whose winding disagrees with `faceNormals[i]` is taken for the
  boundary of a hole; if no coplanar loop encloses it, it used to be marked
  invalid and dropped. That is `orphan_cnt`, and it printed *dropped N reversed
  loops without an enclosing face* on stdout - a line nobody read.

Dropping a face is never the right answer: its edges are then used once instead
of twice and the shell is open along all of them. And nothing enclosing it is
precisely the evidence that it is *not* a hole. It is now kept as an outer bound,
reversed so its winding agrees with the mesh normal and therefore with the
neighbours it shares edges with.

**The symptom is gone and the attribution is not confirmed.** Re-exported on a
build with the fix, `lid10.scad` validates - *28645 entities, 1137 faces, 1
shell*, no unpaired edges - but the export reports no reversed loop kept at all,
so the branch above never executed. Something else closed the shell: the run does
report *moved 1 hole to the enclosing face*, and the mesh is not the one the old
artifact came from (1137 faces against 1854, and 24 surfaces recognised against
8). Keeping the loop is right whatever the history - dropping a face can only
open a shell - but it is not established that dropping one is what opened this
one.

Two lessons, both already in this file for other reasons:

- **A rejected thing is invisible**, and this time it was a face rather than a
  band. The diagnostic existed and was printed. Nobody validates the stdout of an
  export of a real model, and no fixture contained the shape, so the only witness
  was a 2.9 MB artifact committed to `examples/step_test/`.
- **A real model finds what fixtures cannot.** Every fixture in the suite is a
  small synthetic part and all of them validate. Running `tests/validatestep.py`
  over an export of an actual user model is one command and it is not part of any
  test.

### The locale regression

The original code formatted numbers through `std::ostream`, which uses the C++
locale — still the classic one, since nothing calls `std::locale::global`. It
was accidentally immune. Replacing it with `snprintf` for precision introduced a
dependency on `LC_NUMERIC`, and every coordinate on a German system came out as
`-5,394319042217767`, which a STEP reader splits into two arguments.

Every other exporter in the tree guards this with
`setlocale(LC_NUMERIC, "C")` (`export_dxf.cc`, `export_stl.cc`, `export_svg.cc`,
`export_gcode.cc`, `export_amf.cc`). The STEP exporter instead formats through
`std::to_chars`, which is locale-independent by definition, so no global locale
switching is needed.

### The `.stp` suffix

Both the command line and `export()` in a Python script resolve the output
format by handing the *file suffix* to `fileformat::fromIdentifier()`, which
looks it up in a map keyed on the format *identifier*. STEP registers the
identifier `step` and the suffix `stp`, so nothing resolved `stp`. On the
command line that was a clear error; from a script,
`python_export_core()` fell through to its default and wrote a **binary STL
under a `.stp` name**, with only a log line.

The same mismatch already existed for STL, which is why
`identifierToInfo["stl"] = identifierToInfo["asciistl"]` is in that table. STEP
just never got its alias. Exporting from the GUI was unaffected, because there
the format comes from the dialog rather than the filename — which is why this
went unnoticed.

## The checks that guard them

`tests/validatestep.py` parses an exported file and checks one thing per defect
above:

- `CARTESIAN_POINT`/`DIRECTION` carry three numbers, `VECTOR` one. This is what
  catches a comma radix — `-5,394` reads as two numbers, so the count goes up.
  A coordinate written with a comma always splits, because a value with no
  fractional part never contains one.
- REAL literals are well formed, exponents upper case, no nan or inf.
- No `DIRECTION` of zero length.
- Every edge is used by exactly two faces, once in each direction, and every
  edge loop closes. This covers both directions of failure: used once means
  vertices or edges are not shared and the shell has gaps; used more than twice
  means an extra face is reusing them.
- A face's `PLANE` normal agrees with the winding of its outer bound.
- A hole sits *directly* inside its face's outer bound, with no other loop of
  the same plane in between. This is the check that finds the membrane.
- One `FACE_OUTER_BOUND` per face, no face outside a shell, no shell holding two
  disconnected bodies, vertices at identical coordinates shared.
- Units, modelling tolerance and product structure present.

`tests/stepexportsanitytest.py` drives it: export a fixture, validate, then
**re-export under a locale with a comma decimal separator and require the two
files to be identical**, then a third time with
`--enable=step-analytic-surfaces` so the analytic path is covered by the same
invariants as the faceted one. Seven
fixtures in `tests/data/scad/step-export/` each target one defect or one
construction — `step-cube` (sharing), `step-bore` (holes and number formatting),
`step-disjoint` (shell splitting), `step-concave` (face normals),
`step-nested-rings` (the membrane), `step-partial-cylinder` (a wall interrupted
by ribs), `step-chamfered-cylinder` (a cone, and a rim shared by two curved
faces).

The driver returns non-zero on failure and the `-expected` files are empty, in
the same shape as the existing `export-stl-sanitytest`.

### Calibration

Two things were done to establish that these checks are neither too loose nor
too strict:

- **Mutation testing.** A known-good file was broken one way at a time — face
  removed from the shell, face listed twice, plane normal inverted, zero
  `DIRECTION`, comma radix, units removed, vertex duplicated — and every check
  fired. Two real validator bugs surfaced this way: a digit-comma-digit
  heuristic that false-positived on legitimate output like `-5.5,0.`, and a
  topology check that walked all `ADVANCED_FACE` entities instead of shell
  membership, so it missed both the removed-face and duplicated-face mutations.
- **Against a professional exporter.** A file written by SolidWorks passes every
  check. Getting there exposed one false positive: the validator required a
  separate `SHAPE_REPRESENTATION`, but SolidWorks points
  `SHAPE_DEFINITION_REPRESENTATION` straight at the
  `ADVANCED_BREP_SHAPE_REPRESENTATION`, which is equally valid (`374e3af`).

Regressions are also demonstrated rather than assumed: building the exporter at
`HEAD~1` and at `659ac1d` reproduces the membrane and the comma radix
respectively, and the current checks reject both.

**A third round of calibration came for free.** `step-shared-arc` failed on its
first run, and both of the things it found were in the validator rather than the
exporter. They are worth recording because each is a rule that had been correct
until the geometry grew one new shape:

- **The winding check approximated an arc loop by its vertices.** A polygon
  through the ends of a *major* arc lies on the other side of its chord from the
  face itself, so the 270 degree bottom face read as a 90 degree top face and
  its `PLANE` normal was reported as inverted. Nothing had ever produced an arc
  of more than 180 degrees before. The loop is now walked with each arc sampled
  along the curve, which puts the polygon back on the face. `check_hole_nesting`
  had already been given the same warning - *an arc bulges away from its
  chord* - and the lesson simply had not been carried to the winding check.
- **A partial curved face was required to have exactly four edges.** A rim need
  not be one edge: where the neighbouring face is split, the rim is split with
  it. SolidWorks re-saved `step-shared-arc` with three arcs along the z=2 rim
  and therefore five-edge faces, which are perfectly valid. The rule is now two
  rims and exactly two straight ends, with any number of arcs along a rim.

Both fixes were re-checked by mutation: inverting that face's `PLANE` normal
still fails the winding check, and reversing the shared arc in one of its two
faces is caught by the edge-use rule.

## Analytic geometry

Behind the `step-analytic-surfaces` feature, a ring of facets is written as one
`ADVANCED_FACE` on a `CYLINDRICAL_SURFACE` bounded by a `CIRCLE` at either rim.
A tube of 34 faceted faces comes out as 4. With the feature off the output is
unchanged, byte for byte.

It is off by default because a malformed analytic face is worse than a correct
faceted one and only a few importers have been tried against it. Turn it on
with the checkbox in *Preferences → Features*, or with
`--enable=step-analytic-surfaces` on the command line.

Two findings shaped how this works.

**A faceted cylinder and a prism are the same mesh.** A ring of N quads is
exactly the mesh of an N-sided prism; a cube's four side faces fit a cylinder
through its corners with *zero* residual. No measurement of the geometry can
tell `cylinder($fn=6)` from a hexagonal prism, so recognition alone must never
decide. Intent has to come from the model.

**The provenance channel already existed.** `ManifoldGeometry` threads per-face
data through booleans using Manifold's `runOriginalID`, and already uses it to
carry colour. `eb2ae2a` extends the same mechanism to carry the analytic
surfaces a primitive declared. `CylinderNode::createGeometry()` records its wall;
`PolySet::transform()` and `ManifoldGeometry::binOp()` carry it; a subtraction
keeps the tool's surfaces, because a cylinder used to cut a bore leaves a
cylindrical wall of the same axis and radius. `applyOperator3DManifold()` and
`CGALUtils::applyHull3D()` carry them through a hull for the same reason: a hull
only adds material, and an exporter still has to find the wall in the mesh
before it uses the record.

So: **geometry from the mesh, intent from the primitive.** A ring is collapsed
only when the fit succeeds *and* the model declared a matching cylinder.

Two structural consequences worth knowing:

- **Circular edges and cylindrical faces are one change, not two.** The N
  segments of a bore circle are each shared with a different wall quad. Merging
  them into one `CIRCLE` without also merging the wall would orphan those quads
  and break the shell — and an arc edge cannot lie in a planar quad anyway.
- **A periodic face needs a seam.** The loop walks up a seam edge and back down
  it, the same edge used once in each direction:
  `(circle .T., seam .T., circle .F., seam .F.)`. A full circle is one
  `EDGE_CURVE` whose two ends are the same vertex.

`same_sense` is `.T.` for an outer wall and `.F.` for a bore, because a
cylindrical surface's normal points away from its axis and a bore's material
lies outside it.

### Partial cylinders

A wall which does not close is written the same way, minus the periodicity: one
`CYLINDRICAL_SURFACE` face bounded by an arc at either rim and the band's two
straight end edges — arc, line, arc, line, no seam. A band is accepted when it
has one rim vertex per facet *plus one*, the far end of the last facet.

The structural change is on the neighbour's side. A full rim collapses the whole
of the neighbouring loop into one `CIRCLE`; an arc replaces only a **run** of
that loop's edges, so the run has to be consecutive in the loop and no two bands
may claim overlapping runs. Both rims still have to border exactly one face: a
rim which borders one face per facet, as a wall standing on a chamfer does, has
no single loop to rewrite.

Two traps, both found by prototyping the pass in Python over an existing export
before writing any C++:

- **The centre has to be fitted, not averaged.** The centroid of a full rim lies
  on the axis, which is why the closed ring case can average; the centroid of an
  arc sits inside its chord. Averaging put the axis of a 54° arc of a radius 78
  wall at radius 266, and the wall was then rejected for not fitting itself.
  `fitCircleCentre()` uses Kasa's linearisation, and the residual check that
  follows it is what makes an inexact fit safe to attempt.
- **A rim can border more than one face**, and then there is nothing to rewrite.
  This is what rejects the four short bands under the bayonet's lug ramps: their
  upper rim borders ten separate ramp facets.

A facet only joins a band when it is *wholly* on the surface. Where something
flat is cut into a faceted wall the boundary facet is a trapezoid with two
corners on the circle and two inside it - the cutting plane crosses that facet's
chord, and a chord runs inside its arc. In `step-partial-cylinder` those corners
sit five thousandths inside a radius 10 wall, so two facets at each arc end stay
planar and four are collapsed. Taking them would pull the arc's end out onto the
true circle and open the shell by far more than the modelling tolerance.

Run over the bayonet base's exported mesh, the pass finds 4 such bands and
replaces 72 facets — measured by the Python prototype, on the same loops the
exporter sees, not by re-exporting the part.

`validatestep.py` gained `check_cylindrical_faces()` for the two shapes a
cylindrical face may have, and `check_hole_nesting()` now skips loops carrying
an arc — an arc bulges away from its chord, so projecting the loop as a polygon
would understate the face. `step-partial-cylinder.scad` and `step-chamfered-cylinder.scad` are the
fixtures, and the sanity driver now exports every fixture a third time with
`--enable=step-analytic-surfaces` and validates that too, so the analytic path
is covered by the same invariants as the faceted one.

### Cones, and rims shared between two bands

The lid of the bayonet container had 11 exact cylinder fits in it, 26% of its
faces, and produced not one analytic surface. Every one was rejected by the same
rule, and always with the same shape of neighbour: a rim bordering *one face per
facet* rather than a single face. The lid is a "round thin" part, so every wall
is separated from the next by a taper — the bottom chamfer, the two run-outs of
`thinRelief()` — and a taper is a band, not a face.

So a rim gained a third case beside "the complete bound of a face" and "a run
inside one": **shared with another band that is also being collapsed**, written
as one `CIRCLE` used by both faces, once in each direction. That is what lets a
wall stand on a chamfer.

It only pays off with frusta, and two things had to change for those:

- **The band walk could not find one.** It grew across edges parallel to the
  axis; a frustum's rulings are tilted, each one differently, so a chamfer was
  never even a candidate. It now walks a strip: entering a quad through one
  ruling fixes which pair of its edges are rulings, which needs no axis at all
  and is unambiguous on a cylinder too, where both pairs are parallel. The axis
  then comes out of the chords — they all lie in a plane perpendicular to it, so
  two that are not parallel fix it exactly.
- **A frustum has no analytic record.** `CylinderNode` declares cylinders, and
  the shape that actually produces a chamfer — `hull()` of two coaxial
  cylinders — declares the two cylinders, never the cone between them. A cone
  band is therefore accepted when **both of its rims match a declared
  cylinder**: the same statement of intent, made by two primitives instead of
  one. This is what the `hull()` provenance is for, and neither is any use
  without the other.

`CONICAL_SURFACE` takes the same placement as a cylinder plus a half angle,
which ISO 10303 wants in (0, pi/2) — so a cone that narrows along its axis is
written from its other end rather than with a negative angle.

Measured on the two parts, by the Python prototype over their exported meshes:
the lid goes from nothing to the chamfer and the body wall above it, and the
base from 72 facets replaced to 312. What still resists is the 8 bands whose
other rim borders a bayonet ramp, one facet at a time — a ramp is not a surface
of revolution, so there is nothing to share the rim with.

### Two defects the fixtures found

Both left a wall faceted that fits its axis to machine precision, and neither
was visible in the output - a band that is never recognised looks exactly like
one that was never there. They are worth recording because both are traps that
any future surface type will walk into.

**A band walked off its own surface.** It was grown by crossing ruling edges
into any quad, and a rib welded to a tube has quads for side faces: the walk
crossed a vertical edge into the rib, through it, and back into the next arc.
The ring then came out as one band which was never a band, fit nothing, and was
discarded before it could even be reported - `step-partial-cylinder` produced no
candidate at all. The walk now runs twice: once freely, purely to pin down which
surface the seed sits on, then again admitting only facets which lie on it.

**The seam fell on `atan2`'s branch cut.** A periodic face needs a seam along a
ruling, both ends on one radial direction. Choosing each end independently as
the rim vertex of smallest angle looks obvious and is wrong: the cut is at pi, a
polygon with an even number of facets has a vertex sitting exactly there, and
which side it lands on is decided by the sign of a coordinate which is zero to
fifteen digits. In `step-nested-rings` the two rims of the r=38 wall disagreed
on that sign, so its seam ends came out on different rulings and that one wall
was dropped while the other four were kept. Only one end is chosen now and the
other is derived from it along the ruling; where two bands share a rim, one
takes the vertex the other settled, which they must, since the CIRCLE between
them is one edge.

The lesson for the next surface: **do not choose the same thing twice from two
sets of coordinates.** Derive the second from the first.

### What SolidWorks said

It imported the file and re-saved it. The round trip confirms three things:

- It **kept** the cylinders — no re-tessellation.
- It **independently agreed** on `same_sense`: `.T.` for the r=10 wall, `.F.`
  for the r=4 bore. The trickiest flag, arrived at by a different kernel.
- It **splits** each full-turn face into two half-cylinders on re-save, with
  four-edge loops (arc, line, arc, line) and the annuli bounded by two
  half-circle arcs. That is Parasolid's internal preference, not a rejection —
  but the two-half construction is the more conservative one if a stricter
  importer ever objects.

The chamfered cylinder was round tripped separately, once cones and shared rims
landed, and it settles the two constructions nothing else had exercised:

- It **kept the cone**. Its `CONICAL_SURFACE` carries our half angle to sixteen
  digits (0.7853981633974483) and our radius to fifteen, so the cone was
  re-derived from what we wrote rather than refitted from anything.
- It **kept the rim between the cone and the cylinder** — one `CIRCLE` bounding
  two *curved* faces, with no planar face anywhere along it. Parasolid took it,
  left it circular, and split it into two half circles along with the faces it
  bounds. A chamfered body round trips as a chamfered body.
- Four faces came back as six, by the same halving as before.

`validatestep.py` passes on that file unchanged, and its half faces are the only
thing which exercises the partial branch of `check_cylindrical_faces` from a
source other than this exporter.

`step-shared-arc` was round tripped once item 0 landed, and settles the last of
the three rim cases - an arc shared by two *partial* curved faces:

- It **kept both curved faces analytic**, and carried the cone's half angle
  (0.7853981633974482790 against our 0.7853981633974483) to sixteen digits, so
  the cone was re-derived from what we wrote rather than refitted.
- It **kept the shared arc**. Our six faces came back as eight by the usual
  halving, and the 270 degree arc at z=2 came back as *three* arcs - split at
  213.75 and 225 degrees, where it split the faces. Every one of the three is
  used by a conical face and by a cylindrical face, once in each direction,
  with no planar face anywhere along it. That is the construction, re-emitted
  by a different kernel.

The three-arc rim is also the useful part: it is a shape this exporter never
writes, and it caught a check that was too strict (below).

## Running the tests on Windows

Four environment problems cost more time in this session than the exporter did.
All are recorded in `doc/testing.md`; the short version:

- The tests launch the binary from the build tree, which has no DLLs beside it.
  Only `cmake --install` gathers them. Install into a staging tree and point
  `OPENSCAD_BINARY` at it.
- `pythonscad.com` only writes to a real Windows console. From an MSYS2 shell it
  produces no output at all, which reads as a silent failure.
- `--info` reports the version `configure_file()` baked in the last time cmake
  ran, not the compiled code. Use the executable's timestamp.
- The `winconsole` target used to build into its own subdirectory, where neither
  the tests nor the wrapper's own `CreateProcess` lookup could find it
  (`6111c08`).

## A worked example: which walls got through, and why

A user exported the bayonet container's base
(`bayonet_container_v12.scad`, `_part = "base"`, shipped defaults, `$fn = 120`)
with the analytic feature on and asked why the bore came out as a cylinder
and none of the outer walls did. The file is a good measurement of where the
current restrictions actually bite, because the part is nothing but coaxial
cylinders.

It has 452 faces. One is a `CYLINDRICAL_SURFACE`; **280 of the remaining 451
planar faces are facets of a cylindrical wall**, all fitting their axis to
better than 1e-9.

| wall | r | facets | modelled by | declared | closed ring | rims bound a face |
| --- | --- | --- | --- | --- | --- | --- |
| bore | 75 | *(collapsed)* | `cylinder(r=bore)` cut in a `difference()` | yes | yes | yes |
| body | 82.75 | 120 | `roundCylinder()` → `hull()` of two cylinders | **no** | yes | **no** |
| lip | 78 | 120 | `cylinder(r=lipRadius)` in a `union()` | yes | **no** | — |
| lugs | 79.5 | 40 | `smallArc()` → `hull()` with the ramp | **no** | **no** | — |

Only the bore satisfies all three gates, which is the whole of the reported
asymmetry. It is not an inner/outer distinction — it is that the bore is the one
wall in the part that is a bare `cylinder()` primitive *and* survives to the mesh
uninterrupted.

Each of the other three fails differently, and each failure names a different
piece of future work:

- **The body wall is not declared, because `hull()` collapses provenance.**
  `applyOperator3DManifold(..., HULL)` (`manifold-applyops.cc`) collects the
  children's vertices into a point cloud and returns a fresh `ManifoldGeometry`,
  so the `CylinderSurface` that `cylinder(r=82.75)` recorded never reaches the
  exporter. This matters well beyond one model: chamfering or filleting a body
  by hulling it with a smaller copy of itself is the standard idiom, and it
  currently costs the body its analytic identity.
- **The body wall's lower rim adjoins the chamfer, which is a cone.** Its upper
  rim at z=10 *is* the complete outer bound of an annulus, so that end is fine;
  at z=2 the neighbour is 120 separate quads of a 45° frustum and no loop
  matches the rim, so `bottom_loop` is never found. Note that this rejects the
  ring **on its own**, before provenance is even considered: fixing `hull()`
  alone would not add a single face to this file.
- **The lip wall is declared and fits exactly, but the lugs cut it up.** Its 120
  facets are 8 disconnected arcs — 4 of 18 facets running the full z=10..20, 4
  of 10 facets stopping at z=14.25 where a lug ramp lands — plus 8 pentagons at
  the transitions. Each arc has one more rim vertex than it has facets, which is
  exactly what `bottom_set.size() != ring.size()` tests for.
- **The lug walls are 30° arcs and were hulled**, so they fail both gates.

The lip is the interesting one, because it is the case that is *nearly* free.
Those 4×18-facet arcs are bounded by two coaxial circular arcs and two
axis-parallel lines — the arc, line, arc, line construction SolidWorks itself
produced when it re-saved our file. No `SURFACE_CURVE` and no approximation is
needed for them; the trims are planar cuts perpendicular to or parallel with the
axis. Only the 8 pentagons touch a ramp, whose trace on the cylinder is a curve
the mesh only approximates, and those can stay planar.

That case is now handled, and the two which are not tell you what to build next:
see *Partial cylinders* below. The remaining two are measured in *What is
actually left in the bayonet*, which is also where the estimate that sent this
work at item 4 is corrected.

## What generalises

Three rounds of this work — cylinders, then partial cylinders, cones and shared
rims, then measuring what is left — have converged on a shape that is worth
stating before the next surface type is attempted, because almost none of the
difficulty was in the surfaces.

### Three gates, and they fail independently

Every analytic face has to pass all three:

| gate | question | where it lives |
| --- | --- | --- |
| geometry | do these facets fit the surface exactly? | the fit and its residual |
| intent | did the model mean this surface, or is it a prism? | the declared records |
| topology | will every face using these edges agree to the substitution? | the rim rules |

They are independent, and **the third is where the work is**. The measurement
that settled this: the bayonet lid has 11 exact cylinder fits in it, 26% of its
faces, every one of them declared — and it produced *no* analytic surface at
all. All eleven failed the rim rules. Any estimate of a new surface type which
only counts how hard the surface is will be wrong by the same margin.

### Intent can be assembled from more than one primitive

A frustum has no declaration of its own: `hull()` of two coaxial cylinders — the
standard chamfer — declares the two cylinders and never the cone between them.
Accepting a cone when **both of its rims match a declared cylinder** is the same
statement of intent, made by two primitives instead of one.

That pattern is worth reaching for before adding a declaration to a node. The
question is not "does something declare this surface" but "is there a
combination of declarations which can only mean this surface".

### Every operation that can preserve a surface has to carry the record

Transforms and booleans carried them; `hull()` did not, and a chamfered body
therefore lost its wall however exactly the wall fit. Any new operation has to
make that decision explicitly. The test is not "does the geometry survive" but
"can the record still be wrong" — and because a record is only ever a *hint*
which the exporter re-checks against the mesh, carrying one through an operation
that may have destroyed the surface is safe, while dropping one is not. Note the
asymmetry: `minkowski()` is left alone deliberately, because it changes the
radius and would make a hint that is wrong in a way the fit cannot catch.

### The substitution machinery is shared, not per surface

Collapsing N straight edges into one curve rewrites every loop which used them,
so it is only legal when everything agrees. Three cases exist now:

- the rim is the complete bound of one neighbouring face
- the rim is a consecutive **run** of edges inside one such loop
- the rim is **shared with another band** which is being collapsed too

A new surface type should extend this list rather than invent its own — and
should expect to need to. Cones were blocked on the third case, not on
`CONICAL_SURFACE`.

### A facet belongs to a band only if it lies *wholly* on the surface

Where anything flat is cut into a faceted wall, the boundary facet is a
trapezoid with two corners on the circle and two inside it: the cutting plane
crosses that facet's chord, and a chord runs inside the arc it subtends. On a
radius 10 wall at $fn=32 those corners sit five thousandths inside. They have to
stay planar; taking them would pull the face's boundary out onto the true circle
and open the shell by far more than the modelling tolerance.

This is a hard ceiling on what recognition can ever achieve from a faceted mesh,
and it is the fact behind item 4 below: **the trim curve is not in the mesh**.
On the bayonet that turns out to bind on fourteen faces, which is why item 4 is
now last rather than first. Where the trim happens to be a plane perpendicular
to the axis or parallel with it, the bound is an arc and a line and both are
exact. Everywhere else, only the generator knows the curve.

### Four traps that will recur

- **Never choose the same thing twice from two sets of coordinates.** Both ends
  of a seam were picked independently as the rim vertex of smallest angle;
  `atan2` has its branch cut at pi, an even sided polygon has a vertex exactly
  there, and the sign of a coordinate which is zero to fifteen digits decided
  which side it fell on. One wall of five was dropped. Derive the second from
  the first.

  The same trap, found again in `FilletNode` while probing it for declarations,
  and this one had been shipping: the rail where an edge strip meets a corner
  patch is computed by the strip as `p + e_fa - 2f*e_fa + f^2*(e_fa + e_fb)` and
  by the corner as `center + mat * Bezier(...)`. Same point, different
  arithmetic, one unit in the last place apart - `-4.471074380165288` against
  `-4.471074380165289`. `PolySetBuilder`'s vertex lookup is exact, so the mesh
  got two vertices where it needed one. A filleted cube exported with **48
  quadrilateral holes**, one at every place a strip end meets a corner, and
  SolidWorks imported it as loose surfaces instead of a solid. The crack is in
  the `PolySet`, so it was never a STEP problem: every export of a filleted body
  carried it.
- **Grow a region by the surface, not by adjacency.** A band grown by crossing
  edges into any quad walks straight through a rib welded to the wall and out
  the far side. Fit the surface from the seed's neighbourhood first, then admit
  only facets which lie on it.
- **A merge that keeps one of its inputs has to read them all first.** Merging a
  run of bands into a spherical zone writes the result into one of the run - it
  has to, or the others' facets would be handed back - so the reference being
  written through can alias an end that has still to be read. Writing the new
  base before computing the height from the old one silently turned a 19.9 mm
  zone into a 0.38 mm one, and only when the run happened to be seeded from its
  top end. Take a copy of both ends, then assign.
- **A walk that always goes the same way only works on a monotone stack.** The
  sphere merge steps from band to band by each one's *top* rim, which is sound
  because a sphere's bands stack along the axis and every rim is one band's top
  and the next one's bottom. A torus's profile turns around at its widest and
  narrowest points, so the two bands meeting there meet top to top; the walk
  turned round, came back to its seed, and every torus fell out as the stack of
  32 exact cones it also is. Leave a band by the rim you did not enter it by.

### A rejected surface is invisible

A wall that was never recognised looks exactly like a wall that was never there.
Both of this round's defects sat unnoticed in output that validated cleanly, and
both were found within one run of making the exporter print the rule that
rejected each band. Ship the diagnostic with the feature, not after it.

### The recogniser is not part of the exporter

The recognition code and the entity-writing code had grown up interleaved, in
one 530-line stretch of `StepKernel::build_tri_body`. Nothing in that stretch
was about STEP. It answers a question any format with analytic surfaces has to
ask — *which runs of facets were modelled as a surface of revolution, and will
every face sharing their edges accept the substitution* — and the answer is
plain data: bands, their resolved rims, and the rule that rejected each band
that was left faceted.

It now lives in `src/geometry/AnalyticFeatures`, which takes the merged loops
and the declared surfaces and returns that data. `StepKernel` keeps only the
half that writes `CYLINDRICAL_SURFACE` and `CIRCLE`. A FreeCAD or IGES writer
can call the recogniser without taking the STEP entity model with it, and the
`Result` it returns is the natural place to hang a unit test, which the old
shape had no seam for.

Two things about the split are worth keeping:

- **The report is data, not `printf`.** The recogniser returns the lines and the
  caller prints them. *A rejected surface is invisible* above is the reason
  those lines exist at all, and a second consumer must not have to reinvent
  them or, worse, swallow them.
- **`Mesh` is the loops, not a `PolySet`.** The recogniser wants cleaned,
  canonicalised loops with their hole flags and normals — the form the exporter
  had already built for its own reasons. Handing it a geometry object instead
  would drag the whole merge-and-reparent pass in behind it, and that pass is
  genuinely exporter business.

### They are all one surface

Cylinders, cones, spheres, tori and everything `rotate_extrude` can make are the
same object: a profile revolved about an axis. The band machinery — strip walk,
axis from the chords, per rim circle fit, rim substitution, seam along a ruling —
is most of that recogniser already. What remains per type is which STEP surface
to write and how to bound the degenerate cases. Treating the next item as "add a
recogniser for X" rather than "extend the profile of revolution" is the way to
end up with four of them.

## What is actually left in the bayonet

Item 4 below was carrying the note "the real prize", and the obvious next move
was to start it. Measuring first says otherwise, and the measurement is cheap
enough that there was no reason not to:
`examples/step_test/bayonet_container_v1-2.stp` is a faceted export of the whole
model at `$fn = 60`, and `scripts/step-analytic-probe.py` over it gives exactly
the loops the recogniser sees. Every number below is one run of that script -
`surfaces` for the ceiling, `bands` for what the rules do with it.

**1693 faces. 664 of them — 39% — are facets of one of 14 surfaces of
revolution.** That is the ceiling: no recogniser of any sophistication can
collapse a face that does not lie on such a surface.

Splitting the rest by whether the surface is there and only the *trim* is not
planar — every vertex of the face satisfying one line `r = a + b*z` — puts a
number on each of the two remaining items, and they are not close:

| | faces | |
| --- | --- | --- |
| facet of a surface of revolution | 664 | 39.4% |
| on no surface of revolution at all | 999 | 59.3% |
| on a cylinder or cone, trim not planar | 14 | 0.8% |
| planar, perpendicular to the axis | 8 | 0.5% |

The 14 are the whole of item 4 in this part. The 999 are item 5, and they are
what actually dominates it.

| | facets | faces after collapsing |
| --- | --- | --- |
| on a surface of revolution | 664 | — |
| collapsed by the shipped rules | 568 | 13 |
| collapsed after item 0 (both changes below) | 628 | 25 |
| still faceted after both | 36 | — |

So the remaining headroom in the whole recognisable part of this model is 96
facets, and 60 of them come from two small changes that have nothing to do with
trim curves. The last 36 are one band whose rim borders 36 separate pentagons of
the thread relief; there is no surface on the other side to share an edge with,
and there will not be one until the thread itself is analytic.

**Item 4's literal remainder is 14 faces** — eleven on a cylinder, three on a
cone, spread over four surfaces of which the largest carries eight facets. The
planar-trimmed subset that got split off and done was not a subset; it was very
nearly the whole of it.

One caveat on all of these numbers: the prototype has no provenance, so it
measures the geometry and topology gates only. The intent gate can lower them,
never raise them, which is what makes them a ceiling.

### The two changes worth making

Both were found by the same run, both are small, and both have landed - this
section is kept as the record of how they were found and what they were worth,
because the shape of the finding generalises further than the fix does.

**The axis is derived from a walk that has not been constrained yet.** The band
walk runs twice — once freely to pin down the surface, then again admitting only
facets on it — but the axis is still taken from the chords of the *first* walk.
Where that walk runs off the surface it drags foreign chords in, the
perpendicularity test rejects them, and the candidate is thrown away before the
constrained walk ever gets to clean it up. This is the trap recorded under *Grow
a region by the surface, not by adjacency*, surviving in the one place the
earlier fix did not reach.

It costs the bayonet four lug chamfers and the four walls above them. Replaying
the walk on the lug's chamfer facet at r=78..79.5: the free walk crosses the end
of the 5-quad strip into the lug's side face, turns through 90° there because
entering a quad by a different edge redefines which pair are rulings, and comes
back with 7 facets spanning z=89.25..95 and 4 of 14 chords not perpendicular to
anything. The fix is to take the axis from the seed and the facets directly
joined to it across a ruling — two chord directions is all it needs — and then
re-check perpendicularity against the final, confined wall set. With that alone
the recogniser finds 26 bands covering all 664 facets, up from 18 covering 624.

**A rim can be shared between two *partial* bands.** `OTHER_BAND` requires both
bands to cover the full turn, deliberately: a shared rim covered by several
partial bands has to be split into arcs on both sides at once. But the bayonet's
lugs are exactly that shape — a 30° wall at r=78 standing on a 30° chamfer
running out to r=79.5, standing on a 30° wall at r=79.5 — and each of those
joints is one circular arc wanted by two curved faces. It is the same
substitution the closed case already makes, with an arc in place of the circle,
and it is safe under the same condition, strengthened: the two bands must meet
along the *whole* of the rim, so neither has rim edges the other lacks.

Together the two take the bayonet from 568 facets replaced to 628, and from 13
analytic faces to 25.

Neither is a new surface type, and neither needs a curve the mesh does not
already contain exactly.

## How to continue

Ordered by value per unit of work. Each is independently shippable behind the
same flag, and each extends `validatestep.py` with its own surface checks. Cost
each of them against all three gates, not just the surface.

The numbering is the one the previous round used, so that the cross-references
elsewhere in this file still resolve; only the order changed. Item 4 moved from
first to last, and item 5 is new and did not previously exist as work at all —
it is the largest thing in the bayonet and the only item that unblocks another.

Items 0, 0b and the first half of 1 are done. `TOROIDAL_SURFACE` was going to
be next and is not: a torus already collapses 1024 facets to 32 exact cones, so
the surface itself is worth a factor of 32 on the face count and costs a 2D
provenance channel that does not exist - see *A torus is already a stack of
exact cones*. Spheres (3) went the same way as the torus, for the same reason
and for five lines. What is left is the grid grower `SPHERICAL_SURFACE` and
`TOROIDAL_SURFACE` share - and *only* that, since both are bounded by circles
and need none of item 2's curve generalisation. 2, 4 and 5 are all blocked on
declaration channels that do not exist, which is one problem wearing three
hats.

### 0. The two changes above - done and verified

`step-shared-arc.scad` is the fixture for the second; the first needs none,
because `step-partial-cylinder` already contains the shape that provoked it and
simply recognises more of it now. On the fixture the exporter reports *2
surfaces recognised (0 toroidal, 0 spherical, 1 conical, 2 partial), 48 facets
replaced*, taking the part
from 52 faces to 6, and SolidWorks round trips the shared arc - see *What
SolidWorks said*.

One adjacent case was deliberately left: the *probe fit* that seeds the
constrained walk still takes its four vertices from the first three facets of
the unconstrained walk, which is the same weakness the axis had. Substituting
the seed's own neighbourhood there changed nothing measurable on the bayonet,
so it was not made blind - but it is the obvious first suspect if a wall that
plainly fits is ever missing again.

### 0b. What the primitives were not declaring - done

Reading `CylinderNode::createGeometry()` after item 0 turned up two shapes which
could never be analytic, for reasons that had stopped being true:

```cpp
if (!cone && !inverted_cone && r1 == r2 && this->angle == 360) {   // was
```

- **A frustum declared nothing at all**, because `r1 == r2` excludes it. So
  `cylinder(r1=8, r2=12)` exported as facets while `hull()` of two coaxial
  cylinders - the workaround for the same shape - exported as a
  `CONICAL_SURFACE`. The idiomatic construction lost to the workaround. The
  primitive now declares the circle at each of its rims, which is the same
  statement of intent the hull makes, so the two leave identical provenance.
  Nothing in the recogniser changed, and `step-cone-primitive` comes out as
  *1 surface recognised (0 toroidal, 0 spherical, 1 conical, 0 partial), 32 facets
  replaced* - 3 faces.
- **A pie slice declared nothing**, on the reasoning that its flat sides are not
  part of the cylinder. True, and the wrong place to act on it: those sides run
  through the axis, so they fit no cylinder and are discarded on the fit
  anyway. The exclusion also predates partial cylinders - when it was written a
  band had to close on itself to be written at all.

An apex is still left undeclared: there is no circle there to collapse, and a
radius of zero would match every other radius of zero.

`step-cone-primitive.scad` and `step-pie-slice.scad` are the fixtures. The
lesson is worth more than the fix: **both were invisible in the same way a
rejected band is**, because a shape that is never declared looks exactly like a
shape that does not fit. The recogniser's report says why it dropped a band; it
cannot say anything about a band that was never a candidate. When coverage looks
lower than it should, check what was declared before checking what was matched.

### The pie slice was not a pie slice

Writing that fixture found a third defect, this one nothing to do with STEP.
`cylinder(angle = 90)` in a `.scad` file produced a **whole cylinder**: `angle`
was in the parameter list `Parameters::parse()` accepts and was never assigned
to the node, so it parsed and was discarded. `CylinderNode` has the field,
`createGeometry()` honours it, `toString()` prints it, and the Python binding in
`py_primitives.cc` sets it - every part of the feature existed except the one
line in the SCAD front end.

The fixture passed anyway, because a whole cylinder exports perfectly well. What
gave it away was the exporter's own report saying **0 partial** where a 90
degree wall must be partial. That is worth recording as a method note in itself:

> **Predict the diagnostic, not just the exit status.** A fixture that only
> asserts "valid output" cannot tell a feature working from a feature absent -
> both produce a valid file. The counts the exporter prints are cheap to predict
> before the run and they discriminate; the fixture now states the expected
> figure in its own comment, so the next reader can check it in one line.

The parse is fixed, and `step-pie-slice` guards it from the STEP side: if
`angle` is ever dropped again the band comes out closed, and the comment says
what the report has to read. It now says *1 surface recognised (0 toroidal, 0
spherical, 0 conical, 1 partial), 31 facets replaced*, taking the slice from 35
faces to 5. Between the run that found the defect and the run that confirmed the
fix, that one line is
the **only** thing that changed in the whole suite - every other fixture's
report and face count is identical, which is the evidence that the parse fix
touched nothing else.

### 1. `rotate_extrude` declaring its own surfaces - half done

`RotateExtrudeNode` keeps its `profile_func`, so it can recognise that its own
profile is a circle (torus) or a line segment (cylinder or cone) and declare the
surface directly. This is provenance work rather than geometry work, and it
widens coverage well beyond the `cylinder()` primitive.

**The line segment half has landed.** A `rotate_extrude` of a straight profile
edge produces exactly the band the recogniser already handles - a cylinder where
the edge is parallel to the axis, a frustum where it is tilted - so there was no
emission work in it, only the record. `declareSurfacesOfRevolution()` in
`rotate_extrude.cc` walks the profile of the first station and declares one
circle per radius; `step-rotate-extrude.scad` is the fixture, a stepped tube
whose six profile edges cover all three kinds (flat annulus, cylinder, frustum)
and whose two internal rims each bound two curved faces and nothing else. It
reports *3 analytic surfaces available, 4 surfaces recognised (0 toroidal, 0
spherical, 1 conical, 0 partial), 128 facets replaced* - three records for six
edges, four faces from them, and the whole tube down to 6 faces.

Two things it deliberately does not do:

- **It reads the profile after `alterprofile()`**, not the outline as drawn, so
  the origin and offset are already applied. A record has to describe where the
  wall ended up.
- **It declares nothing when the stations differ.** A twist, a helical `v` or a
  Python `profile_func` gives every station a different profile, and the result
  is not a surface of revolution at all. That is not a corner case: it is how a
  screw thread is built, and it is the same fact that sinks item 5.

### A torus is a stack of exact cones, and then one surface

`TOROIDAL_SURFACE` was next on this list until it was measured, the measurement
moved it a long way down, and it was then done anyway - by which time it was
much smaller than it had looked. Both halves of that are worth keeping.

`rotate_extrude()` meshes a circular profile as a grid: one ring per profile
edge, every quad with its four corners at two radii and two heights. That is
exactly a frustum band, and now that a straight profile edge declares the circle
at each of its ends, **every ring of a torus has both of its rims declared**. A
torus therefore already collapses - into a stack of cones, each passing through
the mesh vertices with zero residual, each sharing its rim circle with the ring
above and below.

At `$fn = 32` that is **1024 facets down to 32 faces**, and the profile's 32
radii deduplicate to 17 records because they repeat in pairs about the widest
and narrowest points. A real `TOROIDAL_SURFACE` would be 1 face. So the item is
worth a further factor of 32 on the face count, against the factor of 32 already
collected for nothing - and it costs three hard pieces to get:

- **The declaration has nowhere to live.** `Outline2d` carries vertices, a
  winding flag and a colour, and nothing else; `circle()` leaves no record that
  it was a circle. `ArcCurve` exists but it is a 3D channel hanging off
  `PolySet`, not off `Polygon2d`. Declaring a torus honestly means adding a
  curve channel to 2D geometry and carrying it through `translate`, `offset` and
  the Clipper booleans - a bigger change than all the exporter work here, and
  the same shape of problem as item 5. Fitting the profile instead is not a way
  out: a 32-gon profile revolved gives *exactly* the mesh a circle profile
  revolved gives, so it is the cylinder-and-prism ambiguity again, one dimension
  down.
- **A torus is not a band.** Its facets span many rings rather than two rims, so
  the strip walk does not describe it. It needs a grid grower, which it shares
  with spheres (item 3).
- **A full torus face is bounded by two seams**, not by rims at all. This was
  the part expected to need a new loop shape and it does not: two seams give
  *four* edges, two distinct ones each used once in either direction, which is
  precisely the loop a periodic cylinder already uses. The second seam is a
  second edge, not a different kind of bound.

The practical case for it is quality rather than count: a fillet that comes out
as 32 conical bands has tangent discontinuities where the true surface is
smooth, which matters to whatever the importing CAD system does next. That is a
real argument, and it is a different argument from the one that put the item
high on this list.

**All three are now done**, and the first of them is what made the other two
cheap:

- `rotate_extrude` declares a `TorusSurface` by reading its **child node** -
  a chain of pure translations ending at a whole `circle()`. Not the geometry,
  which by then is a polygon. It is deliberately narrow, because anything it
  does not recognise stays a stack of cones, which is already exact; a
  `difference()` in the way ends the match, and that is the known limit of
  reading the tree.
- The merge is the sphere's, with the run allowed to close on itself.
- The face needs nothing from the mesh but **one vertex** - the corner where
  the two seams cross. Both circles, their centres and their radii come out of
  the record, so there is no grid to recover.

`step-torus.scad` is the fixture, and it earns its place for a second reason:
it is the first thing to chain more than three closed bands through shared
rims - the bayonet's longest chain is a wall on a chamfer on a wall. The seam
pass is a single pass, each band taking whichever rim another has already
settled and deriving the other along a ruling, which stays consistent only while
the chain does not fork. Thirty-two rings closing back on themselves is the
first real test of that, and a fork would show as `EDGE_LOOP does not close`.

### 2. Fillet Bézier patches

`FilletNode.cc` builds surfaces from explicit quadratic Bézier control points
(`Bezier(t, a, b, c)`, `bezier_patch()`) and then tessellates them. The control
points are known at generation time, so a fillet can declare a
`B_SPLINE_SURFACE_WITH_KNOTS` of degree 2 directly, with **no fitting at all**.

This is the general principle for splines: *do not fit them*. There is no unique
B-spline underlying a triangle mesh, and fitting one is an approximation with
infinitely many answers. Where a spline genuinely exists before tessellation,
have the generator declare it. The same applies to glyph outlines
(`DrawingCallback.cc` flattens quadratic and cubic Béziers for `text()`) and to
`SkinNode`/`PathExtrudeNode`, where cross-sections and path are known.

`FrepNode` is the one place nothing is possible — marching-cubes output has no
analytic surface to recover, by construction.

The gate to watch here is topology, not geometry. Every substitution the
exporter can currently make replaces straight edges with a **circle**; a fillet
meets its neighbours along edges which are not circular, so the run and
whole-loop rules have to be generalised to an arbitrary declared curve before a
B-spline patch can be bounded at all.

That generalisation was listed as the bulk of the item and as shared with
everything non-circular that follows, which put it next on this list. **Both
halves of that are wrong**, and the way to see it is to ask what would consume
it:

- **There is no non-circular curve anywhere in the codebase.** `ArcCurve` is the
  only subclass of `Curve` and `CylinderSurface` the only subclass of `Surface`.
  The only thing that produces an `ArcCurve` is `import_step.cc`, reading one
  back off a file, and `build_tri_body()` discards `curves` outright - which
  costs nothing, because every arc it writes is a rim it derived from the mesh
  once the band was accepted.
- **It is shared with nothing that is actually reachable.** The two surface
  types still worth having, `SPHERICAL_SURFACE` and `TOROIDAL_SURFACE`, are
  bounded by *circles* - a sphere zone by its two cap rims and a seam, a torus
  by two seams - so neither needs one line of it. What needs it is splines, and
  a spline has nothing to bound yet: no `Curve` subclass describes one, so the
  generalisation would be written against a type that does not exist. See *What
  the blocked items actually need* - the channel for declaring one is already
  there, which is not what this item used to say.

So the generalisation is a mechanism with nothing to drive it, and worse, it
cannot be *tested*: there is no curve to generalise to, so any rim rule written
for one would be unexercised code. It belongs behind the declaration work, not
in front of it. The order in this item was backwards.

#### What the patches actually are, measured

The declaration work is done, so this item is next, and the first thing to
establish is what there is to declare. Both kinds of patch turn out to be exact
tensor-product Beziers whose control nets fall out of the generating code by
algebra - verified numerically against the vertices `FilletNode` actually emits,
worst residual 2.7e-15 over twenty configurations of the corner and 1.6e-15 over
two hundred random edge strips:

- **An edge strip** is degree (2,1). Expanding the rail
  `p + e_fa - 2f*e_fa + f^2*(e_fa + e_fb)` gives the quadratic Bezier control
  points `(p + e_fa, p, p + e_fb)`: it starts on one face, is controlled by the
  original edge vertex, and ends on the other. The strip is the ruled surface
  between the two rails, so its net is those two triples, 3x2.
- **A corner patch** is degree (2,2), with a 3x3 net whose last row is the apex
  three times. The row at parameter `t` is a quadratic Bezier between `Pxz(t)`
  and `Pyz(t)` through a control point that mixes their coordinates, and all
  three of those are themselves quadratic in `t`, which is what makes the whole
  thing a tensor product. The degenerate row is a singular point, legal in STEP
  and the usual way a rounded corner is written.

**The corners are the item, not the strips.** A corner patch is `(N-1)^2`
triangles and a strip is `N-1` quads, so the corners grow quadratically and the
strips linearly:

| `fillet(fn=N)` on a cube | 8 corners | 12 strips | planes | total | collapsed |
| --- | --- | --- | --- | --- | --- |
| N = 5 | 128 | 48 | 6 | 182 | 26 |
| N = 12 | 968 | 132 | 6 | 1106 | 26 |
| N = 24 | 4232 | 276 | 6 | 4514 | 26 |

Collapsing only the strips - the obvious first slice, and the one that needs no
sharing between two curved faces - wins 276 faces of 4514 at `fn = 24`, six per
cent. So there is no cheap half of this item, and the two halves cannot be
separated anyway: a corner's three boundaries are the rails of its three
adjacent strips, so corner and strip are collapsed together or not at all. That
makes the curve generalisation mandatory for either, exactly as this item
originally said - what has changed is that there is now something to generalise
*to*, and a measured reason to want it.

#### PythonSCAD's fillet is not a fillet

Comparing a filleted cube against the same part filleted in SolidWorks says
something the face counts hide. SolidWorks writes **6 PLANE, 12
CYLINDRICAL_SURFACE and 8 SPHERICAL_SURFACE** - 26 faces, bounded by 24 circles
and 24 lines. That is what a fillet *is*: a quarter cylinder along each edge and
an octant of a sphere at each corner, all of radius r.

`FilletNode` draws neither. Its rails are quadratic Beziers, and a quadratic
Bezier through those control points is a **parabola**, not a circular arc - a
circle needs a rational quadratic, with the middle weight at cos 45. Measured on
`cube(10, center=True).fillet(1, fn=12)`, the twelve points of one cross-section
sit between 1.000000 and 1.059690 from the axis a true fillet would turn about,
where every one of them should be exactly 1:

| | distance from the edge, at the middle of the arc |
| --- | --- |
| what a quadratic Bezier gives | 0.3536 |
| what `fillet(1)` measures | 0.3622 |
| what a quarter circle of r = 1 gives | 0.4142 |

**Six per cent of the radius**, and it is in the mesh, so it is in every export
and every print, not only in STEP. `fillet(1)` does not produce a 1 mm fillet.

This reframes the item rather than blocking it. Writing the patches as
B-splines is faithful to what `FilletNode` draws, and that is worth having for
any generator whose surfaces really are splines. But if `FilletNode` drew
circular arcs instead, a filleted cube would be 12 cylinders and 8 spheres - the
same 26 faces SolidWorks writes, in entity types this exporter *already*
declares, recognises and emits.

#### The Bezier substrate is the right one; the weights are missing

An earlier version of this section concluded "fix the geometry first", meaning
replace the Beziers with arcs. That was the wrong conclusion, and the reason is
worth recording, because it is a case of measuring one thing correctly and then
drawing a design conclusion the measurement does not support.

`FilletNode`'s author chose quadratic Beziers deliberately: they need no axis and
no tangency solve, so they keep working where a circular fillet has no clean
definition - faces that are not perpendicular, edges that are not straight, a
radius that changes sharply in size or direction. Replacing them with arcs would
buy exactness on cubes at the cost of every case the construction exists for.

The measurement stands. The diagnosis was wrong: the error does not come from
using Beziers, it comes from using **non-rational** ones. A rational quadratic
with the middle weight at `cos(θ/2)`, where θ is the turn angle between the two
end tangents, is *exactly* a circular arc - and it uses the same three control
points, computed the same way, with no axis to solve for.

How far off the polynomial is depends on the angle, and it is worst exactly in
the awkward cases the substrate was chosen to survive:

| dihedral between the faces | turn angle | `cos(θ/2)` | polynomial error | rational error |
| --- | --- | --- | --- | --- |
| 60° | 120° | 0.500000 | 25.00% of r | 2e-16 |
| 90° | 90° | 0.707107 | 6.07% of r | 1e-16 |
| 120° | 60° | 0.866025 | 1.04% of r | 2e-16 |
| 150° | 30° | 0.965926 | 0.06% of r | 1e-16 |

The corner is the same story and slightly worse. Replaying `bezier_patch()`'s own
rails-and-mid construction on a cube corner at `fn = 12`:

| | worst distance from the sphere a true fillet turns about |
| --- | --- |
| both weights 1, as today | 9.55% of r |
| rails rational, rows not | 6.07% of r |
| both rational at `cos 45°` | 2.2e-16 |

**The control net `FilletNode` builds is already the classical exact net for an
octant of a sphere**, degenerate apex row and all - the net a rational
bi-quadratic with weights `(1,w,1)x(1,w,1)` reproduces to machine precision. Only
the weights are absent, and the weight the tensor product induces along the rail
is constant, which is why one scalar suffices per patch rather than a table.

So the change is not a replacement of the design but the completion of it:
`Bezier()` at `src/core/FilletNode.cc:136` - which carries its own
`// TODO improve` - takes a weight, and the two callers pass `cos(θ/2)`. The
control points do not move.

What that does to this item: for a constant radius meeting perpendicular faces
the exporter can then write `CYLINDRICAL_SURFACE` and `SPHERICAL_SURFACE`, which
it already recognises and emits, and a filleted cube becomes the 26 faces
SolidWorks writes. The B-spline machinery is not obsoleted by that - it becomes
the general case it should always have been: a varying radius, or a corner where
the faces are not perpendicular, is a rational blend that is exactly a conic
rather than a circle, and `RATIONAL_B_SPLINE_SURFACE` is how STEP says so.

So the order is: make the existing Beziers rational, then let the exporter write
a circle where the weights say circle and a spline where they do not.

**Both halves have since landed and been measured on a real build.** A fully
filleted cube is the Minkowski sum of `cube(a-2r)` with a sphere of radius r, so
the truth is computable rather than a matter of comparing pictures.
`cube(10, center=True).fillet(1, fn=24)` exported to STL now measures

| | volume | surface |
| --- | --- | --- |
| measured | 975.5163 | 547.3143 |
| exact, smooth | 975.587 | 547.363 |
| what the polynomial rails gave | 980.889 | - |

which is the tessellation deficit below the exact figure and 5.37 away from the
parabola. And `step-fillet` exports as *26 faces instead of 1106* - the 20
patches and the 6 flat faces, the same 26 SolidWorks writes - with all 48 shared
seams agreeing and 0 boundary runs unresolved.

#### Where this item stands

`BezierPatchSurface` exists and is verified: evaluation by de Casteljau,
projection by Gauss-Newton from a grid of starting parameters, and the boundary
curves read off as rows and columns of the net. `tests/bezier-patch-check.cc`
builds both patch kinds the way `FilletNode` tessellates them and checks every
vertex - 3754 accepted with none missed, and 312 points deliberately off the
surface rejected with none wrongly accepted.

Two things about it are worth keeping in mind. A Bezier is affine invariant, so
unlike a cylinder it survives a non uniform scale and a shear - `transform()`
moves the control points and always succeeds, where `CylinderSurface::transform`
has to refuse. And the projection starts from a 5x5 grid rather than the middle
of the patch, because a corner fillet is degenerate at its apex and the
derivative vanishes there.

`FilletNode` declares both nets in world coordinates, at the two points where
it draws them, and a recognition pass finds the facets on each and resolves
every boundary. All of it is measured on a real filleted cube at `fn = 12`
rather than on a transcription of the tessellation:

```text
20 analytic surfaces available (0 cylindrical, 0 spherical, 0 toroidal, 20 Bezier)
20 Bezier patches cover 1100 facets
48 of 48 shared seams agree between the two patches meeting there
their boundaries are 48 curved runs over 552 mesh edges, and 24 straight edges
those runs border 0 whole faces, 24 stretches of a face, 48 other patches, 0 unresolved
writing them would give 26 faces instead of 1106
```

1100 of 1106 facets, the six left over being the flat faces. Every boundary can
be substituted, and both sides of every shared seam split it identically - which
they must, because a seam becomes one `EdgeCurve` used by two faces, and a seam
that is one run for the strip and two for the corner opens the shell along it.

Three things had to be fixed to get there, and none of them was in the exporter:

- **A patch run borders a patch, not a face.** A strip's rail of eleven segments
  borders eleven different triangles of the corner it meets, so asking for one
  neighbouring loop rejected all 48 curved runs. The test is agreement on one
  neighbouring *patch*, falling back to one loop.
- **The reversed match was off by one.** When the neighbour walks a run
  backwards, `loop[j+c]` is `verts[count-c]`; the check compared the second
  vertex against the first index, losing 8 of 24 straight runs.
- **Every filleted body was non-manifold**, which is the one that mattered most
  and had been shipping. See the trap above.

The entity writing has since landed too: `B_SPLINE_CURVE_WITH_KNOTS` per curved
run, spliced into the neighbouring loops through the `ArcSubstitution` path the
arcs already use, `B_SPLINE_SURFACE_WITH_KNOTS` per patch, and
`check_bspline_faces()` in the validator for both. `tests/bspline-check-mutations.py`
is that check's calibration and runs as its own ctest test: the mutation it
exists for is a bounding curve with the right two end vertices, used twice in
opposite directions so the shell still closes, but taken from the wrong edge of
the control net - a face that bulges the wrong way and passes every topology
check.

What remained of this item was not the exporter but the geometry: see
*PythonSCAD's fillet is not a fillet* above. Writing a parabola faithfully as a
B-spline is still writing a parabola where the model said fillet. That half
landed when `Bezier()` went rational, and the consequence has now landed too.

#### The quadrics, since the rails went rational

A rational quadratic with the middle weight at `cos(theta/2)` is not a spline
that approximates a circle, it *is* the circle. So once the fillet's rails
became rational, an edge strip along a straight edge at constant radius stopped
being merely close to a cylinder quadrant and became one, and a corner between
three perpendicular faces became an exact sphere octant - and the exporter went
on writing both as `B_SPLINE_SURFACE_WITH_KNOTS`. That is valid and it imports,
but the difference is not cosmetic: a `CYLINDRICAL_SURFACE` is what a CAD kernel
can offset, thread and pattern, and a B-spline is what it tolerates.

`AnalyticFeatures::quadricOfPatch` recovers the surface from the control net.
The two rails of a strip are read as circles - a rational quadratic's centre is
the point on the perpendicular to its first tangent equidistant from its last,
which is closed form and needs no fitting - and the pair is a cylinder when the
radii agree and the line joining the centres is normal to both arc planes. A
corner is a sphere when its first row and first column are arcs of one radius
about one centre, and its polar axis is taken through the apex, which is what
keeps the octant inside a single `(theta, phi)` rectangle instead of straddling
the surface's own seam.

**Reading the net is a candidate, not an answer, and the difference is a real
surface.** Two rails can be concentric circles of equal radius on a common axis
and still not bound a cylinder: turn one against the other and the ruled surface
between them is a hyperboloid touching the cylinder only at its two ends. Every
test made of centres, radii and plane normals passes on it. So the candidate is
then *measured* - the patch is evaluated on a 7x7 grid and every point has to
lie on the quadric within the modelling tolerance - which is also what brings
the weights into the test, so a patch whose middle weight is not `cos(theta/2)`
fails here rather than being written as the circle it is not. Both mutations are
in `analytic_features_test.cc`.

The curves have to move with the surface. A quadric face bounded by splines off
a control net is a face no importer can check against its own surface, so a
curved run on a quadric patch is written as a `CIRCLE` - the same curve, in the
form a kernel will pattern along. Which means the two patches sharing a rail
have to agree about which form it takes, since that rail is one `EDGE_CURVE`
used by both. A patch whose partner is not a quadric therefore withdraws, and
because withdrawing one can withdraw its own partner in turn, the classification
runs to a fixed point. Straight runs are `LINE`s either way and constrain
nothing.

Measured on the headless build, `cube(10, center=True).fillet(1, fn=12)`:

```text
20 Bezier patches cover 1100 facets
20 of 20 patches are exactly quadrics - 12 cylindrical, 8 spherical
written as 26 faces instead of 1106
```

and the file contains **12 `CYLINDRICAL_SURFACE`, 8 `SPHERICAL_SURFACE`, 6
`PLANE`, 24 `CIRCLE`, 24 `LINE` and no B-spline at all** - entity for entity
what SolidWorks writes for the same part.

The geometry is checkable rather than a matter of comparing pictures, because a
filleted box is the Minkowski sum of the box shrunk by 2r with a sphere of
radius r. The eight sphere centres come out at `(+-4, +-4, +-4)` with radius
exactly 1, which is the exact answer, not a fit. `step-fillet-oblique.py` runs
the same measurement on a 14 x 9 x 6 box turned through three angles that share
no factor - nothing is axis aligned - and the centres are the corners of a
12.4 x 7.4 x 4.4 box to nine decimal places, at radius exactly 0.8.

The validator learned one new face shape for this, and it is a shape rather than
an exemption: a sphere octant is bounded by **three** great circle arcs meeting
at right angles, with no straight edge anywhere and no fourth side, because its
fourth side is the pole where the patch is degenerate. The rule that replaces
"a partial face needs two distinct end edges" is that every bounding arc is a
*great* circle - a small circle there would be a rim of some other sphere, which
is the mistake this shape can make.

**Where it stops, and it is not where one would expect.** A strip along a
straight edge is a cylinder whatever the dihedral, so a hexagonal prism's
eighteen strips all qualify on their own; what does not is any corner where the
three faces are not mutually perpendicular. The fixed point then withdraws the
strips with them, and such a body writes no quadric at all.
`step-fillet-refusals.py` is exactly that model and asserts exactly that, which
is worth having because the failure is one-sided: a patch wrongly refused costs
a nicer entity, while a patch wrongly accepted writes a surface the mesh is not
on and still closes, still validates, and still looks right.

Lifting the conservatism - letting a quadric face keep a spline bound on a
shared rail - needs the validator to check a `CIRCLE` bound against the patch it
bounds, which nothing does today.

That fixture could not be written at first, because every model that would have
exercised the refusal failed to export at all: `fillet()` produced a **non
manifold mesh** for non right dihedrals, and a hexagonal prism came out as an
open shell with the analytic path switched off entirely. That is fixed - see
*The corner's frame* below - and it was the same class of error one level up.

#### The corner's frame

`bezier_patch()` builds a corner by replacing its three direction vectors with
axis aligned ones of the same length, working in that frame, and shearing the
result back with a matrix whose columns are the real directions. That is exact
for the control points, because an affine combination commutes with a linear
map. It is wrong for the **weight**: `cos(theta/2)` is measured between tangents
and a shear changes the angle between them, so the weight computed in the
fabricated frame describes a different curve once the frame is undone.

On a cube the matrix is a signed permutation and nothing moves, which is why
this survived so long. Where the three edges are not mutually perpendicular the
corner drew the image of a circle - an ellipse - while the edge strips meeting
it drew true circles in world coordinates, having never left them. The two
curves share their endpoints and nothing else, so every corner carried a lens
shaped hole: 168 edges used by one face on a hexagonal prism, 112 on a sheared
cube.

The fix measures every weight in world coordinates and hands it to `BezierW`
explicitly, leaving the points in the frame where the coordinate mix between the
two rails means anything. The corner's rails are then the same expression on the
same inputs as the strips' rails, which is the rule `6a457d2` established for
the shared *point* - one arithmetic, one answer - applied to the curve instead.

| | before | after |
| --- | --- | --- |
| hex prism, edges used once | 168 | 0 |
| sheared cube, edges used once | 112 | 0 |
| corner boundary against the strip's rail | up to 0.024 apart | the same vertices |
| that boundary as a circle | best fit residual 3.2e-3 | exact, at sqrt(3) - the analytic radius for a 120 degree dihedral at tangent length 1 |
| sharp edges left on the filleted prism | - | none; every kink is tessellation, at most 14.3 degrees at fn 8 |

Note what that radius says about the construction: the rails run through the
edge vertex itself, so `fillet(r)` sets the *tangent length* rather than the
radius, and the arc it draws has radius `r / tan(theta/2)` - equal to r only at
a right angle. That is the shape the fillet has always intended; the corner has
now caught up with it.

All six models of `tests/data/pythonscad/fillet.py` are byte identical across
the change, as are both cube fixtures: it is inert wherever the frame was
already orthogonal.

**What it costs, and where.** Correcting the mesh moved it away from the
*declaration*, which had been matching the wrong surface. The declared net is a
rational bi-quadratic whose weights are separable - `(1, wv, 1)` across
`(1, wu, 1)` - and separability forces the patch's two u-rails, the columns at
`v = 0` and `v = 1`, to share one middle weight. They do share one exactly when
they turn through the same angle, which is to say when the third direction is
perpendicular to the other two. That holds for every prism and every extrusion,
because the direction toward the end face is normal to the profile plane - so a
hexagonal prism still declares all thirty of its patches and writes 38 faces. It
does not hold for a corner where no pair is perpendicular: there the drawn
surface has three rails with three different weights and is not a separable
tensor product at all, so the recogniser refuses the corner patches rather than
believing them.

| | before (open shell) | after (closed) |
| --- | --- | --- |
| hexagonal prism | 30 patches, invalid | 30 patches, 38 faces, valid |
| sheared cube | 20 patches, 26 faces, **invalid** | 12 strips, 8 corners refused, 410 faces, **valid** |

That is the right trade - an invalid file is worth nothing at any face count -
and the refusal is loud, one report line per patch. Declaring a fully skewed
corner exactly needs a non-separable weight net, which `BezierPatchSurface`
already carries the storage for; whether the drawn blend is a bi-quadratic at
all under those weights is the open question, and it is a modelling problem
rather than an exporter one.

Two incidental findings, both since acted on: `OpenSCADUnitTests` was commented
out in `CMakeLists.txt`, so no Catch2 test in this repository was built - which
is why the check above began as a standalone program - and the glob feeding it
only matched `src/utils/*_test.cc`, so `src/geometry/linear_extrude_test.cc` was
invisible to it. The target is back, the glob covers `src/**/*_test.cc`, and the
check is now `src/geometry/bezier_patch_test.cc`. See `doc/testing.md`; enabling
it turned up two test files that had never compiled and one uninitialised
`double` in `vector_math`.

### The round trip, and what it found

Every check in this project until now was the exporter marking its own work.
`validatestep.py` is a good proxy - eleven checks, one per historical defect -
but a proxy knows what this exporter has got wrong before, not what a CAD kernel
requires. `tests/steproundtrip.py` closes that: it reads each export back with
**OpenCASCADE**, the kernel FreeCAD is built on, and asserts a closed, valid,
positive-volume solid whose surfaces the kernel recognises *by type*. Optional
dependency, skipped silently when absent.

It found a real defect on its first run, and the defect is instructive.
`B_SPLINE_SURFACE_WITH_KNOTS` takes `(u_multiplicities, v_multiplicities,
u_knots, v_knots)`. The polynomial branch wrote that. The rational branch - every
fillet patch - built the tail from two per-direction strings and wrote them
interleaved, `(u_mult, u_knots, v_mult, v_knots)`. Each list correct, the order
wrong. `validatestep.py` checked that the right lists were *present*, which an
interleaved tail satisfies, so it passed; OpenCASCADE read `(0.,1.)` where
v_multiplicities belongs, could not build the surface, and dropped the face. A
hexagonal prism that this project called valid at 38 faces came back from the
kernel as **0 solids and 8 planes** - loose surfaces, which is the symptom that
started the fillet investigation in SolidWorks in the first place.

Both branches now share one string, and the validator checks the order rather
than the presence, calibrated by mutating a good file back.

The round trip asserts the kernel's *parameters*, not only its face counts.
Reading twelve cylinders says nothing about whether they are the right twelve: a
recovery that got an axis or a radius wrong writes a surface the mesh is not on,
and almost nothing objects, because the bounding circles come out of the same
recovery and agree with it, the shell still closes, and `validatestep.py`
compares the rim radius against the surface radius - both wrong together. So the
filleted cube states its radii, and the kernel reads `Cylinder 1, Sphere 1` with
axes on the three coordinate directions, four per axis.

It asserts the **edges** too, which is the other half of item 6 and was
previously unverified: a quadric face is bounded by `CIRCLE`s rather than by
splines off a control net, because that is the form a kernel offsets and
patterns along - but nothing checked that a kernel actually *reads* them as
circles. It does: 24 circles of radius 1, 24 lines.

The eight remaining edges are worth knowing about, because they look like a
defect and are not. They come back as zero-length degenerate edges, one per
corner, and the exporter did not write them: a spherical face is a rectangle in
`(theta, phi)` whose fourth side is the pole, and OpenCASCADE inserts an edge
there itself to close the parametric boundary. Seeing exactly eight of them, at
the apex of each octant, is confirmation that putting the polar axis through the
apex was right - that is what keeps the octant inside one parameter rectangle
instead of straddling the surface's own seam.

The round trip also carries a **cross check on the recogniser**, running in the
one direction a missed opportunity can hide in: of the faces the exporter chose
to leave as splines, how many are exactly quadrics after all?
`ShapeAnalysis_CanonicalRecognition` answers that. It is not a replacement for
`AnalyticFeatures` and cannot be - it reads the surface, not the facets, so it
refuses to call a 4, 6, 8, 16, 32 or 64-gon prism a cylinder at any tolerance up
to 3.0, while recognising a true cylindrical face and a `NurbsConvert`ed one
alike as radius 10.000000. The tessellated-cylinder problem has no OCCT tool.
Auditing the spline faces is what it is good for, and there it is a genuinely
independent opinion.

Today it finds the extruded glyphs clean - 32 and 19 genuine splines, which is
right, a font's Beziers being no kind of quadric - and six exact cylinders of
radius sqrt(3) among the hexagonal prism's thirty. Those six are the vertical
edge strips the fixed point withdraws for sharing a rail with a non-quadric
corner, so the follow-on above is worth six faces on that model, measured rather
than guessed.

The measurement that makes the analytic path worth having, finally stated as a
number a kernel produced: a filleted cube is the Minkowski sum of `cube(8)` with
a unit ball, so its volume is exactly **975.587014**. OCCT, rebuilding the
smooth solid from twelve `CYLINDRICAL_SURFACE`, eight `SPHERICAL_SURFACE` and
six `PLANE`, measures **975.587014**. The mesh those were recognised from
measures 975.5163. The export is closer to the truth than its own input.

### 3. Spheres - collapsed, without `SPHERICAL_SURFACE`

Two things this item said were wrong, and finding out which cost one
measurement.

**"The poles are degenerate triangle fans, so a full sphere needs a seam plus
two pole singularities."** Not in this implementation. `SphereNode` puts its
rings at `phi = 180 (i + 0.5) / num_rings`, so the first and the last are
ordinary circles closed by a **flat cap**. OpenSCAD's sphere is a barrel and has
no poles at all, which removes the hardest part of the item before it starts.

**"The fit cannot tell a sphere from a stack of cones ... only the intent gate
stops a sphere coming out as a pile of cones."** True, and the conclusion drawn
from it - that the sphere's own declaration must be matched *before* the cone
rule - was the wrong way round. A stack of cones is not a failure mode here. Its
faces pass through the mesh vertices with zero residual, share every rim, and
leave the shell watertight; it is not the true surface, but neither is the
faceted mesh it replaces, and it is 30 times smaller.

So `SphereNode` declares the circle at each ring, and a sphere collapses with no
recogniser work at all: at `$fn = 32`, 16 rings and 480 quads become 15 bands,
the one straddling the equator a cylinder and the other 14 cones, **482 faces
down to 17**.

### And then a sphere is a stack of bands, not a grid

`SPHERICAL_SURFACE` was the rest of the item, and the roadmap said it needed a
*grid grower*: a sphere's facets span many rings rather than two rims, so the
strip walk cannot describe it. That is true of the strip walk and it is the
wrong conclusion, because the grower does not have to start from facets.

The first attempt did start from facets - flood across any edge into any face
whose vertices lie on the declared sphere - and it swallowed the two caps. That
is not a bug in the test. An OpenSCAD sphere is a **closed polyhedron inscribed
in the sphere**, and its caps are planar polygons with every vertex on the
surface and the same sag as any other facet; no local geometric test separates a
cap from a ring quad, because geometrically there is nothing to separate.

What separates them is structure, and the band pass has already computed it. A
sphere is a **stack of bands**: every ring is a frustum whose two rims are
circles, and the zone is the maximal run of them joined at shared rims whose
vertices all lie on one declared sphere. So there is no new grower - the band
pass does the work, and a merge pass joins up its answer. The run's outer rims
are kept, so the rules resolved for the end bands still hold and the caps are
untouched by construction. **482 faces down to 3.**

The same mechanism gives a torus, whose run closes on itself instead of ending
at a cap - see the torus section for what still stands in the way there.

One thing genuinely is new, and it is the seam. A periodic face is closed by a
seam that has to lie *on* the surface: up a cylinder or a cone that is a
straight ruling, but over a sphere it is a **meridian**, and the straight line
between the same two vertices sags 0.05 mm off a radius 10 sphere at
`$fn = 32` - five thousand times the modelling tolerance. A spherical zone therefore seams
with an arc of a great circle. Nothing else refers to a seam, since it is used
twice by its own face and appears nowhere in the mesh, so swapping the line for
an arc costs no neighbouring loop a rewrite.

### 4. Trimmed faces

Last, not first. This item carried the note "the real prize" for two rounds and
it is worth saying plainly why that is no longer true: **most of the prize has
already been collected by other items**, and what is left of it cannot be won
from the mesh at all.

Provenance survives booleans but the geometry gets *cut*. A cylinder intersected
by another produces a partial cylindrical face bounded by an intersection curve
— in STEP a `SURFACE_CURVE`, usually approximated by a B-spline.

The planar-trimmed subset has been split off and done — see *Partial cylinders*
above. Where the trim is a plane perpendicular to the axis or parallel with it,
the bound is an arc and a straight line, both exact, and no `SURFACE_CURVE`
appears at all. That turned out to be very nearly all of it: of the bayonet's
1693 faces, **fourteen** lie on a cylinder or a cone with a trim that is
neither, spread over four surfaces, the largest carrying eight facets.

What stays is the genuinely hard remainder: a trim curve which exists only as a
polyline in the mesh. Putting that approximation on an analytic face would open
the shell against its planar neighbour by far more than the modelling tolerance,
so those walls stay faceted, and no amount of care over the surface changes that
— the curve has to come from the generator, as with the splines in item 2.

Two lessons are worth more than the item:

- **The earlier estimate counted rings, not faces.** "6 of the 15 detected
  cylindrical rings are full rings, the other 9 are interrupted" was true and
  led to the wrong conclusion, because the nine interrupted ones were interrupted
  by *planar* cuts. The unit that predicts effort is the trim, not the ring.
- **The blocker in front of a hard item is usually not the hard part.** Every
  wall this item was supposed to rescue was in fact blocked on a rim rule or on
  an axis derived from the wrong set of facets. Measure which gate a face
  actually fails before costing the surface behind it.

### 5. Swept surfaces — blocked, and worth knowing why

Not previously on this list, and on the bayonet it is larger than items 1 to 4
put together: **999 faces, 59.3% of that model**, lie on no surface of
revolution at all. It is also what pins the last recognisable band in the part,
since a wall whose rim borders a ramp one facet at a time has nothing to share
that rim with.

That made it look like the item to scope next. Reading where the geometry comes
from says it cannot be started, and the reason is worth recording because it is
a limit on the whole provenance approach rather than on this item.

All 999 come from three places in the model, and **none of them is a PythonSCAD
node**:

- `bayonetChannel()` is a hand-written `polyhedron()`. It builds a point grid
  from a list comprehension — a four point profile swept along an arc while its
  height follows a cam — and hands over the triangles. The sweep exists only in
  the user's `for` clause.
- `hoseRidge()` is another `polyhedron()`, sweeping a four point profile along a
  helix.
- `bayonetLugs()` is `hull()` of two `smallArc()`s at different heights, and a
  `smallArc()` is itself a `difference()` of two faceted cylinders by two cubes.

Provenance cannot reach any of that. The mechanism this exporter relies on —
*geometry from the mesh, intent from the primitive* — needs a primitive with an
intent to declare, and `polyhedron()` is precisely the operation that has none.
By the time PythonSCAD sees it, the sweep has already been evaluated into
vertices, exactly as if the user had pasted an STL.

The thread closes the door a second time, independently. Its profile is **not
constant along the sweep**: a lead-in factor scales the ridge depth over the
first quarter turn and out again over the last, so the cross-section at one
angle differs from the cross-section at another. Even a complete helical-sweep
recogniser, handed the sweep on a plate, could not write this as one swept
surface — it is a different surface at every station.

So what is actually available here is smaller and differently shaped than "add
swept surfaces":

- **Declare surfaces from the nodes that do have them.** `SkinNode`,
  `PathExtrudeNode`, `linear_extrude` with a twist and `rotate_extrude` know
  their cross-sections and their path before tessellating. That is real work
  with real coverage — on models built from those nodes. It is items 1 and 2,
  and it does nothing for this part.
- **Give the user a way to declare a surface.** The gap this measurement
  exposes is that a script which computes a swept surface has no way to say so.
  A declaration at the language level — an OpenSCAD module or Python call that
  attaches an analytic surface to geometry the script produced itself — would
  reach `polyhedron()`, which nothing else can. It is a language design
  question rather than an exporter one, and it should be costed as such.
- **Fitting is still not the answer.** There is no unique B-spline underlying a
  triangle mesh, and this thread would need a different one per station. See
  item 2.

Until one of those lands, the honest statement about this part is that
**39% of it can be analytic and 59% cannot**, and the exporter is now within 36
facets of the first number.

## What a model can do about it

The exporter's three gates are all things a model author influences, and on the
bayonet the model decides far more than the exporter does. This is the practical
counterpart to item 5: where the generator cannot declare a surface, the script
can often avoid needing one.

### Where this part's 59% comes from

Attributing the 999 faces by height settles it: **991 of them are the hose
thread**, spread evenly at about 72 faces per 5 mm over z = 0..75, which is
exactly `pitch x turns`. The four bayonet lugs contribute 8. Everything else in
the part is already recognised.

So the single largest exportability lever in this model is a switch it already
has: `_hoseThread = false`.

### A helical thread can never be analytic, and it takes the bore with it

This is geometry, not modelling style. A helix is not a surface of revolution,
so no band recogniser will ever describe the ridge. Less obviously, a
single-start thread running the length of a bore **slices the bore itself into a
helical ribbon**: between two turns the socket wall survives, but as a strip
with no rim at constant height, which is not a band either. The socket is built
entirely from `cylinder()` and would export as a cone and a cylinder on its own;
unioning the thread onto it costs both.

It also blocks one face beyond its own. The socket's parallel section (r = 78.1,
z = 65..75, a 216 degree arc of 36 facets) fits exactly and is declared, and is
left faceted because its lower rim - the boundary between the tapered and the
parallel section - is crossed by 36 separate faces of the thread running through
it. That is the model doing exactly what it says it does: *"it follows the
socket through both the tapered and the parallel section, so the hose stays
engaged right up to the seat"*. The intent is right and the cost is real.

### Rules that generalise

Cheapest first, and all of them are about the topology gate, because that is
where the losses are:

- **Prefer primitives and booleans to `polyhedron()`.** `cylinder()`, `hull()`,
  `difference()`, `union()` and transforms all carry provenance;
  `polyhedron()` carries none and never can. A shape hand-built from a list
  comprehension is opaque to the exporter in the same way a pasted STL is.
  `smallArc()` in this model is the good pattern: a `difference()` of two
  cylinders by two cubes, which declares both radii and cuts them with planes.
- **Interrupt a wall with planes, not with features.** A plane through the axis
  or perpendicular to it leaves an exact arc and an exact straight edge. Any
  other cut leaves a trim curve the mesh only approximates, and the wall stays
  faceted - see item 4.
- **Let a wall end against one face, not against many.** This is the rule that
  costs the most. A rim bordering a single planar face, or a single chamfer that
  is itself a surface of revolution, collapses; a rim bordering one facet per
  segment does not. In practice: run a chamfer or a fillet all the way round
  rather than stopping it against something small, and keep ribs, ramps and
  threads clear of the rim where two walls meet.
- **Chamfer by hulling with a coaxial copy.** `hull()` of two coaxial cylinders
  is recognised as a cone because both of its rims match a declared cylinder,
  and the rim between the chamfer and the wall is written once and shared. This
  is the idiom the exporter was taught; `roundCylinder()` and `thinRelief()` in
  this model both use it and both come out analytic.
- **A feature that must be non-analytic is cheaper if it is bounded by planes.**
  A thread or a ramp will stay faceted whatever happens, but where it *ends*
  decides whether it also spoils its neighbours.

## Declaring a surface from the model

Everything above depends on a declaration, and until now only a primitive could
make one. `cylinder()`, `sphere()` and `rotate_extrude()` say what they drew;
`linear_extrude()`, `polyhedron()` and anything a user assembles by hand say
nothing, and there is no generator in the pipeline that could speak for them.
That is what kept item 5 - the bayonet's threads and ramps, 59% of its faces -
blocked. A swept thread exists only as a list comprehension.

So the model says it:

```openscad
declare_cylinder(r = 10)
  linear_extrude(height = 20) circle(r = 10);
```

```python
wall = linear_extrude(circle(r=10, fn=32), height=20)
wall.declare_cylinder(r=10).show()
```

`declare_cylinder`, `declare_sphere`, `declare_torus` and `declare_cone`, in
both languages - a module wrapping its children in SCAD, a method on the object
in Python, both building the same `DeclareSurfaceNode`. Radii take `r` or `d`,
and `center` and `axis` default to the origin and Z.

`declare_cone` is named the way `cylinder()` names the same shape, `r1`, `r2`
and `h`, with `center` at the `r1` rim and `axis` pointing towards `r2`. It
exists because the rule it supplements - a frustum is accepted when *both* its
rims match a declared cylinder, which a `hull()` of two coaxial cylinders
satisfies - cannot state a cone whose far rim is only where a boolean cut it.
Asking that trim to declare itself is asking the wrong thing of it. See
`step-declare-cone`, where declaring one cone recovers two surfaces: the
cylinder standing on the chamfer was being refused only because the rim it
shared with the chamfer bordered one face per facet.

**It is a node, not a call that annotates a geometry.** There is no geometry yet
when the model is written, and being a node is what makes the coordinates come
out right: records live in world coordinates, and a transform above the node
moves the geometry and its declarations together. Every later boolean, hull and
transform then carries the record the way it carries a primitive's own, because
it is the same channel and the same records.

**A wrong declaration is bounded.** The exporter re-checks every record against
the mesh and against the topology before acting on it, so `declare_cylinder(r =
9)` on the wall above leaves the body faceted and valid rather than wrong. The
one thing the check cannot catch is a declaration which happens to fit some
*other* feature of the model exactly - the same reason `minkowski()` drops
records instead of scaling them. Bounded, not zero, and it is the model's
statement to make.

A declaration that cannot be read - no radius, a radius of zero, an axis with no
direction - warns and is dropped, and the children are kept. A model missing a
hint exports the same body with faceted walls; it should not fail to export.

## Known quality gaps

Everything here is measured, and none of it is a lost declaration or an invalid
file. These are places where the exporter writes a correct but faceted body
where it could have written a surface.

### A boolean on the CGAL backend splits the walls it did not touch

Exactly the four fixtures whose top level object is a Nef polyhedron - the ones
with a boolean in them - recognise differently on the two backends. Every
fixture that stays a PolySet is identical on both, down to the entity count.

| fixture | Manifold | CGAL | what is lost |
| --- | --- | --- | --- |
| step-bore | 2 | 1 | the outer wall; the bore survives |
| step-nested-rings | 5 whole | 17 partial | every ring, cut into arcs |
| step-partial-cylinder | 4 | 0 | all four arcs |
| step-shared-arc | 2 | 0 | both walls |

Same declarations - the two runs report the same count available, which is what
the sanity test asserts - and the same number of faces in the faceted export.
`step-nested-rings` gives the mechanism away: five closed rings become seventeen
arcs. A Nef polyhedron records the boolean's seams, so the wall of a ring the
operand never reached still gets cut where the operand's plane passed through
it, and a wall in several pieces is a wall whose rims no longer bound a single
face. The rim rules then reject it, correctly, for the mesh they were given.

Two of the four fall all the way to zero rather than to more arcs, because those
are the fixtures whose arcs already depend on each rim bordering exactly one
face; splitting them once more leaves nothing that qualifies.

**What to do about it today:** export analytic STEP on the default Manifold
backend. The gap is entirely on `--backend=CGAL`, which is the old and slow path
anyway.

**What would close it:** merging runs of bands that are coplanar in their rims
and cocylindrical in their walls back into one band, before the rim rules are
applied. That is roadmap item 5 below, and it would also pick up seams left by a
Manifold boolean wherever those occur.

### The gaps that are by design

Two more places produce a faceted body on purpose, and should not be read as
defects to fix:

- **`minkowski()` drops every record.** It changes each radius it touches, so a
  surviving declaration would be wrong in a way the fit gate cannot always
  catch - the one case where a record could still match some other feature of
  the result and be acted on. Both backends drop them.
- **A non uniform scale drops the records it cannot express.** A cylinder under
  `scale([2,1,1])` is an ellipse, and no `Surface` subclass describes one.
  `Surface::transform` returns false and the record is discarded rather than
  kept wrong.

## What the blocked items actually need

Three items on this list have been described as blocked on "a declaration
channel that does not exist". That is one sentence covering three different
situations, and only one of them is true. The question that separates them is
not how metadata would be carried - it is **where the declaration comes from**.

### The channel exists, and it is not a sidecar

`PolySet::surfaces` *is* metadata written during evaluation, and it is already
the mechanism `cylinder()`, `sphere()` and `rotate_extrude()` use.
`PolySetBuilder::addSurface()` is the public API any 3D generator can call.
Transforms move the records, `hull()` keeps them on both backends, and
`minkowski()` deliberately drops them.

One real gap, found while writing this down and since fixed: **`CGALNefGeometry`
carried no surfaces at all.** On the Manifold backend `binOp()` merges them, but
a union or difference on `--backend=CGAL` converts both operands to Nef
polyhedra, and every record was lost on the way in. Manifold is the default so
this was mostly latent, but an analytic export under the CGAL backend silently
came out faceted.

A Nef polyhedron is a set of half-spaces and can express none of this, but it
does not have to: it only has to *carry* the records between the one conversion
in and the one conversion out. `CGALNefGeometry` now holds the same list the
other two representations hold, `createNefPolyhedronFromPolySet` fills it,
`getGeometryAsPolySet` hands it back, and the four operators merge or - for
minkowski - deliberately drop it.

Two smaller leaks of the same kind turned up alongside it. `PolySetBuilder`
flattens a `GeometryList` into one mesh and was keeping no declarations from its
inputs, so two disjoint cylinders lost both. And the comparison used to
deduplicate records lived privately inside `ManifoldGeometry.cc` and tested only
whether each side was a cylinder: a sphere and a torus about the same axis
through the same point compared *equal*, and merging them kept one. That
comparison is now `Surface::sameAs`, virtual, checking the dynamic type first,
and shared by all three backends' merge paths.

All three were invisible in the same way the recogniser's own failures are: the
output validates, and a file with no analytic surfaces looks exactly like a
model that declared none. So the sanity test now exports every fixture a third
time under `--backend=CGAL` and compares **how many declarations reached the
exporter**, which both runs report. Every fixture agrees, from 1 to 18.

Not how many were *written*, which was the first thing this test asserted and
was wrong. Those are different quantities and only the first is the channel -
the two backends genuinely do not write the same surfaces, which is measured
under *Known quality gaps* above.

### Only one item is short of a channel

| item | what it is short of |
| --- | --- |
| 2, fillets and splines | a `Surface` subclass, not a channel. `FilletNode` already builds through `PolySetBuilder`, so it could declare today - what is missing is a type to declare, the matching, the emission and the rim generalisation |
| the torus | a **2D** channel. `Outline2d` carries vertices, a winding flag and a colour, so `circle()` cannot say it was a circle |
| 5, ramps and threads | a **user-facing** declaration. There is no generator at all: the sweep exists only in the user's `polyhedron()` list comprehension |

So metadata is the answer for two of the three, and for different reasons.

For the **torus** there are two shapes it could take. The honest one is a curve
channel on `Outline2d`, which then has to survive the Clipper booleans and
`offset()` - that is where the cost is. The cheap one is for `rotate_extrude` to
read its **child node tree** rather than its geometry: a transform of a
`CircleNode` says the profile is a circle exactly, with no channel at all. It
breaks the moment a `difference()` intervenes, but it covers
`rotate_extrude() translate([R, 0]) circle(r)`, which is the idiom.

For **item 5** a user-facing declaration is the only possibility, and PythonSCAD
is unusually well placed for one: solids are Python objects that already accept
attribute assignment (`python__setattro__` forwards to `python__setitem__`). A
call which appends a `CylinderSurface` to an object's `PolySet::surfaces` would
then be carried by transforms, booleans and hulls for free - the machinery is
all there, only the way in is missing. The SCAD equivalent is a module wrapping
its children.

### Two properties that make a loose scheme safe

**A record is only ever a hint.** The exporter re-checks every declaration
against the mesh and against the topology before acting on it, which is why the
scheme tolerates imprecision. A *global* list of "surfaces mentioned anywhere in
this model" would work nearly as well as the scoped one - it would only offer
more candidates for the fit to reject. That lowers the bar for any metadata
design a long way, and it is the reason a user-facing declaration is not as
dangerous as it sounds.

**World coordinates are the one thing that must ride along.** Records are stored
in world coordinates and moved by the transforms above them. A sidecar written
at render time would have to capture the transform stack in effect where the
declaration was made, which is exactly what living inside the geometry gives for
nothing. That is the argument against a separate file and for staying in
`PolySet`.

The limit is worth stating too: the fit gate catches a wrong declaration only
when it does not match the mesh. `minkowski()` is dropped precisely because it
changes a radius in a way that could still fit something else, and a user-facing
declaration inherits that - someone can assert a cylinder which happens to fit a
prism elsewhere in the part. Bounded, but not zero.

### Ordered by value per unit of work

1. ~~**`rotate_extrude` reading its child node.**~~ Done: a torus is one
   `TOROIDAL_SURFACE`.
2. ~~**Fix the CGAL backend dropping records through booleans.**~~ Done, with
   the two neighbouring leaks above.
3. ~~**A user-facing `declare_*` in the Python API.**~~ Done, and in SCAD too:
   see *Declaring a surface from the model* below.
4. **`FilletNode` declaring a B-spline.** No channel work at all: a surface
   type, the matching, the emission, and the rim generalisation that item 2 has
   been carrying all along. Sized under *What the patches actually are* above:
   one indivisible piece, and worth 4514 faces down to 26 on a filleted cube.
5. **A wall split by a Nef boolean.** Measured under *Known quality gaps*
   above: on `--backend=CGAL` a boolean leaves the wall of a ring it never
   touched cut into arcs at the seams where the operands met, and the recogniser
   then sees several walls where there is one. Merging runs of
   coplanar-and-cocylindrical bands back together before the rim rules are
   applied would recover it, and would also help the seams a Manifold boolean
   leaves behind.

## Method notes

Two habits earned their keep and are worth repeating on this code:

**Prove the test catches the bug.** Building the exporter at the commit before
each fix, and confirming the new check rejects that output, is cheap and turns
"this test looks right" into evidence. It caught a validator that silently
missed two of the seven mutations it was supposed to detect.

**Prototype the pass in Python over an already exported file.** Every round did
this and every time it paid for itself before any C++ existed: it caught the
centroid of an arc not lying on the axis, it proved the strip walk finds a
frustum where the old growth could not, and it found both of item 0's defects.
An exported STEP file is a complete description of the merged mesh, so parsing
one gives the same input the exporter sees, with none of the build.

That parser is now `scripts/step-analytic-probe.py`, so the next round starts
with it rather than rewriting it:

```bash
# the ceiling: how much of this part lies on a surface of revolution at all
scripts/step-analytic-probe.py surfaces part.stp
# every band the recogniser fits, and the rule that rejected each one it drops
scripts/step-analytic-probe.py bands part.stp
# ... and what the two changes of item 0 would add
scripts/step-analytic-probe.py bands --local-axis --shared-arcs part.stp
# why a wall that plainly fits was never even a candidate
scripts/step-analytic-probe.py trace part.stp --z 89.25 90.75 --r 78 79.5
```

Run it on a **faceted** export. It replays the recogniser itself, so an
analytic export would be measuring the answer rather than the question. It has
no provenance to read, so it measures the geometry and topology gates only -
which is exactly what makes its numbers a ceiling. Every figure in *What is
actually left in the bayonet* is one run of it over
`examples/step_test/bayonet_container_v1-2.stp`.

**Make the exporter say why it refused.** See *A rejected surface is invisible*
above.

**Measure an item before starting it, not after.** Item 4 sat at the top of this
list for two rounds on an estimate that counted rings rather than trims. The
measurement that corrected it took under an hour, reused the parser from the
habit above, and changed the order of everything below it — and the first thing
it turned up was not a missing feature but a defect in code that already
shipped. `scripts/step-analytic-probe.py surfaces` is one command and it bounds
any surface-recognition item you are about to cost.

**Prefer the cheapest experiment that discriminates.** The question "can
cylinders be recognised in post-boolean meshes?" was answered by a
~250-line Python script run over existing exports (1e-14 residuals on a
1685-face model) before any C++ was written. The same script, run on a cube,
produced the zero-residual false positive that reframed the whole feature.

**Check what you assume you cannot build.** Several rounds of this work were
done by extracting new functions into standalone translation units with the
project's headers stubbed out, on the belief that nothing here would compile.
Most of it does: `glib`, `Eigen` and the Python headers are all present, and
`src/io/StepKernel.cc`, `src/io/export_step.cc`, `src/geometry/AnalyticFeatures.cc`,
`src/core/primitives.cc` and `src/python/pyfunctions.cc` all pass
`g++ -fsyntax-only` with `-I . -I src -I src/core -I src/geometry` and
`pkg-config --cflags glib-2.0`. Only the files reaching CGAL, clipper2 or
manifold genuinely cannot be built. The cost of not checking was a new source
file that referenced `BuiltinModule` without including `core/module.h`, which a
one-second syntax check would have caught and which instead cost a full build
and test cycle. `src/geometry/rotate_extrude.cc` does not compile with
`ENABLE_MANIFOLD` off, at HEAD and before it - that one is pre-existing.

Two diagnoses in this session were confidently wrong and were corrected by data
rather than by argument — the orphaned-hole theory of the membrane, and a
missing UCRT DLL that was never missing. Both times the correction came from the
user running the thing and pasting the output.

## Sampling a declared sweep

A `declare_grid` fit interpolates a cubic through the stations the generator
emitted, so it is exact *at* them and the only place it can stray is between.
Where it strays most is not random: it is wherever the model's sampling is
thinnest against geometry that is changing fastest, and for a swept feature that
is almost always the run-in and run-out.

The exporter says so. Every sweep it writes reports how far the surface passes
from the middle of the furthest facet it claimed, and where along the sweep that
was:

```text
the fitted sweep passes within 0.1724 of the middle of every facet it claims,
against a tessellation band of 0.2527 - worst at 2% along the sweep
```

On the four sweeps in the tree the worst point is at 1%, 1%, 2% and 98%. Every
one of them is at an end.

**This is a property of the model, not of the fit.** The reference lid's
`hoseRidge` takes `steps = max(24, round($fn*turns))` stations spaced uniformly
in `t`, while its ridge depth ramps over the first and last few percent of the
sweep - so the ramps get the same station density as the long uniform middle and
need far more. Quadrupling the station count on the same part measures it:

| | claims whole | band | worst | at |
| --- | --- | --- | --- | --- |
| as shipped | 490 | 0.2527 | 0.1724 | 2% |
| 4x stations | 1928 | 0.0459 | 0.0199 | 99% |

The deviation falls 8.7x, and it falls *relative to the band* as well - 68% of it
down to 43% - so this is not simply both numbers shrinking with the mesh. Four
times the stations also claims four times the facets whole.

So the guidance for a generator declaring its own sweep: **sample where the shape
moves, not uniformly in the sweep parameter**. A run-out that ramps over 5% of
the sweep deserves something like the station density of the other 95%, not 5%
of it. Nothing needs to change in the exporter for that - the stations are the
model's to choose, and the report above is how to tell whether the choice was
good.

Worth knowing what this is *not*. Interpolating the grid as a **surface** rather
than as rails overshoots at the ends far harder: a mock-up of this fit measured
0.378 against a facet sagitta of 0.109, which is worse than the facets it
replaced at the one point that mattered, while being thousands of times better
everywhere else. `GridSurface::splineForm` writes `degree_v = 1` and solves the
poles one rail at a time precisely to avoid that, and the numbers above are what
remains once it has been.
