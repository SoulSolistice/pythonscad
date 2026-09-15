# STEP export: work in progress

Open items, what is unexplained, what is parked, and where the next session
starts. Everything here is provisional by construction.

What is settled lives in `doc/step-export-development.md`; build and test
mechanics live in `CLAUDE.md`. Read both before this one — several items below
are one sentence because the reasoning behind them is there.

**Last updated 2026-09-14, for the state after `6c74a41` (item 11's root cause C)
and the flagship check's repair that follows it.**

---

## Read this first

Verify the tree before changing anything, and again after:

```bash
ctest --test-dir build -R 'export-step-|mutations'
```

**53 tests**, and it has to be this regex and not `-R step`, which drops
`bspline-check-mutations` and `closed-sphere-check-mutations` — the harnesses
whose job is to prove the other tests would fail if the defect came back. What
the 53 are: 33 SCAD fixtures in `tests/data/scad/step-export/`, 12 Python in
`tests/data/pythonscad-step-export/`, the two mutation harnesses, the three
flagship tests below, and three `*_arg-permutations` tests that have nothing to
do with STEP and match `mutations` by substring. It was 52 until
`export-step-flagship-corners` was added on 2026-09-14; a build configured
before that sees 52. Run it under the machine's own locale, not `LC_ALL=C`,
which breaks five UTF-8 filename tests.

**Two of the 53 fail on purpose and report green**, both registered
`WILL_FAIL`: ctest reports them *passed* while they fail, and will report each
*failed* on the day it starts passing — which is how that fix gets noticed. Do
not delete them, and do not read their green as the defect being gone.

- `export-step-flagship-strict` asserts that the plane veto never gives up the
  crossing-curve boundary. It does, on all six flagship coupons.
- `export-step-flagship-corners` asserts that every corner of an analytic face
  lies on that face's surface, as every fixture's round trip does. It fails on
  all six coupons, normal build and relaxed veto alike, on the tessellation's
  slack: 0.01 to 0.72 against an allowance near 1e-6.

**Until 2026-09-14 the flagship check could not fail its round trip.** It read
`not roundtripSTEP(target)`, and that returns a tuple. Every earlier "flagship
check green, round trip included" in this file - item 11's "five pass" under the
relaxation among them - is a validator result. See item 11.

Three things make a green result a lie if they are missing:

- **`cadquery-ocp==7.8.1.1.post1`.** `tests/steproundtrip.py` needs
  `TopTools_IndexedMapOfShape`, which 8.0 no longer exposes, so it **skips in
  silence** and the suite goes green with the round trip never run. 7.9.3.1.1
  is what this machine has and it works; 8.0 does not.
- **Both feature flags.** The analytic path is behind
  `--enable=step-analytic-surfaces --enable=step-approximate-surfaces`. Without
  them the exporter writes facets, still prints "3 analytic surfaces available",
  and every check passes for the wrong reason. That cost a session its first
  hour on 2026-09-10.
- **lid10's customizer.** `-p examples/step_test/lid10.json -P "New set 1"`,
  and pass it as an argv list, never through a shell function: on 2026-09-10 the
  quoting of `"New set 1"` was lost that way, lid10 exported its default
  component, and a veto count was reported as 4 of 6 when it is 6 of 6.

`export-step-flagship-coupons` is what covers the coupons this work is about:
the band family at `$fn` 24, 48, 64 and 96 (the fixture only ever exports its
declared 32) and both reference parts, through the validator and the kernel
round trip, failing on any `EXPORT-ERROR`. It exists because a green 50-test
suite let a broken `$fn` 64 and a broken lid10 past it.

**Mutating a file to prove a check bites:** write the working tree with
`git show <sha>:<path> > <path>` and restore from a copy. `git checkout <sha> --
<path>` *stages* the file, and on 2026-09-10 the next `git add -A` swept a
reverted change back into a commit while every build and test ran against the
correct working tree.

---

## Where it stands

**Last measured 2026-09-14, on `claude/solidworks-fault-code-17-b58858`.** Items
1, 3, 9, 10, 11 and 13 below carry the detail; this is the map.

### Root cause C is closed, and the gate it was holding shut is not what it said

2026-09-14. Item 11 has the five whys; the short of it:

- **C was not coaxial owners that never meet.** At `$fn` 24 provenance names two
  owners for none of the junctions. The bore's original has 2 whole facets of
  the 3 ownership needs, so its 48 rim corners got the outer wall as *sole*
  owner, and the conic placement slid each 3.0 along the cap onto it.
- **lid10 had the same defect, unrecorded:** 120 chamfer corners slid 0.65 and
  2.25 onto r = 82.70 - the point root cause B called a real junction. The
  validator passed it; its bound is 5% of the radius.
- **It does not need a relaxed veto.** Three primitives - a bore with a frustum
  subtracted through its length - write the bore 3.0 off itself today.
- **Fixed at the root** (`6c74a41`): the conic placement keeps a corner on a
  declared quadric it is already on. **And loud:** no move may take a corner
  off an analytic face it bounds, an `EXPORT-ERROR`, unreachable on all 45
  fixtures and 6 coupons with the veto and without it, and fired on exactly
  `band-fn024` and lid10 when the root fix is removed - which is what makes
  `export-step-flagship-coupons` fail without it.
- **The flagship check could never fail its round trip** (`97ce12b`). Read
  properly, all six coupons fail it in both configurations on corner slack,
  and pass everything else. That is now its own `WILL_FAIL` test.

**What that does to item 11's gate.** "The flagship check green under the
relaxation, round trip included" can be measured for the first time, and it
splits three ways:

- Validator, `EXPORT-ERROR`, one solid, `BRepCheck`, positive volume: **green
  under the relaxation on all six.**
- Every corner on its own face: **red on all six in both configurations**, by
  the same tessellation slack, so "green with the round trip" as written is
  reachable by no build.
- **And a fourth defect the veto conceals, which no check asserts** - item 11,
  root cause D. Under the relaxation lid10 reads back as a valid solid of
  292144.94 against 227707.04 from the normal build, +28%, and bayonet 225216.89
  against 238911.27, -6%. Before C's fix the relaxed lid10 read 309176.65, so C
  was part of it and is not all of it. The band family's volumes move *toward*
  the model's under the same relaxation.

So the veto stays, for a measured reason and not only a pending decision.
Nothing was relaxed.

**And SOLIDWORKS, asked the same day of that relaxation as an experiment** (item
1, *Run 2026-09-14*): `f04` reads `faults=0`, the sweep kept, where its normal
build reads code 17 - n = 2. Bayonet's sweep loses its 17 too; lid10's does not,
and lid10 gains a face SOLIDWORKS reads inside out, the first lead on D.

### Code 17 moved, on the one coupon whose boundary was written

SOLIDWORKS 34.0, settings read back by the driver: `knit=form-solids`,
`check-and-repair=1`, `analytical-conversion=on`, `run-diagnostics=on`,
`solid+surface=on`, units from file. Run on 2026-09-10 and again on 2026-09-11
after root causes A and B, with every number identical to four decimals and a
`-FaultDetail` table matching byte for byte.

| coupon, analytic | crossing curves / chords | faces | volume | SOLIDWORKS |
| --- | --- | --- | --- | --- |
| `f02` control, 2026-09-08 | 0 / 640 | 9 | 19555.2696 | `faults=2`, bspline **17** + cylinder **17** |
| **`f02-band-fn032`** | **593 / 47** | 9 | 19511.9331 | `faults=1`, bspline **21**, cylinder **clean** |
| `f04-band-fn064` | 1 / 1251 | 39 | 19511.5976 | `faults=2`, bspline 17 + cylinder 17 |
| `r01-lid10` | 1 / 623 | 787 | 226032.0463 | 5 faces + 1 edge, codes 7/17/21 |
| `r02-bayonet` | 1 / 623 | 58 | 237141.7973 | 5 faces + 1 edge, codes 7/17/21 |

The control has now reproduced `faults=2 faultyfaces=2 codes=17` six times.

Two readings, and the second is the stronger. **The bore cylinder's code 17 is
gone on the one coupon whose sweep-to-bore boundary is written as the crossing
curve**, and stands on the three where it is not. And the analytic volume is
now nearly `$fn`-independent, as a correct solid must be: `f02` and `f04` agree
to 0.0017%, where the control sat 43 mm³ off its own `$fn` 64 sibling.

That is n = 1 on the positive side. It stays n = 1 until the plane veto stops
withholding the boundary from `f04`, lid10 and bayonet — which is item 11.

### What landed, 2026-09-10 and 11

- **Corner placement by Newton.** `projectOntoBoth` moved to `AnalyticFeatures`
  beside `closestOnSurface`, keeps its best iterate rather than its last, and
  solves to an *absolute* 1e-11 because the acceptance is absolute. Band family
  at `$fn` 32: junction vertices on the crossing curve 502 → **640** of 704,
  corners left on one surface 138 → **0**, chords 142 → **47**. lid10: 1082 of
  1598, none left off.
- **The surface-against-line placement solves along the line.** No count moved;
  it can no longer return the far root of a line through a quadric.
- **`GridSurface::project` descends once per declared profile span** and takes
  its v derivative inside the span. It had been unable to find a point it had
  itself evaluated: 0.78 mm out on the band family's ridge, now under 1e-7.
- **The written vertex is keyed by where the corner ended, not where it began**
  (item 11, root cause B), and **a section edge is not written where the face
  across will be fanned into triangles** (root cause A).
- **Diagnostics that could not be read now can.** The arc fitter says how near it
  came and how much of the miss is against a surface stated algebraically
  against one that answers by projecting; the veto says how many of the corners
  it refuses touch a bent face; cut planes say how near a declaration came.
- **Should-never-happen is loud.** Losing a plane section's agreement is an
  `EXPORT-ERROR` in the exact tier and a warning under the approximation flag;
  the plane veto is an `EXPORT-WARNING` pinned by the `WILL_FAIL` test above.
- **Unit tests:** seven for the two corner solvers, four of them asserting a
  refusal, and one asserting a declared grid can find a point it evaluated.
  Each was shown to fail with its fix removed.

### Open, in the order to take it

1. **The corner slack**, which is what `export-step-flagship-corners` is red on
   and the last thing between the veto and its gate. Measured 2026-09-14 and it
   is two populations, not one - item 15.
2. **Then the veto itself**, and only through the gate: the flagship check under
   the relaxation, then SOLIDWORKS on `f04`, lid10 and bayonet with the `f02`
   control in the same session.
   The relaxation to use is `b57c8e73b`'s settle loop.
3. **Item 13: provenance loses an original a boolean cut on every facet.** C was
   one consequence; corners left with no owner at all are the other, and they
   are what keeps the three-primitive reproduction of C from being a fixture.
4. **Item 14: the two-owner placement moves corners off declared surfaces** -
   240 on lid10, 120 on bayonet - without leaving any face they bound. Unexplained.
5. **Code 21 on `f02`'s sweep** is new and unexplained.
6. **lid10's faceted control reads `faults=6 codes=24`**, which weakens lid10 as
   a test until it is understood.
7. **lid10 exports 810 analytic faces** where 818 and 822 were recorded earlier;
   it moved with this work and has not been re-derived.
8. **Item 12**, the survey for other should-never-happen counters. No build.
9. **The last 47 chords on `f02`.** Not the fitter and not the projector, both
   cleared; whether they are the edges touching the 64 coaxial-owner vertices is
   unmeasured.
10. **Only the sweep side of a sweep-to-bore edge carries a pcurve**, because the
    two-pcurve writer needs two pushed sides and only the quadric emitter pushes.
11. **Every plane section on the band family is fitted to a ridge-flank facet**,
    a plane the model does not have — a plane section written for a cut that is
    not one.

### As it stood on 2026-09-08

Kept because the later measurements are read against it. Where item 1 or the
table above disagrees, they supersede this.

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

**Re-run 2026-09-11, after root causes A and B.** Both fixes are reasoned to be
unreachable on today's exports - `section_unshared` is zero and no two corners
coincide - and that reasoning is two counters reading zero. A CAD system is a
stronger witness, so the four coupons went through SOLIDWORKS again on the same
settings.

Every number is identical to the run above: the same face counts, the same
volumes to four decimals, the same codes, and a `-FaultDetail` table that
matches the earlier one byte for byte. The control reproduced `faults=2
faultyfaces=2 codes=17` for the sixth time.

So A and B are confirmed to change nothing an importer sees, by measurement
rather than by inference, which is what a fix aimed at a latent defect should
look like.

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

**The gate is named, it is not one of the ones fixed for item 3, and it has an
item of its own — 11.** On f04 the
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

#### Run 2026-09-14: `f04` under the relaxed veto reads clean

An experiment, not the gate: C's fix (`6c74a41`) plus `b57c8e73b`'s settle loop
behind a temporary switch that was not committed, both flags, lid10 and bayonet
with their customizer as argv lists. SOLIDWORKS 34.0 started by hand; settings
read back by the driver: `solid+surface=on free-curves=off run-diagnostics=on
analytical-conversion=on attributes=on step-config-data=off
multibody-as-parts=off knit=form-solids units=file-units assembly-mapping=default
check-and-repair=1 custom-tolerance=0`, labelled
`c17-relaxed-veto-probe-2026-09-14`. Counts first, then `-FaultDetail` over the
four files with faults; both passes agree.

| file | crossing curves / chords | faces | SW volume | SOLIDWORKS |
| --- | --- | --- | --- | --- |
| `f02` control, 2026-09-08, byte-identical | 0 / 640 | 9 | 19555.2696 | `faults=2 codes=17`, 7th time |
| `f04-band-fn064`, normal build | 1 / 2 | 39 | 19511.5976 | `faults=2 codes=17` |
| **`f04-band-fn064`, relaxed** | **1198 / 35** | 57 | 19514.0874 | **`faults=0`** |
| `r01-lid10`, relaxed | 589 / 22 | 799 | 293174.2338 | `faults=1 faultyfaces=8 faultyedges=1 codes=7/17/21` |
| `r02-bayonet`, relaxed | 589 / 22 | 58 | 235537.9892 | `faults=1 faultyfaces=4 faultyedges=1 codes=7/17/21` |

The sweep's recognition is unchanged by the relaxation: 1212 facets whole and
128 cut on `f04`, 875 and 125 on lid10 and bayonet. The control and the normal
`f04` reproduce the 2026-09-10/11 `-FaultDetail` rows to six decimals.

**`f04` is clean, and not by discarding the sweep.** What SOLIDWORKS saved back
has the normal `f04`'s surface census, B-spline 1 -> 5 and cylinder 9 -> 10, and
its volume moved 0.03%. So the prediction of item 1 holds on a second coupon, and
more completely than on `f02`: bore and sweep both lose code 17 once the
boundary between them is the crossing curve. That is n = 2.

**`-FaultDetail` against the normal build of 2026-09-11**
(`build/interop-kit-ab/faults.tsv`, same settings):

- **bayonet:** the sweep's B-spline (2422 mm², 17/17) is **gone**. What stays is
  the same four faces and one edge as the normal build - a 15.16 mm² B-spline
  (17), a 19287 mm² cone (7), an 850.75 mm² cone (21), a 15.3 mm² plane (7) - all
  at r ~ 79 near z ~ 0, a feature that is not the sweep-to-bore boundary.
- **lid10:** the sweep still reads 17/17 (knitted to 6518 mm² now), the same four
  r ~ 79 faults stand, and three are new: two 1.47 mm² planes (21) and **a plane
  of negative area, -1159.62 mm², centred on the axis at z = 77.85** - a face
  SOLIDWORKS finds wound inside out. That is the first entity-level lead on root
  cause D, and SOLIDWORKS measures this lid10 at 293174, agreeing with OCCT's
  292145 that it is the wrong solid.
- lid10 and bayonet carry identical fault entities in both builds, which is also
  why their worst corners were identical: they share that geometry.

What not to read into it: this is one relaxed configuration behind an
uncommitted switch, and D still blocks the relaxation. SOLIDWORKS' bayonet
volume, 235538, is 4.6% from OCCT's 225217 and it dropped 7 cylinders on
re-export, so the two kernels do not even agree which wrong solid bayonet is.

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

### 11. The plane veto is blunt by 89 to 1, and it is concealing three defects

**Built, measured and reverted on 2026-09-10.** This is the gate that decides
whether the crossing curve of item 3 fires at all, and it is why the SOLIDWORKS
run above could only move code 17 on one coupon.

When a corner move would take a planar face out of the plane it asserts, the
corner is held on the mesh. That much is right. What was wrong is the scope: one
bent face refuses **every** corner move in the model.

**How blunt, measured rather than estimated.** The refusal now reports it:

```text
1263 corners are left where the mesh put them: moving them would bend 7 faces
that keeps a plane ... of those corners 14 actually touch a bent face, so 1249
are refused for someone else's
```

Seven faces, fourteen corners implicated, **1249 refused for a problem they are
not part of**. And the cost is not abstract: with the corners held, almost no
edge is a candidate for the curve its two surfaces cross on.

| coupon | crossing curves / chords | why |
| --- | --- | --- |
| `step-band-family` `$fn` 32 | 593 / 47 | the veto never fires |
| `step-band-family` `$fn` 64 | 1 / 1251 | 1263 corners held |
| `lid10` | 1 / 623 | the same |

`$fn` 32 is the only coupon measured where the veto does not fire at all. It
fires on every one of the six the flagship check exports, which is why that
check's strict half fails on all six rather than on the four first reported -
an earlier per-coupon measurement lost `-P "New set 1"` to shell quoting and
exported `lid10` with default parameters.

`$fn` 32 is the only coupon of the four whose code 17 moved. It is also the only
one where this veto does not fire. That is not a coincidence and it is the whole
of item 1's `n = 1`.

#### What was tried, and exactly how it failed

Refusing only the implicated corners, settled until nothing bends - a fixpoint,
on the argument the section settle loop already uses, that refusals only grow so
it terminates. It does what it was meant to:

| | before | after |
| --- | --- | --- |
| `step-band-family` `$fn` 64 | 1 curve / 1251 chords | **1198 / 54** |
| `lid10` | 1 / 623 | **589 / 35** |
| corners moved / dropped at `$fn` 64 | 0 / 1263 | **1249 / 14** |

And it breaks both of them:

```text
f04-band-fn064-analytic   2 edge(s) used by only one face (shell is not closed)
r01-lid10-analytic        VERTEX_POINT #45 and #70 sit on the same coordinates
```

**Neither is caught by the test suite**, because neither coupon is a fixture -
50/50 passed on the broken build. The kit's own validator caught them. That is
the second time on this branch that a green suite has hidden a broken flagship
export, and it is worth its own entry rather than a footnote.

#### What it means, chased to the root on 2026-09-10

The first reading of those two failures was that they are the cost of moving
some corners and not others, and that the veto is therefore protecting something
real about partial moves. That reading is **wrong**, and relaxing the veto to
work around it would have been building on it.

Both failures were reproduced and read entity by entity. Neither is about
*partial* moves. Both are latent defects in what happens when a corner moves at
all, and the all-or-nothing veto hides them by ensuring that on these coupons no
corner moves.

**Root cause A, the open shell: an edge's geometry is decided twice, once by
each of its two faces.**

```text
edge #21826  curve=ELLIPSE  face #22164 on CYLINDRICAL_SURFACE, 1 bound
edge #24575  curve=LINE     face #24590 on PLANE, 1 bound
      both from (-3.370953, 19.694450, 37.941337)
      both to   (-3.901806, 19.615706, 38.007493)
```

The same boundary, between the same two points, written as the conic by the
cylinder and as a chord by the plane. Two edges where there should be one, so
each is used once and the shell is open.

The exporter already knows. It compares the sections that agreed before the
corners were placed against those that agree after, and warns - *17 plane
sections agreed before the corners were placed and do not after*. Its own
comment has the mechanism exactly right: a plane **fitted to mesh facets** does
not survive the placement, because the corner is moved onto the surface the
model declared and that is precisely off the facet plane it happened to share
with a neighbour. A **declared** plane survives, being what the corner was moved
onto.

So this is an ordering fault: agreement is settled before the move that
invalidates it, and the warning is raised instead of the decision being retaken.

**Root cause B, the coincident vertices: `get_vertex` is keyed by mesh index.**

```cpp
auto get_vertex = [&](int ind) {
  if (step_verts[ind] == nullptr) { ... new Vertex(entities, point); }
  return step_verts[ind];
};
```

One `VERTEX_POINT` per *mesh vertex*, whatever its position. On lid10 the
placement puts **three** of them on one point - a cylinder-and-plane corner and
two cone-and-plane corners at (-66.905705, -48.609840, 95.0), which is a real
junction of those four surfaces - and three vertices are written where the
geometry has one.

That is not a consequence of moving only some corners either. It is what happens
whenever a placement maps two mesh vertices onto the same true point, which a
correct placement *should* do at a junction like this.

#### The root fixes, both with precedent in this file

1. **Decide an edge's geometry once, keyed by the edge.** This is exactly what
   the crossing curve already does: `crossing_curves` is a map on the vertex
   pair and both emitters look the edge up in it, so "the two faces name one
   geometry however the passes are ordered" - and the note on that work records
   that the earlier revert failed precisely because `decide_sections` rebuilt
   the map *between* the two emitters. The plane-section pass has no such map;
   each face decides for its own boundary runs. Giving sections the same
   treatment makes A impossible by construction rather than detected after the
   fact, whatever the corner placement did.

2. **Key the written vertex by position, not by mesh index.** Two mesh vertices
   moved onto one point are one vertex, and the file should say so. Precedent
   again: `GridSurface` keeps a `lookup` of positions rounded to a grid so
   membership is a lookup rather than a scan, and its comment points at
   `VertexSnapper` in `core/FilletNode.cc` as the same problem solved before.

**And the veto is then not the thing to relax.** With A and B fixed at the root,
the question of whether one bent face should refuse fourteen corners or 1263 can
be asked again on its own merits, against coupons that no longer break for
unrelated reasons - and it may not need relaxing at all, because a face bent by
a move is a face that could be re-planed or fanned once its edges and vertices
are consistent. Relaxing it first would have been arguing from two defects it
happened to be concealing.

**Order:** B first - it is small, self-contained, and a defect wherever it
occurs rather than only under a relaxed veto. Then A, which is the larger
change and the one that needs the section pass restructured. Then, and only
then, the veto.

**B is done.** And it needed no new mechanism: the rule was already written and
already implemented, and only ran too early. `get_vertex` now resolves through a
position map instead of indexing by mesh vertex, which is the same rule at the
first moment it can be obeyed - every caller emits faces, and that is after the
moves are applied.

Verified where it can be. Nothing coincides on today's exports, so the suite can
only show it breaks nothing; the measurement that shows it *fixes* something is
the relaxed-veto configuration, which is where the defect lives:

| under the relaxed veto | before | after |
| --- | --- | --- |
| `lid10` | `VERTEX_POINT #45 and #70 sit on the same coordinates` | **valid**, 830 faces, one shell |
| `f04` | `2 edge(s) used by only one face` | unchanged, as expected |

**A is done too, and the first attempt at it was aimed at the wrong condition.**
The sharing was never missing - `section_edges` is keyed by the run of corners
exactly as `crossing_curves` is keyed by the vertex pair, and the quadric pushes
its EdgeCurve into `arc_subs` so the planar face across uses the same one. What
was missing is that the far face may be *unable* to take it: a loop fanned into
triangles is never written as one face, so it never reads `arc_subs`, and a
section spanning several corners is not any one triangle's side in any case.

Binding on "does that loop still spell the run" was measured and refused the
hypothesis - it is reached zero times even under the relaxed veto. Binding on
`split_for_corners`, which is settled long before any face is written, is the
condition that holds.

| under the relaxed veto | before | after |
| --- | --- | --- |
| `f04-band-fn064` | `2 edge(s) used by only one face` | **valid**, 60 faces, one shell, 1198 crossing curves |
| `r01-lid10` | valid, from B | **valid**, 830 faces, one shell, 589 crossing curves |

#### It was concealing a third defect, and the gate caught it

With A and B closed the relaxation was put to the whole flagship check -
validator and kernel round trip over all six coupons, which is step 1 below.
Five pass. `band-fn024` does not, and on something neither A nor B touches:

```text
#5561: trimmed CYLINDRICAL_SURFACE is bounded by points up to 3 off it,
       on a radius of 20
```

Three millimetres is the wall thickness: the bore at radius 20 and the outer
wall at 23. These are the 64 corners measured under item 9 whose two provenance
owners are **coaxial cylinders that never intersect** - Newton correctly finds
no crossing between them. The veto was holding them still. Released, each is
placed on one of its owners and ends three millimetres from a face it bounds.

**Why nothing caught it is the same shape as A and B.** The placement checks
that a move does not bend a *planar* face. Nothing checks that a move does not
leave a *curved* face's boundary off its own surface, and both faces here are
cylinders, so the bent-plane test never looks. A guard covering one case of a
general rule, again.

So that is root cause C, and the veto is load-bearing for more than the two
things first found behind it. Two coupons measured by hand looked like a green
light and were not; the gate over all six was what said so.

**One correction, found 2026-09-14:** "five pass" above is a validator result.
The flagship check's round trip could not fail (see *Read this first*), and read
properly none of the six passes it, in either configuration, on corner slack.

#### Root cause C, chased to the root on 2026-09-14

Reproduced with the relaxation of `b57c8e73b` behind a temporary switch, so one
binary served both configurations; nothing of the switch was committed.

**Which path moved the corners?** Named before measuring: (H1) the two-owner
placement, whose acceptance reads only its first owner - Newton's best iterate
on the far cylinder; (H2) the pass onto a face's own quadric, for a vertex in
two patches' runs; (H3) the conic placement; (H4) the vouched-plane reroute.

- **H1 refuted at once:** at `$fn` 24 provenance names two owners for **0** of
  368 junction vertices. The pair branch never runs.
- **H3, the one predicted uninvolved:** 48 corners at r = 20.0000 exactly, z = 0
  and 40, single owner the r = 23 cylinder, **3.0000 off it before the move**,
  moved 3.0000 along the cap with `reach = 40` - the travel bound on that path
  is the *farthest* vertex of the faces at the corner. Every one bounds the face
  written on r = 20. H2 and H4 moved none of them.

**Why is the r = 23 wall its only owner?** Named: (W-a) provenance attributes a
bore-rim vertex to the wall's original through the cap and the bore's original
owns nothing; (W-b) the owner is right and only the travel bound is loose; (W-c)
two owners collapsed by deduplication. Measured `ids: id1[0] id3[]` on all 48:
the bore's original owns **nothing**, so W-a, with W-b in the same chain; W-c
refuted.

**Why does the bore's original own nothing?** Named: (W2-i) the ridge cuts
nearly every bore facet, and ownership needs three whole; (W2-ii) the whole-facet
test rejects a subtracted cylinder; (W2-iii) the record is dropped.

| `$fn` | bore original on r = 20, whole / cut | provenance |
| --- | --- | --- |
| 24 | **2** / 344 | one owner for all 368 junctions |
| 32 | 19 / 324 | two owners for all 704 |
| 48 | 7 / 679 | two for all 736 |
| 64 | 85 / 601 | two for all 1406 |

W2-i. W2-ii refuted by the same test claiming the bore at 32, 48 and 64; W2-iii
by the record being surface 3 in every export. The coaxial pair of the original
description is the `$fn` 32-and-up population, where the two-owner placement
correctly finds no crossing and moves nothing; `$fn` 24 is the one member where
the pair collapses to a single wrong owner.

**The same defect on lid10, unrecorded until now.** Measured over every proposed
move, before the veto, on all fixtures and coupons: does it take a corner
further off the surface of an analytic face it bounds?

| export | corner-face pairs leaving the face | worst |
| --- | --- | --- |
| `band-fn024` | 60 (48 corners), trimmed quadric | 3.0000 |
| `lid10` | 240 (120 corners), recognised bands | 1.5877 |
| every fixture, the other four coupons | 0 | - |

lid10's 120 are the same chain: `id1[0] id2[]`, id2 with 0 whole facets on the
declared cylinders s6/s7/s8 (r 81.25, 80.45, 82.05, the rims its chamfer cones
hull between), corners at r = 82.0547 and 80.4547 moved along z = 95 onto
r = 82.70 with `reach = 164`. That is `(-66.9057, -48.6098, 95.0)`, the point
root cause B's entry above calls "a real junction of those four surfaces". It is
not; it is C. OCCT reads the relaxed file's cones at up to 1.5877 off, where
`validatestep.py` passed it: its bound is 5% of the radius, 4.05 on r = 81 and
1.0 on the bore, which is the whole reason one of the two was caught. The plane
corners 5.7057 off in that same relaxed lid10 were C's too, and are gone with it.

**It does not need the veto relaxed.** Three primitives - a tube with a frustum
subtracted through its whole length, half a step rotated so it crosses every
bore facet mid-facet - make the bore's and the frustum's originals own nothing,
and today's build writes the bore 3.0 and the frustum 2.43 off themselves with
the veto silent. Kept out of the suite for now; see item 13 for why.

**The fix, and the choice in it.** Two rules were measured against every export:

| rule | refuses on `band-fn024` / lid10 | elsewhere |
| --- | --- | --- |
| a corner on a declared quadric stays on it, asked by the conic placement | 48 / 120 | 0 |
| no move off the surface of an analytic face the corner bounds, over every path | 48 / 120 | 0 |

The same corners. The first is the root - the conic placement's premise, "one
declared surface holds this corner", is false for them, and the pass onto a
face's own quadric already asks exactly this - and it is what landed in the
placement. The second is the rule the veto enforces for planes, over every path;
landed as the backstop, and with the first in place nothing reaches it, so it is
an `EXPORT-ERROR` that also holds the move.

Asking the first of *every* path was rejected on measurement: it would also
refuse 240 two-owner moves on lid10 and 120 on bayonet that leave a declared
surface but no face - item 14, unexplained, and not a reason to widen a fix.
The triple-point placement shares the premise and moved no corner off a quadric
anywhere, so it was left alone; the backstop covers it loudly.

A branch not separated: whether a deserting corner can be a sagitta *off* the
quadric rather than on it, which the first rule would miss and the second
catch. The model built to separate them - radial gear teeth through the bore -
never had its bore written analytic, so it said nothing either way.

**Verified where the defect lives.**

| build | `band-fn024` | lid10 | 43 other exports |
| --- | --- | --- | --- |
| fix, veto as is | 48 held | 120 held | silent |
| fix, veto relaxed | 48 held | 120 held | silent |
| backstop without the root fix | `EXPORT-ERROR` 48, 3.0000 | `EXPORT-ERROR` 120, 1.5877 | silent |

At suite level, with the root fix removed from the working tree and the
backstop kept, `export-step-flagship-coupons` fails on exactly `band-fn024` and
lid10; on `777247eb1`, with neither, the suite was 52/52. That failure is the
suite catching the defect - through the backstop, since no fixture exercises C
on today's build. The three-primitive model does, and item 13 is what it waits on.

#### What relaxing it would still take

A validator passing is the thin evidence that was accepted last time and should
not be again:

1. ~~Root cause C fixed~~ **Done 2026-09-14**, above. Then
   `export-step-flagship-coupons` green under the relaxation - the validator
   **and** the kernel round trip over all six. **Measured 2026-09-14, and it
   splits:** validator, `EXPORT-ERROR`, one solid, `BRepCheck` and a positive
   volume are green on all six under the relaxation; every corner on its own
   face is red on all six, relaxed or not, by the same slack
   (`export-step-flagship-corners`). "Green with the round trip" as written is
   reachable by no build. And the green half hides root cause D below: a
   valid solid of the wrong volume is green on every check here.

#### Root cause D is closed: a face's bounds include its holes

**Chased and fixed 2026-09-14, `f218e8af`.** SOLIDWORKS' negative-area plane was
the thread to pull.

The plane veto measures the bend over every bound a face has, holes included.
The test deciding *whether to measure a face at all* read only its outer loop,
so a face bent purely through a hole was never examined. Branches named first:
(F1) the outer-loop-only test, (F2) the hole is `consumed` and skipped from the
measurement, (F3) the bend happens after the veto. Measured: F1, and it fires in
the normal build too - 3 such faces on lid10 and 4 on bayonet, 7 and 8 under the
relaxation, the worst hole ending **5.7057** off its face's plane. F2 and F3 are
refuted: the writer reports every hole attached in both configurations, and the
bend is already there when the veto runs.

What that writes is a broken solid rather than a bent face. OpenCASCADE cannot
read a planar face whose inner bound is off the plane, so it splits the loops
into separate faces: lid10's bottom becomes a full disc of radius 81.8 -
pi*81.8^2 = 21021.15 mm², against the 612.23 mm² ring the normal build writes -
the cavity behind it is sealed, and the volume reads +28%. The plane area total
gives it away at a glance: 12762 mm² over 786 planes normally, 54476 over 815
under the relaxation.

Refuted on the way: that the hole was dropped as `consumed`, as fanned, or by
the rim substitution - every one of those reads zero in both configurations; and
that the *inner wall* seen missing in a CAD view is D, which it is not. The
normal build renders the same way, both files read back as one closed
BRepCheck-valid shell with the same six cylinder faces, and SOLIDWORKS keeps 799
of 843 faces (787 of 814 normally): it is a CAD system hiding faces it judges
faulty, in both builds equally.

The corners a bent face implicates are its holes' too - keyed on the outer loop
alone, a refusal drops nothing and the settle loop gives up on the whole export.

| under the relaxed veto | before | after |
| --- | --- | --- |
| `lid10` | 292144.9441 | **225616.3034** (normal build 227707.0400) |
| `bayonet` | 225216.8920 | **236835.7818** (normal build 238911.2712) |
| band family, every `$fn` | - | unchanged |

Every normal-build export is unchanged, on all 45 fixtures and all six coupons,
so this is latent exactly as A and B were, and the veto is the only reason.

**And it is loud, at a threshold derived rather than chosen.** A planar face may
not assert a plane one of its own bounds is off by more than *the mesher's own
coplanarity tolerance* - `coplanarTolerance` in `GeometryEvaluator.cc`, 1e-8 of
the model's diagonal, which is what a merged loop is entitled to be warped by.
On lid10 that allowance is 2.53e-06 and the worst merged quad sits at 2.262e-06
under it; nothing in either configuration exceeds it. With the fix removed it
fires on relaxed lid10 and bayonet at 1.8000, an `EXPORT-ERROR` the flagship
check fails on.

Found 2026-09-14 reading the round trip's volumes rather than its verdict, and
not chased. OCCT, both flags, lid10's customizer as an argv list:

| export | faces read | volume |
| --- | --- | --- |
| lid10, normal build, before and after C's fix | 814 | 227707.039963 |
| lid10, relaxed veto, before C's fix | 843 | 309176.648762 |
| lid10, relaxed veto, after C's fix | 843 | 292144.944117 |
| bayonet, normal build | 82 | 238911.271234 |
| bayonet, relaxed veto, before and after | 83 | 225216.891998 |

A correct solid cannot move 28% because corners moved by a sagitta, so this is
a defect and not slack. What SOLIDWORKS read of the normal-build files on
2026-09-11 - 226032.0463 and 237141.7973 - is within 0.7% of the normal rows.
Nothing in the suite asserts a flagship volume, and none can be derived for
these two parts, so the first measurement is the entity-by-entity read that
found A and B: which of the 29 extra faces carries the volume, and on which
side of it the solid is.

The band family is not affected the same way - its relaxed volumes, 19512.75 at
`$fn` 48 and 19506.93 at 96, are closer to the 19511.6-19511.9 the analytic
exports agree on than the normal build's 19573.08 and 19521.30.
2. A SOLIDWORKS run: `f04`, `lid10` and `bayonet` carry a chorded boundary
   today and code 17 with it, and the whole prediction of item 1 is that a
   boundary written as the crossing curve is what moves that. `f02` is the one
   coupon where the veto never fires and the one whose code 17 moved.
3. Only then the question of whether refusing 14 corners or 1263 is right - and
   possibly not that at all, since with A and B closed the reason to refuse any
   of them may be gone.

`export-step-flagship-strict` still fails by design and still says so.

#### Both conditions are now enforced rather than reported

Neither of these should ever be reached, and saying so in a comment is not
saying it. So each is now held to that, at the strength the measurement
supports.

**Losing a plane section's agreement is an `EXPORT-ERROR`.** It is unreachable
today - measured zero on the band family at `$fn` 24, 32, 48, 64 and 96 and on
both reference parts - and it became reachable exactly once, under the relaxed
veto, where it produced an open shell. That is not a degradation to report and
live with; it is two faces contradicting each other about one edge, and the file
is wrong. `step-flagship-check.py` fails on any `EXPORT-ERROR`, so the property
is enforced rather than stated, and the relaxation would have been caught by it
at once.

**The veto is an `EXPORT-WARNING` and is asserted never to fire.** It was a plain
line of information while what it reports is the whole crossing-curve boundary
being given up. It fires today on **all six** flagship coupons - the band family
at `$fn` 24, 48, 64 and 96 and both reference parts - so it cannot be an error
yet - but `export-step-flagship-strict` asserts it never fires and is
marked `WILL_FAIL`. It is expected to fail, and that is the point: on the day a
fix stops the veto firing, ctest reports *that* as the failure, so the win
announces itself instead of going quiet. When it does, drop `WILL_FAIL`, fold
`--strict` into the main test, and the veto becomes an error like the other.

**The fixture gap was the precondition and it is now closed.**
`export-step-flagship-coupons` exports the band family across `$fn` 24, 48, 64
and 96 and both reference parts, and runs them through the validator and the
kernel round trip the fixtures already use. Checked the way this repository
checks a guard: with the change above reinstated it fails three of its six
coupons — `band-fn024`, `band-fn064` and `lid10` — and `band-fn024` was not even
one of the two found by hand. It asserts nothing that has to be derived, so
coupons still earn face counts and volumes one at a time as fixtures; see item
7, which asks for that for a different reason.

### 12. Survey the rest of the exporter for conditions that should never be reached

**Filed 2026-09-10, out of item 11.** Two lines in the corner placement turned
out to be reporting conditions that ought to be impossible - one of them
genuinely unreachable and merely warned about, the other reached on every
flagship coupon and not even warned about. Both are now held to that: losing a
plane section's agreement is an `EXPORT-ERROR` the flagship check fails on, and
the plane veto is an `EXPORT-WARNING` asserted never to fire under a `WILL_FAIL`
test.

Neither was found by looking. They were found by relaxing something else and
watching what broke, which is an expensive way to find a class of defect that a
read can find. So: go through the rest of the exporter and ask of every counter
and every diagnostic line the same two questions.

**The two questions.**

1. *Should this ever be non-zero?* A counter that reports how often a fallback
   fired is describing a defect if the fallback is not supposed to be needed.
   The plane veto reported 1263 corners as though it were a statistic.
2. *If it should never be non-zero, what happens when it is?* Reporting is not
   holding. The three strengths available, and the measurement that picks one:
   - reached today, and its cost is real -> `EXPORT-WARNING` plus a `WILL_FAIL`
     assertion that it is never reached, so a fix announces itself;
   - unreachable today -> `EXPORT-ERROR`, which the flagship check fails on, so
     it can never quietly become reachable again;
   - impossible by construction -> no message at all; delete the branch.

**Where to look, and these are candidates rather than findings.** Every one of
these is a place the exporter counts something it had to give up on, and none
has been asked whether the giving up is a defect:

- `%d regions stay faceted, no fit having been found - which is always a valid
  export`. It says its own answer is valid, which is true and is not the
  question: on the band family at `$fn` 64 it is 2 regions and 30 facets, and a
  region left faceted is a region whose boundary is chords.
- `%d declared sweep left faceted - %d wrap the surface's seam, %d await the
  approximation flag`. A declared sweep that is not written is the declaration
  channel not being used, which is the whole point of the channel.
- `%d facets of the sweep are left faceted: a corner of each is further off the
  fit than four times the %f this claim is typically off`. An outlier rule, and
  outlier rules are where "should never happen" hides.
- `%d facets have every corner on the sweep and their middle off it, by up to
  %f against an allowance of at most %f` - 149 facets by up to 0.8604 against
  0.0807 on `$fn` 64, which is ten times the allowance and reported as a fact.
- `%d corners are left where the mesh put them: they were welded onto an ...`,
  the other corner refusal, which has had none of the attention item 11 gave the
  first.
- `%d regions are not turned surfaces because a vertex is off the ring its
  height puts it on`.
- `%d plane sections written as the conic it is - %d on a plane the model
  declared, %d on one taken from the mesh`. The mesh half is the fragile half
  and item 11 explains why; on the band family it is all of them.
- Every `continue` in `decide_sections` and in the emitters that drops a
  candidate without counting it at all. Those are worse than a counter, because
  there is nothing to survey.

**How to do it without guessing.** The counters are already printed. Export the
six flagship coupons, collect every `STEP export:` line, and sort by whether the
number is zero. The non-zero ones are the survey; the zero ones are candidates
for promotion to `EXPORT-ERROR` at no cost, because they are already unreachable
on everything measured.

**What it is worth.** Item 11's two defects were concealed for as long as they
were because the thing concealing them looked like a statistic. This is a read
of an existing report, it needs no build, and it is the cheapest thing on this
list.

**Found in passing on 2026-09-14, and each is the survey's kind.**

- **A check that could not fail.** The flagship check's round trip, above; now
  repaired, and the kind worth grepping the harness for: a function returning
  `(ok, lines)` tested for truth.
- **A bound proportional to the radius.** `validatestep.py`'s trimmed-quadric
  corner check allows 5% of the radius - 4.05 on lid10's chamfers, which let
  root cause C's 2.25 through. Not changed: tightening it is a validator
  acceptance, and wants a derivation of what a trimmed quadric's corners may
  honestly stray (item 13's slack is the same question).
- **The conic placement's travel bound is the farthest vertex** of the faces at
  the corner - 40 on a cap, 164 on lid10 - where §4 of the development document
  says a corner may travel no further than its nearest neighbour, and the triple
  placement does bound it so. C no longer depends on it; the bound is still not
  the rule it is documented as.
- **A faceted export that is not valid.** The tube with a rotated r = 20.05
  24-gon subtracted over z 5..35 exports *faceted* as `hole 1240 is not directly
  inside this face - the outer bound of #4246 lies between them`. No analytic
  flag involved; the model is quoted under item 13.
- **lid10 and bayonet report identical worst corners** - cone 7.1595e-01,
  cylinder 1.0349e-01, plane 5.2461e-01, B-spline 8.5764e-02 - over 814 and 82
  faces. The two parts may share that geometry; not checked.

### 15. The corner slack is two populations, and one of them is where SOLIDWORKS still objects

**Measured 2026-09-14.** `export-step-flagship-corners` fails on all six coupons
because corners of analytic faces do not lie on the surface those faces are
written on. Counted per file, corners off by more than 1e-6:

| export | B-spline | cylinder | cone | plane |
| --- | --- | --- | --- | --- |
| `band-fn064`, normal | 822 of 1252, worst 0.0310 | 850 of 1412, worst 0.0241 | - | 0 of 236 |
| `lid10`, normal | 304 of 622, worst 0.0858 | 24 of 160, worst 0.1035 | 290 of 966, worst 0.7159 | 13 of 4306, worst 0.5246 |

**The bulk** is junction vertices a boolean made, sitting on chord planes at
sagitta scale - which is what the corner placement exists to move and what the
veto refuses wholesale. Relaxing the veto barely touches it (`f04`'s cylinders
0.0241 -> 0.0212), so this is mostly item 13: provenance names no owner, so
nothing places them.

**The outliers are a different animal and they are localised.** lid10's 0.5246
plane corner sits at (78.6, 10.41, 1.72) and its 0.7159 cone corner nearby -
r ~ 79, z ~ 0 to 3. That is exactly the cluster SOLIDWORKS objects to in *both*
builds and in every run since 2026-09-10: the 15.16 mm² B-spline (17/17), the
19287 mm² cone (7/7), the 850.75 mm² cone (21/21/21), the 15.3 mm² plane (7/7)
and the faulty edge at (79.3344, 1.0534, 2.5811). bayonet carries the same
entities, which is why the two parts report identical worst corners.

So the next chase is that feature, not the slack in general: one small region of
one part, faulty in a CAD system, carrying corner strays five to seven times
anything else in the file. Read `-FaultDetail` against what the model puts there
before forming a theory.

#### Chased 2026-09-15: the bulk is the veto, and it costs a CAD system the whole wall

**What the slack costs, measured rather than assumed.** lid10's inner wall
between the thread turns is not one cylinder: the lid has a draft, so it is a
run of *cone* faces (refR 79.402, half angle 1.15 degrees) spanning z 1.2 to 65.
Sampling the wall confirms it - a point at r = 79.0 lies on one of them at
0.0000. SOLIDWORKS knits those pieces into a single face of 19293.86 mm², larger
than any face this exporter writes, and reports it faulty with code **7**,
`swEdgeVertexNotLie` - a vertex that does not lie where it should. One faulty
face, and the whole wall between the turns disappears from the view. That is
what a CAD user sees, and it is item 15's bulk doing it.

**The veto is what holds those corners off the surface.** With the veto relaxed
(and C and D fixed), the same file's wall corners are almost all placed:

| cone (inner wall) | normal build, corners off / worst | relaxed veto |
| --- | --- | --- |
| four of them | 25, 60, 60, 30 / up to 0.036 | **0, 0, 0, 0 / 0.0000** |
| the two largest | 46 / 0.1075, 48 / 0.1050 | 1 / 0.0200, 1 / 0.1050 |
| the lowest | 13 / 0.0667 | 1 / 0.0667 |
| the bottom 45-degree chamfer | 5 / **0.7159** | 4 / **0.7159** |

290 stray corners become 9. So on lid10 - unlike the band family, where it moves
0.0241 to 0.0212 - relaxing the veto is decisive, and the earlier note that it
"barely touches" the slack is corrected.

**The outlier is not corner placement at all.** The 0.7159 sits at
(79.5875, 0, 0), a corner of the bottom chamfer, and following that one vertex
through the export settles what it is not:

- it belongs to **one original** (`id134`), so provenance never calls it a
  junction: no two-owner move, no single-owner pass, not welded, no move ever
  proposed for it, in either configuration;
- the chamfer it bounds is a **fitted** cone - the exact tier writes no cone of
  that radius at all - claimed by the trimmed-quadric pass over 6 facets;
- and that claim's worst corner, over every facet it claimed, is **0.1089**. The
  0.7159 vertex is not in the claim, not in any of its boundary runs, and not a
  corner of any facet it took.

So the written face's loop acquires a vertex the recogniser never accepted,
somewhere between the claim and the writer. That is root cause A's shape - a
boundary settled in two places - for *vertices* rather than for edge geometry.
The next measurement is `patchFromFacets` and the loop assembly for that patch:
which step adds the vertex, and on whose authority.

Four faces meet at it - the fitted cone, a 10-corner fitted B-spline, and two
planes, one of them the negative-area triangle of item 16 - so the thread
run-out at theta = 0 is one small neighbourhood carrying three of this file's
oddities at once.

### 16. What a CAD view of lid10 shows, and which of it is ours

**Measured 2026-09-14** after three observations from a SOLIDWORKS session on
`lid10-D-fixed-normal.stp`, the committed build's export.

**"The wall between the thread turns is missing" - not ours.** The same gaps
appear in the normal build and under the relaxed veto, and on the file itself:
`free_boundaries` is **0**, `BRepCheck_Analyzer` is valid, the shell is closed,
and both files carry the same six cylinder faces at 53685 mm². Read back with
OpenCASCADE, meshed and rendered, the inner wall and the thread are continuous -
nothing is missing. SOLIDWORKS keeps 799 of our 843 faces (787 of 814 in the
normal build) and hides what it rejects, which is what the view shows. A
rendering of our own file is the cheap control for this and it should be the
first thing done whenever a CAD view suggests missing geometry.

**"The sweep is faulty" - ours, and known.** That is item 1's code 17 on the
swept B-spline. Under the relaxed veto `f04` reads `faults=0` and bayonet's
sweep fault disappears; lid10's does not - see the run of 2026-09-14.

**"Stray faces shaped like a double T at the rim" - ours, but the mesher's.**
The band z in [88, 96] holds 346 faces, 344 of them planes totalling only
1725 mm²: 112 are hairline, the smallest **0.0003 mm² spread over a 0.089 mm
diagonal with 8 vertices** - a zero-width zig-zag, which is what a CAD view
draws as a double T. The four-fold repeats are the bayonet's own symmetry, not
duplicates. The faceted control carries them too (26 hairline faces against the
analytic export's 35, same worst aspect 0.002679 over 1.235), so these are the
boolean's slivers surviving `mergeTriangles`, not something the analytic path
invents. The exporter's degenerate-face filter passes them because they have
area; what they have instead is no width.

**And one face OpenCASCADE reads with a negative area**, at r = 79.417,
z = 1.384 - inside the r ~ 79, z ~ 0..3 cluster of item 15 where SOLIDWORKS
faults in every run. It is a triangle, and the file's description of it is
consistent: its three edges pass through the vertices they name to 2e-16, its
loop traversal winds right-handed about the plane it asserts (dot = +0.9999, and
`check_face_normals` agrees), and OpenCASCADE builds the right parametric region
for it, u spanning 2.2008 and v 0.9243. Only the *sign* of the area is negative.
Unexplained: either a kernel quirk on this geometry - the plane's reference
direction is parallel to one of the triangle's edges - or something in the file
that neither the validator nor the loop arithmetic can see. It is the first
thing to settle in item 15, because a face a kernel reads inside out is a face
it will refuse.

**A fourth finding, from trying to render it:** `import_step` cannot read this
exporter's own output. It reports `Unknown Type B_SPLINE_SURFACE_WITH_KNOTS` and
`B_SPLINE_CURVE_WITH_KNOTS` and then aborts with a `boost::container::length_error`.
The round-trip oracle in `doc/step-export-development.md` §1 is therefore
OpenCASCADE only, and our own importer is neither a check nor a viewer for
anything the analytic path writes. Crashing on input is worth fixing on its own.

### 13. Provenance loses an original a boolean cut on every facet

**Filed 2026-09-14, out of item 11's root cause C.** An original owns a surface
when at least three of its facets lie on it *whole*
(`export_step.cc`, `least = 3`), and cut facets are deliberately not counted -
§10 of the development document records why: a facet cut across a seam has two
corners on the far surface, and counting it made every solid own everything it
touches. The rule is right about seams and blind to one case: an original whose
every facet a boolean cut. It then owns nothing, its declared surface drops out
of the vote at every junction it is part of, and two things follow.

1. **A wrong single owner**, where one other original meets it. That was C, and
   the conic placement now refuses to act on it. The owner is still wrong; the
   placement no longer believes it.
2. **No owner at all**, where the other original is cut the same way. Nothing
   places those corners, and they stay on the mesh's chords.

The model that shows both, with nothing else wrong in it:

```scad
difference() {
  cylinder(r = 23, h = 40, $fn = 24);
  translate([0, 0, -1]) cylinder(r = 20, h = 42, $fn = 24);
  translate([0, 0, -1]) rotate([0, 0, 7.5]) cylinder(r1 = 20.6, r2 = 19.4, h = 42, $fn = 24);
}
```

The frustum crosses every bore facet mid-facet over the full height, so bore and
frustum each own 0 whole facets (72 cut apiece) and the outer wall owns 48.
Provenance: one owner for 48 junctions - the bore's 24 rim corners at z = 40 and
the frustum's 24 at z = 0, each handed the wall - and none for the 48 where bore
and frustum cross. Both flags:

| build | validator | worst corner, OCCT |
| --- | --- | --- |
| before `6c74a41` | bore 3.0 and frustum 2.43 off themselves | cylinder 3.0000, cone 2.4276 |
| after | valid | cylinder 0.1711, cone 0.1725 |

The 0.17 left is consequence 2: the unowned crossing corners, a sagitta of the
r = 20 24-gon, 20(1 - cos(pi/24)) = 0.1711, off the bore and about as much off
the frustum. That is why this model is not a fixture yet. It would fail the round trip after C's fix for a
reason C's fix does not touch, and a fixture must pass once its defect is fixed.
When consequence 2 is fixed it becomes the fixture for both, and its volume is
derivable: pi (23^2 40 - 20^2 20 - (35/3)((20.6 - 1/35)^3 - 20^3)) = 15485.700931,
the frustum meeting the bore in the circle at z = 20.

Four models were built on the way to it, and what each refuted is worth keeping:

- a rotated r = 20.05 24-gon subtracted over z 5..35 reproduces C but exports
  invalid *faceted* as well (item 12):

  ```scad
  difference() {
    cylinder(r = 23, h = 40, $fn = 24);
    translate([0, 0, -1]) cylinder(r = 20, h = 42, $fn = 24);
    translate([0, 0, 5]) rotate([0, 0, 7.5]) cylinder(r = 20.05, h = 30, $fn = 24);
  }
  ```

- a torus bead through the bore leaves the bore whole facets and fires the veto;
- a ring of square section, r 19..21, cuts the bore only along its own corner
  lines, so every facet stays whole;
- the frustum over z 5..35 only leaves the 48 bore facets below z = 5 whole, cut
  horizontally by the frustum's cap along the bore's own corners.

What counts as ownership is the question, and the seam case in §10 is the
constraint any answer has to keep.

### 14. The two-owner placement takes corners off declared surfaces, and no face notices

**Filed 2026-09-14, measured and not chased.** While choosing C's fix, every
proposed move on every export was asked whether it takes a corner off a declared
exact surface the corner is on to 1e-9, split by path:

| export | two-owner path, off a declared quadric | off a declared plane |
| --- | --- | --- |
| `lid10` | 60 | 180 |
| `bayonet` | 0 | 120 |
| every fixture, the band family | 0 | 0 |

None of these leaves a face it bounds - that check read zero on both parts - so
the file is not wrong for it, and C's fix was deliberately not widened to refuse
them. Either the declared surface is one those corners are on by coincidence
(an infinite cylinder's record beyond the part, a plane through a rim), or the
crossing two owners find is not the corner's whole story. The measurement to
make is the entity read: which declarations, and where the face across is.

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

> Read `doc/step-export-wip.md` — "Read this first" and "Where it stands" before
> anything else, then open items 11, 1 and 12 — and `doc/step-export-development.md`.
> Build and test mechanics are in `CLAUDE.md`.
>
> The branch should be green. Verify with
> `ctest --test-dir build -R 'export-step-|mutations'`: **53 tests**, not
> `-R step`, which drops the mutation harnesses. Run it under the machine's own
> locale. Two of the 53 fail on purpose and are registered `WILL_FAIL`, so they
> report green: `export-step-flagship-strict` (the plane veto fires) and
> `export-step-flagship-corners` (corners of analytic faces off their surface
> by the tessellation's slack). Do not read either as fixed, and do not delete
> them. Confirm the round trip actually runs (`cadquery-ocp` 7.x, not 8.0)
> before trusting any green result - and remember the flagship check could not
> fail its round trip before `97ce12b`.
>
> Every STEP export needs `--enable=step-analytic-surfaces
> --enable=step-approximate-surfaces`; without them it writes facets and passes
> for the wrong reason. lid10 needs `-p examples/step_test/lid10.json -P "New set
> 1"` passed as an argv list, never through a shell function that can lose the
> quoting.
>
> **Where it stands.** Code 17 moved for the first time: on `f02-band-fn032`,
> whose sweep-to-bore boundary is 593 crossing curves to 47 chords, the bore
> cylinder is clean and the sweep reports code 21. On `f04`, lid10 and bayonet —
> one crossing curve each — code 17 stands. The gate withholding the boundary
> from those three is the plane veto, which fires on all six flagship coupons.
> That is n = 1, and the work is to make it n = 4 honestly.
>
> **Take item 11's root cause D.** A, B and C are fixed at the root; C turned
> out to be provenance handing a corner a single wrong owner, not coaxial owners,
> and lid10 carried it too. With C closed the veto's gate was measured for the
> first time and it is not green for a reason no check asserts: under the
> relaxation lid10 reads back as a valid solid 28% too large (292144.94 against
> 227707.04) and bayonet 6% too small, while the band family's volumes move
> toward the model's. Find which of lid10's 29 extra faces carries it, entity by
> entity, the way A and B were read. Then the decision item 11 leaves open: what
> the veto waits for on corner slack, which is red in both configurations.
>
> **How to work it, which is how A, B and C were found:**
>
> - Five whys with the branches named before each measurement, and the refuted
>   ones recorded. This investigation has refuted more hypotheses than it has
>   kept, including two of its own root causes on the first attempt; the
>   refutations are part of the result.
> - Fix at the root, not at the gate. Relaxing the veto was proposed on an 89:1
>   ratio and turned out to be holding back four defects. Do not relax it until
>   D is closed, the flagship check is green under the relaxation - read its
>   volumes, not only its verdict - and SOLIDWORKS agrees.
> - Prefer the declaration to a deduction. When a structural fact about a
>   declared surface is needed, ask the surface — `splineForm`,
>   `membershipTolerance`, `isDeclaredPoint` — rather than reaching around a
>   protected member or inferring it by projection.
> - Verify a latent fix where the defect lives. A, B and C change nothing the
>   flagship coupons' normal build writes, and were verified against the
>   relaxed-veto configuration (`b57c8e73b`'s settle loop), where they do. Mutate
>   by writing the working tree with `git show <sha>:<path> > <path>`, never
>   `git checkout <sha> -- <path>`, which stages the file.
> - A defect the suite did not catch does not land its fix until something in the
>   suite fails without it. A unit test where the defect is in a solver, derived
>   and shown to fail; a fixture only where it is visible solely in a whole export.
> - Anything that should never be reached is loud, at the strength the
>   measurement supports: `EXPORT-ERROR` if unreachable today, `EXPORT-WARNING`
>   plus a `WILL_FAIL` assertion if reached today. Item 12 is the survey for more.
> - Ask what a check rejects before quoting what it accepts: hand it a file known
>   to be wrong. The flagship round trip passed a 25-shell file for four days.
>
> **Standing rules.** Every expectation derived from the model, never captured
> from a run. Report how much of each sweep was recognised beside any fault
> count. For SOLIDWORKS: start it by hand, label every run with its import
> settings, never kill the driver mid-import, write its output to a file rather
> than through a buffering pipe, and put the 2026-09-08 control
> `build/interop-kit3/f02-band-fn032-analytic.stp` in the same session — it must
> read `faults=2 faultyfaces=2 codes=17`, or a page of clean rows means nothing.
> Read `-FaultDetail` before forming a theory.
