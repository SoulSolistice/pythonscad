# STEP export: status

A status assessment of the STEP exporter, taken against `doc/step-export.md` and
checked against the tree rather than read off it.

- **Basis:** `doc/step-export.md` as of `e764969`, tree at `0e8ab94`
  (`claude/step-export-feature-detection-7e75pq`, merged with upstream master
  2026-08-15).
- **Method:** static reading of the four exporter files and the test wiring, plus
  two things actually run — `scripts/step-analytic-probe.py` and
  `tests/validatestep.py` over the two committed exports in
  `examples/step_test/`. No build was available in this environment, so every
  runtime claim that needs the binary is marked as such in *What is not verified*
  below.
- **Every number below is either a line reference or one run of a script named
  next to it.** Nothing here is restated from the doc without a check.

## Headline

The faceted path is finished and guarded: eleven checks in `validatestep.py`,
sixteen fixtures, one check per historical defect. The analytic path — behind
`step-analytic-surfaces`, still off by default — now writes cylinders, cones,
spheres, tori *and* B-spline patches, which is one surface family further than
the doc's own roadmap section says. On the reference model the recogniser is
within 36 facets of the geometric ceiling, reproduced here.

Two things are true of the current state that the doc does not say:

1. **Item 2's emission has landed.** The doc's *Where this item stands* still
   reads "what remains is entity writing"; `StepKernel` writes
   `B_SPLINE_SURFACE_WITH_KNOTS` and `B_SPLINE_CURVE_WITH_KNOTS` today, and the
   validator gained `check_bspline_faces` with its own mutation harness.
2. **A committed analytic export of a real model does not pass the project's own
   validator.** `examples/step_test/lid10.stp`, written by pythonscad on
   2026-08-13, has 94 edges used by one face. That was finding F1 below, the only
   open correctness question in this assessment, and it has since been traced to
   the exporter dropping a face it could not place and fixed.

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
| 1 — `rotate_extrude` declaring surfaces | half done (line segment) | confirmed; `step-rotate-extrude` present |
| torus | done, one `TOROIDAL_SURFACE` | confirmed: `TorusSurface`, `TOROIDAL_SURFACE`, `step-torus` |
| 3 — spheres | done, one `SPHERICAL_SURFACE` | confirmed: `SphereSurface`, `SPHERICAL_SURFACE`, `step-sphere` |
| 2 — fillet B-splines | recognition done, "what remains is entity writing" | **further along than the doc**: emission, validator check and mutation harness all landed (`1310d1a`, `8e96e6c`, `tests/bspline-check-mutations.py`) |
| 4 — trimmed faces | last; 14 faces of the bayonet | unchanged; nothing in the tree attempts it |
| 5 — swept surfaces | blocked on a user-facing declaration | **the blocker has landed.** `declare_*` exists in both languages, so item 5 is no longer "impossible", it is "the model has to say so" |

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
2.9 MB, referenced by nothing in the tree (`grep -rn lid10` finds only the files
themselves). It is a useful artifact, which is an argument for wiring it in or
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

## 5. What is not verified here

No build was available (CGAL, manifold, Qt and the embedded interpreter are all
absent), so the following remain doc-asserted and were not re-checked:

- every per-fixture report line and face count (`step-pie-slice` reporting *1
  partial, 31 facets replaced*, the fillet cube's *20 patches / 1100 facets / 26
  faces*, and the rest);
- the sanity driver's three invariants — locale-identical re-export, the analytic
  pass validating under the same checks, and the CGAL/Manifold declaration-count
  agreement;
- every SolidWorks round trip;
- the CGAL-backend quality gap table under *Known quality gaps*.

Both pure-Python tools did run, which is what makes §3 and §4 measurements rather
than readings.

## 6. Recommended order

F1 to F5 have all been acted on; what stands below them is the work itself.

1. **Confirm F1 on a real build** — the one thing here that this environment could
   not do. Export `examples/step_test/lid10.scad` with the analytic feature on and
   run `validatestep.py` over the result; the two commands are in
   `examples/step_test/README.md`. Expect no unpaired edges, and a line saying how
   many reversed loops were kept as their own face.
2. **Make `FilletNode`'s Beziers rational.** `fillet()` draws parabolas, out by
   6% of the radius at a right angle and 25% at a 60° dihedral, and the corner
   patch is 9.5% off the sphere. The Bezier substrate is deliberate and correct -
   it needs no axis and survives non-perpendicular faces and a varying radius -
   and the fix keeps it: a rational quadratic with weight `cos(θ/2)` on the same
   three control points is exactly a circular arc, and `FilletNode`'s corner net
   is already the exact sphere-octant net. `Bezier()` takes a weight, the two
   callers pass it, the control points do not move. The exporter can then write
   `CYLINDRICAL_SURFACE` and `SPHERICAL_SURFACE` where the weights say circle,
   and the B-spline path stays for everything else. It is a mesh fix, not a STEP
   one, and it changes the geometry of every filleted body - so it wants the
   maintainer's nod and a build to verify.
3. **The short-edge collapse in `FilletNode`, if the fillet work continues.** The
   pass that merges two corners when an edge is shorter than 2r is disabled, and
   `src/core/FilletNode.cc` now carries the five things standing between it and
   being switched on - measured, not read off the code. The substantive one: the
   collapse is over-determined (four planes surround the pair, three are used) and
   the residual is linear in the edge length, so the `< 2*r_` gate does not bound
   the damage. On one ordinary configuration a 0.5 mm edge moves the vertex 1 mm
   off the ignored face, and with shallow end faces a 0.1 mm edge sends it 12
   units away with no error reported.
4. **Re-measure item 5 now that `declare_*` exists.** The 59% is not one number
   any more: part of it is a thread that can never be a surface of revolution,
   and part is ramps and lugs a model could declare today. Nobody has separated
   those two, and the probe plus one annotated copy of the model would.
