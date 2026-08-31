# STEP export: status

A status assessment of the STEP exporter, taken against `doc/step-export.md` and
checked against the tree rather than read off it.

- **Basis:** written against `doc/step-export.md` as of `e764969` and the tree
  at `0e8ab94` (`claude/step-export-feature-detection-7e75pq`, upstream master of
  2026-08-15), and maintained since. §9 and the F1 rewrite are from
  `claude/pythonscad-step-export-next-lmcu01`, upstream master of 2026-08-31.
- **Method:** it began as a static reading of the four exporter files and the
  test wiring, plus two things actually run - `scripts/step-analytic-probe.py`
  and `tests/validatestep.py` over the two committed exports in
  `examples/step_test/`. A headless build was made later in the same exercise
  (Manifold + CGAL, no Qt), so most runtime claims are now measured rather than
  read; §5 says exactly which are not.
- **Every number below is either a line reference or one run of a script named
  next to it.** Nothing here is restated from the doc without a check.

## Headline

The faceted path is finished and guarded: twelve checks in `validatestep.py`,
thirty-one fixtures, one check per historical defect, and every fixture now
asserts the exporter's own report rather than only its validity. The analytic
path — behind `step-analytic-surfaces`, still off by default — writes cylinders,
cones, spheres, tori and B-spline patches. On the reference model the recogniser
is within 36 facets of the geometric ceiling, reproduced here.

**A build exists in this environment now** (headless: Manifold + CGAL, no Qt),
which changes what this document is. It began as a static reading with two
Python tools run against committed exports; §5 listed what could not be checked.
Most of that list is now measured. Where a number appears below without a
"doc-asserted" caveat, it came out of a run.

Two things are true of the current state that the doc does not say:

1. **Item 2's emission has landed.** The doc's *Where this item stands* still
   reads "what remains is entity writing"; `StepKernel` writes
   `B_SPLINE_SURFACE_WITH_KNOTS` and `B_SPLINE_CURVE_WITH_KNOTS` today, and the
   validator gained `check_bspline_faces` with its own mutation harness.
2. **A committed analytic export of a real model does not pass the project's own
   validator.** `examples/step_test/lid10.stp`, written by pythonscad on
   2026-08-13, has 94 edges used by one face. That was finding F1 below, the only
   open correctness question in this assessment. Re-exported on a build with the
   fix it validates cleanly - but the path that was fixed did not fire, so the
   symptom is closed and the attribution is not (see F1).

## 1. Capability ledger, as verified in the tree

| Capability | Evidence |
| --- | --- |
| `CYLINDRICAL_SURFACE`, `CONICAL_SURFACE` | `src/io/StepKernel.h:320`, `:454` |
| `SPHERICAL_SURFACE`, `TOROIDAL_SURFACE` | `src/io/StepKernel.h:408`, `:364` |
| `B_SPLINE_SURFACE_WITH_KNOTS`, `B_SPLINE_CURVE_WITH_KNOTS` | `src/io/StepKernel.h:505`, `:539`; emission at `src/io/StepKernel.cc:649-720` |
| Declarable surface types | `SphereSurface`, `TorusSurface`, `BezierPatchSurface`, `CylinderSurface` — `src/geometry/Surface.h:66,89,133,167` |
| Model-level declaration, both languages | `declare_cylinder` / `declare_sphere` / `declare_torus` / `declare_grid` — Python at `src/python/pyfunctions.cc:990-1010` plus the object methods at `:1060`, OpenSCAD builtins in `src/core/DeclareSurfaceNode.cc`. The rules a declared grid must satisfy live once, in `GridSurface::fromRows`, because both front ends must reject the same grids for the same reasons |
| Recogniser separated from the format | `src/geometry/AnalyticFeatures.cc` (1408 lines) against `src/io/StepKernel.cc` (1114) and `src/io/export_step.cc` (94) |
| Validator | 12 checks: real literals, references, units/context, directions, topology, shared vertices, face normals, hole nesting, cylindrical faces, B-spline faces, shell volumes, shells (`tests/validatestep.py`) |
| Fixtures | 22 SCAD in `tests/data/scad/step-export/`, 9 Python in `tests/data/pythonscad-step-export/`, both wired by glob at `tests/CMakeLists.txt:1162-1165` |
| Feature gate | `src/Feature.cc:53` |

The three-gate model the doc describes (geometry / intent / topology) is visible
in the code as written: the recogniser returns its report as data
(`AnalyticFeatures::Result::report`) and the caller prints it
(`StepKernel.cc:365`), which is what keeps a rejected surface from being
invisible.

## 2. Roadmap ledger

The doc's numbering, with the tree's answer beside it.

| Item | Doc says | Tree says |
| --- | --- | --- |
| 0 — local axis, shared partial rims | done | confirmed: both are the probe's default behaviour (§3) |
| 0b — what the primitives declare | done | confirmed; `step-cone-primitive`, `step-pie-slice` present |
| 1 — `rotate_extrude` declaring surfaces | half done (line segment) | **done**: the segment half was there; the arc half arrived with the Arc2d channel, as `TOROIDAL_SURFACE` per profile arc (`step-rounded-profile`) |
| torus | done, one `TOROIDAL_SURFACE` | confirmed: `TorusSurface`, `TOROIDAL_SURFACE`, `step-torus` |
| 3 — spheres | done, one `SPHERICAL_SURFACE` | confirmed: `SphereSurface`, `SPHERICAL_SURFACE`, `step-sphere` |
| 2 — fillet B-splines | recognition done, "what remains is entity writing" | **further along than the doc**: emission, validator check and mutation harness all landed (`1310d1a`, `8e96e6c`, `tests/bspline-check-mutations.py`) |
| 4 — trimmed faces | last; 14 faces of the bayonet | **measured and blocked**: still 14 faces (0.8%, 803.6 of area), and every one of them borders faceted geometry with no analytic surface, so there is no exact trim curve to write. Blocked on item 5, not on effort — see §10 |
| 5 — swept surfaces | blocked on a user-facing declaration | **measured, and closed as far as declarations can take it.** `declare_*` exists in both languages, but on the reference part a declaration recovers *nothing*: 99.8% of the uncovered area is one helical thread built as a hand-written `polyhedron` (§6) |

Item 5 changing category is the most consequential ledger movement and the doc
does not register it. Its stated blocker was that a `polyhedron()` sweep has no
generator to speak for it; `7ec41de` gave the model itself a voice. What remains
is not a channel but a measurement: how much of the bayonet's 59% a hand-written
`declare_*` can actually recover, given that a helical thread is not a surface of
revolution at all and never will be.

## 3. Coverage, re-measured rather than quoted

One run of `scripts/step-analytic-probe.py` over
`examples/step_test/bayonet_container_v1-2.stp` (a faceted export — 1693 `PLANE`,
no analytic entity, so it is the question and not the answer):

```
2168 vertices, 1693 loops (8 holes)
664 facets (39.4% of outer loops) lie on 14 distinct surfaces of revolution
26 bands fit exactly (664 facets)
25 survive the rim rules (628 facets replaced)
rejected: 36 facets in 1 band — the rim borders one face per facet
```

Three things follow, all confirming the doc:

- The **ceiling** (39.4% on 14 surfaces) is exactly as documented.
- The exporter is **within 36 facets of it**, and the single rejection is the
  r=78.1 socket wall whose lower rim is crossed by 36 separate faces of the hose
  thread — the case the doc attributes to item 5, correctly.
- **Item 0 is in the probe's default path**: running it with
  `--local-axis --shared-arcs` produces byte-identical output, so those flags are
  now historical.

`examples/step_test/lid10.stp` was *not* used for a coverage number, because it
is an analytic export (4 `CYLINDRICAL_SURFACE`, 4 `CONICAL_SURFACE`, 12 `CIRCLE`)
and the probe replays the recogniser — measuring it would be measuring the
answer. It is used below for validity only.

## 4. Open findings

Ranked by what they cost. F1 is the only one that is a question about
correctness; the rest are hygiene and drift.

### F1 — a committed analytic export is not a closed shell — **closed, on the third attempt**

`tests/validatestep.py` over `examples/step_test/lid10.stp`
(`FILE_NAME` says pythonscad, 2026-08-13):

```
94 edge(s) used by only one face, e.g. #1438 (shell is not closed)
#49832 / #49835 / #49999 / #50381: hole lies outside the outer bound of its face
#50451: hole 20954 is not directly inside this face (its face is left sealed)
```

Characterised further: of 4519 `EDGE_CURVE`s, the 94 used once are **all `LINE`s,
and all 94 single users are `PLANE` faces** — 61 distinct ones. The file's eight
analytic faces and twelve circles are clean. So this is not the analytic path
failing; it is the faceted planar remainder of that model. It looked at first like
the same class as the fillet non-manifoldness fixed in `6a457d2`, and it is not -
that was a shell split along near coincident vertices, this one is missing faces.
Measuring which of the two it was is what led to the line responsible.

Why it matters: every fixture in the suite is a small synthetic part, and none of
them reproduces this. A real model does.

**Diagnosed and fixed.** Three measurements narrowed it to one line. All 94 are
`LINE`s whose single users are `PLANE` faces, so it is not the analytic path. The
nearest other open edge to each is 0.2 mm away rather than 1e-16, so it is not a
duplicate-vertex crack like the one every filleted body had — faces are absent,
not doubled. And the exporter had exactly one path that removed a face after
accepting it: a merged loop whose winding disagreed with `faceNormals` was taken
for a hole boundary, and when no coplanar loop enclosed it, it was marked invalid
and dropped (`orphan_cnt`). Nothing enclosing it is precisely the evidence that it
is not a hole; it is now kept as an outer bound, reversed to agree with the mesh
normal only when its winding is what marked it. See *The dropped loop* in
`doc/step-export.md`.

The fix is reasoned from the artifact, not run: confirming it is one export of
`examples/step_test/lid10.scad` and one validator run, and `examples/step_test/README.md`
carries the two commands. If the reversal decision were ever wrong the validator
catches it — a backwards face fails the winding check, and an inconsistently
wound one fails the edge-use rule.

**That confirmation was never done, and the fix above was not the end of it.**
Marking this fixed was premature in two ways. The re-export said to validate
cleanly was of the model's *default* component rather than the lid — `lid10.scad`
selects its part through `lid10.json`, and without `-p`/`-P` it renders something
else entirely. Exported as shipped, it still failed. And the committed artifact
itself was never regenerated, so it sat in the tree failing the project's own
validator for the whole time this section claimed otherwise.

What was actually wrong took three defects, none of them the dropped loop above:

1. **T-junction slivers.** The mesh is manifold and contains 26 zero-area
   triangles of three collinear points, which is how a mesh stitches a vertex
   lying inside another face's edge. The exporter rightly refuses to write a
   face with no normal, and wrongly *dropped* it — reopening every junction, for
   48 edges used once. A sliver's span is now split at the vertex it was holding.
2. **The merge warped flat input.** 6780 planar triangles in, 26 non-planar
   merged faces out, worst 0.1636. The hole re-insertion in `mergeTriangles`
   moved triangles between planes on a 2.56 degree normal test, which at this
   part's radius of 78 permits 3.5 of out-of-plane error. Those arrive wound the
   other way, which is where the reversed loops came from — so the pass this
   section credits was treating a symptom.
3. **The containment probe.** Both exporter and validator asked which face
   encloses a hole using the hole's *first vertex*, which on a T-junction mesh
   sits exactly on the parent's boundary, where an even-odd ray is ambiguous.

With all three, `examples/step_test/lid10.stp` regenerates and validates:
53417 entities, 1977 faces. The artifact in the tree is now that export rather
than the 2026-08-13 one.

