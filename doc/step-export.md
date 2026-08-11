# STEP export

Notes on the STEP exporter: the defects that were fixed and why they happened,
the checks that now guard them, and what a next round of work would look like.

Written after a session that started from "the STEP output has gaps and
degenerated faces" and ended with files SolidWorks reads as solids, with
cylinders optionally written as real cylinders.

## Orientation

The exporter lives in three files:

| File | Role |
| --- | --- |
| `src/io/export_step.cc` | entry point: mesh to `StepKernel`, then the ISO-10303-21 header |
| `src/io/StepKernel.cc/.h` | the entity model and everything that decides what gets written |
| `src/io/import_step.cc` | the reader, useful as a round-trip oracle |

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
files to be identical**. Five fixtures in `tests/data/scad/step-export/` each
target one defect — `step-cube` (sharing), `step-bore` (holes and number
formatting), `step-disjoint` (shell splitting), `step-concave` (face normals),
`step-nested-rings` (the membrane).

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
cylindrical wall of the same axis and radius.

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

## How to continue

Ordered by value per unit of work. Each of the first four is independently
shippable behind the same flag, and each extends `validatestep.py` with its own
surface checks.

### 1. Cones

`CylinderNode::createGeometry()` already produces them (`r1 != r2`); the
provenance guard excludes them today. A cone ring is topologically identical to
a cylinder ring — N quads, two circular rims perpendicular to the axis — and
`CONICAL_SURFACE` takes the same `AXIS2_PLACEMENT_3D` plus a half-angle. Seam
construction, `same_sense` and rim collapse are reused unchanged. The fit
generalises from "all vertices equidistant from the axis" to "radius varies
linearly with height". Roughly 150 lines.

Exclude a true cone (`r2 = 0`) at first: its apex is a degenerate rim needing a
`VERTEX_LOOP`. Chamfers and countersinks are frusta and do not hit this.

### 2. `rotate_extrude` declaring its own surfaces

`RotateExtrudeNode` keeps its `profile_func`, so it can recognise that its own
profile is a circle (torus) or a line segment (cylinder or cone) and declare the
surface directly. This is provenance work rather than geometry work, and it
widens coverage well beyond the `cylinder()` primitive.

`TOROIDAL_SURFACE` is doubly periodic and needs two seams, so the emission side
is more work than the declaration side.

### 3. Fillet Bézier patches

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

### 4. Spheres

`SphereNode` builds a `num_rings × num_fragments` lat/long grid, and
`SPHERICAL_SURFACE` exists. The poles are degenerate triangle fans, so a full
sphere needs a seam plus two pole singularities. Common primitive, moderate
work, no new concepts beyond the pole handling.

### 5. Trimmed faces

The real prize, and a project rather than a step.

Provenance survives booleans but the geometry gets *cut*. A cylinder intersected
by another produces a partial cylindrical face bounded by an intersection curve
— in STEP a `SURFACE_CURVE`, usually approximated by a B-spline. The current
implementation sidesteps this by accepting only full closed rings whose rims are
complete face bounds.

In the bayonet container, 6 of the 15 detected cylindrical rings are full rings;
the other 9 are walls interrupted by the bayonet channels. Handling those is
what separates "cylinders survive if nothing touched them" from "analytic
geometry survives modelling", and it is a larger jump than cones, spheres and
tori combined.

## Method notes

Two habits earned their keep and are worth repeating on this code:

**Prove the test catches the bug.** Building the exporter at the commit before
each fix, and confirming the new check rejects that output, is cheap and turns
"this test looks right" into evidence. It caught a validator that silently
missed two of the seven mutations it was supposed to detect.

**Prefer the cheapest experiment that discriminates.** The question "can
cylinders be recognised in post-boolean meshes?" was answered by a
~250-line Python script run over existing exports (1e-14 residuals on a
1685-face model) before any C++ was written. The same script, run on a cube,
produced the zero-residual false positive that reframed the whole feature.

Two diagnoses in this session were confidently wrong and were corrected by data
rather than by argument — the orphaned-hole theory of the membrane, and a
missing UCRT DLL that was never missing. Both times the correction came from the
user running the thing and pasting the output.
