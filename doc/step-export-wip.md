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

### 1. Fault code 17: it moved, on the one coupon whose boundary was written

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

**What was tried after the population was found, and what it returned.**

1. ~~Round-trip the series and read what came back.~~ **Run.** SOLIDWORKS'
   own re-export does not separate them either: the knitted face structure of
   the clean `t010` and the faulty `t005` is near-identical, 3 B-splines and 10
   cylinders each, with the largest cylinder the same 2890.265 mm² in every
   member of the series.
2. ~~Ask whether knitting is the variable at all.~~ **Refuted.** *Try forming
   solid(s)* and *Create analytic faces* were both changed and SOLIDWORKS'
   reading of `f02` is bit-identical across the change, down to the two faulty
   faces' areas to six decimals. The knit option has little to act on here: our
   files carry a `MANIFOLD_SOLID_BREP` over one `CLOSED_SHELL`, so the solid is
   read rather than sewn — which is also why solids arrived under *Do not knit*
   all along.
3. ~~Eyeball `t007` against `t008`.~~ **Done, and it returned three things.**
   The two are "visually rather identical". On the faulty one **virtually the
   whole sweep is flagged**, not a region of it, which retires every local
   reading of a face-level complaint. And the boundary of the sweep is
   *visibly* a staircase — which turned out to be true of every member, clean
   and faulty alike, and true of the faceted control as well, so it is the
   mesh's and not the analytic path's. It is not the trigger; it is open item 3.

**So there is no measurement left on this side.** Everything the exporter
counts, everything measurable on the file, the boundary's shape, SOLIDWORKS' own
re-export and three import settings all fail to separate a faulty coupon from a
clean one that differs from it in a single parameter by 0.01. What remains is
not a better measurement of the trigger but removing the class: give the sweep a
boundary that lies on its own surface (open item 3), and there is no longer a
mesh polyline for a kernel to object to.

#### Run 2026-09-10: the prediction above was put to SOLIDWORKS, and it held

The paragraph before this one said the way out was not a better measurement but
removing the class - give the sweep a boundary that lies on its own surface and
there is no mesh polyline left to object to. Open item 3 built that boundary.
This is what SOLIDWORKS made of it.

**Settings, read from Tools > Options > Import by the driver itself rather than
trusted to a label:** `solid+surface=on free-curves=off run-diagnostics=on
analytical-conversion=on attributes=on step-config-data=off
multibody-as-parts=off knit=form-solids units=file-units
assembly-mapping=default check-and-repair=1 custom-tolerance=0`.

**Positive control, same session, first:** the recorded 2026-09-08
`f02-band-fn032-analytic.stp` - 640 chords, no crossing curve - re-imported as
`solid, 9 faces, faults=2 gaps=0 faultyfaces=2 faultyedges=0 codes=17`. Its
fifth reproduction. The harness, the machine and the settings are behaving, so
a difference in the new file is the file.

**The result.**

| | control, 640 chords | new, 593 curves + 47 chords |
| --- | --- | --- |
| faults | 2 | **1** |
| bspline face | 1027.845 mm², **17/17** | 1032.453 mm², **21** |
| bore cylinder | 2098.714 mm², **17/17** | **not faulty** |
| volume | 19555.2696 | 19511.9331 |

**The bore cylinder's code 17 is gone**, which is the predicted mechanism
exactly: its boundary is now the curve it is cut on rather than a polyline of
chords, and it no longer has an edge that does not lie on it. This is the first
time code 17 has moved on this coupon in the whole chase.

The sweep's fault is *not* code 17 any more either. It is **21**,
`swFaceSelfIntersecting`, one record rather than two, on a face 4.6 mm² larger
and 1.05 mm higher up. That is a different objection to a different thing and it
is unexplained - see below.

**Corroborated by a number the model fixes and the tessellation must not.** The
analytic volume cannot depend on `$fn`, and now it nearly does not:

| | analytic volume |
| --- | --- |
| f02 at `$fn` 32, control | 19555.2696 |
| f02 at `$fn` 32, new | **19511.9331** |
| f04 at `$fn` 64, new | **19511.5976** |

0.0017% apart, where the old f02 sat 43 mm³ from its own `$fn` 64 sibling. The
two coupons are the same solid and now say so.

#### And why the other three coupons did not move

They never got the boundary. Measured on the files themselves:

| coupon | crossing curves | chords | SOLIDWORKS |
| --- | --- | --- | --- |
| `f02-band-fn032` | **593** | 47 | cylinder clean, sweep 21 |
| `f04-band-fn064` | 1 | 1251 | 17/17, unchanged |
| `r01-lid10` | 1 | 623 | 7/17/21 |
| `r02-bayonet` | 1 | 623 | 7/17/21 |

The one coupon whose boundary was actually written as the crossing curve is the
one whose code 17 moved, and the three that kept a chorded boundary kept their
fault. That is the correlation this chase has been trying to establish, and it
is **n = 1** on the positive side.

**The gate is named and it is not one of the ones fixed for item 3.** On f04 the
solvers work: 1278 of 1406 junction vertices reach the crossing curve and *no*
corner is left on one surface. Only two edges are ever candidates. The exporter
says why itself:

```text
1263 corners are left where the mesh put them: moving them would bend 7 faces
that keeps a plane, and half a boundary moved is worse than none
```

f02 prints no such line. A veto that protects planar faces holds 1263 of 1406
corners on the mesh, so almost no edge becomes a crossing candidate at all.
Lifting it is what would replicate this result three more times, and until it
is, n = 1 is what this is.

#### What not to read into it

- **Code 21 is new and unexplained.** It may be a real self-intersection the
  fitted boundary introduced, or SOLIDWORKS re-diagnosing geometry it now reads
  differently. Nothing here distinguishes those and it wants its own chase.
- **47 chords remain on f02**, so this is not a coupon with an exact boundary -
  it is one with a mostly exact boundary, and the sweep is still faulty.
- **`r01-lid10-faceted`, the control, now reports `faults=6 codes=24`.** A
  faceted control with faults of its own weakens lid10 as a test, and that is
  worth understanding before reading anything into its 7/17/21.
- **lid10 writes 810 analytic faces** where 818 and 822 are recorded earlier in
  this file. That moved with the corner work and has not been re-derived against
  the model, which per §7 is the only way that number means anything.

Positive control in the same session, every time: `f02-band-fn032-analytic.stp`
as it stood on 2026-09-08 must read `faults=2 faultyfaces=2 codes=17`, or the
run says nothing. It has reproduced that on five separate runs. Keep that file;
the current export of the same coupon no longer reads it, which is the point of
the entry above.

### 2. `declare_sweep` is landed and nothing uses it

- **No fixture.** It needs one with derived expectations. The screw sweep's
  volume is a closed form, so this is one of the few sweeps whose `VOLUME:` can
  be stated.
- **`step-band-family.scad` is the obvious first caller.** It computes an exact
  profile and an exact helix, emits a `polyhedron()`, and throws both away.
- **No SCAD builtin.** `src/python/pyfunctions.cc` has `declare_sweep`;
  `src/core/DeclareSurfaceNode.cc` does not. `doc/step-export-development.md`
  says to keep both front ends in step, and this is the one that is not.

### 3. The crossing curve on a sweep: why 142 edges fell back to chords

**Built 2026-09-10.** A declared sweep is now trimmed to the curve where it
crosses a quadric, the same way two quadrics have been since 2026-09-07. The
full account of the three things that had to be right is in the commit; the two
worth carrying here are that the previous revert mis-attributed the shell
opening — `along` and the grid emitter walk the same vertex sequence, and the
fault was that `decide_sections` rebuilt `crossing_edges` *between* the two
emitters — and that the tessellation band belongs in the gate asking whether a
mesh vertex is on a surface, and nowhere else. Putting it in the fit as well
makes `intersectionArc` accept a cubic immediately and write a curve no better
than the chord: identical boundary deviation to seven digits, measured.

**What it bought.** On `f02-band-fn032`, 498 of 640 candidate edges are written
as the true crossing, fitted to within 9.96e-08 of both surfaces. Faces whose
boundary leaves their own surface by more than 1e-3 go from 9 of 12 to 7 of 12.
Shell closed everywhere, 50/50, and lid10 unchanged at 818 faces over one shell.

**What it did not.** Code 17 does not move: `f02` reads `faults=2
faultyfaces=2 codes=17` with 498 crossing curves exactly as it did with none,
and the run-out flip survives — `t007` faulty, `t008` clean, on the new
exporter. Its volume moves by 0.006%. That is not yet a refutation: 142 edges
were still chords, and *one chorded edge is enough to fail its face* is this
project's own finding from the first attempt.

#### Why the 142 fail: an iteration budget, not a geometry

Chased as five whys on 2026-09-10. Four links hold and are measured; the fifth
is open, and two hypotheses died on the way — including the one this section
used to name.