The lesson worth keeping is not about geometry. Every number in this section was
correct; the conclusion was wrong because the confirming run was skipped and the
section still said **fixed**. A finding is closed by a measurement, not by a
diagnosis — and where a model carries a parameter set, exporting it without one
measures a different part.

### F2 — the doc understates item 2, and a code comment contradicts the code — **fixed**

`doc/step-export.md`'s *Where this item stands* ends "what remains is entity
writing: `B_SPLINE_CURVE_WITH_KNOTS` per curved run … and the validator checks
for both". All three exist. `src/io/StepKernel.cc:366` also still reads "Bezier
patches are found but not yet written", immediately above a path that writes
them. Both are five-minute edits, and both are the kind of drift that makes a
reader re-derive what the code already says.

### F3 — one commit is outside the repo's own convention — **addressed**

`05d202a` is titled `add test`. `CLAUDE.md` makes Conventional Commits a
release-automation requirement and a commit-msg hook is supposed to enforce it,
so this one either bypassed the hook or the hook is not installed on that
machine. The same commit adds `examples/step_test/lid10.{scad,json,stp}` —
2.9 MB. `grep` finds no reference, which was misleading: `tests/CMakeLists.txt`
globs `examples/**/*.scad` recursively, so both models were being rendered by
dump-examples, render-*, preview-* and throwntogether-* — 16 tests failing for a
baseline they should never have needed. They are excluded from that glob now. It is a useful artifact, which is an argument for wiring it in or
saying what it is for, not for leaving it untitled.

`examples/step_test/README.md` now says what both artifacts are, which one the
probe reads and why it has to stay faceted, and that neither is a known-good
reference — with the reason each one fails the validator. The commit message
itself is history and stays as it is.

### F4 — two test programs cannot run where they are — **fixed**

- `tests/bspline-check-mutations.py:13` hardcodes
  `sys.path.insert(0, '/home/user/pythonscad/tests')`. It passes here (all five
  mutations behave: two accepted, three rejected) and will fail on any other
  checkout or in CI.
- `tests/bezier-patch-check.cc` has no build target — `OpenSCADUnitTests` does
  not appear in `tests/CMakeLists.txt` at all, which is the incidental finding
  the doc records and which is still true. The 3754-vertex verification it
  performs is therefore not run by anything.

### F5 — the probe's reference input fails the validator — **documented**

`bayonet_container_v1-2.stp` (2026-08-10, faceted) fails one check: the hole
nesting that catches the membrane. That is consistent with it being an export
from before the membrane fix, and it does not affect its use as probe input —
the probe reads loops, not validity. But the repository's canonical measurement
input being a file the repository's own validator rejects deserves a sentence in
`examples/step_test/` saying so, or a regenerated file. It has the sentence now,
in `examples/step_test/README.md`.

### F6 — `fillet()` is non-manifold on every non right dihedral — **fixed**

Found while looking for a refusal fixture for item 6, and it is not a STEP
problem at all. `fillet()` on a body whose faces do not meet at right angles
produces an open shell:

| model | single-use edges, analytic path **off** |
| --- | --- |
| `cylinder(r=10, h=10, fn=6).fillet(1, fn=8)` | 168 |
| `cube(10, center=True)` sheared by 0.4, `.fillet(1, fn=8)` | 112 |

The analytic path is not involved - those numbers are with it switched off
entirely - and a cube, a non-cubic box, a square prism and a box turned through
three arbitrary angles are all clean, so this is specifically the non right
dihedral. It is the same *class* as the defect `6a457d2` fixed for the shared
rail (two ways of computing one point disagreeing in the last place), but not
the same instance, since that one is fixed and this survives it.

Two things followed. It was a real bug in PythonSCAD's own feature, ahead of
anything else in this document in user-visible cost: a filleted hexagonal prism
was not a solid, in STL as much as in STEP. And it was what made item 6's
refusal path untestable by fixture.

**Diagnosed and fixed, and the cause is one line of frame.** `bezier_patch()`
replaces the corner's three direction vectors with axis aligned ones of the same
length, does its arithmetic in that frame, and shears the result back with a
matrix whose columns are the real directions. That is exact for the control
points - an affine combination commutes with a linear map - and wrong for the
*weight*, because `cos(theta/2)` is measured between tangents and a shear
changes the angle between them. On a cube the matrix is a signed permutation and
nothing moves, which is why this only ever showed where the three edges are not
mutually perpendicular: there the corner drew the image of a circle, an ellipse,
while the edge strips meeting it drew true circles in world coordinates. The two
agreed only at their shared endpoints and left a lens between them.

The fix measures every weight in world coordinates and passes it in explicitly
(`BezierW`), leaving the points in the frame that makes the coordinate mix
meaningful. The corner's rails then become the same expression on the same
inputs as the strips' rails - the "one arithmetic, one answer" rule that
`6a457d2` established for the shared rail, applied one level up to the curve
rather than the point.

Measured:

| | before | after |
| --- | --- | --- |
| hex prism, edges used once | 168 | **0** |
| sheared cube, edges used once | 112 | **0** |
| hex prism corner boundary vs the strip's rail | up to 0.024 apart, and not a circle | the same vertices |
| hex prism, arc radius at a corner | best fit residual 3.2e-3 | exact circles at sqrt(3), the analytic answer for a 120 degree dihedral at tangent length 1 |
| hex prism, sharp edges remaining | - | 0; every kink is tessellation, at most 14.3 degrees at fn 8 |

**Nothing else moved.** All six models of `tests/data/pythonscad/fillet.py` are
byte identical before and after, as are the cube and oblique box fixtures - the
change is inert wherever the frame was already orthogonal, which is every right
angled body. (`pythonscad_fillet` and `pythonscad_fillet_csg` do fail in this
container, but they fail identically on a build without this change: they are
image comparisons against a different rasteriser, like the PDF suite in §5.)

With the shape exportable, item 6's refusal path has a fixture after all:
`step-fillet-refusals.py` is that hexagonal prism, asserting all thirty patches
stay splines and none becomes a quadric.

**One consequence, and it is a coverage loss worth naming.** Correcting the mesh
moved it away from the declaration, which had been matching the wrong surface.
The declared net's weights are separable, which forces the patch's two u-rails
to share a middle weight; they do exactly when the third direction is
perpendicular to the other two. Every prism and extrusion satisfies that, so the
hexagonal prism still declares all thirty patches. A corner where *no* pair is
perpendicular does not, and its corner patches are now refused:

| | before (open shell) | after (closed) |
| --- | --- | --- |
| hexagonal prism | 30 patches, invalid | 30 patches, 38 faces, valid |
| sheared cube | 20 patches, 26 faces, **invalid** | 12 strips, 8 corners refused, 410 faces, **valid** |

An invalid file is worth nothing at any face count, so this is the right trade,
and the refusal prints one line per patch rather than going quiet. Declaring a
fully skewed corner exactly wants a non-separable weight net - the storage for
which `BezierPatchSurface` already has - and that is a modelling question, not
an exporter one. It is the natural follow-on to this fix.

### F7 — every rational B-spline face was unreadable to a CAD kernel — **fixed**

Found by the round trip on its first run, and it is the exact defect that
exercise existed to find: a file that passes all eleven of this project's checks
and which no CAD kernel can use.

`B_SPLINE_SURFACE_WITH_KNOTS` takes its arguments as `(u_multiplicities,
v_multiplicities, u_knots, v_knots)` - every multiplicity, then every knot. The
exporter has two branches for it, and only one was right. The polynomial branch
wrote `(3,3),(2,2),(0.,1.),(0.,1.)`. The rational branch, used for every fillet
patch since the Beziers went rational, built the tail out of two per-direction
strings and wrote them **interleaved**: `(3,3),(0.,1.),(2,2),(0.,1.)`.

Every list in it is individually correct, the degrees and the control net are
right, and `validatestep.py` passed it - its knot check looked for the *presence*
of `(d+1,d+1)` anywhere in the tail, which an interleaved tail still satisfies.
OpenCASCADE reads `(0.,1.)` where the schema puts v_multiplicities, fails to
build the surface, and drops the face:

| `step-fillet-refusals` (hexagonal prism) | before | after |
| --- | --- | --- |
| `validatestep.py` | valid, 38 faces, 1 shell | valid, 38 faces, 1 shell |
| OCCT reads | **0 solids**, 8 faces, all planes | 1 solid, 38 faces, 30 BSplineSurface + 8 Plane |

So a filleted body imported as loose surfaces, silently, with nothing in this
suite objecting - which is precisely the symptom that started the whole fillet
investigation in SolidWorks. The two branches now share one string, so they
cannot drift apart again, and `validatestep.py` checks the *order* rather than
the presence, calibrated by mutating a good file back to the old order (all 30
surfaces flagged, and OCCT reproduces the 0-solid symptom on the same file).

Note what this says about the timing. The filleted cube no longer writes any
B-spline at all - item 6 turned all twenty of its patches into quadrics - so the
one model most likely to be round-tripped by hand had stopped exhibiting the bug
a commit before it was found. It survived in every *other* filleted body.

### F8 — the whole export report was invisible in the GUI — **fixed**

`StepKernel.cc` had thirteen `printf` calls and no `LOG`, while the rest of
`src/io/` uses `LOG(message_group::…)` throughout. `printf` goes to stdout, and
a GUI build has no stdout anyone reads - so on Windows and macOS the entire
report was gone: the surface counts, the face counts, and every "left faceted:
…" line.

That is worse than an inconvenience, because this project's design leans on that
report. `AnalyticFeatures.h` says it plainly: *"A band that is never recognised
looks exactly like one that was never there, so a caller which swallows this
loses the only signal that a wall which should have been written was not. Print
it."* It did print it, to a place no GUI user could see. Now on `LOG`, with the
corrections the exporter makes to the mesh - dropped degenerate faces, reparented
holes, kept orphan loops - raised to `Export_Warning` so they stand out from the
running commentary.

### F9 — three fixtures were exporting inside out — **fixed**

Found by the twelfth validator check on its first run, and by nothing before it.
All three of the suite's hand-written polyhedra were built with their faces the
wrong way round:

```
step-t-junction              -1.0000  ->  +1.0000
step-approximate-cylinder    -3763.86 ->  +3763.86
step-approximate-turned      -6076.75 ->  +6076.75
```

OpenSCAD orders a polyhedron's face clockwise seen from outside, so the
right-hand normal of each face points *into* the solid. Written the other way
round the exported solid is inverted.

Nothing noticed because nothing was looking. Winding does not affect topology:
the shells still close, the edges still pair in opposite directions, each face
still agrees with its own surface normal, and the recogniser still finds the
same cylinder and the same sphere. Every `EXPECT:` line in all three fixtures
still holds after the fix, which is the point - the counts they assert were
never sensitive to it.

`step-t-junction` is the one worth remembering: it was added to prove the
T-junction weld, and it was a unit cube of volume -1. A fixture written to
demonstrate a correctness fix was itself wrong in a way the suite could not see.

**The check that found it, and why the obvious one would not have.** Conserving
the *total* volume of an export catches nothing here: the bayonet lid's two
shells sum to the correct 223482 precisely because one carries the sign that
cancels the other. Only the sign of each shell on its own says anything, so
`check_shell_volumes` measures that. It speaks only for shells made entirely of
planes, where the number is exact - the polygon through samples of a curved
face's boundary is not that face, and on the analytic lid that error moved the
total by a third.

## 5. What is and is not verified here

**On OpenCASCADE.** The round trip is optional and skips silently when `OCP` is
absent, which is how every number in §9 came to be measured without it. It is
installed now - `pip install cadquery-ocp` into the interpreter CMake finds,
which on Windows is the one ctest drives the tests with, not MSYS2's - and all
31 STEP fixtures exercise it on every run.

