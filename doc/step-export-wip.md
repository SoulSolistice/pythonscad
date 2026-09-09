# STEP export: work in progress

Open items, what is unexplained, what is parked, and where the next session
starts. Everything here is provisional by construction.

What is settled lives in `doc/step-export-development.md`; build and test
mechanics live in `CLAUDE.md`. Read both before this one — several items below
are one sentence because the reasoning behind them is there.

**Last updated for the state at `04a5ad0`, 2026-09-08.**

---

## Read this first

Verify the tree before changing anything, and again after:

```bash
ctest --test-dir build -R step
```

That regex picks up the fixtures — 33 SCAD in `tests/data/scad/step-export/` and
12 Python in `tests/data/pythonscad-step-export/` — plus
`bspline-check-mutations` and `closed-sphere-check-mutations`. Run it under the
machine's own locale: `LC_ALL=C` was used here for a while to
get past a decimal-comma problem that is now fixed in `export_step.cc`, and it
breaks five UTF-8 filename tests of its own.

Two things make that result a lie if they are missing:

- **`pip install cadquery-ocp==7.8.1.1.post1`.** `tests/steproundtrip.py` needs
  `TopTools_IndexedMapOfShape`, which `cadquery-ocp` 8.0 no longer exposes, so it
  **skips in silence** and the suite goes green with the round trip never run.
  Whole classes of failure are invisible without it.
- **lid10 is the blast-radius specimen and is not in the suite.** It needs
  `-p examples/step_test/lid10.json -P "New set 1"`; without those you get the
  default component and every number is incomparable.

---

## Where it stands

**Landed and measured, 2026-09-08.**

- `sphere()` closes at its poles. SOLIDWORKS reads it at `(4/3)pi r^3` and
  `4 pi r^2` to the digit, one face in and one face out with no repair.
- `export_step.cc` holds `LC_ALL`'s numeric part at `"C"` for the export, as
  every other exporter already did.
- `GridSurface::project` descends, and starts inside a span rather than on a
  profile corner. The band family's claim goes from 960 facets whole and 963 cut
  to 1826 and 192, recovering the upper flank of every sweep.
- `BezierPatchSurface::project` is guarded the same way. Nothing moved; it was
  never a defect there, because twenty-five restarts hid it.
- `declare_sweep`: a profile carried along a helix, declared as the shape.
  Membership is an inversion, exact, with no band.

**What the last interop run says** (48 files, same session settings, after the
projection fixes):

| coupon, analytic | faces before | faults | faces after | faults |
| --- | --- | --- | --- | --- |
| f01 band fn 24 | 149 | 0 | **17** | 0 |
| f02 band fn 32 | 366 | 2, code 17 | **9** | 2, code 17 |
| f03 band fn 48 | 313 | 0 | **49** | 0 |
| f04 band fn 64 | 362 | 2, code 17 | **39** | 2, code 17 |
| f05 band fn 96 | 662 | 0 | **153** | 0 |
| r01 lid10 | 1088 | 1, codes 7/13/21/30 | **797** | 1, codes **7/17/21** |
| r02 bayonet | 359 | 1, codes 7/13/21/30 | **68** | 1, codes **7/17/21** |

Codes 13 and 30 are gone from both real parts. Recognition improves by four to
forty times. The fault *count* is unchanged at six files, which is exactly why it
is the wrong number to read alone: the same six files, carrying different faults
over a markedly better solid.

**Corner exactness**, `scripts/step-occt-strict.py`, target 0 to within 1e-9:

```text
step-bored-cone            8.01e-14   done
step-bored-cylinder        7.99e-14   done
step-declare-grid-scad     3.79e-12   done
step-cut-cone              4.45e-13   done
step-exact-trim            3.55e-15   done
py-step-declare-grid       1.86e-13   done, and the flat bottom kept
py-step-declare-grid-strip 1.86e-13   done, and the flat bottom kept
step-band-family           7.44e-02   B-spline only, inside its own band
```