**Why does SOLIDWORKS fault the swept B-spline and the bore cylinder, and
nothing else?** Those two faces share all 640 of the edges that lie on neither
of them exactly. Measured against the bore it bounds, a fitted arc strays at
most **1.04e-07**; a chord strays up to **9.57e-02**, median 9.5e-03. Nothing
else in the file is off by more than 1e-6. *Consistent, not confirmed — it still
has to be put to SOLIDWORKS.*

**Why is an edge a chord rather than the crossing curve?** Because
`intersectionArc` is only attempted where both endpoints already lie on both
surfaces. Every one of the 1280 endpoints lies on the **bore** to 6.2e-10, the
chords' as much as the arcs'. It is the **sweep** they miss: the 498 by at most
1.2e-13, the 142 by **1.2e-03 to 1.0e-01**. Two populations nine orders apart
with nothing in between.

**Why do those endpoints miss the sweep?** Because that is what the corner
placement does on purpose. When a junction vertex cannot be put on both
surfaces, `export_step.cc` places it on the exact one — the declared bore — and
leaves the fitted sweep as a tolerance to be checked against. The exporter says
so itself: *138 corners whose two owners do not cross transversally are placed
on the exact one of them, moving at most 0.0963*.

**Why can it not put them on both?** Because it finds the crossing by
**alternating projection capped at 64 iterations**, and that contracts the error
by `cos^2(theta)` per cycle. A corner starts on a bore facet's chord plane,
`20(1 - cos(pi/32)) = 0.0963` inside the cylinder, and has to reach 1e-9. So it
needs `log(1e-9/0.0963) / log(cos^2 theta)` cycles: **30 at 42.70 degrees, 237
at 15.85**. A 64-cycle budget reaches exactly the crossings above **29.98
degrees** — and of the 142 chords, **141 cross below 29.98 and one at 30.29**,
against a median 42.70 for the 498 that succeed. A derived threshold on a
measured population boundary, agreeing to a third of a degree.

**Why is the cap 64?** Because it was never derived. The comment beside it
records the symptom as a property of the model — "where the two do not cross
transversally the projection does not converge" — and quotes the median failing
angle, 15.85 degrees, as though that were a tangency. It is not. At 15.85
degrees the projection converges; it takes 237 cycles.

**Root cause: a fixed iteration cap on a linearly convergent projection, written
down as a geometric fact.**

#### What the experiment then refuted

Raising the cap, with the counts predicted first from `cos^2(theta)` and
recorded before the run:

| cap | reaches above | corners left off | vertices on the curve | chords predicted | chords measured |
| --- | --- | --- | --- | --- | --- |
| 64 | 29.98 deg | 138 | 502 of 704 | — | 142 |
| 512 | 10.82 deg | 18 | 622 of 704 | 21 | **56** |
| 4096 | 3.84 deg | 0 | 640 of 704 | 1 | **47** |

The direction is not in doubt — the corners the placement gives up on go 138 to
18 to zero, and the edges written as the true crossing go 498 to 584 to 593. The
*magnitude* is refuted: at cap 4096 **every** corner converges and 47 edges are
still chords. The iteration budget accounts for 95 of the 142 and no more.

**And the survivors are not the shallow ones.** The obvious follow-on — that the
two-surface Newton in `projectOntoBoth` is conditioning-limited, its 2x2 having
determinant `~sin^2(theta)`, so attainable precision is about `eps/sin^2(theta)`
against a demanded `solve_tol` of `1e-14*scale` — predicts failures only below
about 1.6 degrees. The 47 span **1.35 to 23.46 degrees, median 14.86**, spread
almost evenly from 6 degrees up. At 20 degrees that limit is 1.9e-15 against a
demanded 2.8e-13. Refuted.

#### Also refuted, earlier the same day

- **Tangency at the run-out.** The crest returns to exactly the bore's radius at
  each end, so the two surfaces are tangent there by construction, and 131 of
  the 142 chords sit in the run-in and run-out fifths. `FLOOR` was added to
  `band-family-controls.scad` to stop the taper short and remove the tangency:
  chords went 142 to 144. The location was right and the mechanism was wrong.
- **The band gate admitting endpoints the fit then rejects.** Tightening the
  `on` gate from `declaredBand()` to `1e-9*scale` was tried. It is not the
  separator — both populations sit on the bore exactly, and the difference is
  the sweep. What the attempt did show is that the crossing curve is
  load-bearing for the *cylinders'* own acceptance: with fewer curves,
  `boundary_lies_on_surface` refuses the bore patches and the whole analytic
  export collapses to 705 facets.