It earned that immediately: it rejected `step-nested-rings` within minutes of
being installed, on a regression `validatestep.py` had passed. The lesson is
worth keeping - the validator checks that a file is well formed, and only a
kernel checks that it is the solid the mesh was.


A headless build was made in this environment - Manifold and CGAL, no Qt, Release
- so most of what this section used to disclaim is now measured. What runs:

- **all 23 fixtures**, each asserting the exporter's own report through
  `EXPECT:` lines, measured on that build rather than transcribed;
- **the sanity driver's three invariants** - the locale-identical re-export, the
  analytic pass validating under the same twelve checks, and the CGAL/Manifold
  declaration-count agreement - on every fixture, every run;
- the unit tests (808 assertions) and the B-spline mutation harness.

Still not verified, and honestly so:

- ~~**every SolidWorks round trip.**~~ **Measured.** This was the largest
  untested claim in the whole exercise, and it is now a test.
  `tests/steproundtrip.py` reads every export back with **OpenCASCADE** - the
  kernel FreeCAD is built on - and asserts it comes back as a closed, valid,
  positive-volume *solid* whose surfaces the kernel recognises by type. It is an
  optional dependency (`pip install cadquery-ocp`) and skips silently when
  absent, exactly as the locale check does.

  It paid for itself immediately: see F7, a defect that made every rational
  B-spline face unreadable to a CAD kernel while passing all eleven of this
  project's own checks. SolidWorks and Fusion specifically are still unopened -
  OCCT is one kernel, not all of them - but "no CAD kernel has ever read one of
  these files" is no longer true.
- **the CGAL-backend quality gap table** under *Known quality gaps*.
- **the full GL regression suite.** The 483-test sweep hangs in this container
  after about 45 minutes with no test process running, which looks like a
  fixture deadlock rather than anything to do with the exporter. A 14-test
  sample across circle, offset, rotate and text-font rendering passes with
  `DISPLAY` set explicitly. Note the trap: `ctest` owns the `Xvfb` lifecycle
  through `tests/virtualfb.sh`, and if the PID file survives while the server
  does not, the fixture concludes it is already running and never exports
  `DISPLAY` - every GL test then fails with "Unable to open a connection to the
  X server". Delete `build/tests/virtualfb.{PID,DISPLAY}` and let ctest start it.
- **the `pythonscadecho` suite**, 34 of whose 36 tests fail here because the
  harness never writes its output file. The values print correctly to stdout and
  match the expected files byte for byte, so this is the harness in this
  environment and not the geometry.

## 6. Why the coverage stops where it does

Every OpenSCAD model is built from closely defined mathematics, so it is fair to
ask why the surface of every resulting face is not simply known. Mostly it
could be. This section says where that reasoning holds, where it fails, and what
the failure costs - it is the argument behind items 5 to 7 of the order below,
and it was not written down anywhere.

### Booleans are not the problem

A union, difference or intersection **creates no new surface**. Every face of
the result lies on a surface of one of the operands; what a boolean creates is
new *edges* - the intersection curves - and new trims of surfaces that were
already there. For the boolean core of the language the ambition is therefore
exactly right: the surface of every face is knowable.

### Three things break it

**Some operations really do create surfaces.** `hull()`, `minkowski()` and
`offset()` produce surfaces present in no operand. This model has one: the
collar in `examples/step_test/bayonet_container_v1-2.scad` is a `hull()` of two
cylinders, and the chamfer cone it generates (`r 81.8..82.4, z 0..0.6`, 60
facets) exists in neither. It is recognised and written today - see below, it
matters more than it looks.

**Some geometry never had the mathematics.** The bayonet's thread, which is
99.8% of everything this exporter leaves faceted on that part, is a hand-written
`polyhedron` over a computed point list (`bayonet_container_v1-2.scad:765`):

```
steps = max(24, round($fn*turns));
points = [ for (i = [0 : steps]) let (t = i/steps, a = 360*turns*t, ...
```

The script chose the discretisation, and the surface it approximates is not a
named one in any case: the profile is scaled by a run-out factor that varies
along the sweep, following a *tapered* helix. Not a helicoid, not a swept
constant profile, not a NURBS. Nothing was lost in export - the model never held
it.

The eight remaining uncovered faces are the same story told small.
`bayonetChannel` is a second hand-written `polyhedron` (`:835`, again over
`steps`), and the cam ramps at `z 89.25..95` are that polyhedron's own
quadrilaterals - warped quads, written as one face each, with nothing to
collapse and nothing to declare. Both halves of this part's uncovered 34% are a
`polyhedron()` the script computed.

This is the structural difference from a B-rep kernel, and it is not an
implementation gap. In a CAD kernel the exact surface *is* the model and the mesh
is derived from it. In OpenSCAD the discretisation boundary sits **inside the
language**: `$fn` is a user-facing modelling parameter and `polyhedron()` is a
first-class primitive. A fully analytic export is therefore impossible in
general, for any exporter, by construction.

**The exact surface is only half of a face.** A face also needs its boundary
written as curves lying *on* that surface, and two exact quadrics generally meet
in a quartic rather than a conic. STEP can express it - `INTERSECTION_CURVE`,
pcurves, an approximating B-spline - but that is roadmap item 4, and this part
already has 14 faces in exactly that state: surface known, surface recognised,
trim not writable. More surface knowledge moves the bottleneck rather than
removing it.

### What a different declaration channel would buy

Today `PolySet::surfaces` carries *model-scoped hints*, re-checked against the
mesh by fitting. That is what the three gates are for, and it is why a six-sided
prism can be collapsed if a matching declaration happens to be in scope.

Per-face identity is the stronger design, and the plumbing for it already exists
and is proven - it is carrying a different payload. `manifoldutils.cc:72-73` groups
triangles into runs and tags each run with an id:

```cpp
mesh.runIndex.push_back(mesh.triVerts.size());
mesh.runOriginalID.push_back(id);
```

Manifold preserves that id through booleans, and `ManifoldGeometry.cc:196` reads
it back per run to reconstruct each face's **colour**. The runs are grouped by
colour. Grouping them by declared surface as well would give exact per-face
surface identity across arbitrary boolean chains, on the Manifold backend, for
very little: it removes the geometry gate outright and makes the intent gate
exact instead of probabilistic.

**It is not strictly stronger, which is the part worth remembering.** No face of
either operand lies on the collar's hull chamfer, so per-face provenance would
lose a surface the present design writes. Fitting catches what provenance
cannot; provenance catches what fitting cannot. Both channels, not a
replacement.

### The idiom matrix

"How generic is this?" is answerable by measurement rather than by argument, so
here it is measured: every common way an OpenSCAD model makes a curved surface,
exported analytically on the headless build. What collapses is a fixture; what
does not is *also* a fixture, in `step-extrude-refusals.scad`, because a wrong
declaration is invisible and that is the direction this fails in.

| idiom | result |
| --- | --- |
| `cylinder(r1, r2)` | `CONICAL_SURFACE`, 3 faces |
| `linear_extrude(circle())` | `CYLINDRICAL_SURFACE`, 3 faces |
| `linear_extrude(scale = s, circle())` | `CONICAL_SURFACE`, 3 faces - identical to `cylinder(r1, r2)` |
| `linear_extrude(offset(r =, square()))` | 4 `CYLINDRICAL_SURFACE` + 6 `PLANE` |
| `linear_extrude(rotate(a, circle()))` | collapses - the record rotates with the profile |
| `linear_extrude(scale(k, circle()))` | collapses - uniform, so the radius scales |
| `linear_extrude(difference(circle, circle))` | both walls collapse, 4 faces |
| `difference(rounded box, cylinder)` | 5 cylinders, 11 faces - the bore is declared by the tool |
| `rotate_extrude(polygon of segments)` | cylinders and cones |
| `rotate_extrude(offset(r =, ...))` | 4 `TOROIDAL_SURFACE`, 8 faces from 1088 facets |
| `rotate_extrude(angle < 360)` | partial bands, 6 faces |
| `sphere()` | one `SPHERICAL_SURFACE` |
| `rotate_extrude(circle())` | one `TOROIDAL_SURFACE`, 1 face from 1024 facets |
| `linear_extrude(twist =, ...)` | **refused** - a helicoid |
| `linear_extrude(scale = [a, b], ...)` | **refused** - a general ruled surface |
| `linear_extrude(scale([a, b], circle()))` | **refused** - an ellipse before it is swept |
| `linear_extrude(v = oblique, circle())` | **refused** - an oblique cylinder |
| `linear_extrude(scale = s, translate(circle()))` | **refused** - an oblique cone |
| `linear_extrude(scale = 0, circle())` | faceted - an apex, not a second rim, as `cylinder(r2 = 0)` |
| `linear_extrude(text())` | the font's own Beziers, as `B_SPLINE_SURFACE_WITH_KNOTS` |

Every refusal is a surface that exists and is exactly describable; what it is not
is a *quadric*, and this exporter's rule is exact fit or stay faceted. Three of
them - the oblique cylinder, the oblique cone, the elliptical cylinder - would be
written by `SURFACE_OF_LINEAR_EXTRUSION` over the profile's own curve. That entity
was §7's item 2 and is measured there as worth nothing on any model in the tree;
these are the cases that would change that, and none of them appears in a fixture
that was not written to provoke it.

`linear_extrude(text())` was the one gap that was neither a refusal nor covered,
and it is covered now. Glyph outlines *are* curves - `FreetypeRenderer`
decomposes them through `line_to`, `conic_to` and `cubic_to` - and
`DrawingCallback` discretised them one line after holding the control points,
exactly as `circle()` used to do with its radius. `Bezier2d` records them there.

Extruded, such a segment sweeps a patch of degree (n, 1) whose control net is
the segment's own control points at each end of the sweep: exact rather than
fitted, because a Bezier and a linear sweep are both affine in their control
points. It needed **no recogniser work at all** - `BezierPatchSurface` already
takes a general degree and `recogniseBezierPatches` already accepts any declared
patch, collects the facets on it and classifies its boundary. A glyph wall is the
same shape of thing as a fillet strip, which is what that machinery was built
for.

| | patches | written | faces |
| --- | --- | --- | --- |
| `text("S")` | 32 | 32 | 68 → 36 |
| `text("O")` | 19 | 19 | 42 → 21 |

The consumer needs *fewer* refusals than the arc one, because a Bezier maps
through any affine transform by its control points: the shear, the non-uniform
scale and the oblique `v` that cost a circle its record all leave a Bezier a
Bezier. Even an uneven `scale` works - the station at t is the profile scaled by
lerp(1, s, t) and the patch's own linear interpolation in v gives lerp(P, sP, t),
the same points. Only `twist` is refused, a rotation by t times the angle not
being linear in t.

One real gap turned up on the way. `recogniseBezierPatches` skipped hole loops
when resolving what a patch borders, so every patch around the counter of an O
found one neighbour instead of two and stayed unresolved - the letter kept eight
of its nineteen. A patch's *facets* are always outer bounds, but what it borders
may be a hole; the band path had always used every valid loop for that lookup and
the emitter already substitutes into one, so the patch path was inconsistent with
both. Most of the alphabet has a counter, so this was most of the alphabet
quietly half-collapsing.

### The order that follows from this

1. ~~**Exact 2D profiles.**~~ Done, as `Arc2d` on `Polygon2d` - a channel of
   hints rather than an exact-arc geometry, which is what makes it survive
   Clipper: a record the boolean has trimmed to nothing is simply never fitted.
   See item 5 of §7 for what it recovered. What it does *not* do is give the
   trim curves of item 3 below; it names surfaces, not boundaries.