At the last full sweep, over the 42 fixtures that existed then, none strayed on a
plane, cylinder, cone, sphere or torus. The two text fixtures read 4e-09, which
the baseline already calls noise.
`step-band-family` is the one left, and its residual is corners its sweep's face
*uses* that were never on the sweep — a claim reaching past its surface rather
than a placement to fix.

---

## Open items, in order

### 1. Fault code 17 has never been diagnosed

`swFaceBadEdge`. After the projection fixes it is **the only code left standing
alone in the kit** — on `f02-band-fn032`, `f04-band-fn064`, `r01-lid10` and
`r02-bayonet`. Everything the five-why method needs is in place: `-FaultDetail`
names the entity, the standalone-ridge control removes the boolean, and the
derived screw-sweep volume `turns * 2pi * A * (R + dc)` adjudicates.

This is the next thread.

### 2. `declare_sweep` is landed and nothing uses it

- **No fixture.** It needs one with derived expectations. The screw sweep's
  volume is a closed form, so this is one of the few sweeps whose `VOLUME:` can
  be stated.
- **`step-band-family.scad` is the obvious first caller.** It computes an exact
  profile and an exact helix, emits a `polyhedron()`, and throws both away.
- **No SCAD builtin.** `src/python/pyfunctions.cc` has `declare_sweep`;
  `src/core/DeclareSurfaceNode.cc` does not. `doc/step-export-development.md`
  says to keep both front ends in step, and this is the one that is not.

### 3. The crossing curve, revisited

Worth reopening now and not before. The slack it was meant to pay off is the
*boundary* of a face, and until the projection fixes half the surface inside that
boundary was missing, so the measurement means something it did not mean then.

Land it on its own merits — 210 edges made exact is a result — and judge it on
whether it moves code 17, **not** on the granted slack, which it has already been
shown not to move. The full record of what was built and why it was reverted is
in `doc/step-export-development.md`, *Refuted claims*. Two things it must get
right that the first attempt did not: accept a point that is *on* both surfaces
to a stated residual rather than one the iteration arrived at, and give the grid
emitter the same edge ladder the quadric emitter has, because half of it is worse
than none — a crossing curve taken by the cylinder's face while the sweep's face
still writes a chord gives one edge two geometries and the shell comes apart on
420 edges.

### 4. `c06` imports clean and inside out

Noticed by eye: SOLIDWORKS reports **no fault at all** on `c06-partial-torus`,
and its inner fillet is *concave* where the model has it convex. That sits beside
the 14% volume shortfall this coupon has always had, and a fillet turned the
wrong way is roughly that shape — though the arithmetic in
`doc/step-export-development.md` rules out a simple corner flip as the whole of
it. Worth taking together rather than separately, and worth taking against an
import with no faulty faces to compare with.

### 5. `validatestep.py` checks pcurves only on cylinders and cones

A pcurve on a B-spline or a plane is checked by nothing. `c11` writes 581
`SURFACE_CURVE`s, all of them on B-splines, so all 581 of its pcurves are
unchecked. Mapping through a B-spline surface's parametrisation is a bigger piece
than mapping through a quadric's; it should be done next time that path moves,
and it should be closed whatever the cause of the faults turns out to be.

### 6. Import time is a diagnostic the driver does not record

A file SOLIDWORKS is happy with opens in seconds; one it has to work at takes
minutes. That is a signal available before any fault count.
`scripts/step-interop-solidworks.ps1` times nothing; one `Stopwatch` around
`LoadFile4` and a column would have it.

### 7. `VOLUME:` deserves to be on more fixtures

It is the only line in this suite that has ever noticed a chorded trim. Any
fixture whose model has a closed-form volume should state it.
`step-approximate-turned` gained one on 2026-09-08 and is a fair example of the
cost of not having one: the fixture asserted a census, a facet count and a
surface report, and every one of them was satisfied by a ball whose poles were
flat.

### 8. The band family's refusal counts have not been re-measured

The taper finding — that the outlier-refusal rule fires on the profile *changing
shape* and not on the helix, the tessellation or the wall — was established
before the projection fixes, with the untapered `f = 1` control. The mechanism
should hold; the numbers in it are pre-fix and the split between clean and faulty
members has not been re-counted since recognition improved four to forty times.
One kit run and one refusal column.

