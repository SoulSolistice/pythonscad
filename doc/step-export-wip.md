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
ctest --test-dir build -R 'export-step-|mutations'
```

That regex picks up the fixtures — 33 SCAD in `tests/data/scad/step-export/` and
12 Python in `tests/data/pythonscad-step-export/` — plus
`bspline-check-mutations` and `closed-sphere-check-mutations`, 50 tests in all.
**It has to be this regex and not `-R step`**, which matches 45 and drops
precisely those last two: the harnesses whose job is to prove the other tests
would fail if the defect came back. Measured 2026-09-09, 45 against 50. Run it under the
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

**Reproduced 2026-09-09** on coupons re-exported from this commit, and `f02` has
now given `faults=2 faultyfaces=2 codes=17` on four separate runs. Two rows of
that table also gained detail worth carrying:

- The **untapered** wall variants are clean at every `$fn` — 24, 32, 48, 64 and
  96 — which closes an open item and refutes the taper reading; see open item 1.
- **Recognition is flat.** Every wall variant now claims 90.4–90.5% of its
  sweep's facets whole, across `$fn` and across the taper, over three of the
  profile's four spans. The fourth span is the one buried in the wall, which the
  union cuts away, so it is structural rather than a defect — but it means a
  quarter of the declared surface is never claimed and no line in the report
  says so in those terms.

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

### 1. Fault code 17: the population is known, the trigger is not

`swFaceBadEdge`. Chased as five whys on 2026-09-08/09, with nine predictions put
to SOLIDWORKS. **Two held and seven failed.** What that bought is the population;
what it cost is every mechanism proposed for it.

**Settled.** `-FaultDetail`, read fresh, names the faulty faces on every faulty
coupon: **two faces, the swept B-spline and the bore cylinder**, two code-17
records apiece, `faultyedges=0`. Not a planar facet, not a cap, not an edge.

```text
f02-band-fn032-analytic  face 1  bspline   1027.844725  0.0298;0.0882;17.3483  17/17
f02-band-fn032-analytic  face 4  cylinder  2098.714282  0.1454;0.0000;20.0000  17/17
f04-band-fn064-analytic  face 2  bspline   1019.021056  0.0748;0.0634;21.9361  17/17
f04-band-fn064-analytic  face 4  cylinder  2094.822070  0.0267;0.1369;20.0000  17/17
```

Read the areas. We write **nine** faces on the bore totalling about 2100 mm²;
SOLIDWORKS reports **one** faulty cylinder of 2098.71. It knitted all nine and
then objected to the result. The faces it complains about are not the faces we
wrote — which is the single most important thing this chase established, and the
reason the next step is a change of instrument rather than of theory.

**Refuted, each by measurement** — the detail is in
`doc/step-export-development.md` §10:

- the boundary sag (`f01` has the largest in the family and no fault);
- the sliver (`f02` and the same model untapered carry the *same edge*,
  0.000916 mm, both ends at `r = 20.000000000`, and one is faulty);
- the ratio of the two, which separated all seven files whose verdicts were then
  known and failed on the next three, one of them with the worst ratio in the
  set;
- the share of the sweep claimed, flat at 90.4–90.5% across the whole family;
- the wire closing, and the vertex-off-edge, which is *large on the clean*
  members and zero on the faulty ones;
- the taper, killed by walking `RUNOUT` continuously: clean at 0, faulty at 0.05
  to 0.07, clean at 0.08 to 0.10, faulty at 0.20 and 0.40.

Five files that differ in one number, three faulty and two clean, and **not one
quantity measurable on our own output tells them apart** — shortest edge, worst
edge off its face, their ratio, faces written, faces read, planes, ellipses,
vertex-off-edge, volume. Nor does SOLIDWORKS' own re-export: the knitted face
structure of the clean `t010` and the faulty `t005` is near-identical.

**What still holds underneath it.** The slivers are real, they are the
boolean's, and their lengths are predicted to about 5% by the model's two
angular steps — see the alignment entry in §10. They are simply not what
SOLIDWORKS objects to.

**Next, and it is about the import rather than the export.**

1. **`-FaultDetail` over the whole taper series including the clean members.**
   It only emits rows for faulty entities, so the clean files' corresponding
   faces have to be reached another way — which is what (2) is for.
2. **Round-trip the series** and read what came back. This is the only way to
   see a knitted face as geometry rather than as an area and a centre. Already
   run once and it did not separate them, but the per-face comparison was coarse.
3. **Ask whether knitting is the variable at all**, by importing one faulty
   coupon under `knit=form-solids` against `knit=do-not-knit`. That is a settings
   change and therefore the user's to make, but it is one toggle.
4. **Eyeball `t007` against `t008`.** They straddle the sharpest boundary found —
   run-out 0.07 faulty, 0.08 clean, 19 faces each — and nothing numeric
   distinguishes them. Human eyes have found two defects in this project that no
   instrument did.

Positive control in the same session, every time: `f02-band-fn032-analytic.stp`
must read `faults=2 faultyfaces=2 codes=17`, or the run says nothing. It has
reproduced that on four separate runs.

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
shown not to move.

**It now has a second justification that owes nothing to code 17.** Opened in a
CAD system, the boundary of a declared sweep against the bore is *visibly*
jagged — a staircase, not a curve. Measured, the turning angle between
consecutive boundary chords has a median of 5 to 15 degrees and a maximum near
94 on every member of the family, clean and faulty alike. That is the mesh's
intersection polyline standing in for a curve neither surface has any reason to
step along, and the crossing curve is what replaces it. A user sees it before
any instrument does, which is an argument the slack measurement could not make.

The full record of what was built and why it was reverted is
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

**Half answered on 2026-09-09.** The *claim* was re-counted post-fix and it is
now flat: every wall variant claims **90.4–90.5%** of its sweep's facets whole,
across `$fn` and across the taper alike, so the 56.3% / 49.9% split that the
"faults=0 is bought by refusing facets" reading rested on no longer exists, and
the faulty and clean members are indistinguishable on it. The kit records the
two numbers per file now, in `sweep_whole` / `sweep_cut` / `sweep_pct_whole`.

What has *not* been re-counted is the refusal column itself — where along the
sweep the outlier rule fires, post-fix. The mechanism (it fires on the profile
changing shape, not on the helix, the tessellation or the wall) should hold, but
the tenths-of-the-sweep numbers in `doc/step-export-development.md` are pre-fix.
One kit run and one refusal column.

---

## Unexplained, and to be validated

- ~~**Untapered band family at `$fn` 32** trades code 17 for 13/30 with sixteen
  faulty edges~~ — **closed 2026-09-09, and it was a pre-fix reading.** Measured
  again after the projection fixes, the untapered wall variant at `$fn` 32
  imports **clean**: `solid, 9 faces, faults=0`. So does `$fn` 64, and so do 24,
  48 and 96. The whole untapered row is clean, and the 13/30 signature is gone
  with the half of the sweep that used to be left faceted.
- **The run-out band, and it is the sharpest unexplained result in the file.**
  With `$fn` 32 and everything else held, code 17 is on at run-out 0.05, 0.06
  and 0.07 and off at 0, 0.08, 0.09 and 0.10, then on again at 0.20 and 0.40.
  Deterministic: `t007` faulty three times and `t008` clean three times in one
  session under six different file names, plus the control.

  **Across that flip every count the exporter makes is identical** — claims 607
  whole and 64 cut, 12 facets left faceted, 2 smooth regions of 12 facets,
  covers 521 over 218 runs, 3 of the profile's 4 spans, at 0.06, 0.07, 0.08 and
  0.09 alike. Nor does the boundary's shape move: the turning angle between
  consecutive boundary chords has median 11.586 on `t007` and 11.588 on `t008`,
  max 94.395 against 94.393, 14 edges over 30 degrees on each.

  **The faceted run-in's *area* is the one measure that nearly works, and a
  count hides it** — the same 12 facets span a run-in whose length changes with
  the parameter. Subtracting the wall's two constant annuli (810.531 mm², the
  only planes at run-out 0.20 and 0.40):

  ```text
  run-out      0     0.05    0.06    0.07  |  0.08    0.09    0.10    0.20  0.40
  run-in area  20.60 32.63   37.94   35.32 |  33.07   31.11   29.41   0     0
  verdict      clean FAULTY  FAULTY  FAULTY|  clean   clean   clean   FAULTY FAULTY
  ```

  Pairwise it is right and it was found by eye before it was measured: `t007`
  carries more faceted run-in than `t008`, 35.32 against 33.07. Across the
  series it fails twice — the faulty band 32.63–37.94 overlaps the clean band
  0–33.07 by 0.44 mm², and both ends invert it, with a clean file at 20.60 and
  two faulty ones at **zero**. The analytic claim behaves the same way: inside
  0.05–0.10 the B-spline area falls monotonically 3353 → 3292 with a clean gap
  at the flip, and outside it the clean `t000` holds the largest B-spline area
  in the set and the faulty `t040` the smallest. A monotone quantity cannot
  produce a verdict that alternates.

  Seen by eye the two are "visually rather identical", and on the faulty one
  **virtually the whole sweep is flagged** — not a region of it. So whatever
  SOLIDWORKS is keying on is not in any count we produce, not in the boundary's
  shape, and not local. That is what makes the knit toggle and the crossing
  curve the two things worth doing next, rather than another measurement of the
  file we write.
- **An `ELLIPSE` whose own end vertex is 1.02 mm off it**, on `f01` — a member
  SOLIDWORKS calls **clean**. Computed from the file's own entities with no
  kernel: vertex `#5420` at `(-2.38819, 19.68559, 38.01818)` against an ellipse
  of `a = 120.511759, b = 20.000000`. Semi-axes `r` and `r/sin θ` put that plane
  **9.6 degrees off the cylinder's axis** — a glancing cut, where a small error
  in the plane becomes a large one along the curve. The three clean band members
  write 8, 40 and 144 ellipses and carry vertex-off-edge of 1.02, 0.25 and 0.058;
  the faulty two write none and carry 4e-15. `sectionTiltFloor`'s question from
  the other end, and with `check-and-repair` on, "SOLIDWORKS calls it clean" may
  mean it repaired it.