2. **`SURFACE_OF_LINEAR_EXTRUSION` and `SURFACE_OF_REVOLUTION`,** neither of
   which the kernel writes. Both take an *arbitrary* generatrix curve, so any
   `linear_extrude` is exactly the first and any `rotate_extrude` exactly the
   second - exact, not approximated, and far wider than the quadric family.
3. **Per-face provenance through `runOriginalID`,** beside the hint channel.
4. ~~**The glyph outlines**, as a Bezier record on the arc channel.~~ Done - see
   the matrix above. What is left on that line is `rotate_extrude(text())`, which
   revolves a Bezier into a rational surface of revolution rather than a patch,
   and nothing declares it.

None of the three touches a thread. That one is a policy question - approximate
within a tolerance, or stay faceted - and it is the only place the exporter's
*exact fit or stay faceted* rule has to be decided rather than applied.

## 7. Recommended order

F1 to F5 have all been acted on; what stands below them is the work itself.
Items 5 to 7 are the near work; §6 is the argument for why they are the right
three and what they cannot reach.

1. ~~**Confirm F1 on a real build.**~~ Done, on Windows, along with the rest of
   this branch. What the run established, and what it did not:

   | check | result |
   | --- | --- |
   | `lid10.scad`, analytic, validated | ok — 28645 entities, 1137 faces, 1 shell, no unpaired edges |
   | reversed loops kept | **none reported**, so the fixed branch never ran and F1's cause is still unattributed |
   | `step-fillet` | 26 faces instead of 1106, 48 of 48 seams agree, 0 runs unresolved |
   | `cube(10).fillet(1, fn=24)` | volume 975.5163, surface 547.3143 against an exact 975.587 / 547.363 |
   | `cube(10).fillet(5.1)` | twelve edges refused with a reason, and the result is exactly `cube(10)` — 12 triangles, 8 vertices, volume 1000.000000, surface 600.000000, every edge used twice |
   | `bspline-check-mutations` | 10 mutations, all as expected |
   | the two STEP models | out of the example suite, 16 failures gone |

   Nothing on this list is open. The `fillet(5.1)` case was the last of them: it
   measured 1532 inside its own 10x10x10 box while every edge was refused and
   every corner rounded anyway, and it is now the cube it should be.
2. ~~**Make `FilletNode`'s Beziers rational.**~~ Done, with the maintainer's
   agreement that it is what the TODO in `Bezier()` intended. `fillet()` drew
   parabolas, out by 6% of the radius at a right angle and 25% at a 60° dihedral,
   with the corner patch 9.5% off the sphere. The Bezier substrate stayed - it
   needs no axis and survives non-perpendicular faces and a varying radius - and
   the control points did not move: the weight is `cos(θ/2)` computed per cross
   section from the actual tangents, which makes each quadratic an exact circular
   arc. Measured on the Windows build, `cube(10, center=true).fillet(1, fn=24)`
   is volume 975.5163 / surface 547.3143 against the exact Minkowski 975.587 /
   547.363, where the parabola gave 980.889. The rational form reaches STEP as a
   `RATIONAL_B_SPLINE_SURFACE` complex instance, which the validator and the
   mutation harness both check.

   What this *opens* is item 6 below: those patches are now exact quadrants and
   octants, and the exporter still writes every one of them as a B-spline.
3. ~~**The short-edge collapse in `FilletNode`.**~~ Done, and it turned into two
   passes rather than one. The disabled pass merged two corners of an edge
   shorter than 2r by cutting three of the four planes that surround the pair,
   which is where its damage came from: the residual is linear in the edge
   length with an unbounded factor, so a 0.5 mm edge moved the vertex 1 mm off
   the ignored face and a 0.1 mm edge between shallow end faces sent it 12 units
   away with nothing reported. All four planes now go in, in least squares, and
   the residual decides - which is correct, and which also bounds what that pass
   can ever do: both corners are 3-edge corners, so four concurrent planes mean
   a zero-length edge, and a short edge of non-zero length always has a residual
   (0.2887 times its length on a cube with a cut corner, at every size from 0.4
   down to 1e-9). It cleans up slivers and refuses everything else, out loud.

   What removes a real short edge is collapsing the small **face**: three planes
   through one point, exactly determined. That pass is now there too, ahead of
   the edge one. On the cut-corner cube the intersection is exact - residual 0,
   the mesh manifold, every face planar, the sharp corner restored - at cut sizes
   where every edge collapse of the same shape is refused. The gate is not size:
   all three edges must have been *selected for rounding*, so each is a genuine
   sharp edge between two 3-edge corners and steeper than minang, and all three
   must be shorter than 2r. A tessellated sphere is nothing but faces smaller
   than the radius and never reaches it - its edges are too shallow to be
   selected and its vertices carry more than three faces. Verified against a
   standalone harness compiled from the block itself: collapses at cut 0.4 and
   0.01, declines at 1.9 (edges longer than 2r), declines when the dihedral gate
   excludes the edges, and leaves the mesh manifold and planar in each case.
4. ~~**Re-measure roadmap item 5 now that `declare_*` exists.**~~ Done, with a
   new `regions` subcommand on the probe, and the answer is one-sided enough to
   settle the item: **a declaration recovers nothing on this model.** The split
   the doc assumed - some thread, some ramps and lugs a model could declare - is
   not there.

   `scripts/step-analytic-probe.py regions examples/step_test/bayonet_container_v1-2.stp`:

   | | faces | area |
   | --- | --- | --- |
   | on a surface of revolution | 664 (39.4%) | 62691 (59.1%) |
   | uncovered | 999 (59.3%) | 36434 (34.3%) |
   | planar, perpendicular to the axis | 8 (0.5%) | 6192 (5.8%) |
   | on a quadric, trim not planar | 14 (0.8%) | 804 (0.8%) |

   The first thing the table says is that **59% was the wrong denominator**. It
   is 59% of the *face count*, and a faceted helix is nothing but faces; by area
   the uncovered part is 34.3%, and the part is already 59.1% analytic.

   The second is where that 34.3% is. The 999 uncovered faces fall into 14
   smooth regions, and ten of them are a single face each - a real warped
   quadrilateral, already one entity, with nothing to collapse. Of the four
   faceted regions, one is 977 faces and 36335 of the 36374 remaining units of
   area, at `r 74.24..79.38, z 1.20..75.00`. That is the hose thread, exactly:
   `_hoseThreadTurns` 3 times `_hoseThreadPitch` 25 is the 75 mm. The other
   three regions are its run-out at z<1.2 and total 39 units, 0.04% of the part.

   The eight single-face regions above the thread are the bayonet cam ramps at
   `z 89.25..95`, 7.5 units each: warped quads, one face apiece, already written
   as one entity. There are no lugs to declare.

   The thread is also the cause of the only band the recogniser rejects: the
   r=78.1 wall is dropped because its bottom rim is crossed by 36 faces, and
   those 36 faces are thread facets.

   So item 5 is not a declaration problem and never was on this part. What is
   left is a *helical* surface family, and that is a bigger decision than it
   looks: a circular helix is not a NURBS curve - the arc is rational in t but
   the height is proportional to the angle, which is not - so a thread cannot be
   written exactly by the B-spline machinery item 2 built, only approximated
   within a tolerance. The exporter's rule to date is *exact fit or stay
   faceted*. A thread is where that rule has to be decided, not a recogniser.
5. ~~**Roadmap item 1's other half: an arc in a `rotate_extrude` profile.**~~
   Done, and it turned into §6's item 1 - the arc channel - because the gap was
   one level lower than the item said. `Outline2d` is a bare list of points, so
   `circle()` was dead before any extruder saw it; the torus was recovered by
   *reading the node tree* for a `CircleNode`, which stopped at the first
   `difference()` or rotation.

   `Arc2d` on `Polygon2d` is the channel: recorded by `circle()` and by
   `offset(r=)` (one arc per input vertex, plus the operand's own arcs moved by
   the offset), carried through `transform` under a similarity and through
   union, difference and intersection, and consumed by both extruders.
   `linear_extrude` declares a `CylinderSurface` per arc; `rotate_extrude`
   declares a `TorusSurface`, or a `SphereSurface` where the arc's centre sits on
   the axis. `torusOfRevolution()` is gone - 57 lines of node-tree walking whose
   own comment named "a curve channel on Outline2d, which has to survive Clipper"
   as the honest fix.

   The recogniser needed one thing after all: its zone merge accepted a
   *complete* torus only, so a rounded corner - a quarter of one - fell back to
   the exact cone stack. The sphere pass already merged a run with two free ends,
   and generalising it to a toroidal zone was the whole change, plus one test to
   tell a real end of the surface from the *turnaround* where a torus's profile
   doubles back: not whether the end rim is shared, but whether the band across
   it lies on the same torus.

   Measured on the headless build, with the three new fixtures:

   | | before | after |
   | --- | --- | --- |
   | `linear_extrude(circle(r=10))`, fn 32 | 34 faces, declared by hand or not at all | 3 faces, declared by the circle |
   | `linear_extrude(offset(r=3, square))`, fn 60 | 66 planes | 4 `CYLINDRICAL_SURFACE` + 6 `PLANE` |
   | `rotate_extrude(offset(r=2, ...))`, fn 32 | 36 faces (32 exact cones) | 4 `TOROIDAL_SURFACE` + 2 cylinders + 2 planes, 8 faces from 1088 facets |

   All three validate on both backends, so the records survive the conversion to
   a Nef polyhedron as well. `validatestep.py` gained the partial toroidal face -
   two rim circles of latitude and one seam along the tube, against the complete
   torus's two closed seams and no rims.
6. ~~**Write the fillet's exact quadrants as quadrics.**~~ Done, and it needed
   no rim rules after all - a patch already knows its own boundary runs, so the
   whole change is one recovery function, one branch in the emitter and one new
   face shape in the validator.

   `AnalyticFeatures::quadricOfPatch` reads a candidate off the control net in
   closed form - a rational quadratic's centre is the point on the perpendicular
   to its first tangent equidistant from its last - and then **measures** the
   patch against it on a 7x7 grid. That second half is not belt and braces: two
   rails can be concentric circles of equal radius on a common axis and bound a
   *hyperboloid* rather than a cylinder, if one is turned against the other, and
   every test made of centres, radii and plane normals passes on it. The grid is
   also what brings the weights into the test, so the parabola through the same
   points fails rather than being written as the circle it is not.

   The curves had to move with the surface: a quadric face bounded by splines
   off a net is one no importer can check against its own surface, so a curved
   run on a quadric patch is written as a `CIRCLE`. That makes the two patches
   sharing a rail agree about one `EDGE_CURVE`, so a patch whose partner is not
   a quadric withdraws, to a fixed point.

   Measured on the headless build, `cube(10, center=True).fillet(1, fn=12)`:

   | | before | after |
   | --- | --- | --- |
   | surfaces | 20 `B_SPLINE_SURFACE_WITH_KNOTS` | 12 `CYLINDRICAL_SURFACE`, 8 `SPHERICAL_SURFACE` |
   | curves | 48 `B_SPLINE_CURVE_WITH_KNOTS` | 24 `CIRCLE` |
   | faces | 26 | 26 |

   No B-spline is left in the file - entity for entity what SolidWorks writes
   for the same part. And the geometry is checkable rather than a matter of
   comparing pictures: a filleted box is the Minkowski sum of the box shrunk by
   2r with a sphere of radius r, so the eight sphere centres have to be at
   `(+-4, +-4, +-4)` with radius exactly 1, and they are. `step-fillet-oblique.py`
   repeats it on a 14 x 9 x 6 box turned through three angles sharing no factor,
   where nothing is axis aligned, and the centres are the corners of a
   12.4 x 7.4 x 4.4 box to nine decimal places at radius exactly 0.8.

   The validator gained one face shape, and it is a shape rather than an
   exemption: a sphere octant has **three** edges, all great circle arcs, because
   its fourth side is the pole where the patch is degenerate.

   **Where it stops is not where one would expect, and F6 is why it cannot be
   fixtured.** A strip along a straight edge is a cylinder at any dihedral - a
   hexagonal prism's eighteen would all qualify - but a corner between faces
   that are not mutually perpendicular is not a sphere, and the fixed point
   withdraws the strips with it. So a non right angled body writes no quadric.
   That conservatism is guarded by unit tests over the nets rather than by a
   fixture. It was written when F6 meant no such body exported at all; F6 is
   fixed and `step-fillet-oblique.py` is exactly such a body, so a fixture is
   available now and the unit tests are simply where the guard happens to live.