---

## Unexplained, and to be validated

- **Untapered band family at `$fn` 32.** Every other untapered member is clean.
  This one trades code 17 for 13/30 with sixteen faulty edges — `c11`'s signature
  and the only rise in edge faults recorded — while writing the same fraction of
  its sweep as the members that are clean. A sweep of constant profile at a
  coarse tessellation is its own case and is not explained.
- **`c11`'s two shortest edges.** The sliver lengths are otherwise predicted to
  three digits by two tessellations that do not align: at `r = 19.2` the ridge's
  60 stations every 9.1525 degrees meet the cylinder's 96 facets every 3.75, and
  the angular gap times the radius gives the edge — station 34 sits 0.0636 deg
  from a boundary, arc 0.021299, measured 0.021267. It does **not** explain the
  two at 0.006470 mm at theta 172.5 and 7.5, which sit exactly on facet
  boundaries with no station near them. Recorded as a second family and an open
  branch rather than folded into the first, because a mechanism that explains
  most of a set is the easiest kind of wrong answer.
- **One planar face whose boundary leaves its own plane by 0.019968**, found by
  scanning all 948 faces of a reference part. A straight edge between two points
  of a plane cannot leave it, so the polygon is genuinely non-planar and is
  written as a `PLANE`. The facet-merge coplanarity tolerance is `1e-8 x model
  diagonal` — microns here — so it did not come from that drifting. Independent
  of everything else and the only open item that is not a trade-off.
- **lid10's `PLANE` face with a corner 2.2623e-06 off it**, against an allowance
  of 1.2595e-06. Not the analytic work: the **plain faceted export with the
  analytic pass switched off reports the identical figure**. lid10's mesh has a
  facet its own vertices are not coplanar to, and the round-trip invariant's
  `1e-8 * extent` is marginally too tight at this model's size. Settle it on its
  own; measuring the faceted export first is what keeps it from being blamed on
  whatever changed last.
- **`r01-lid10-analytic` round-trips to something that is not a solid at all**,
  whose volume "exceeds its own bounding box", with every `Cone`, `Cylinder` and
  `Plane` gone from what SOLIDWORKS wrote back. `r02-bayonet-analytic` loses 7
  cylinders and gains 2.4% of volume. Both pre-existing, both on the reference
  parts rather than the coupons.
- **The band family degrades to splines on the way back out at every `$fn`.**
  The surface survives as geometry but not as intent, which is what feature
  recognition needs.
- **An open discrepancy with a recorded result.** The face split was recorded at
  the time as taking SOLIDWORKS "from 82 faulty faces to 1".
  Half reproduces and half does not: the body type is now *solid* with 0 gaps,
  which is the stronger claim, but the API reads 81/82 faulty faces where the
  record says 1. On the band family the dialog counts *higher* than the API (32
  against 3, 5 against 2), so a dialog reading of 1 against an API reading of 81
  would invert the established relationship. The check that settles it has not
  been run: open `r01-lid10-analytic.stp` with Import Diagnostics enabled and
  read the dialog's own faulty-face count, which is the instrument the original
  claim was made with.
- **`r01` and `r02` were near-duplicates in the exact tier** — the same face
  census over the same 4360-facet mesh, 85 bytes apart, which is the filename in
  the header — because neither parameter set selected a component. Both JSONs now
  set `_part` ('lid' and 'base'), so this should be closed, and it has not been
  re-measured. Until it is, do not quote either as corroborating the other.
- **The committed artifacts are stale.** `examples/step_test/lid10.stp` is dated
  2026-09-01 and `bayonet_container_v1-2.stp` 2026-08-10, both behind the sphere
  closure, the projection fixes, the plane declarations and the trim work. See
  `doc/step-export-development.md` for how to regenerate.
- **`pointMember`'s absolute tolerance.** Known-imperfect and fail-safe, and
  never re-derived.

---

## Parked experiments

All measured, all rejected, reasons in their commit messages. None is a candidate
to land as it stands.