#### Still open

**The 142 are down to 47, and every corner is placed.** Three changes, each of
which had to be measured before it could be believed, and two hypotheses died
between them — the full account is in items 9 and 10.

| | vertices reaching the curve | corners left off | chords |
| --- | --- | --- | --- |
| at the head of this branch | 502 of 704 | 138 | 142 |
| Newton in the corner placement | 598 | 42 | 116 |
| `GridSurface::project` per span | 598 | 42 | 116 |
| the solve tolerance made absolute | **640** | **0** | **47** |

593 crossing curves and 47 chords — which is exactly what an iteration cap of
4096 reached in the experiment that opened this, arrived at by converging rather
than by iterating.

**What stops the remaining 47.** Not the fitter and not the projector, both of
which were suspected here and cleared. The fitter reports them as coming within
1.21e-02 to 4.73e-02 of the sweep against the 1e-07 asked for, with *none*
failing to fit at any degree, and the arcs sit on the quadric they bound to
5.5e-06 — so the whole miss is against the declared sweep. 64 of the 704
junction vertices still do not reach a crossing curve, being claimed by two
coaxial cylinders 3.000 mm apart that never meet, and those are correctly
refused. Whether the 47 are the edges those 64 touch has not been measured, and
is the next thing to establish.

**Every crossing curve has a pcurve on the sweep and none on the cylinder.** All
640 edges are a `SURFACE_CURVE` carrying exactly one `PCURVE`, on the B-spline
side; the bore is left to re-project the curve itself. The code that would give
both sides one cannot run: it requires `crossing_sides[key].size() == 2`, and
the only push site is the quadric emitter, so a sweep-against-cylinder edge
always has one. It is legal STEP either way — but the pcurve pass exists
precisely so that a kernel is *told* rather than left to decide, and on these
two faces it is not.

#### The measurement to make next

Put the coupon to SOLIDWORKS. Its boundary is now 593 crossing curves against 47
chords, where the run that produced `faults=2 faultyfaces=2 codes=17` had 498
against 142, and every junction vertex that can be on both surfaces is. That
makes it the first honest test of "is code 17 the boundary", which every earlier
attempt was not.

Keep `f02-band-fn032-analytic` from the recorded run in the same session as the
positive control, and read `-FaultDetail` and the recognised share rather than
`faults=0` — a clean count over a half-recognised sweep has been mistaken for
success in this file before.

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

### 9. Four alternating projections: what each of them turned out to be

**Chased and settled 2026-09-10.** The corner placements found where a vertex's
owners cross by alternating projection, four loops of it, every one capped at 64
iterations. Alternating projection contracts the error by `cos^2(theta)` per
cycle, so a fixed cap is not a solver but a *crossing angle* below which the
loop gives up - 29.98 degrees, derived in item 3.

That was the theory. Only one of the four was what the theory said.

| site | intersects | gave up on | what it actually was |
| --- | --- | --- | --- |
| `export_step.cc` | surface ∩ surface | 138 band, 39 lid10 | **the iteration budget.** Fixed. |
| `StepKernel.cc` | surface ∩ line of two mesh planes | 280 | **not a solve at all** - see below |
| `StepKernel.cc` | surface ∩ plane | 0 | converges everywhere it is asked |
| `StepKernel.cc` | surface ∩ vouched plane | 0 | the same |

**The surface-against-surface one was the iteration budget**, and Newton on both
implicits fixes it. Measured with both flags on: junction vertices reaching the
crossing curve go 502 to 598 of 704 on the band family and 743 to 1079 of 1598
on lid10; corners left on one surface go 138 to 42 and 39 to 3; the band
family's chorded boundary edges go 142 to 116.

Two things were needed beyond swapping the solver. `projectOntoBoth` had to move
out of `StepKernel.cc` into `AnalyticFeatures`, beside `closestOnSurface`, where
`export_step.cc` can reach it - it is a geometry predicate on a `Surface` and
belongs there anyway. And it had to keep its **best** iterate rather than its
last: Newton's final step is not always its closest approach, and the caller's
acceptance test is a better judge of a candidate than the solver's own
tolerance. Gating on the tolerance instead threw away 124 corners of lid10 that
the acceptance would have kept.