7. ~~**Give the remaining fixtures their `EXPECT:` lines.**~~ Done: all 23 state
   their counts as assertions now, measured on the headless build rather than
   transcribed, and calibrated by mutation - changing step-sphere's 480 facets
   replaced to 479, or step-extrude-text's 32 patches to 31, fails the test and
   names both texts.

## 8. What this session established, and what to do next

### The takeaways worth keeping

**The declaration channel was one level too high.** Every item that looked like a
recogniser problem turned out to be a *channel* problem one layer down.
`Outline2d` is a bare list of points, so `circle()` was dead before any extruder
saw it and a torus had to be recovered by walking the node tree for a
`CircleNode` - which stopped at the first `difference()` or rotation. Giving
`Polygon2d` an `Arc2d` and a `Bezier2d` record closed roadmap item 1's second
half, deleted 57 lines of node-tree walking, and made `linear_extrude(text())`
work with **no recogniser changes at all**. The lesson generalises: when a
surface cannot be written, ask what threw the mathematics away and how far
upstream that was.

**The recogniser was already more general than its callers.** `BezierPatchSurface`
took an arbitrary degree; `recogniseBezierPatches` accepted any declared patch.
A glyph wall is the same shape of thing as a fillet strip, so the machinery built
for `fillet()` wrote extruded text unchanged. Two of the three gaps that *did*
need recogniser work were inconsistencies rather than absences - the zone merge
accepting only a complete torus, and the patch path skipping hole loops the band
path had always used.

**Refusals need fixtures as much as successes do.** The taper bug -
`linear_extrude(scale = 0.5)` declaring a cylinder of a frustum - survived
because a wrong declaration is invisible: the fit rejects it, the export stays
valid, and the report says neither "nothing declared" nor "something recognised".
`step-extrude-refusals.scad` exists for that class, and it asserts the
*availability* line, which is stronger than "nothing was written".

**Face count is the wrong denominator.** The bayonet's "59% uncovered" is 59% of
faces and 34.3% of area, and 99.8% of that area is one helical thread. Measuring
by area turned an open-looking roadmap item into a closed question.

**Two process notes, both learned the hard way.** Never run two `ninja`
processes in one build directory: it corrupts `.ninja_deps`, and because adding a
member to `Polygon2d` changed its size, the stale objects and the fresh ones
disagreed about every offset - which presented as a plain `square(10)` extrusion
breaking on the CGAL backend, hours from anything that could explain it. And
`pkill`-ing `ctest` orphans the `Xvfb` it owns; see §5.

### The recommendation

**Item 6 is done** - a filleted cube now exports as 12 `CYLINDRICAL_SURFACE`,
8 `SPHERICAL_SURFACE` and 6 `PLANE`, bounded by circles and lines, with no
B-spline in the file and the sphere centres exact to nine decimal places. That
was the last item on this list which was both substantial and reachable, so the
recommendation now moves on: **stop adding surface families.**

Two things stand ahead of any further surface work, in this order.

**F6 is fixed** - `fillet()` produced a non manifold mesh on every non right
dihedral, and a filleted hexagonal prism is now a closed solid. That was a
correctness bug in a shipping feature rather than an unverified claim, so it
went first; it also unblocked item 6's refusal fixture.

**The CAD kernel round trip is done, and it found something.** This was §5's
largest gap - nothing in the exercise had ever opened an export in a real
kernel. `tests/steproundtrip.py` now reads every export back through
OpenCASCADE and asserts it is a closed, valid, positive-volume solid whose
surfaces the kernel recognises by type. On its first run it found F7: every
rational B-spline face was unreadable, so a filleted body imported as loose
surfaces while passing all eleven of this project's own checks. That is exactly
the class of thing self-validation cannot reach, and it is why the item was top
of the list.

What the kernel now reads back, one line per surface family, every fixture a
single closed solid:

| declared | OCCT reads | fixture |
| --- | --- | --- |
| `CYLINDRICAL_SURFACE` | Cylinder | `step-bore`, `step-declare`, `step-extrude-circle`, … |
| `CONICAL_SURFACE` | Cone | `step-cone-primitive`, `step-tapered-extrude`, … |
| `SPHERICAL_SURFACE` | Sphere | `step-sphere`, `step-fillet` |
| `TOROIDAL_SURFACE` | Torus | `step-torus` (one face for the whole torus), `step-rounded-profile` |
| `B_SPLINE_SURFACE_WITH_KNOTS` | BSplineSurface | `step-extrude-text`, `step-fillet-refusals` |

And the geometry is exact rather than merely acceptable. A filleted cube is the
Minkowski sum of `cube(8)` with a unit ball, so its volume is computable in
closed form: **975.587014**. OCCT, reconstructing the smooth solid from our
twelve cylinders, eight spheres and six planes, measures **975.587014** - within
1.1e-07. The mesh those entities were recognised from measures 975.5163, so the
analytic export carries *more* geometric truth than the mesh it came from, which
is the whole point of the exercise stated as a number.

**What the kernel is asked, beyond "did it read".** Counting face types says the
kernel read twelve cylinders; it does not say they are the right twelve. A
recovery with a wrong axis or radius writes a surface the mesh is not on and
almost nothing objects - the bounding circles come from the same recovery so
they agree with it, the shell closes, and `validatestep.py` compares the rim
radius against the surface radius, both wrong together. So the fixtures assert
the parameters as well:

| | asserted | reads |
| --- | --- | --- |
| radii | `RADII: Cylinder=1 Sphere=1` | `Cylinder 1, Sphere 1`, axes on the three coordinate directions, four per axis |
| edges | `EDGES: Circle=24 Line=24 degenerate=8` | 24 circles of radius 1, 24 lines, 8 degenerate |

The edge census is the other half of item 6 and had never been verified: a
quadric face is bounded by `CIRCLE`s because that is what a kernel offsets and
patterns along, and nothing checked that a kernel *reads* them as circles. It
does.

The eight degenerate edges look like a defect and are not. The exporter did not
write them: a spherical face is a rectangle in `(theta, phi)` whose fourth side
is the pole, and OpenCASCADE inserts a zero-length edge there itself. Exactly
eight, one at the apex of each octant, is confirmation that putting the polar
axis through the apex was right - it is what keeps the octant inside one
parameter rectangle rather than straddling the seam.

**The cross check, and what it says today.** OCCT's
`ShapeAnalysis_CanonicalRecognition` answers a *different* question from the one
`AnalyticFeatures` asks - it works from the surface rather than from the facets,
so it cannot recognise a tessellated cylinder and cannot replace anything here
(measured: it refuses to call a 4, 6, 8, 16, 32 or 64-gon prism a cylinder at
any tolerance up to 3.0, while recognising a true cylindrical face, and the same
face after `NurbsConvert`, as radius 10.000000). What it *can* do is audit what
the exporter chose to leave as a spline, which is the one direction a missed
opportunity hides in. Every fixture with a B-spline face now reports it:

| fixture | B-spline faces | OCCT says they really are |
| --- | --- | --- |
| `step-extrude-text` | 32 | 32 genuine splines |
| `step-extrude-text-counter` | 19 | 19 genuine splines |
| `step-fillet-refusals` | 30 | **6 exact cylinders**, 24 genuine splines |

The glyph fixtures coming back clean is the useful negative: a font's Beziers are
not quadrics, and nothing there is being left on the table. The prism's six are
the vertical edge strips, radius sqrt(3) - exactly the faces item 6's fixed point
withdraws because each shares a rail with a corner that is not a quadric. So the
follow-on below is worth six faces on that model, and it is now a number from a
third party rather than an estimate from the recogniser being audited.

A count that *grows* is the regression signal, which is why the fixtures assert
it; `step-fillet` and `step-fillet-oblique` assert `BSplineSurface=0` instead,
which says the same thing from the other side - that `quadricOfPatch` left
nothing behind at all. Both are calibrated by mutation.

**Why the reverse-engineering route is not the route.** OCCT has no turnkey mesh
feature recognition, and the pipeline usually assembled in its place -
`Poly_Connect` to grow regions, a fitter to classify them, `BRepLib_MakeFace`
and `BRepBuilderAPI_Sewing` to rebuild - is the right answer to a *different*
question: recovering geometry from a mesh whose generator is gone. Two
measurements say it is the wrong answer here.

The first is that OCCT has no quadric fitter to reach for. `GProp_PEquation` is
the point-cloud classifier, and its entire vocabulary is point, line, plane,
space - there is no `IsCylindrical`. Asked about 64 points lying exactly on a
cylinder it answers `Space`, and `GC_MakeCylindricalSurface` is a *constructor*:
it wants the axis and radius already derived. Deriving them from a run of facets
is precisely what `AnalyticFeatures` does, and it is the part OCCT does not
have.

The second is the intent gate, which this document has argued from the start and
which is now measurable in one line: `GProp_PEquation` gives the **same answer**,
`Space`, for a 32-sided tessellated cylinder and for a hexagonal prism. Every
vertex of a regular prism lies exactly on a cylinder, so a fitter that did know
about quadrics would return one for both. The mesh does not carry the difference;
only the declaration does. A reverse-engineering pipeline would be throwing away
the channel that makes this exporter correct.

What *is* worth taking from that toolbox is the diagnostic.
`ShapeAnalysis_FreeBounds` assembles the edges used by one face into wires, so a
shell that does not close reports where rather than merely that: on the committed
`lid10.stp` it names 18 closed and 6 open free wires with a length and a
position for each, where the round trip previously said only "0 solids from 1860
faces". That is now part of the failure path.

### Approximation: the measurement, behind its own flag

`step-approximate-surfaces` is the second gate, and it needs the first. The
exact pass writes a surface only where the model declared one and the mesh fits
it exactly; this one is about what is left, which on the reference part is 99.8%
of the uncovered area and is geometry OpenSCAD never held the mathematics for.

The pass groups those facets into smooth regions and measures **the band**: how
much room the model's own tessellation leaves for a fitted surface to occupy.
The mesh does not say where the true surface is. It says the surface passes
through these vertices and cannot stray further from the facets than their
sagitta, which for two facets meeting at a dihedral theta across a chord c is
`(c/2)*tan(theta/4)`. A fit inside that band asserts nothing the mesh does not
already allow; a fit outside it is inventing geometry.

That criterion is not theoretical - it comes from getting it wrong. A B-spline
interpolated through this project's own thread mesh overshot **0.378 mm** at the
run-out where the band is **0.109 mm**, and the visible result was a hook at the
end of the ridge. Measured against the vertices it interpolated, that fit scored
1e-13 and looked perfect. Measured against the band it is rejected three and a
half times over. Vertex error is the wrong question; the band is the right one.

Both the widest band and the typical one are reported, because they answer
different questions and the reference part needs both:

| | facets | band | typical | worst dihedral |
| --- | --- | --- | --- | --- |
| `linear_extrude(twist)` wall | 736 | 0.0053 | 0.0051 | 5.7° |
| the bayonet's hose thread | 1016 | 0.7813 | 0.0748 | 24.5° |