| branch | what it holds | why it is parked |
| --- | --- | --- |
| `claude/step-corner-ownership` | corner placement before `mergeTriangles` | destroys the merge, 12–62x the faces |
| `claude/step-sweep-boundary-snap` | projecting a sweep's corners onto the sweep | takes them off the other surface they are on |
| `claude/step-sweep-cone-guard` | refusing a cone that meets a sweep only in chords | the restriction is empirical and has no fixture |
| `claude/step-sweep-vertex-exactness` | refusing a sweep corner past a quarter of its band | the quarter is chosen, not derived |
| `claude/step-corner-split` | the first triangulation | superseded by what landed |
| `claude/step-snap-poc`, `-owned` | the vertex snap and its two halves | superseded by the ladder, which was then rejected |

Two attempts of 2026-09-08 were built whole and reverted rather than branched —
splitting a sweep into one face per profile span, and extending the crossing
curve to declared sweeps. Both are written up in
`doc/step-export-development.md`, *Refuted claims*, because both are the kind
that get proposed again.

---

## Roadmap: what is still not declared

Ordered roughly by value per unit of work. Every one of these is a *producer*
that knows its surface and does not say so, which is the shape
`doc/step-export-development.md` argues is worth closing.

### A revolve that touches its own axis collapses to nothing

The largest single gap by measured cost. The same wine-glass outline:

| outline | faces written |
| --- | --- |
| hollow, never touches the axis | **82** |
| solid, touching the axis | **2616, all planes** |

Revolving a segment that ends on the axis sweeps a cone whose apex is *on* the
axis, which is an exact `CONICAL_SURFACE`. It tessellates as a triangle fan
meeting at one shared vertex, and a fan is not a ring; the region at the apex is
unrecognised and the whole stack above it follows it down.

**The cascade is not the bug and must not be "fixed".** A collapsed band's rim is
a `CIRCLE` and a faceted neighbour's is a polyline; they cannot be the same edge,
so a band whose neighbour stays faceted has to stay faceted too. Dropping it is
what keeps the shell closed. The rule is right and the trigger is wrong. The item
is: **recognise an apex fan as the cone it is.** Exact, no fitting, no tolerance
band. It matters for arbitrary models because every knob, lens, dome, bottle base
and solid stem is a revolve that touches its axis.

**And it needs the mesh fixed first.** The apex cone was declared correctly,
recognised, and still could not be written, because `rotate_extrude` emits
twenty-four zero-area facets where the profile meets the axis and the shell opens
where they are skipped — the same solid from `cylinder(r1, r2 = 0)` is one shell
of 65 faces, and from the revolve it is two solids and three shells. A
declaration cannot rescue a bad mesh.

### `linear_extrude(twist=)` declares nothing

The last thing a `linear_extrude`'s own parameters determine that is not
declared. 400 of a twisted square's 514 facets stay faceted; a twisted square is
four helicoidal faces and two caps — six, against 514.

Neither `slices` nor `$fn` is the obstacle: the node holds profile, height,
direction, twist and scale, and a twisted extrusion can compute its stations
exactly from those. `GridSurface` is already the channel for exactly this case,
and `declare_sweep` is a tier above it for the constant-profile case. It would
land in the approximation tier for the same reason `declare_grid` does:
OpenSCAD's mesh for a twisted extrude is the `slices` polyline, so the declared
smooth surface and the mesh differ by the slice sagitta.

### `PathExtrudeNode` and `SkinNode` declare nothing

The same argument and the same channel. Both know a profile and a path up front,
and both are what a user reaches for when the alternative is `polyhedron()`.

### No surface type for an ellipse

`linear_extrude.cc` refuses explicitly — *an ellipse, not a circle* — and again
for a sheared or oblique sweep. Any non-uniform `scale()` on a cylinder drops to
facets. This is the one item that needs a new `Surface` subclass rather than a
new call site, so it is larger, but it unlocks a class rather than a node. An
ellipse extruded is a `SURFACE_OF_LINEAR_EXTRUSION`, which needs no new subclass
at all if the 2D channel below is finished first.

### `SURFACE_OF_REVOLUTION`

A revolved arbitrary curve is exactly what the entity is for, and writing one
would take the glass above from 82 faces to three. Listed low because the band
stack already reaches 82 on the same shape: this buys a tidier file rather than
coverage.

