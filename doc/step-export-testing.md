# Testing the STEP exporter

A STEP export that is silently wrong still validates. It parses, the shell
closes, every face is well formed, and the file opens in a CAD system — and the
part is the wrong shape, or the analytic surfaces the whole feature exists for
were quietly left as facets. Everything here is built around that: each layer
asks a question the layer below it cannot answer.

## The four layers

| layer | asks | run by |
| --- | --- | --- |
| `validatestep.py` | is the file well formed by this project's lights? | every fixture |
| the report | did the recogniser recognise what it should have? | `EXPECT:` |
| `steproundtrip.py` | does an independent kernel read it as the intended solid? | `ROUNDTRIP:`, `VOLUME:`, … |
| interop kit | does a *commercial* kernel agree? | by hand, see `step-interop-validation.md` |

`validatestep.py` has one check per historical defect and has caught real bugs,
but it is a proxy: it knows what this exporter has got wrong before, not what a
kernel requires. The round trip is OpenCASCADE, the kernel FreeCAD is built on,
and it is an optional dependency — **it skips silently when absent, so a green
suite does not mean it ran.** Install it before touching the exporter:

```bash
pip install cadquery-ocp
```

## Derived, not captured

The rule that matters most, and the one that is easy to lose:

> A fixture asserts numbers worked out from the model's own dimensions. A number
> that can only be obtained by running the exporter is a last resort, and the
> fixture has to say that is what it is.

A census captured from the tool under test locks its behaviour in, which is
worth having, but it cannot say the behaviour was ever *right*. Regenerate it
after a regression and the test agrees with the regression. `pi*r^2*h` is not an
opinion about this exporter, and a kernel measuring the exported solid has no
access to the arithmetic that predicted it — the two agreeing is the only thing
in the suite that says the solid is the *intended* one rather than merely a self
consistent one.

Writing the derivation beside the number is part of it, because the derivations
are where the surprises live:

- **A sphere is not `(4/3)pi r^3`.** OpenSCAD's tessellation has no pole vertex;
  its outermost ring sits at `180/$fn` off the axis, so the export legitimately
  keeps two flat caps. `step-sphere` states 4188.6447513, being
  `4188.7902048 - 0.1454535`, and the caps are real geometry rather than an
  artefact.
- **A filleted cube comes apart into the pieces the fillet is made of** — the
  Minkowski sum of `cube(a-2r)` with a ball: `512 + 384 + 24*pi + (4/3)*pi`.
- **`step-rounded-profile` falls out of Pappus** as `2*pi*(1612 + 52*pi)`, once
  the profile is sliced by height rather than by shape.

Where a figure is genuinely underivable, **omit `VOLUME:` and say why**. A
partly recognised wall is neither the polygon's volume nor the circle's but a
mixture; a twisted extrude's mesh has no closed form. Pasting a captured number
in there would be the circularity this section exists to prevent.

## The directives

All of them are comment lines, matched anywhere in the fixture. The regexes are
anchored with `(?<![-\w])` so `APPROX:` does not match inside `ROUNDTRIP-APPROX:`
— they did once, and every approximation fixture failed at the same moment.

| directive | asserts | derived? |
| --- | --- | --- |
| `EXPECT:` / `EXPECT-NOT:` | a substring of the exporter's own report | the *claim* is (32 facets on a 32-gon band) |
| `APPROX:` / `APPROX-NOT:` | the same, for the extra approximation run | as above |
| `ROUNDTRIP:` | the kernel's surface census, **exhaustively** | counts, mostly |
| `ROUNDTRIP-APPROX:` | the same, for the approximation export | mostly |
| `VOLUME:` | the kernel's measured volume | **yes, always** |
| `RADII:` | every face of a kind has this radius | yes — the model says `r=10` |
| `EDGES:` | the kernel's edge census | mostly |
| `CANONICAL:` | what the kernel thinks each B-spline really is | no — it is an audit |

### `ROUNDTRIP:` is exhaustive

It accounts for **every** surface kind the kernel reads, not only the ones a
fixture names. Checking a subset let an unlisted kind through in silence: a
stray `TOROIDAL_SURFACE` from a recogniser that overreached would be read back
and noticed by nothing, because no fixture would think to write `Torus=0`. The
cost is having to state the planes, which is no bad thing — the plane count is
the number that moves when a substitution goes wrong.

This is why the refusal fixtures carry one too. On `step-extrude-refusals` the
assertion is not `Plane=2152`, which is the mesher's business; it is that no
`Cylinder`, `Cone`, `Sphere`, `Torus` or `BSplineSurface` appears beside it, and
there is nowhere for one to hide.

### `VOLUME:` and its tolerance

```text
// VOLUME: 3962.5955337
// VOLUME: 4824.92783 +/- 0.02
```

The tolerance is optional and absolute, defaulting to 1e-6 relative, which is
what a kernel agrees to when it can read the geometry straight. Exactly one
fixture needs more and has to justify it in prose: `step-oblique-trim` writes
its elliptical trim as a plain 3D curve with no pcurve, so the reader
re-derives the parameterisation on the cylinder and lands about 2e-6 away. That
is a stated property of the exporter — the same displacement is measurable on
OpenCASCADE's *own* export of the same solid, 5026.548244 with its pcurves
against 5026.558839 with them stripped — and not slack to be widened when a
number stops matching. A fixture asking for a looser bound has to say which of
the two it is.