The twisted wall is uniformly tessellated, so the two agree to within 4% - one
surface could cover it. The thread does not: it is broadly smooth at 0.075 with
a ten-fold tail, and those bad edges are where the boolean cut the sweep against
the socket wall. Same measure, and it tells them apart.

**The band says how accurate a fit could be. Whether one is available at all is a
different question, and it is the one that settles this.** Fitting needs the
facets' *ordering* - which follows which along the sweep - and no fitting
machinery recovers that from an unordered set; `GeomAPI_PointsToBSplineSurface`
wants a structured grid, and so would anything written by hand. A mesh straight
from a generator still carries it: a swept quad grid, split into triangles the
same way everywhere, gives every interior vertex a valence of exactly 6. A
boolean does not preserve it, because trimming a sweep against a wall
retriangulates the seam.

So the pass measures that too, as the fraction of interior vertices at the modal
valence, and the two models answer the two halves in opposite directions:

| | facets | band | typical | grid regular |
| --- | --- | --- | --- | --- |
| `linear_extrude(twist)` wall | 736 | 0.0053 | 0.0051 | **100%** of 330, valence 6 |
| the bayonet's hose thread | 1016 | 0.7813 | 0.0748 | **36%** of 956 |

That is the answer to fitting-versus-declaring, and it is not the one the
mockup suggested. The mockup fitted the thread beautifully - 1226 facets to 6
faces, every vertex interpolated to 1.2e-13 - but it read `hoseRidge`'s own
154x4 array, not the exported mesh. In the export that ordering is gone: the
union with the socket wall retriangulated it, and 36% regularity means there is
no grid left to walk.

**So: fit where the generator's grid survives, declare where the boolean took
it.** `declare_grid()` is that second half - see below. The thread is 99.8% of the uncovered area on this part and falls on the
declaring side, which puts it back with roadmap item 5 and with §8's standing
lesson - every item that looked like a recogniser problem turned out to be a
channel problem one layer down. `hoseRidge` knows it swept a profile along a
helix; nothing downstream can be told to guess it.

**Nothing is fitted yet, and the pass says so on every run.** That is deliberate:
a pass which silently did nothing would look exactly like one which found
nothing - the same trap the band report itself exists for.

### The declaration channel for what has no name

Every other `declare_*` names a surface: a cylinder of this radius about that
axis, and the exporter checks the mesh against it. A helical thread has no name
to give - the model built it with `polyhedron()` over a computed point list and
the surface has no closed form - so `declare_grid()` carries the other thing
instead, the one the mesh loses and the generator still has: the **order** the
points were swept in.

`GridSurface` holds that grid, and membership is answered twice. A facet whose
every corner is one of the emitted points belongs to the sweep by *position* -
no tolerance, nothing to converge, and that is the exact answer for the facets
the generator itself produced. For every other facet the grid is evaluated as a
surface and its corners are projected onto it.

The interpolant is deliberately lopsided: cubic B-spline along the sweep, where
the samples are dense and the surface is smooth, and ruled - degree 1 - across
the profile, so a thread flank's corners stay corners instead of being rounded
away by a spline that was never told they were creases. Parameters are
chord-length, averaged across columns, with clamped averaged knots; the solve is
one banded `Eigen::PartialPivLU` factorisation reused per column. Projection is
Gauss-Newton from a coarse pre-sample, because a helix passes near itself once
per pitch and a single seed converges to the wrong turn.

Measured on a helical ridge of 356 facets, fused onto a wall the way `hoseRidge`
is fused onto its socket:

| | claimed whole | cut across |
| --- | --- | --- |
| the ridge alone | 356 | 0 |
| the ridge unioned with a wall it clears | 356 | 0 |
| the ridge unioned with a wall that cuts its base, by position alone | 63 | 237 |
| the same, with projection onto the interpolated grid | **348** | **185** |

The first two lines are the channel working: a declaration survives the boolean
intact, which is exactly what the older `Surface` records could not promise for
geometry with no name. The last two are the interesting pair, and the distance
between them was not free.

**The tolerance has to be the tessellation band, not a fitting tolerance.**
Projecting at 1e-7 moved the count from 63 to 66 - three facets, which is to say
nothing. The reason is that the boolean cuts the *faceted* mesh, not the smooth
surface it approximates, so a vertex the trim invents sits on a chord and can be
a full sagitta away from the interpolant. That distance is a property of the
tessellation, not an error: it is exactly what `tessellationBand()` already
reports for the claims line. Using it as the projection tolerance takes the
count from 63 to 348, at a band of 0.0660 on this ridge. A tolerance chosen for
fit quality is the wrong instrument here; the right one is the width of the gap
the tessellation itself left.

So the grid now both carries the ordering and answers membership on it.

**General knots are in place.** `GridSurface::splineForm` hands out the same
surface `evaluate` answers membership against, in the shape a STEP file wants:
degrees, the control net, and each direction's distinct knots with their
multiplicities. That last part is why it had to exist. A fillet's patch is a
Bezier, whose knots follow from its degree, so the exporter synthesised them and
`BSplineSurface` had no way to be told otherwise; an interpolated grid has one
interior knot per interior station, coming from the chord lengths between them,
and nothing already in the file could reconstruct those. `setKnots` is that way,
and both branches of the emission - polynomial and rational - take their four
lists from one place, so the ordering that F7 got wrong cannot drift again.

`validatestep.py` had the same Bezier assumption baked in twice over: it derived
the net's shape from the degrees, and compared the knot tail against a literal
string. Both are now the general contract - the multiplicities sum to control
points plus degree plus one, both ends clamped, values increasing, and the net's
shape read from the multiplicities, which is the only place the file records it.
The positional read of the four lists is kept exactly as it was, because that is
what catches an interleaved tail: a `(0.,1.)` where `v_multiplicities` belongs
still fails, now as a parse rather than a string mismatch.

**The claimed facets are a face's worth of facets.** `recogniseGridPatches`
answers for a grid what `recogniseBezierPatches` answers for a fillet - one
sheet, and its boundary split into the runs that each have to become a single
curve - and it reaches that answer by a different rule, because a declared grid
is not a patch that covers its own parameter square. A Bezier's boundary lies on
the four edges of that square, so the split follows the geometry: this stretch
is `u=0`, that one is `v=1`. A trimmed grid's boundary lies wherever the boolean
cut it, which is exactly why its facets had to be claimed by projection in the
first place. So the split follows the topology instead: **one run per stretch of
consecutive boundary segments with the same face on the far side.** That is the
property a run actually needs - every segment of it is replaced in one
neighbouring face, by one curve, or the shell opens - and the parameter square
was standing in for it all along.

On the reference ridge:

```
1 declared sweep covers 348 facets over 2 boundary cycles,
split into 185 runs of up to 7 mesh edges, 0 unresolved
```

Two things in that line were not free. **The boundary is two cycles**, because a
sweep closed around its profile and trimmed against a wall is an annulus, and
the walk inherited from the Bezier path stopped at the first cycle it closed and
then reported that the boundary did not close - which was true of the walk, not
of the region. A fillet patch is a disc and never produced the case. **The caps
are not swallowed**: a face every one of whose corners lies on the sweep gets
claimed too, and a region that takes its own end caps closes into a shell with
no boundary at all. The unit test builds a tube fanned to a hub at each end for
exactly that reason.

The 185 runs are the 185 cut facets, one run each, none unresolved - so every
boundary of this face has a single neighbour that could replace it in step.

**The surface now covers the whole sweep.** A profile declared closed has one
more span than it has columns - the strip from the last column back to the first
- and no column of the net names it. Without it a four sided ridge had a surface
over three of its sides, and facets on the fourth could be claimed by position,
because the generator emitted their corners, and never by projection. That is
precisely the half a boolean destroys, so the gap was in the worst possible
place. `splineForm` writes the closed case with its first column repeated at the
end, which is the strip `evaluate` covers and no column carries.

**And then the seam is what stands between recognition and a face**, which is
not what it looked like from the run structure. The intuition was that a trimmed
grid's runs are neither rows nor columns of its net - the exporter's rule for a
curve that provably lies on a spline face - so each would need a curve fitted
onto the surface. That is true and it is not the blocker, because the
neighbouring faces here are the mesh's own planar facets: a curve on the sweep
does not lie in a neighbour's plane, so a shared edge between them can only be
the chord it already is. The boundary stays as the mesh has it, and the face is
written on the smooth surface inside it.

The blocker is one layer up. A sweep whose profile is declared closed is a tube,
its surface is closed across `v`, and the exporter measures whether the claimed
region closes around it:

```
its facets lie over 4 of the profile's 4 spans - the region closes around the
profile, so its face crosses the surface's seam
```

A face on a surface written as an open rectangle cannot be bounded across that
seam. A region covering only some of the spans is a strip, whose boundary stays
inside the rectangle - **and those are now written.**

### Written: one face per declared strip

`step-declare-grid-strip.py` is the reference ridge with its profile declared
open rather than closed, which is the whole difference between a region a face
can carry and one that cannot. The export replaces **240 facets with one
`B_SPLINE_SURFACE_WITH_KNOTS` face**: cubic along the sweep with the interior
knots the chord lengths gave it, ruled across the profile, bounded by the mesh's
own straight edges. OpenCASCADE reads it back as one solid of one shell and puts
the volume 0.005% above the faceted export's - the smooth surface standing off
the chords by about the band, in the direction it should.

Three things had to be true first, and two of them were not.

**Membership is a question about facets, not about corners.** Every corner of a
facet closing a declared-open profile is a point the generator emitted, so
position alone claims it while its middle is nowhere near the surface. So is
every corner of a facet the boolean retriangulated across two stations. The
centroid is the cheapest point that is not a declared point, and requiring it on
the surface refuses 108 of them on the strip fixture and 49 on the closed one -
including, at last, the two end caps, which until now the region swallowed.

**One tolerance, not two.** The recogniser asked `onSurface` with the raw
tessellation band while `pointMember` floored it at 1e-7, so a grid whose
interpolant is exact - a sweep along a straight line, where the band is zero -
had the two disagreeing about the same point. `membershipTolerance()` is now the
single answer, and the tube unit test is what caught it.

**The boundary stays as the mesh has it**, which is the design and not a
compromise, and it is where the intuition from the fillet path was wrong. A
fillet's rail becomes an arc because that arc lies in the flat face beside it. A
trimmed sweep's neighbours are the planar facets the boolean left, so a curve on
the sweep lies in none of them, and a shared edge can only be the chord it
already is. Taking the edges from the same map every other face uses is both the
simplest thing and the only one that keeps the shell closed: the neighbour is
asked to give up nothing.

The face is written only under `step-approximate-surfaces`. That gate is honest
rather than cautious: every other analytic face this exporter writes carries a
surface the mesh lies on exactly, and this one is matched to within the grid's
tessellation band - the model's own resolution, and not zero.

The approximation export is now read back through OpenCASCADE too, not only
validated. That file is where the newest geometry is, and `validatestep.py` can
only say an entity is well formed; a kernel says whether a face builds from it.
F7 is the standing argument - a B-spline surface with its knot lists in the
wrong order passed every check in this suite and was silently dropped.

### The seam: cut, not seamed

A region that closes around its profile is now **cut into arcs of spans**, each
of which is a sheet in its own right and its own face. The reference ridge
declared closed - the case that was left faceted - is written as two faces
replacing 299 facets, and the export says so:

```
a sweep closing around its profile was cut into 2 faces, so that no face
crosses the surface's seam
```