**The surface-against-line one refuted the reason for touching it.** Its 280 are
94 corners that have no two mesh planes to cross - not a solve - 2 that are off
their own surface, and 184 that had **converged**: none off the surface, none
off a plane, every one refused for landing past the nearest neighbour. Newton
changed the numbers not at all.

What it was instead: a line meets a quadric **twice**, and the iteration was
finding the far root. Worst on lid10, 1453 times the allowed travel away. The
travel limit that caught it is a proxy for the question rather than the
question, and the comment beside it records that the far root once took a cone
that was exact out by 1.41.

So that site solves *along* the line now - `base + t.dhat`, one unknown, one
equation - which makes both planes exact to rounding and puts the far root out
of reach by construction. It changes no count on any fixture: within the allowed
travel the line comes no nearer the declared surface than a median 1.05e-02 on
lid10, 3.02e-01 on `step-exact-trim`, 4.42e-01 on `step-shared-arc`. Those 184
corners are correctly refused. Two facet planes crossing gives a line that is an
artifact of the tessellation, and where the surface curves away it can miss the
surface entirely.

**The two that give up on nothing are left alone deliberately.** A change with
no measurable effect on any fixture has regressions that are equally invisible.

**Reading the counts needed a fix of its own.** Both log lines in the pair
printed `int(triple)`, so the surface-against-plane line reported the
triple-point counter under its own text and the same 280 appeared twice; and
that site counted nothing at all, its three give-up paths being bare
`continue`s. Its figure was invisible rather than zero. The give-up reasons are
now kept apart - off its own surface, no two faces to cross, parallel faces, the
solve - because one number for five reasons says nothing about which to work on,
and that is the whole reason this item was mis-ordered to begin with.

**Seven unit tests** in `analytic_features_test.cc` cover both solvers, four of
them asserting a *refusal*: tangency, parallel planes, a ruling lying in the
surface, and a crossing outside the window. Deleting the window makes that last
one fail, which is the check earning its place.

### 10. Survey: approximations that could be sharper, and analytics that stop short

**Written 2026-09-10, out of items 3 and 9.** Both of those turned on the same
shape of defect - a method that converges, held under a budget or a criterion
that was never derived, reporting "cannot" where the truth is "did not". This is
a sweep of the rest of the exporter for the same thing, and the ordering is by
what is measured rather than by what looks worst.

Nothing here is a bug report. Each is a place where the code returns less than
it knows, with what is actually established kept apart from what is inferred.

#### (a) Approximations that could profit from a sharper method

**A1. `intersectionArc` interpolates at uniform nodes, up to degree 9.** The
collocation parameters are `t = i/degree` and the degree is raised until the fit
is inside tolerance. Uniform interpolation is the worst-conditioned choice as
degree rises, and this raises the degree *precisely* on the edges that are
hard, so the method degrades exactly where it is leaned on. Chebyshev-Lobatto
nodes cost nothing but the arithmetic to place them. **Inferred, not measured**:
no count yet of how many chords this would recover.

**A2. The foot-point implicit is first order, so Newton is linear on a declared
sweep.** `footPointImplicit` models the surface by the tangent plane at the foot
point, and says so in its own comment. **Measured**: `projectOntoBoth` on the
band family is flat at 598 of 704 junction vertices from 24 iterations through
1024. It is not iteration-starved, it is *model*-starved. A second-order local
model - the normal curvature at the foot point, available from evaluations the
projection already makes - would restore superlinear convergence there.

**A3. DONE, and it was not the objective.** `GridSurface::project` could not
find a point `evaluate` had just produced: 0.78 mm out on the band family's own
ridge, at the closing strip. The squared-distance floor below is real arithmetic
and was the wrong suspect — so was the descent, which Levenberg-Marquardt
damping did not move by one significant digit. The start was already a local
minimum: the profile is a polyline, so each span is a separate smooth piece a
descent cannot walk out of, and the best coarse sample lay on a flank while the
point sought was on the back. One start per *declared* span took it to 7.8e-07,
and holding the v difference inside its own span — a central difference at a
profile corner measures the average of two slopes — took the rest to rounding.
A unit test pins it, and fails by six orders without the per-span starts.

*What the arithmetic below still says, unmeasured:* `GridSurface::project`
descends on the *squared* distance. Minimising
`|S(u,v) - p|^2` rather than `|S(u,v) - p|` halves the significant digits:
within `sqrt(eps)` of the minimum the squared distance stops changing in double
precision, so the attainable accuracy in the distance itself is about
`1.5e-08` relative. The corner acceptance asks for `1e-9` absolute. **Inferred**
from the arithmetic and consistent with the band family's remaining 42 corners
being flat under every iteration count; **not yet confirmed** by instrumenting
the projection's achieved residual, which is the measurement to make.