### One derivation, not five guards

The five declaration sites grew one at a time and their shape is the shape of
their history. `geometry/rotate_extrude.cc` decides what a profile segment sweeps
with three refusals and a fallthrough, and **the second refusal is wrong** — a
segment touching the axis at constant height sweeps a disc, which the line above
has already taken, and one touching it while the height changes sweeps a cone
whose apex is on the axis, which is a wall like any other. It cost every solid of
revolution that meets its own axis, and it read as a deliberate decision because
it carried a comment.

What replaces it is a **total function**: segment class and operation in, surface
out, with every class enumerated — horizontal, vertical, sloped, axis-touching,
arc, arc centred on the axis — and no silent `continue`. An unhandled case then
reads as an absence rather than as a decision.

**Everything else derives from its 2D representation.** `Outline2d` already
half-carries it — arcs survive, which is why `offset(r = 2)` corners come out as
cylinders — and finishing that channel is what makes one derivation possible for
both extruders. The ellipse item dissolves into it, the twist becomes one branch
rather than a special case, and the apex fan becomes one row of the enumeration.
And the discrepancy that started this — `cylinder(r1 = 10, r2 = 0, h = 20)`
writing a cone while the identical solid revolved from a profile writes 96 planes
— stops being possible, because both answer from the same table.

**The primitives keep declaring directly, and that is deliberate.** A function
that knows its own surface should say so in one line that cannot be wrong. The
bugs are not in the primitives; they are in the derivations.

### What still rests on the mesh with no declaration at all

Surveyed 2026-09-08, not acted on. `minkowski()` declares nothing, so a rounded
cube exports as 142 planes. `hull()` declares its inputs but not the blend it
creates, so a hull of two spheres arrives as 28 recognised cones. `import()`,
`surface()` and `projection()` have nothing to declare.

Each of the first two is analytically known from its operands — a minkowski with
a sphere is planes, cylinders and spheres — so the question worth asking of each
is what the operands' declarations *imply* about the result. That is a different
question from the one `minkowski()`'s deliberate record-dropping answers, and it
does not reopen it: implying a surface from operands is not the same as carrying
a record through an operation that invalidated it.

### A wall split by a Nef boolean

On `--backend=CGAL` a boolean leaves the wall of a ring it never touched cut into
arcs at the seams where the operands met, and the recogniser then sees several
walls where there is one. Merging runs of coplanar-and-cocylindrical bands back
together *before* the rim rules are applied would recover it, and would also pick
up seams a Manifold boolean leaves behind.

### And the standing decision

**Whether the analytic path stops being experimental.** Both flags are off by
default. Nothing measured so far settles it; what would is the interop kit
passing in SOLIDWORKS and Fusion together, read with the recognition column
beside the fault column.

---

## A prompt to start the next session with

> Read `doc/step-export-wip.md`, then `doc/step-export-development.md`. Build and
> test mechanics are in `CLAUDE.md`. The branch should be green; verify with
> `ctest --test-dir build -R step` before changing anything, and run it under the
> machine's own locale rather than `LC_ALL=C`, which breaks the UTF-8 filename
> tests. Confirm `cadquery-ocp==7.8.1.1.post1` is installed, or the round trip
> skips in silence.
>
> Take open item 1: diagnose SOLIDWORKS fault code 17, which after the projection
> fixes of 2026-09-08 is the only code left standing alone in the coupon kit — on
> `f02-band-fn032`, `f04-band-fn064`, `r01-lid10` and `r02-bayonet`.
>
> Work it as five whys with the branches named before each measurement, and
> record the refuted ones — four of the five links in the last such chase were
> wrong, and the refutations were worth more than the answer. Every expectation
> must be derived from the model and never captured from a run; the derived screw
> sweep volume is `turns * 2pi * A * (R + dc)` and the standalone ridge is the
> control that removes the boolean. Read `-FaultDetail` before forming a theory,
> and report how much of each sweep was recognised beside any fault count,
> because `faults=0` over a half-faceted sweep has been mistaken for success in
> this project before.