The alternative was to write the surface as closed across `v` and carry a seam
edge round the loop, which is how a full cylindrical band is already written
here. That works when the region *is* the whole tube. This one is whatever the
boolean left of a tube, with a trim boundary meeting the seam wherever it
happens to, and the seam edge would have to be spliced into a loop whose shape
is not known in advance. Cutting does not depend on that boundary at all: the
cut runs along mesh edges the two arcs already share, so the neighbours are
asked for nothing, and the two arcs cover the wall exactly once - a cut, not a
reduction and not an overlap, which is what the unit test checks.

Halves are tried first and one face per span is the fallback, so a region with a
hole in it - which a half would not be a sheet of - still gets written. The cost
is one extra face per sweep, stated on export rather than left to be found in
the file.

OpenCASCADE reads the closed ridge back as one solid of one shell. Its volume is
0.15% under the faceted export's, which is the smooth surface departing from the
chords: over the ridge's roughly 1280 square units that is an average normal
displacement of about 0.02, against a tessellation band of 0.0660. Inside what
the mesh leaves open, which is the whole claim being made.

### The approximation pass writes

`step-approximate-surfaces` no longer only measures. It fits a cylinder to each
uncovered smooth region and, where the fit holds, **declares it** - after which
the ordinary recogniser does everything else. That indirection is the design:
the approximation contributes a declaration rather than a face, so a fitted
surface goes through exactly the checks a declared one does - the same fit test,
the same rim topology, the same refusals - and nothing downstream has to trust
it or even know where it came from. It is roughly forty lines of fitter and ten
of wiring, against reimplementing band recognition for guessed surfaces.

`step-approximate-cylinder.py` is a hand-written `polyhedron` tube, which is
what an imported mesh or a library sweep looks like from the exporter's side:
**66 facets become 3 faces**, one `CYLINDRICAL_SURFACE` and two planes. OCCT
reads the radius back as exactly 10 and the volume as 3769.911184 against an
exact pi*100*12 of 3769.911184 - the fitted solid is *closer* to the intended
one than the mesh it replaced, which had the volume of a 64-gon.

**The intent gate is the region, not the fit**, and that is what makes guessing
defensible here at all. §6's standing objection is that a hexagonal prism and a
six sided tessellation of a cylinder are the same mesh. They do not produce the
same *region*: regions are grown across edges meeting at less than the smoothing
angle, 25 degrees by default and settable with `OPENSCAD_STEP_SMOOTH_ANGLE`, and
a prism's 60 degree dihedrals never form one. The whole judgement is one number
in one place, and the unit test asserts both sides of it.

**The fit is not itself an approximation.** A tessellated cylinder has its
vertices *on* the cylinder and only its facets inside it, so the axis and radius
come back exact and acceptance is the ordinary modelling tolerance rather than
the band. A region whose vertices do not lie on one common cylinder is refused,
and so is a flat one - every normal the same makes the scatter matrix rank one,
and any axis in its plane would fit equally well.

`step-approximate-report.scad` keeps its place by being the negative: a helicoid
is not a cylinder at any tolerance, so all four of its regions are tried and all
four refused, and the pass reports both numbers - how many it took and how many
it left. A pass that quietly wrote nothing would look exactly like one that
found nothing to write.

### Recovering the grid, which closes the other half

`step-approximate-report` has measured its four helicoid walls at **100% regular
over 330 interior vertices** since the day that measurement landed, and declined
them ever since. `gridFromRegion` recovers what that number says is still there.
**2048 facets become 4 faces**, each a 24x17 or 10x17 cubic sweep. OCCT reads the
result as one solid of one shell, volume 798.716 against the faceted export's
800.717 - over 570 square units of wall, an average normal displacement of
0.0035 against a band of 0.0107.

The recovered grid is handed over as a *declaration*, the same as the cylinder
fit, so everything after it is the path already built for grids the model
declared: membership, the boundary walk, cutting at the seam, emission.

Three things had to be got right, and the first two were got wrong first.

**Which edge is the diagonal.** The obvious rule - the edge that is the longest
in both its triangles - is the hypotenuse rule, and it holds for a grid of
rectangles. A swept wall is a grid of skewed parallelograms, where the short
diagonal is shorter than the sides, and near the boundary a side can be the
longest edge a triangle has. Both failures were measured before the rule was
replaced: quads are now scored by how close they come to a parallelogram and
paired greedily from the best down, with augmenting-path repair because greedy
alone stranded triangles on every wall.

**Which way round a quad is written.** The layout turns the same way at every
step, so it needs every quad wound the same way. Taking the shared edge as the
edge *key* gives each quad an arbitrary handedness - the key is sorted by vertex
index - and the layout then disagreed with itself wherever two neighbours were
written opposite ways round. The winding comes from the mesh instead.

**What the band actually measures.** This one was a real defect in code that had
already shipped. `tessellationBand()` measured the sagitta of the chords along a
column: how far the interpolant bows away from the sweep direction. But
membership is asked about *facets*, and a facet's middle is neither a vertex nor
a chord midpoint - it sits inside a cell, and the flat triangles covering that
cell stand off the smooth surface by more than the chord midpoint does. Measured
on a twisted patch: band 0.000271, middles out by 0.000373. That gap refused
every facet of a grid that had just been recovered from them, while their
corners passed. The band is now sampled across each cell against the bilinear
patch through its corners, plus half the warp - the amount by which either
triangulation of a non-planar quad falls inside that patch. The declared ridge's
band moves from 0.0660 to 0.1290 for the same reason, and it was always the
truer number.

There is one guard, and it earns its place: the band is not only a measurement
here but the tolerance membership is then answered at, so a wild recovery would
arrive with a wild band and admit everything. A grid whose interpolant bows by
more than a quarter of a cell is refused - a property of the grid itself, not of
the caller's bookkeeping.

### Turned surfaces, and where the axis comes from

The approximation pass now also takes cones and spheres, and the shape of that
work is not what this section previously predicted. Neither has a surface type
to declare here, which is deliberate rather than missing: `primitives.cc`
expresses a frustum as two rims matching declared cylinders, and a sphere as a
stack of those bands absorbed into a spherical zone. So `fitRevolved` hands over
*rings*, one `CylinderSurface` each, and the band pass makes the surfaces out of
them with no new emission code. `step-approximate-turned.py` is a bare
`polyhedron` frustum and sphere: **48 wall facets become one
`CONICAL_SURFACE`**, at a volume OCCT reads as 1960.353816 against an exact
1960.35, and **480 become one `SPHERICAL_SURFACE`** of radius exactly 10 - one
face rather than the fifteen the rings alone would give, because a
`SphereSurface` among the declarations lets the zone pass absorb the whole
stack.

**Finding the axis is the whole difficulty, and two closed forms are degenerate
on exactly the shapes that matter.** Both were implemented and measured before
being replaced, and both are worth recording:

- **The rims.** A frustum has two and they give the axis directly. A sphere's
  cap meets the band beside it at the angle of one ring - 11 degrees on a 32x16
  sphere - which is well inside the smoothing angle, so the caps *join* the
  region and it has no boundary left to read.
- **A screw fit.** Every normal of a turned surface is coplanar with the axis
  and the radial direction, `n . (a x (c - p)) = 0`, which is linear in six
  unknowns and solvable as a null space. A cone and a sphere both have every
  normal line through one point, and the null space comes out **three**
  dimensional rather than one - three eigenvalues at 1e-16 on the frustum.

So the axis is *proposed and then verified*. A rim proposes one, a cap that
joined the region proposes its normal, and the apex - which every tangent plane
of a cone contains, so a linear least squares finds it - proposes the mean
ruling direction. What accepts a candidate is the ring test: every vertex on the
circle its own height puts it on. That test is strict enough that a wrong axis
cannot survive it, which is what lets the proposals be rough.

One bug in it is worth keeping because it hid so well. Rings are grouped by
quantised height, and using the quantised key *as* the height rounds every ring
to the quantum. A cylinder does not care - it is infinite along its axis - so
cones kept working, while the sphere test, which measures heights against a
radius, failed silently. Carrying the true mean height alongside the key is what
turned fifteen faces into one.

### What a refused patch says

`quadricOfPatch` had eight ways to refuse and reported none of them, so a patch
left as a spline looked exactly like one the recogniser was failing to see -
which is not hypothetical, since OCCT named six exact cylinders among
`step-fillet-refusals`' thirty splines while the pass reported a bare zero. Each
gate now names itself, and the counts settle what was left open when those six
were recovered:

```
12 patches are not quadrics because the rails are parallel but not coaxial
12 patches are not quadrics because its two meridians have different radii
6 of 30 patches are exactly quadrics - 6 cylindrical, 0 spherical
```

The twelve top and bottom strips are equal circles in parallel planes whose
centres are offset sideways as well as along the edge, which rules an *oblique*
cylinder - correctly refused. The twelve corners are blends rather than octants,
which is what that fixture exists to assert. And OCCT closes the argument rather
than the reasoning: its canonical census now reports all twenty four remaining
splines as genuine splines. The third party that named the loss reports none.

**What remains.** The round trip through SolidWorks and Fusion, which are not
OCCT and where the failure that started this was seen; roadmap item 4, trimmed
faces, which nothing has attempted; and non-separable weight nets for fully
skewed fillet corners, which is a modelling question rather than an exporter
one.

**This has now been done, and OCCT is no longer the only kernel that has read
these files.** See *The SOLIDWORKS round trip* below. Fusion is still untried.

**What not to do, and why, so nobody re-derives it:**

- **A resolution parameter for the approximation.** Facetting is an
  approximation with a knob, so a tolerance for the surface fit looks like the
  matching idea. It already has one: the fit is accepted only inside the
  tessellation band, `(c/2)*tan(theta/4)` over a chord `c` at a dihedral
  `theta`, and both shrink as `$fn` rises - 0.6000 on a coarse declared ridge,
  0.3101 at the bayonet's thread scale, 0.0963 on a leftover region. Raise the
  resolution and the fit is held to a tighter standard, with no second knob to
  keep consistent. A knob which *widened* the band would be worse than
  redundant: a fit inside the band asserts nothing the mesh does not already
  allow, and one outside it invents geometry, which is the rule the whole
  exporter is built on. A tighten-only cap would be safe, and is unnecessary for
  the same reason - the output is never worse than `$fn` already dictates.
- `SURFACE_OF_LINEAR_EXTRUSION` / `SURFACE_OF_REVOLUTION` (§6 item 2) collapse
  **zero** faces on every fixture and both real parts, and quadrics are the
  better representation where they apply. The only thing they uniquely express is
  the oblique cylinder, which needs its own surface family *and* recogniser.
- Per-face provenance through `runOriginalID` (§6 item 3) is architecturally
  cleaner but adds no coverage, and would *lose* the hull-generated surfaces that
  fitting currently catches.
- The bayonet's last rejection is geometrically necessary, not unimplemented: 36
  pentagons each take one edge of the r=78.1 rim, and an arc through two vertices
  of a planar facet lifts 0.30 mm out of its plane. It is correctly refused.
- The thread cannot be written exactly by anything. It is a hand-written
  `polyhedron` over a computed point list, and a circular helix is not a NURBS
  curve. Writing it means approximating within a tolerance, which breaks the
  *exact fit or stay faceted* rule this exporter is built on - a decision for the
  maintainers, not a task.

**And the open question that is not a feature at all:** the analytic path is
still behind `step-analytic-surfaces`, off by default. Everything above makes it
better; none of it decides when it becomes the default. That wants the round trip
evidence first.

## 9. The SOLIDWORKS round trip

The failure that started this work was seen in SOLIDWORKS, and until now every
number in this document came from OpenCASCADE. `scripts/step-interop-kit.py`
writes fourteen coupons, each exported twice - analytic, and faceted as a
control - and `scripts/step-interop-solidworks.ps1` drives SOLIDWORKS 2026 over
all twenty-eight through its API.

**Every file imports as a solid body with zero errors, and all fourteen coupons
pass.** Not one analytic export failed where its faceted control succeeded,
which is the only shape a finding could have taken.