**A4. Finite differences where the derivative is writable.** `GridSurface`'s
Jacobian is taken with `h = 1e-6` and `footPointImplicit`'s normal with
`h = 1e-5`; the derivative error of a central difference is about `eps/h`, so
`1e-10` and `1e-11` respectively. `evaluate` is piecewise polynomial in v and
interpolatory in u, so both derivatives are closed forms. This compounds A2 and
A3 rather than standing alone.

#### (b) Analytics that give up before their capability

**B1. DONE, and refuted as a cause.** `intersectionArc` no longer abandons the
fit when one degree's samples will not project — the next degree samples at
different parameters, so the attempt that failed says nothing about the one
after it. Correct, and worth no measurable change on any fixture: the reporting
added as B2 showed *zero* edges failing to fit at any degree. Sampling was never
what stopped them. The original text follows, and the measurement that made it
look like the candidate was sound — it was the inference from it that was not.

**B1 (as written).** `intersectionArc` abandons the whole fit when a single
interior sample fails. `if (!ok) return false;` - it does not try the next degree, and the next
degree samples at *different* parameters, so the attempt that failed says
nothing about the one that would have followed. **Measured**: at an iteration cap
of 4096, with every corner placed and none left off, 47 edges of the band family
were still chords. Those are arc failures and not corner failures, which makes
this the top candidate for the boundary that remains. **This is the next thing
to work on.**

**B2. DONE, and it is what found everything above.** The fitter now reports how
many edges never fitted at any degree, how near the rest came, and — the half
that mattered — how much of that miss is against a surface stated algebraically
against one that answers by projecting. On the band family: 6.60e-06 and
1.73e-02. Three orders apart, and it is what sent this at the projector. A count
of chorded edges could not have said any of it.

**B2 (as written).** The degree cap of 9 is a silent floor. An edge whose curve will not fit
by degree 9 becomes a chord - an error of order 0.1 mm against a demanded 1e-7 -
and nothing reports how close it got. The fitter computes `intersectionArcError`
and throws the number away. Saying it would turn "142 chords" into "142 chords,
worst fit 3.4e-05", which is the difference between a wall and a gradient.

**B3. `sameSurfaceGeometrically` can merge two different cones.** From the
external audit and confirmed by reading: slopes are compared in absolute value,
justified by reversing an axis negating the slope, but `axesAgree` accepts
*parallel* axes too - so two same-direction cones of slope `+s` and `-s` sharing
a refpt compare equal though one widens and the other narrows. Reachability into
a wrong export is still not established.

**B4. `_off_surface` records a failed projection as zero error.** Its `else 0.0`
reads "no projection result" as "no deviation" - fail-open, in the instrument
that measures how far a face's boundary leaves its own surface.

**B5. `step_real` rewrites every nonfinite as `0.`** A failed computation becomes
an ordinary-looking coordinate, and the validator's `nan`/`inf` text search
cannot see it.

**B6. Nonuniform scaling drops plane declarations.** `PlaneSurface` inherits the
similarity-only `transform`, though a plane is representable under any affine
map. A recognition loss, never a wrong solid.

**B7. `check_surface_curves` covers only cylinders and cones.** The sweep
crossing curve writes `SURFACE_CURVE`s on a B-spline face, 498 of them on
`f02`, and the validator certifies none of them. Needs a de Boor evaluator; exposure
measured at 8.4e-6 mm, so it is a coverage gap rather than a live defect.

#### Deliberately not on this list

The two remaining alternating projections, `surface ∩ plane` and `surface ∩
vouched plane`, both capped at 64. Measured over all 33 STEP fixtures plus
lid10: they give up on **nothing**. Changing a method whose failure path no
fixture reaches means the change's own regressions are unreachable too.

#### Order of work

B1, B2 and A3 are done, and the ordering above was wrong in an instructive way:
B1 was ranked first on a measurement that was sound and an inference from it
that was not, and A3 was ranked third with a caveat to measure before assuming
— which is the only reason the actual defect, six orders worse than the
arithmetic predicted, was found rather than papered over.

What is left:

1. **A2**, the first-order foot-point implicit, which is why Newton is linear on
   a declared sweep. Now the more interesting of the two, because A3's fix
   removed the noise that was hiding it.