## Mutation check every new assertion

A green run proves nothing about a directive that is not being read. Both of the
directives above were added by perturbing them and confirming the failure:
dropping `Plane=2` from a census, and moving a volume by 0.2%. This is not
ceremony — a directive collision once made six fixtures fail at once, and
earlier a round trip's *activity* in a log was misread as its success when the
test was already red.

## What a fixture looks like

The house style is a long comment that argues for the model, then the
assertions. The argument is the point: it says which defect the fixture exists
to catch and why the numbers are what they are, so the next person can check the
reasoning rather than regenerate the numbers.

Fixtures live in `tests/data/scad/step-export/*.scad` and
`tests/data/pythonscad-step-export/*.py`, and are picked up by a glob — **adding
one needs a `cmake -B build` before ctest can see it**, which costs a full
rebuild next time. Generate the expected output with:

```bash
TEST_GENERATE=1 ctest --test-dir build -R <fixture>
```

Keep both front ends in step. Anything reachable from SCAD and from Python
wants a fixture each, because the two bindings can drift — `declare_cone` takes
four doubles and so could not use the macro that generates its three siblings.

## Committed artifacts go stale

`examples/step_test/*.stp` are generated files kept in the tree so a reader can
open one without building anything. Nothing regenerates them, and no test reads
them, so **every improvement to the exporter silently invalidates them** - twice
now they have been a whole feature behind, and each time the part looked in a
CAD system exactly as though the feature did not work.

Regenerate after any change that alters what the exporter writes. The lid needs
its parameter set and both flags, and its own report is the check:

```bash
cd examples/step_test
../../build/staging/pythonscad.com lid10.scad -p lid10.json -P "New set 1" \
  -o lid10.stp --enable=step-analytic-surfaces --enable=step-approximate-surfaces
```

A quick way to tell whether one is current is to count its faces against a
fresh export with `grep -c ADVANCED_FACE`. If the two differ, the committed
file is behind.

## Scope

The suite covers what the exporter can reach, and the reference part
`examples/step_test/lid10.scad` is a specimen rather than a target: it is where
the adversarial cases were found, not the thing being optimised. A finding on it
is worth having only once it is expressed as a synthetic fixture that isolates
it — which is why `step-t-junction`, `step-oblique-trim` and `step-declare-cone`
exist.

## Kernel agreement is not proof

**A fix is not validated by showing that OpenCASCADE now agrees with the file.**
This is the same closing of the circle as putting a kernel's measurement into a
fixture and calling it an expectation, and it is easier to fall into, because it
does not look like capturing anything.

It happened here on 2026-09-02, on the face-splitting fix. The evidence offered
was that the reference parts went from *586 faces written, 595 read* to *595
written, 595 read*, and that not one `ROUNDTRIP-APPROX` line in any fixture
moved. Both are true. Neither is evidence:

- OpenCASCADE had been silently splitting those faces on read, so 595 *was*
  OpenCASCADE's own repair. Matching it says the exporter now agrees with the
  repair, not that either is right.
- The `ROUNDTRIP-APPROX` lines were captured from that same repair when they
  were written. They could not have moved.

A wrong fix that split the faces along different lines would have produced
exactly the same two observations.

What replaced it is an assertion about the file: `check_bound_enclosure()` in
`validatestep.py` takes each face with more than one bound, maps its loops into
the surface's own parameters, and requires every inner bound to have a vertex
inside the outer one. A hole does; a second region of the same surface does not.
It is proven by being run against a file known to carry the defect - the
pre-split `r02-bayonet-analytic.stp`, where it names two faces - and against the
same part after, where it is silent.

The order matters and it is worth stating as a rule:

1. Derive the property from the model, and assert it on the file.
2. Prove the assertion catches a file that is known to be wrong.
3. *Then* confirm with a kernel, and preferably one that was not the source of
   any number in step 1 - here SOLIDWORKS or Fusion rather than OpenCASCADE.

Kernel agreement is a cross-check downstream of a derivation. It is never the
derivation.

## The ladder a change climbs

Sections 17 to 22 of `step-export-status.md` cost a dozen iterations, and most
of what went wrong was a step taken out of order - a fix landed before it was
measured, a measurement trusted because a kernel agreed with it. The order that
worked, in the end:

1. **PoC.** Make the change badly and cheaply, on a branch, and measure it.
   Landing it is not the goal; finding out whether the mechanism does what it is
   supposed to is. Two of the ideas in §§19-20 died here, which is the cheapest
   place for an idea to die.
2. **Fixture.** A model small enough to reason about, with every expectation
   derived from the model - never captured from a run - and every directive
   mutation-checked. See the rest of this document for what that means.
3. **Provenance.** Run the fixture set and read the provenance report against
   what the models are. It answers *which solid is this* where every other check
   answers *what is this near*, and it caught a wrong statement about `hull()`
   in the first sweep that used it (§22).
4. **OpenCASCADE.** The round trip, and `scripts/step-diagnostics/` for the
   things a round trip does not say. Necessary, not sufficient: OCCT repairs
   silently, so agreeing with it proves nothing about a defect it repairs.
5. **Final.** A kernel that was the source of no number in steps 1 to 4 -
   SOLIDWORKS or Fusion, through `scripts/step-interop-*`. This is the only step
   that can tell you the export is right rather than self-consistent.

Skipping 3 or 5 is what makes a wrong fix look finished.