Where an exact value exists, the analytic export matches it:

| coupon | SOLIDWORKS reads | exact |
| --- | --- | --- |
| c01 cylinder | 6283.1853 | pi*10^2*20 = 6283.18531 |
| c07 fillet quadrics | 975.5870 | Minkowski 975.587, as quoted in §7 |
| c04 sphere | 4188.6448 | 4188.7902 |

The sphere's shortfall is its two flat polar discs and nothing else - the
analytic export is one `SPHERICAL_SURFACE` plus two `PLANE`s, which is the two
unreplaced facets `step-sphere` already asserts. Solving the cap volume backwards
gives a latitude of 84.3755 degrees where the tessellation puts it at 84.3750.

### What SOLIDWORKS writes back

`scripts/step-interop-roundtrip.ps1` reads a coupon in and saves it straight out
again, which settles two things this document previously asserted without
checking.

The first is §7's claim about the fillet - "entity for entity what SolidWorks
writes for the same part". It is exactly true, and now measured rather than
reasoned:

| c07 fillet | ours | SOLIDWORKS |
| --- | --- | --- |
| `ADVANCED_FACE` | 26 | 26 |
| `PLANE` | 6 | 6 |
| `CYLINDRICAL_SURFACE` | 12 | 12 |
| `SPHERICAL_SURFACE` | 8 | 8 |
| `CIRCLE` / `LINE` | 24 / 24 | 24 / 24 |

The second is the question an importing user actually has, which no validity
check answers: having read a `CYLINDRICAL_SURFACE`, does the receiving system
still believe in it? **No quadric was downgraded to a spline anywhere in the
kit.** `c09`'s twenty-four `RATIONAL_B_SPLINE_SURFACE` complex instances - the
highest risk in `doc/step-interop-validation.md`, and the class of defect F7 was
- come back as twenty-four. SOLIDWORKS accepts the form and re-emits it.

Where it differs, it is splitting periodic surfaces at their seams rather than
losing anything: our one-face cylinder returns as two, our one-face torus as
four. That is a representation choice, and on this evidence ours is the more
compact of the two.

### The one finding, and how it closed

`examples/step_test/lid10` exported with its own parameter set came back from
SOLIDWORKS as two bodies whose mass properties were impossible. Two separate
things were wrong, and only one of them was in the exporter as it stood.

**The two-shell split was a regression introduced the same day**, by probing for
a hole's enclosing face with a point interior to the loop instead of the loop's
first vertex. That finds more enclosing faces, which sounds strictly better and
is not: on concentric rings it parents a hole onto a face which does not own it,
and the solid comes apart. It was bisected against the lid at the time and
cleared - but the bisection compared edge counts and never the shell count,
which is where the damage was. Reverted.

**What remained was real, and OpenCASCADE is what made it tractable.** It does
not merely say a file is bad; it names the face and the defect. Three faces of
2451, all `InvalidImbricationOfWires`, all quadrants of the annulus at z=84.9,
each carrying a three-corner wire of span about 1. Three fixes followed, in the
order the kernel pointed at them:

1. **A hole belongs to a face only if the face holds all of it.** One probe point
   says a loop *starts* inside a candidate; it does not say the loop is inside
   it. The parents chosen for three of the eight small loops on that plane held
   1, 2 and 3 corners out of 3, 3 and 5.
2. **The fallback had to learn the same test.** Doing (1) alone changed nothing:
   when the search rejects every candidate the code falls back to the parent
   `mergeTriangles` proposed, which is the one just rejected.
3. **A hole pinched to its own boundary is not a hole.** Three shared exactly one
   vertex with the loop carrying them, and a point on a boundary is where an
   even-odd ray is ambiguous - so containment called all five corners inside
   while BRepCheck still refused the face. Vertex indices are exact where the
   geometry is not, so they are asked first.

Along the way the orphan branch stopped reversing loops nothing encloses. It had
been winding them to agree with the bucket's mesh normal, which is the wrong
reference for exactly those loops: on this model they are notches at the rim
whose own winding is what the mesh said. Reversing gave fifteen edges used twice
in the same direction; not reversing gives none.

The result, and the first clean kernel round trip this part has had:

| | lid | bayonet control |
| --- | --- | --- |
| `validatestep.py` | ok, 2457 faces, 1 shell | ok |
| OpenCASCADE | round trip ok, 0 faces rejected | ok |
| OCCT volume | 223482.298444 | 234864.242474 |
| mesh volume | 223482.30 | 234864.24 |
| SOLIDWORKS, faceted | 223482.2984 | 234864.2425 |

### What this licenses, and what it does not

The open decision in §8 was whether the analytic path stops being experimental,
and that this wanted round trip evidence first. The evidence now says: on every
coupon, in a second kernel, the analytic export imports as a solid, measures
exact where an exact value exists, and survives a round trip with its surfaces
intact. That is the case for turning it on.

Nothing stands against it on the evidence. The one real part that failed has
been fixed, and both real parts now round trip through OpenCASCADE and import
into SOLIDWORKS as single solids of the right volume. The decision is a
maintainer's, and this is the case rather than the verdict.

What the evidence does not cover is worth saying plainly. Two kernels are not
every kernel; every coupon is small and every model here is within a couple of
hundred units of the origin; and none of this says whether the faces are laid
out the way a mechanical engineer would want them, which is a judgement rather
than a measurement.

Fusion is installed on the same machine and has not been tried. The same kit
runs against it; only the driver is SOLIDWORKS-specific.


## 10. Open work

§7's list is closed - every item on it is struck through - and §8's "what to do
next" predates the SOLIDWORKS round trip. This is what is actually left, in the
order it is worth doing.

### 1. ~~The lid's hole nesting~~ - closed

Fixed; see §9. The lid round trips through OpenCASCADE with no face rejected and
a volume of 223482.298444 against a mesh of 223482.30, and the whole interop kit
of 28 files is clean in both SOLIDWORKS and OCCT.

The one thing worth carrying forward is the method. `validatestep.py` passed
every one of the files OpenCASCADE refused - the two-shell one, and each of the
three stages of the hole nesting defect. A validator says whether a file is well
formed; only a kernel says whether it is the solid the mesh was, and only a
kernel names the face.

### 2. Whether the analytic path stops being experimental

§8 left this open pending round trip evidence. §9 now supplies it: on every
coupon, in a second kernel, the analytic export imports as a solid, measures
exact where an exact value exists, and survives a round trip with its surfaces
intact. Against that stands item 1 - not a defect of the analytic path, since
the faceted control fails the same way, but a distinction a user meeting it
would not care about. The decision is a maintainer's, and it is the only thing
blocking it.

### 3. Fusion

Installed on the same machine, never tried. The kit is already written; only
`scripts/step-interop-solidworks.ps1` is SOLIDWORKS-specific. Cheapest
outstanding item by some distance, and a third kernel is worth more than a
second was.

### 4. Roadmap item 4, trimmed faces - blocked on item 5, not on effort

Measured rather than assumed. The remainder is still fourteen faces - 0.8% of
the bayonet, 803.6 of its area - lying on four surfaces the recogniser already
knows:

```
8 facets  cylinder r=78          3 facets  cone r=80.6-1*z
2 facets  cylinder r=78.1        1 facet   cylinder r=74.2356
```

`doc/step-export.md` says of these that "the curve has to come from the
generator". Asking what is on the other side of each trim says something
stronger: **every one of the fourteen borders geometry the recogniser classifies
as uncovered** - a region with no analytic surface at all.

| faces | on | borders |
| --- | --- | --- |
| 8 | cylinder r=78, z 85..95 | one uncovered face each - the bayonet cam ramps |
| 3 | cone, r 79.4..80.6, z 0..1.2 | one or two uncovered |
| 2 | cylinder r=78.1, z 65..75 | three uncovered each |
| 1 | cylinder r=74.2356, z 64.2..66.6 | four uncovered - this is the hose thread's root, r 74.24 |

A trim curve is the intersection of two surfaces. Where the second surface is
only a mesh there is no exact curve to compute and none for a generator to
declare, because the geometry on that side does not come from a PythonSCAD node
at all - §7 item 4 establishes that all 999 uncovered faces come from three
hand-written places in the model.

So item 4 cannot be finished before item 5, and item 5 is not an implementation
task: a circular helix is not a NURBS curve, so a thread can only be
approximated within a tolerance, and that breaks the *exact fit or stay faceted*
rule this exporter is built on. That is a decision for the maintainers rather
than a piece of work, and it is the same decision §8 already records.

**Completing item 4 would therefore not complete the feature set**, and nothing
here is waiting on effort.

**Nor does the declaration channel finish it, which is worth knowing before
anyone assumes otherwise.** `declare_grid` reaching OpenSCAD unblocks item 5 -
the thread can now speak for itself in the language the model is written in, and
the proof of concept turns 1470 of its facets into four B-spline faces at the
shipped part's own scale. Item 4 does not follow.

`step-declare-grid-scad.scad` shows why on a model small enough to read. A ridge
is declared and written as a B-spline; it is fused into a wall of two cylinders,
one of which it cuts. The export reports two cylindrical surfaces available and
one recognised, and the file contains exactly one: `CYLINDRICAL_SURFACE('',#4,
22.5)`, the *outer* wall, which the ridge does not touch. The inner wall at
r=20 stays faceted, with both sides of its trim now carrying a surface, because
the curve where they meet still has no representation.

That is item 4 entire, in miniature: not a missing surface but a missing curve.
What it needs is `SURFACE_CURVE` with a pcurve on each face, which the exporter
does not write.

### 5. The committed bayonet artifact is stale

`examples/step_test/bayonet_container_v1-2.stp` was exported 2026-08-10 and
fails `validatestep.py` today. A fresh export of the same part is clean at the
same 1685 faces, so regenerating would move no documented probe figure - but it
is the input to `scripts/step-analytic-probe.py` and appears in two documents,
so it is a decision rather than a cleanup. `examples/step_test/README.md`
records the situation.

### 6. `pointMember`'s absolute tolerance

`Surface.cc` asks whether a point lies on a surface with an absolute `1e-5`,
where `AnalyticFeatures.cc` scales everything to the geometry. On a model in
microns it would accept almost anything; in kilometres, almost nothing, dropping
every surface to facets in silence. The obvious fix is 78 times looser on the
bayonet lid than the measurements in this document were taken against, and no
fixture is far enough from unit scale to say whether it helps. It wants a
large-model fixture first. The reasoning is at the top of `Surface.cc`.

### 7. Known-imperfect, fail-safe, and low priority

Each of these costs a missed optimisation rather than wrong output, and each
falls back to faceted:

- `gridFromRegion`'s augmenting path has no blossom contraction, so it can miss
  a pairing around an odd cycle, and it gives up past a recursion depth of 4096;
- `boundaryCycles` treats two boundary loops meeting at a pinch vertex as a hard
  failure rather than splitting them, though its Bezier caller demands a single
  cycle anyway;
- `StepKernel`'s entities are arena-owned and do not leak, but a derived
  constructor throwing would leave a dangling pointer. Closing it means taking
  self-registration out of 126 construction sites for a path only reachable on
  allocation failure.

### 8. Not tasks, and recorded so they are not mistaken for tasks

The helical thread cannot be written exactly by anything - a circular helix is
not a NURBS curve - so writing it means approximating within a tolerance, which
breaks the *exact fit or stay faceted* rule the exporter is built on. Fully
skewed fillet corners want non-separable weight nets, which is a modelling
question. §8's *what not to do* list stands unchanged: `SURFACE_OF_REVOLUTION`
and per-face provenance both collapse zero faces, and the bayonet's last
36-facet rejection is geometrically necessary.