2. **A1**, cheap and self-contained: Chebyshev-Lobatto collocation in place of
   uniform. Worth measuring against the 47 chords that remain.
3. **B3 to B7**, unchanged, and none of them measured to reach an export.

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

## External review, 2026-09-10: what was checked and what it found

A read-only audit of `d3aaafa4` by an outside reviewer, checked here claim by
claim. Every entry below was reproduced before being accepted — by running the
code, by a unit test written to fail first, or by reading the implementation —
and the reviewer's hit rate on the ten claims examined was ten out of ten.

Its scope limits are worth repeating because they bound what the audit can be
read to say: it never inspected `AnalyticFeatures.cc`, saw `StepKernel.cc` only
in fragments, ran no tests, and executed no CAD import. Its own closing line —
that it cannot say what causes fault code 17 — agrees with this document.

### Fixed

| finding | how it was reproduced |
| --- | --- |
| **The foot-point residual accepted a point off the end of a patch.** `evaluate` clamps and `project` returns `true` unconditionally, so for a point beyond the patch the foot lands on the boundary and `n . (p - foot)` measures the *tangent plane*, not the surface. A flat grid over the unit square and a point at `(2, 0.5, 0)` gives a residual of **exactly zero** one unit outside. | Read; counterexample exact. Newton keeps `f` and `grad`; every acceptance test now takes the true distance `\|p - foot\|`. Nothing measurable moved — still 498 crossing curves at 9.96e-08 — so the case was not being reached, but the criterion could not have excluded it. |
| **A mirrored sweep declared different geometry from its mesh.** `SweepSurface::transform` gates on `m^T m == scale^2 I`, which every orthogonal matrix satisfies whatever its determinant, and `evaluate` builds its second radial direction as `normdir x ref` — negated by a reflection. | Unit test written first as `[!shouldfail]`: it reported **20.0 mm**, exactly `2 x radius`, the point on the far side of the axis. Reflections are now refused. |
| **A rational boundary curve was never held to the surface's weights.** Count and positivity were checked; the values were not compared with the rail they claim to be. | Mutation from the review, run before any fix: the fixture's middle weight changed from 0.70710678 to 0.5 — a parabola where a circular arc was meant — was **accepted**. Now rejected, compared up to a common positive factor so that uniformly rescaled weights still pass. Three mutations added; ten in the harness. |
| **The corner allowance scaled by distance from the world origin.** `max(1e-7, 1e-8 * extent)` with `extent` the largest `\|p\|`, so translating an unchanged solid relaxed its own acceptance — near 1e9, an allowance near ten coordinate units. | Read. Now the bounding-box diagonal, which is translation invariant. Suite unchanged, so nothing was relying on it. |

### Confirmed and open

| finding | how it was reproduced |
| --- | --- |
| **A zero-pitch sweep rejects points its own evaluator made.** `make` refuses `turns == 0` and accepts `pitch == 0`, and `localCoords` then picks the turn from the height, which on a ring says nothing — so `k` is forced to zero and anything past the half turn inverts to a negative `t`. | Unit test: `evaluate(0.75, 0)` on a radius-10 ring is not `onSurface` to 1e-6. Pinned `[!shouldfail]`; the repair is a decision about what `declare_sweep` accepts, not a local edit. |
| **An empty `DATA` section validates.** All structural checks sit under `if entities:`, so a file with none accumulates no problems. | Ran it: `STEP validation ok (0 entities, 0 faces, 0 shell(s))`, exit 0. |
| **Shell closure is accounted globally, not per shell.** `check_topology` flattens the faces of every shell into one list and one `edge_dirs` map, so an edge used once in each of two shells satisfies "twice, in opposite directions". | Split a validated cube's single `CLOSED_SHELL` into two three-face halves — neither watertight. Validator: `ok (2 shell(s))`, exit 0. |
| **`sameSurfaceGeometrically` can merge two different cones.** Slopes are compared in absolute value, justified by "reversing a cone's axis negates it" — but `axesAgree` accepts *parallel* axes too, so two same-direction cones with slopes `+s` and `-s` and a shared refpt compare equal though one widens and the other narrows. | Read. Reachability into a wrong export not established; it changes which branch a junction takes. |
| **A failed projection is recorded as zero error.** `_off_surface` ends `else 0.0`, so "no projection result" reads as "no deviation". | Read. Fail-open. |
| **`step_real` rewrites every nonfinite value as `0.`** A failed computation becomes an ordinary-looking coordinate, and the validator's `nan`/`inf` text search cannot see it. | Read. |
| **Nonuniform scaling drops plane declarations.** `PlaneSurface` has no `transform` override and inherits the similarity-only base, though a plane is representable under any affine map. | Read. A recognition loss, never a wrong solid — which is why it is recorded rather than fixed here: fixing it changes what is written and wants its own re-derivation. |
| **The round trip requires *at least* `expect_solids`.** Extra bodies pass, and aggregate positive volume does not establish that each body has one. | Read. |
| **`check_surface_curves` handles only cylinders and cones.** Directly relevant now: the sweep crossing curve writes `SURFACE_CURVE`s on a B-spline face, 498 of them on `f02`, and this validator certifies none of them. | Read. Already recorded as an open item; the new work widened what it does not cover. |