- **A face on the bore, bounded at the outer wall.** In the untapered wall
  variant at `$fn` 24, four faces are written on the bore cylinder `r = 20` with
  vertices at `r = 23` — their edges are **3.000000 mm** off the surface they
  claim, exactly the wall thickness. Confirmed by eye: *"the entire inner wall
  is gone; the sweep is looking good but all faces marked as faulty."*
  SOLIDWORKS imports it as **half the part**,
  10401.67 against its siblings' 20359, with code **16** `swFaceBadVertex` ten
  times on one cylinder and code **21** `swFaceSelfIntersecting` five times on
  each of two 18.9 mm² planes. Not code 17, one cell of the factorial only, and
  the more serious of the two defects.
- **The analytic standalone ridge drifts away from its derived volume as the
  mesh is refined.** Against `turns * 2pi * A * (R + dc)` = 5119.783734 it
  measures −7.3e-6 at `$fn` 24 and −3.2e-6 at 32, then **+7.9e-4 at 48, +7.9e-4
  at 64 and +4.9e-4 at 96** — the wrong direction for everything. It goes with
  the outlier refusal firing on a degenerate threshold there ("further off the
  fit than four times the 0.0000 this claim is typically off, by up to 0.0000")
  and with the sweep being cut into 4, 4, 18, 40 and 82 faces. The faceted
  control matches the model's own polyhedral volume to 1e-15 at every `$fn`, so
  the derivation and the pipeline are both calibrated and the drift is real.
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
> `ctest --test-dir build -R 'export-step-|mutations'` — **not** `-R step`, which
> drops the two mutation harnesses — and run it under the machine's own locale
> rather than `LC_ALL=C`, which breaks the UTF-8 filename tests. Confirm
> `cadquery-ocp==7.8.1.1.post1` is installed, or the round trip skips in silence.
>
> Take open item 1. Code 17's *population* is settled — `-FaultDetail` names the
> swept B-spline and the bore cylinder on every faulty coupon — and every
> *trigger* proposed for it is refuted, including the two that survived longest,
> the sliver and the taper. Nothing measurable on our own output separates the
> faulty files from the clean ones, and the faulty cylinder is nine of our faces
> knitted into one, so it does not exist until SOLIDWORKS makes it.
>
> So stop measuring the file we write and measure the import: the round trip,
> `-FaultDetail` across clean and faulty alike, and the one knit toggle. And put
> `t007` against `t008` in front of your own eyes — run-out 0.07 faulty, 0.08
> clean, 19 faces each, and no number tells them apart.
>
> Put `f02-band-fn032-analytic.stp` in **every** interop run as the positive
> control; it must read `faults=2 faultyfaces=2 codes=17`, or a page of clean
> rows means nothing. Start SOLIDWORKS by hand and never kill the driver
> mid-import. Every expectation must be derived from the model and never
> captured from a run, and report how much of each sweep was recognised beside
> any fault count — the kit records it now, in `sweep_whole` / `sweep_cut` /
> `sweep_pct_whole`.
