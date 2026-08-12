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
of the exporter so it can be reused and tested on its own.

*What generalises* and *What is actually left in the bayonet*, below, are the
parts worth reading before starting a fourth.

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
files to be identical**, then a third time with `PYTHONSCAD_STEP_ANALYTIC=1` so
the analytic path is covered by the same invariants as the faceted one. Seven
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

## Analytic geometry

Behind `PYTHONSCAD_STEP_ANALYTIC=1`, a ring of facets is written as one
`ADVANCED_FACE` on a `CYLINDRICAL_SURFACE` bounded by a `CIRCLE` at either rim.
A tube of 34 faceted faces comes out as 4. Without the variable the output is
unchanged, byte for byte.

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
`PYTHONSCAD_STEP_ANALYTIC=1` and validates that too, so the analytic path is
covered by the same invariants as the faceted one.

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
with `PYTHONSCAD_STEP_ANALYTIC=1` and asked why the bore came out as a cylinder
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

- **The body wall is not declared, because `hull()` drops provenance.**
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

### Two traps that will recur

- **Never choose the same thing twice from two sets of coordinates.** Both ends
  of a seam were picked independently as the rim vertex of smallest angle;
  `atan2` has its branch cut at pi, an even sided polygon has a vertex exactly
  there, and the sign of a coordinate which is zero to fifteen digits decided
  which side it fell on. One wall of five was dropped. Derive the second from
  the first.
- **Grow a region by the surface, not by adjacency.** A band grown by crossing
  edges into any quad walks straight through a rib welded to the wall and out
  the far side. Fit the surface from the seed's neighbourhood first, then admit
  only facets which lie on it.

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

### 0. The two changes above - done

`step-shared-arc.scad` is the fixture for the second; the first needs none,
because `step-partial-cylinder` already contains the shape that provoked it and
simply recognises more of it now.

One adjacent case was deliberately left: the *probe fit* that seeds the
constrained walk still takes its four vertices from the first three facets of
the unconstrained walk, which is the same weakness the axis had. Substituting
the seed's own neighbourhood there changed nothing measurable on the bayonet,
so it was not made blind - but it is the obvious first suspect if a wall that
plainly fits is ever missing again.

### 1. `rotate_extrude` declaring its own surfaces

`RotateExtrudeNode` keeps its `profile_func`, so it can recognise that its own
profile is a circle (torus) or a line segment (cylinder or cone) and declare the
surface directly. This is provenance work rather than geometry work, and it
widens coverage well beyond the `cylinder()` primitive.

Half of this is now free. A `rotate_extrude` of a **line segment** produces
exactly the band the recogniser already handles - a cylinder when the segment is
parallel to the axis, a frustum when it is not - so for that case there is no
emission work at all, only the declaration. Start there: it is a few lines and
it widens coverage past `cylinder()` immediately.

`TOROIDAL_SURFACE` is the real work in this item, and it is emission work: it is
doubly periodic and needs two seams, so the loop is not the four edge one every
face has used so far.

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
B-spline patch can be bounded at all. That generalisation is the bulk of the
item, and it is shared with anything else non-circular that follows.

### 3. Spheres

`SphereNode` builds a `num_rings × num_fragments` lat/long grid, and
`SPHERICAL_SURFACE` exists. The poles are degenerate triangle fans, so a full
sphere needs a seam plus two pole singularities.

Two warnings, both learned from the cylinder/prism problem one level up:

- **A sphere is not a band.** Its facets span many rings, not two rims, so the
  strip walk does not describe it. It needs its own grower, or the band walk
  generalised to a grid.
- **The fit cannot tell a sphere from a stack of cones.** One ring of a lat/long
  sphere has each of its rims at a constant radius and the two radii differ,
  which is exactly what a frustum band looks like; the facets are chords of the
  sphere, but nothing in the current test notices. Today only the intent gate
  stops a sphere coming out as a pile of cones — no `cylinder()` declared those
  radii. The moment `SphereNode` declares anything, that protection weakens, so
  the sphere's own declaration has to be matched *before* the cone rule gets a
  chance at those bands.

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

Two diagnoses in this session were confidently wrong and were corrected by data
rather than by argument — the orphaned-hole theory of the membrane, and a
missing UCRT DLL that was never missing. Both times the correction came from the
user running the thing and pasting the output.