### Which of these block the sweep work, measured rather than judged

Three of the open findings are fail-open conditions in measurements the fault-17
and crossing-curve threads actually rely on, so each was put to the files those
threads use before being treated as a blocker. **None of them is currently
biting.**

- **The failed projection scored as zero error** is not reached. `_off_surface`
  only falls through to `GeomAPI_ProjectPointOnSurf` for surfaces that are not
  plane, cylinder or cone - which is exactly the sweep - so it is the path every
  B-spline corner measurement takes. Counted: **0 of 640** corners on the
  crossing-curve `f02` and **0 of 623** on lid10 return no projection. The
  corner-exactness figures for sweeps are real numbers, not silent zeroes.
- **Closure accounted globally rather than per shell** does not weaken the
  crossing-curve verification, because every file it was verified on has exactly
  one shell, where the global count and the per-shell count are the same
  question. It would matter the moment a change produces two.
- **Pcurves unchecked on anything but cylinders and cones** is the one with real
  exposure, since the sweep crossing curve writes 640 `SURFACE_CURVE`s with 640
  `PCURVE`s. Measured directly instead of assumed: the worst written pcurve sits
  **8.4e-6 mm** from its own 3D curve, on two cylinder faces, against 0.000001
  and 0.000000 for what a projection would give. Small - but above the 1e-7 the
  exact tier claims, and near enough to a kernel's linear precision to be worth
  knowing.

So the thread's own next step - the 142 edges that still fall back to chords -
is not blocked. What the missing validator costs is that a *future* pcurve
regression on a B-spline face would not be caught, and closing it means writing
a de Boor evaluator: `validatestep.py` has none, and OpenCASCADE cannot stand in
because it silently builds a pcurve where the file stores none, so it cannot be
asked which one it read.

### The one correction worth making to the review

It lists the cone deduplication among "the highest-confidence production
concerns" alongside the residual, the mirrored sweep and the sweep inversion.
The first three are demonstrable defects with reproductions; that one is a
demonstrable *comparison* defect whose effect on an export depends on guards the
reviewer could not see. Its own body says so. Kept here at that lower weight.

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
> Do not open another measurement of code 17 on this side. Twelve mechanisms
> have been proposed and refuted, the last four in one session — the sliver, the
> taper, the boundary's jaggedness and the import settings — and the two coupons
> that differ by 0.01 in one parameter are identical in every count the exporter
> makes, in SOLIDWORKS' own re-export, and to the eye.
>
> Open item 3 has landed: a declared sweep is trimmed to the curve where it
> crosses a quadric, 498 of 640 edges on the band family, shell closed, 50/50.
> It did **not** move code 17 — and that is not a refutation, because 142 edges
> still fall back to chords and one chord is enough to fail a face.
>
> So take the 142. Count them by position along the sweep before theorising;
> the tangent stretch at the shallow end of the ridge is where every other
> measurement of this boundary has concentrated and where `projectOntoBoth` is
> documented to fail. If that is where they are, ask whether a crossing curve is
> the right entity there at all rather than pushing the solver harder.
>
> Only when all 640 are exact does "is code 17 the boundary?" become a question
> the answer can be trusted on. Put `f02-band-fn032-analytic.stp` in every
> interop run as the positive control regardless.
>
> Put `f02-band-fn032-analytic.stp` in **every** interop run as the positive
> control; it must read `faults=2 faultyfaces=2 codes=17`, or a page of clean
> rows means nothing. Start SOLIDWORKS by hand and never kill the driver
> mid-import. Every expectation must be derived from the model and never
> captured from a run, and report how much of each sweep was recognised beside
> any fault count — the kit records it now, in `sweep_whole` / `sweep_cut` /
> `sweep_pct_whole`.
