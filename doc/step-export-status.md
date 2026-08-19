# STEP export: status

A status assessment of the STEP exporter, taken against `doc/step-export.md` and
checked against the tree rather than read off it.

- **Basis:** `doc/step-export.md` as of `e764969`, tree at `0e8ab94`
  (`claude/step-export-feature-detection-7e75pq`, merged with upstream master
  2026-08-15).
- **Method:** it began as a static reading of the four exporter files and the
  test wiring, plus two things actually run - `scripts/step-analytic-probe.py`
  and `tests/validatestep.py` over the two committed exports in
  `examples/step_test/`. A headless build was made later in the same exercise
  (Manifold + CGAL, no Qt), so most runtime claims are now measured rather than
  read; §5 says exactly which are not.
- **Every number below is either a line reference or one run of a script named
  next to it.** Nothing here is restated from the doc without a check.

## Headline

The faceted path is finished and guarded: eleven checks in `validatestep.py`,
twenty-three fixtures, one check per historical defect, and every fixture now
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
| Model-level declaration, both languages | `declare_cylinder` / `declare_sphere` / `declare_torus` — `src/python/pyfunctions.cc:990-1002`, plus the object methods at `:1060` |
| Recogniser separated from the format | `src/geometry/AnalyticFeatures.cc` (1408 lines) against `src/io/StepKernel.cc` (1114) and `src/io/export_step.cc` (94) |
| Validator | 11 checks: real literals, references, units/context, directions, topology, shared vertices, face normals, hole nesting, cylindrical faces, B-spline faces, shells (`tests/validatestep.py`) |
| Fixtures | 14 SCAD in `tests/data/scad/step-export/`, 2 Python in `tests/data/pythonscad-step-export/`, both wired by glob at `tests/CMakeLists.txt:1162-1165` |
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
| 4 — trimmed faces | last; 14 faces of the bayonet | unchanged; nothing in the tree attempts it |
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

### F1 — a committed analytic export is not a closed shell — **fixed**

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

## 5. What is and is not verified here

A headless build was made in this environment - Manifold and CGAL, no Qt, Release
- so most of what this section used to disclaim is now measured. What runs:

- **all 23 fixtures**, each asserting the exporter's own report through
  `EXPECT:` lines, measured on that build rather than transcribed;
- **the sanity driver's three invariants** - the locale-identical re-export, the
  analytic pass validating under the same eleven checks, and the CGAL/Manifold
  declaration-count agreement - on every fixture, every run;
- the unit tests (808 assertions) and the B-spline mutation harness.

Still not verified, and honestly so:

- **every SolidWorks round trip.** Nothing here can open one. This remains the
  largest untested claim in the whole exercise: the exporter's purpose is that a
  CAD kernel accepts the file, and `validatestep.py` is a proxy for that, not a
  substitute.
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
6. **Write the fillet's exact quadrants as quadrics.** Since item 2 an edge strip
   is an exact cylinder quadrant and a corner an exact sphere octant, and both go
   out as `B_SPLINE_SURFACE_WITH_KNOTS`. That is valid and imports, but a
   `CYLINDRICAL_SURFACE` is what a CAD kernel can offset, thread and pattern.
   The weights say which patches qualify - all of them, on a constant-radius
   fillet - and `Surface.h` already carries the fit machinery. Needs an axis
   recovered from the control net and the same rim rules the band path uses.
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

**Do item 6 - write the fillet's quadrants as quadrics - and then stop adding
surface families.**

It is the only remaining item that is both substantial and reachable. Since the
Beziers went rational, an edge strip *is* an exact cylinder quadrant and a corner
an exact sphere octant, and all of them still leave as B-splines. A
`CYLINDRICAL_SURFACE` is what a kernel can offset, thread and pattern; a B-spline
is what it tolerates. The weights say which patches qualify - all of them on a
constant-radius fillet - the partial-cylinder and partial-sphere machinery
exists, and `fillet()` is PythonSCAD's own feature rather than upstream's. What
it needs is an axis recovered from the control net and the rim rules the band
path already applies.

**Then verify against a real CAD kernel before anything else.** §5's largest gap
is not a surface family, it is that nothing in this exercise has opened an export
in SolidWorks, FreeCAD or Fusion. `validatestep.py` is a good proxy - eleven
checks, one per historical defect, and it has caught real bugs - but the
exporter's purpose is that a kernel accepts the file, and that has been assumed
throughout. One round trip per surface family would either retire that risk or
find something no amount of self-validation can.

**What not to do, and why, so nobody re-derives it:**

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
