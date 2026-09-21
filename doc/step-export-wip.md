# STEP export: work in progress

Open items, what is unexplained, what is parked, and where the next session
starts. Everything here is provisional by construction.

What is settled lives in `doc/step-export-development.md`; build and test
mechanics live in `CLAUDE.md`. Read both before this one — several items below
are one sentence because the reasoning behind them is there.

**Last updated 2026-09-21.** Start at *Handover: the next session starts here*
immediately below - it carries the priming prompt, the branches, the standing
rules and the tooling. The session of 2026-09-16 to 18 is summarised under
*Where it stands*; 2026-09-18 to 19 is open item 23. The suite is green at 54 of
54 on `claude/step-ladder-precision`, and every SOLIDWORKS number since
2026-09-17 was taken with `-Raw`.

---

## Handover: the next session starts here (2026-09-21)

### The priming prompt

Paste this into a new session, adjusting only the worktree path:

```text
You are continuing work on the PythonSCAD STEP exporter, in a worktree of
E:\29_Git_Soulsolistice\pythonscad. Branch: claude/step-handover-2026-09-21,
which is claude/step-ladder-precision plus this handover. Nothing is pushed.

Read first, in this order: CLAUDE.md (build, test, the dev loop), then
doc/step-export-wip.md "Handover: the next session starts here" - the standing
rules there bind - then doc/step-export-development.md sections "The census",
"How to interrogate a reader", "Fault codes" and "Entities: what ISO 10303-42
has, and what we write", then this file's open items 21, 22 and 23.

The development loop is committed; do not write your own build or export
scripts. From PowerShell:

  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 build
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 test
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 export lid10
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 census <file.stp>

scripts/step-dev.sh is the same from bash. Verify with `test` (54 of 54) before
and after any change. Never run two builds or two suites at once.

Every diagnostic is catalogued by the question it answers in
scripts/step-diagnostics/README.md - read it before writing any instrument. Run
the census on our own file before asking any reader anything.

Read doc/step-recognition-design.md before touching recognition. Steps 1 and 2
of it are landed on claude/step-claim-two-pass: every recogniser's claims are
recorded in a ledger, and a shadow check holds the exporter against it. Step 3
is next - flip the source of truth from `consumed` to the ledger, and not one
number in the suite may move.

Where to start: (1) **make a declared surface's recognition whole**. lid10's
cone 50 is one connected region of one declared cone written as two kinds of
face - 56 facets to a band's wall, 6 to a quadric face, touching along 2 mesh
edges - and the bayonet has a cylinder and a cone the same way. The quadric
pass would have written all 62 as one face; the band's claim is the partial
one. Two candidate fixes, both needing a PoC measured on derived volume and the
curve kinds at the rims, are in doc/step-recognition-design.md. Do *not*
arbitrate facet ownership first: measured, 452 of lid10's 452 contested facets
name the same surface as the claim holding them, so an ownership arbiter has
almost nothing to decide.
(2) lid10's two code-21 faces, planes of area 1.466 at (-56.999,-55.863,89.788)
and (-55.863,56.999,89.788), never investigated; (3) open item 21's corner and
the 1% volume question in item 23's SOLIDWORKS run.
```

### Branches

Nothing has been pushed. Every branch below is local.

| branch | head | what it is |
| --- | --- | --- |
| `claude/step-claim-two-pass` | `cadd09045` | **the live work, 2026-09-21**: the build fixes, both doctrines, the recognition design, PointOrigin, and steps 1-2 of the ledger. Suite 54 of 54 |
| `claude/step-claim-revision-parked` | `e30a43240` | the claim revision on the arbiter's verdict. Right mechanism, wrong place - see item 25 |
| `claude/step-claim-after-placement-parked` | `df15f82d6` | the unconditional claim and the corner-stray diagnostic; lid10 42 -> 52, f04 38 -> 8 |
| `claude/step-handover-2026-09-21` | `b9703205b` | the previous handover, docs only |
| `claude/step-ladder-precision` | `1123b8e4c` | **the landed work**: the corner arbiter and its two tables, the constraint tiers, the code-7 instrument fix, the survey and the SOLIDWORKS run. Suite 54 of 54 |
| `claude/step-grid-claim-parked` | `2bd2cfdf4` | the grid claim without its statistical refusal, spans read from corners, and sweep cuts written as the surface's own column curves. f04 code 17 38 -> 8, lid10 42 -> 52 |
| `claude/step-corner-arbiter-parked` | `4f4411b94` | the first arbiter; **superseded** by `claude/step-ladder-precision` |
| `claude/step-grid-junction-parked` | `954a2c7d3` | the relative-distance exemption; **superseded and refuted** - it was a threshold |
| `claude/step-sweep-chord-parked` | `1de808ff9` | the sweep chord promotion, parked 2026-09-18 on the validator's B-spline pcurve gap (item 22) |
| `claude/pythonscad-step-solidworks-fault-6e908a` | `7bb1fde58` | the session branch this work grew from; contained in the ladder branch |

### What this session established (2026-09-18 to 21)

Measured, each on the reference parts, suite green at every landing:

1. **A corner is determined by the faces the file writes on it, and three of
   them meet in a point.** `projectOntoThree` solves that; the ladder had no such
   solver and two rules deciding by distance instead. Code 17 on lid10 and the
   bayonet 62 -> 42, f02 38 -> 8, f04 62 -> 38; codes 16 and 13 to zero.
2. **The travel bound was direction-blind.** "No further than the nearest
   neighbour" refused a 1.338 move whose nearest neighbour lay 0.2356 away
   *perpendicular* to it, with 4.6 of room along it. It is replaced by asking the
   file's own question: no edge at the corner turns round, and no facet written
   as itself folds or leaves its plane. Facets an analytic face absorbed are not
   asked - they are not in the file.
3. **Chord planes must not vote.** Scoring the tessellation's own planes in the
   arbiter caused every downstream defect seen this session: shells opening,
   fans that could not be cut, and two corners placed on one root 1.4e-12 to 1.9
   apart in the mesh. With mesh planes `used = false`, none of it occurs. The
   merge built for the "coincident corners" - and the ring dedupe, and the cap
   recognition, and a coaxial-circle construction - were all removed unused.
4. **Three instruments were blind, two of them ours.** `vertex-on-curve.py` (and
   through it the census's code-7 row) returned 0 for every `SURFACE_CURVE`,
   which is every sweep edge, and measured a B-spline only at its end control
   points; it now unwraps and evaluates by de Boor, mutation-checked.
   `validatestep.py` rejected any B-spline curve that was not one Bezier span
   (fixed on the parked branch). The veto's "faces would turn over" counted the
   faces that did *not*.
5. **Recognition and the writer disagreed about a facet's profile span** -
   centroid against corners - and the corner reading is the right one. With it,
   a wrapping claim is seen and cut, and lid10's seam crossings go 2 -> 0.
6. **A cut along a sweep's column is a curve of the surface itself**, not a
   chord: `GridSurface::columnCurve`, the surface's own u-knots and that column
   of its control net. 155 chords off the sweep on lid10 become 1.
7. **What blocks the rest is a claim question, not a solver.** The five corners
   left at the thread's ends are exactly on two exact declarations and 0.10-0.22
   off the sweep - they sit on the ridge's flat end cap, which the sweep never
   reaches - so the facets attaching them to the sweep face are not the sweep's.
   A claim decided by *solvability* is the next design step.
8. **SOLIDWORKS moved.** lid10 went from `faultyfaces=7 faultyedges=1
   codes=7/17/21` to `faultyfaces=3 faultyedges=0 codes=21/30`; the bayonet to
   code 30 alone; f02 and f04 are clean by every API check.

### Standing rules

These are not preferences. Each was paid for, and the last two are new.

1. **Only declared values and closed algebraic solutions are viable upfront.** A
   magic number, a statistical threshold (a median, an outlier factor) or an
   approximation band is the **absolute last resort** and needs explicit
   approval before anything is built on it. The exporter's hard defects have all
   been a computable quantity decided by a threshold instead.
2. **A band excuses the defect it should expose.** A grid publishes its
   tessellation's sagitta; measuring against it hid 19 of 27 sweep chords and two
   corners 0.02-0.04 off the sweep. Measure against the surface **as written**.
3. **Precision beats ladder order - the rung philosophy.** Wherever a less
   precise rung has claimed a corner, face or edge first, the more precise answer
   replaces it. The low-precision rungs are *historical*: they were written
   before the exact ones existed, and first refusal (`moves.count(v)`) kept them
   winning. Since 2026-09-19 the corner placement is an **arbiter over
   candidates** rather than a ladder of claims, and its precedence lives in two
   tables at the top of `src/io/StepKernel.cc`:
   - `kCornerTiers`: what kinds of constraint a corner has, with `rank`
     (precision: declared exact, declared interpolant, fitted face, provenance
     owner, mesh plane), `hard` (leaving it disqualifies a candidate) and `used`
     (whether it enters the arbiter at all).
   - `kCornerRungs`: which rung wins a tie between equally exact answers.
     Reordering the ladder is editing a table, not the control flow. Measured
     satisfaction decides first, so a table cannot let a less precise rung beat a
     more precise answer. The tables, what each setting was measured to do and
     what the arbiter does *not* decide are in `doc/step-export-development.md`,
     *The ladder is an arbiter now, and its precedence is two tables*.
4. **Exactness takes precedence**: a declared surface always beats a fitted one,
   and a substituted curve must pass through its own run's endpoints.
5. **Prefer the declaration to a deduction.** When two things meet, ask whether
   both are declared before computing their crossing.
6. **Kernel agreement is not proof.** OpenCASCADE heals on read. New checks read
   the STEP text with no kernel in the path - and when an instrument reads zero,
   mutate a file and prove it can read non-zero.
7. **Five whys with named branches**; record the refuted ones. Land no fix until
   something in the suite fails without it, and **land no guard that nothing
   exercises** - three exact constructions were removed this session for that
   reason alone.
8. **Half a fix is worse than none.** A change that trades one part's numbers for
   another's parks, with its measurements.

### SOLIDWORKS: how every run is made from now on

Start SOLIDWORKS by hand, never kill the driver mid-import, and run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\step-interop-solidworks.ps1 `
  -KitDir <kit> -ImportSettings "raw (do-not-knit, diagnostics off, analytic conversion off)" `
  -Raw -RoundTrip -Heal -FaultDetail <kit>\faults.tsv
```

- **`-Raw` always.** Knitting, healing and analytic conversion are on by default
  and each changes what is measured; the script restores them afterwards.
- **`-RoundTrip` always**, and **`-Heal`** to isolate the repair: the pair
  `<coupon>_SW.STEP` and `<coupon>_SW_healed.STEP` differs by the repair alone.
  Compare with `scripts/step-interop-sw-roundtrip.py --kitdir <kit>`, which runs
  both comparisons, and `scripts/step-diagnostics/reader-diff.py` for a pair.
- **`-FaultDetail` always.** The counts say how many faults; the TSV says *which
  entities*, with kind, area and centre - which is what turned "lid10: 3 faulty
  faces" into "one B-spline at the thread's run-in and two planes at z = 89.788".
  It re-imports every file a second time, so on a full kit take the counts first
  and re-run with `-FaultDetail -Only` over the files whose counts were not zero.
  Note it writes relative to the *caller's* directory.
- **Always include the control** `f02-band-fn032-analytic.stp` (in
  `build/interop-kit3/` of the `solidworks-fault-code-17-b58858` worktree), which
  must read `faults=2 faultyfaces=2 codes=17`. A run without a positive control
  proves nothing.
- Label every run with its import settings, and count faults **before** any heal.

### The tooling, and the order to use it

- **`scripts/step-diagnostics/README.md` is the catalogue**: every instrument
  this work has built, grouped *by the question it answers* - the census first,
  then one check per fault relation, then how to locate a fault in the file, then
  how to interrogate a reader. Read it before writing a new instrument; the
  question has usually been answered already.
- **First on any export**: `scripts/step-diagnostics/fault-census.py <file.stp>`,
  which keys every property known to become a SOLIDWORKS fault by the code it
  becomes. Our file is the part we control; a reader's verdict is the last thing
  to ask.
- The dev loop is `scripts/step-dev.ps1` / `.sh`:
  `build | test | flagship | export <part> | census <file>`. It sets MSYS2 up,
  passes arguments untouched (lid10's `-P "New set 1"` is what an inline command
  mangles), exports with both feature flags, and runs the suite with the staging
  DLLs on PATH.
- `CLAUDE.md` has the build, the test invocation, the Windows traps and the
  interop kit; `doc/step-export-development.md` has what is settled.

---

## Read this first

Verify the tree before changing anything, and again after:

```bash
ctest --test-dir build -R 'export-step-|mutations'
```

**54 tests**, and it has to be this regex and not `-R step`, which drops
`bspline-check-mutations` and `closed-sphere-check-mutations` — the harnesses
whose job is to prove the other tests would fail if the defect came back. What
the 53 are: 33 SCAD fixtures in `tests/data/scad/step-export/`, 12 Python in
`tests/data/pythonscad-step-export/`, the two mutation harnesses, the three
flagship tests below, and three `*_arg-permutations` tests that have nothing to
do with STEP and match `mutations` by substring. It was 52 until
`export-step-flagship-corners` was added on 2026-09-14 and 53 until
`step-cap-meets-wall` on 2026-09-16; a build configured before those sees fewer,
and **a new fixture needs `cmake -B build` before ctest can see it at all**. Run
it under the machine's own locale, not `LC_ALL=C`,
which breaks five UTF-8 filename tests.

**One of the 54 fails on purpose and reports green**, registered `WILL_FAIL`:
ctest reports it *passed* while it fails, and will report it *failed* on the day
it starts passing — which is how that fix gets noticed. Do not delete it, and do
not read its green as the defect being gone.

- `export-step-flagship-corners` asserts that every corner of an analytic face
  lies on that face's surface, as every fixture's round trip does. Since
  2026-09-19 it **passes on the four band-family coupons** and fails only on
  lid10 and the bayonet, where OpenCASCADE's own repair at the thread's run-in
  is what it measures (item 23). Its allowance is near 1e-6.

`export-step-flagship-strict` was the second until 2026-09-16, when open item 11
landed; it asserts that the plane veto never gives up the crossing-curve
boundary and passes on its own merits.
`export-step-sanitytest_step-cap-meets-wall` was the third until 2026-09-16,
when the placement onto the crossing of the two faces a corner bounds fixed the
slack it was marked for - ctest reported it as a *failure* that day, which is
what the marking is for, and it now passes on its own merits.

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

**Last measured 2026-09-15, on `claude/solidworks-fault-code-17-b58858`.** Items
1, 11, 13, 14, 15 and 16 below carry the detail; this is the map.

### The session of 2026-09-16 to 18 in one page

**What it fixed, each measured on our own file first:**

| change | measured on lid10 |
| --- | --- |
| a substituted curve passes through its run's own endpoints; planar faces are surfaces of the corner placement | vertices off their own edge's curve (code 7) **14 -> 0**, worst 1.6196 |
| a band's rims reach the placement | corners the plane veto holds 20 -> 9 |
| the re-seam: a closed profile's seam goes in a span the face does not use | faces crossing their surface's seam **2 -> 0** |
| the plane veto: a holed face is triangulated, and a triangle is never "bent" | corners held **9 -> 0**; item 11 closed; `flagship-strict` off WILL_FAIL |
| the conic a plane cuts, hyperbola and parabola included | edges off a surface they bound **91 -> 23**; 64 hyperbolas written |
| a chord on a sweep becomes the crossing curve, with a pcurve checked against it | **parked** on `claude/step-sweep-chord-parked` - see item 22 |

SOLIDWORKS moved once: **the bayonet lost codes 7, 17 and 30** with the conic
promotion. lid10 has not moved, and the plane-veto fix made it *worse* for a
while (code 13 returned) by leaving one corner behind - item 21.

**What it learned about method, which is worth more than the fixes:**

1. **A fault code is a relation between two entities, and the sentence says
   which.** 7 is vertex-against-its-edge's-curve, 16 vertex-against-face-surface,
   17 edge-against-face-surface. Every instrument before this session measured
   16, which never fires. See *Fault codes* in the development doc.
2. **Second-order codes are what a reader makes of a first-order defect.** A
   0.219 edge is a good edge until the reader widens a strayed vertex past its
   length - then it is code 13. The census reports the precursor, not the code.
3. **Import raw, round-trip always, round-trip twice to isolate a repair.**
   Knitting, healing and analytic conversion are on by default and each changes
   what is measured; analytic conversion had been altering the control's volume
   by 1.1 mm3 in every run ever made. The plain/healed round-trip pair is what
   showed SOLIDWORKS's "General geometry problem" to be 215 curves re-fitted by
   at most 9.6e-05 - nothing structural - when no fault list could.
4. **A failed repair makes the diagnosis worse.** `ImportDiagnosis` took the
   bayonet from 64 faces to 2 and no volume. Count faults before any heal.
5. **The API's fault checks and the Import Diagnostics dialog are different
   analyses, and neither contains the other.** `Check`, `Check2` and `Check3`
   agree with each other everywhere; the dialog disagrees with all three.
6. **Measure against the surface as written, not against its declared band.** A
   grid's band is its tessellation's sagitta; by that standard a chord 0.1 off
   the written sweep is "on" it, which hid 19 of 27 sweep chords.
7. **Kernel agreement is still not proof.** OpenCASCADE reports strays our file
   does not contain and misses ones it does, because it heals on the way in.
   Every new check this session reads the STEP text with no kernel in the path.
8. **SOLIDWORKS's strictness is the asset.** Every defect above was found because
   it refused a file OpenCASCADE had quietly repaired.

**What it built:** `fault-census.py` (every known defect keyed by the code it
becomes; validated by the 2026-09-08 control, 2420 chords against 38-62 for
current files), `vertex-on-curve.py`, `seam-straddle.py`, `reader-diff.py`,
and `step-interop-solidworks.ps1 -Raw -RoundTrip -Heal`.

**Where to start next.** Item 22's sweep half turned out to be a declaration
problem, not a curve problem: every plane that failed to cross the sweep is
**undeclared** - a facet the boolean left, meeting a declared sweep at 3.7 to 20
degrees, so the crossing is ill-conditioned and not the edge's geometry.
**Answered the same day** (item 22, *The undeclared planes are the thread's own
facets*): they are the ridge's flank where it dives into the wall, refused by
the grid claim's outlier rule, and every refused corner is a junction with a
declared wall. Admitting them is parked on `claude/step-grid-junction-parked`;
it needs three-surface corners placed first, then a fan and a wrap fixed. After
that: the three pinched loops (code 21, never investigated) and item 21's one
corner.

### The session of 2026-09-14/15 in one page

Two root causes closed, a check repaired, one CAD verdict moved, and the slack
turned from a test failure into a named cost:

| | before | after |
| --- | --- | --- |
| `f04` in SOLIDWORKS, relaxed veto | code 17 on sweep and bore | **`faults=0`**, sweep kept - n = 2 |
| lid10, relaxed veto | 292144.94 (+28%), bottom sealed | 225616.30, bottom a 612 mm² ring |
| lid10's inner wall corners | 290 off their cone | **9** under the relaxation |
| the flagship round trip | could not fail at all | enforced; corners in their own `WILL_FAIL` test |
| the suite | 52 tests | 53 |

- **Root cause C** (`6c74a41`): the conic placement trusted provenance's single
  owner and slid corners off surfaces they were already on - 48 on
  `band-fn024`, 120 on lid10. Fixed at the root, with an `EXPORT-ERROR` backstop
  over every path.
- **Root cause D** (`f218e8af`): the plane veto's "does a move touch this face"
  test read only the outer loop, so a face bent through a *hole* was never
  examined. That sealed lid10's bottom and cost +28% of its volume.
- **The gate itself was unmeasurable** (`97ce12b`): `step-flagship-check.py`
  tested `not roundtripSTEP(...)`, a tuple, so no coupon could ever fail its
  round trip.
- **Item 15 is no longer cosmetic:** the corner slack is why SOLIDWORKS condemns
  lid10's whole inner wall - one knitted cone, code 7 `swEdgeVertexNotLie` - and
  relaxing the veto takes those corners from 290 off to 9.
- **Item 16** separates what a CAD view shows from what our files contain: the
  "missing wall" is SOLIDWORKS hiding a face it rejects, the "double T" strays
  are the mesher's slivers (the faceted control has them too), and `import_step`
  cannot read our own B-spline output at all.

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

**D is closed since** (`f218e8af`, item 11), so that third clause is spent: the
relaxed exports now read 225616.30 and 236835.78 against the normal build's
227707.04 and 238911.27 - under 1%, and in the direction the band family moves.
What is left of the gate is the corner slack, item 15. Nothing was relaxed.

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

The control has now reproduced `faults=2 faultyfaces=2 codes=17` **nine times**, three of them on 2026-09-14/15.

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

1. ~~**The veto, now that the mechanism is demonstrated.**~~ **Done 2026-09-16,
   and it answered the question it was asked.** The veto holds no corners on any
   coupon now - lid10, the bayonet and the band family at `$fn` 64 all read
   zero - and `export-step-flagship-strict` came off `WILL_FAIL`. Put to
   SOLIDWORKS with the `f02` control in the same session, **lid10 and the bayonet
   come back worse**: codes 13 and 30 return and each gains a faulty edge, on the
   0.219-long edge running out of the one corner the travel bound still leaves
   0.105 off its cone. So the veto was a real defect, fixing it was right, and
   the file a CAD system sees improves only when item 21 lands too.
2. **Item 15's seam fix, which does not land yet.** The chase is finished - the
   0.7159 is OpenCASCADE repairing a face that holds facets either side of its
   closed profile's seam - and the fix on `claude/step-export-seam-fix` leaves
   one face per reference part that `BRepCheck` calls `UnorientableShape`, because the reader
   re-projects a chord's pcurve to v = 1.0221. Either write a boundary it will
   not re-project beside the seam, or leave the seam-side facets of a crossing
   claim unclaimed, which is always a valid export and was measured clean.
3. **Item 19's remaining half: the rim rules themselves.** The measurement is
   done - the refused bands and the fitted cones are the same 120 facets, and the
   fit gains nothing because the declared record claims them in the
   trimmed-quadric pass. What is left is why a band is refused for
   *the rim borders one face per facet* and *the band sharing this rim was
   dropped*, which is item 10's territory.
4. ~~**Then the veto**, through what is left of its gate.~~ **Done, and the
   wall's code 7 does not clear.** Its cone is still faulty with the veto holding
   nothing, so what SOLIDWORKS objects to there is not the corner placement. Note
   also that SOLIDWORKS ships a vertex 0.1015 off its own edge's curve in the
   file it saves after repairing `f02` by hand, so code 7 is not a claim about
   the tier we hold ourselves to.
5. **Item 13: provenance loses an original a boolean cut on every facet.** C was
   one consequence; corners left with no owner at all are the other, and they
   are what keeps the three-primitive reproduction of C from being a fixture.
6. **Item 14: the two-owner placement moves corners off declared surfaces** -
   240 on lid10, 120 on bayonet - without leaving any face they bound. Unexplained.
7. **Item 16's loose ends:** the face OpenCASCADE reads with a negative area at
   r = 79.417, and `import_step` aborting on this exporter's own output.
8. **Code 21 on `f02`'s sweep** is new and unexplained.
9. **lid10's faceted control reads `faults=6 codes=24`**, which weakens lid10 as
   a test until it is understood.
10. **lid10 exports 810 analytic faces** where 818 and 822 were recorded earlier;
   it moved with this work and has not been re-derived.
11. **Item 12**, the survey for other should-never-happen counters. No build.
12. **The last 47 chords on `f02`.** Not the fitter and not the projector, both
   cleared; whether they are the edges touching the 64 coaxial-owner vertices is
   unmeasured.
13. **Only the sweep side of a sweep-to-bore edge carries a pcurve**, because the
    two-pcurve writer needs two pushed sides and only the quadric emitter pushes.
14. **Every plane section on the band family is fitted to a ridge-flank facet**,
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

#### Run 2026-09-16: `f02` reads `faults=0`, and the two faulty faces were the chorded ones

SOLIDWORKS 34.0, settings read back by the driver - `knit=form-solids`,
`check-and-repair=1`, `analytical-conversion=on`, `run-diagnostics=on`,
`solid+surface=on`, units from file - with the 2026-09-08 control in the same
session:

| file | SOLIDWORKS |
| --- | --- |
| `f02-band-fn032-analytic`, 2026-09-08 control | `faults=2 faultyfaces=2 codes=17`, **the tenth reproduction** |
| `f02-band-fn032-analytic`, today | **`faults=0 faultyfaces=0 faultyedges=0`** |

The control's two faulty faces, from `-FaultDetail`, are the swept B-spline of
1027.84 mm² and the bore cylinder of 2098.71 mm², both `17/17` - which are
precisely the two faces that share all 640 of the edges lying on neither of them
exactly. That was this item's first why, measured on 2026-09-10 and only
*consistent* with the theory until now.

What changed in the file between those two rows is the boundary and nothing else:
498 crossing curves and 142 chords became 630 and 10, and every corner of an
analytic face is on the surface that face is written on - p95 5.98e-02 to
3.50e-13. Recognition is unchanged at 12 faces over one shell.

**So code 17 on this coupon was the chorded boundary.** It is one coupon, and the
other four hold their corners still under the plane veto (item 11), so the
generalisation is not made here - but the mechanism this item was opened to test
is now demonstrated rather than inferred, against a control that reproduced in
the same session.

**And a refutation worth keeping:** *one chorded edge is enough to fail its face*,
this project's own finding from the first attempt, does not hold as stated. Ten
edges are still chords in the file that reads `faults=0`.

**A second, independent signal.** SOLIDWORKS measures today's solid at
19510.2905 mm3 where OpenCASCADE reads 19510.181245 from the same file - 0.0006%
apart. The control's two kernels disagree by 43 mm3 on its own file. Two kernels
that never saw each other's arithmetic agreeing to six digits is the closest this
coupon has come to a derived volume.

#### Run 2026-09-16, second: `f04` isolates the veto, and its faults are the same two faces

Same session, same settings, the control imported again:

| file | boundary | SOLIDWORKS |
| --- | --- | --- |
| `f02-band-fn032-analytic`, 2026-09-08 control | 0 curves / 640 chords | `faults=2 faultyfaces=2 codes=17` - **eleventh reproduction** |
| `f04-band-fn064-analytic`, today, veto as it stands | 1 curve, the rest chords | `faults=2 faultyfaces=2 codes=17` |
| `f04-band-fn064-analytic`, today, **veto relaxed** | **1223 crossing curves**, 10 chords | **`faults=0 faultyfaces=0 faultyedges=0`** |

`-FaultDetail` gives the same two faces in both faulty files, and they are the
same two the control has: the swept B-spline (1019.02 mm2 on `f04`, 1027.84 on
the control) and the bore cylinder (2094.82 and 2098.71), every one `17/17`.

**On `f04` the only variable is the veto.** One binary, one model, one session -
the relaxation is `STEP_RELAX_VETO`, a switch that refuses only the corners a
bent face implicates and settles, and it is never committed. So the chain is
closed on this coupon: the veto holds the corners, the corners hold the
boundary as chords, and the chords are what SOLIDWORKS faults.

With `f02` reading `faults=0` earlier the same session, that is n = 2 for *a
boundary written as the curve the two surfaces cross along carries no code 17*,
against a control that reproduced its own `codes=17` both times.

**What it does not yet settle.** The relaxation is not landable as it stands:
under it lid10 and the bayonet part are valid STEP and one solid, but
`BRepCheck` still rejects one face each - the thin seam-side piece of open item
15, which the corner placement does not rescue. `f04` under the relaxation is
clean by every check here. And two kernels disagree about `f04`'s volume by
0.14% (SOLIDWORKS 19513.5059 against OpenCASCADE 19540.5751 on the relaxed file)
where they agreed to 0.0006% on `f02`; unexplained, and worth a look before that
number is used as evidence for anything.

#### Run 2026-09-16, third: the veto is landed, and `f04` is clean by default

The settle loop is no longer an experiment - it is what the exporter does. Same
session, control imported a twelfth time:

| file | SOLIDWORKS |
| --- | --- |
| `f02-band-fn032-analytic`, 2026-09-08 control | `faults=2 faultyfaces=2 codes=17` |
| `f04-band-fn064-analytic` | **`faults=0`** |
| `r01-lid10-analytic` | `faults=1 faultyfaces=7 faultyedges=1 codes=7/17/21` |
| `r02-bayonet-analytic` | `faults=1 faultyfaces=4 faultyedges=1 codes=7/17/21` |

**`f04` needs no switch now.** The band family is clean at `$fn` 64 in the
committed build, where the same coupon read `faults=2 codes=17` on the swept
B-spline and the bore this morning.

**What lid10 and the bayonet part still carry is one neighbourhood and the sweep.**
From `-FaultDetail`, both parts fault on the same four entities - the 19282.95 mm2
inner-wall cone (7/7), the 15.16 mm2 B-spline (17/17), the 850.75 mm2 cone
(21/21/21), the 15.33 mm2 plane (7/7) and the edge at (79.3344, 1.0534, 2.5811) -
which is exactly item 15's cluster at r ~ 79, z ~ 0 to 3, unchanged since
2026-09-10. lid10 adds its main 6521.23 mm2 sweep at 17/17 and two 1.466 mm2 rim
planes at 21; the bayonet's sweep is clean.

That the wall cone still reads code 7 with 775 of its corners now placed says the
wall is not waiting on the veto any more. What lid10's sweep is waiting on is
its own seam: its worst kernel slack is 1.083252, the signature of the face that
straddles the closed profile's seam, and that is open item 15's fix - parked on
`claude/step-export-seam-fix` because it trades that face for one `BRepCheck`
calls unorientable.

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

#### Answered 2026-09-16: the 47 are the edges whose endpoints are not on the sweep

Measured over every candidate edge on `step-band-family` at `$fn` 32:

| | n | p50 | max |
| --- | --- | --- | --- |
| endpoint off the quadric | 1234 | 2.2e-13 | 9.6e-12 |
| endpoint off the sweep | 1234 | 4.4e-16 | **1.0e-01** |
| **edges whose endpoints miss the sweep by more than 1e-7** | **47** | 8.2e-02 | 1.0e-01 |

Exactly 47, which is the count that fell back to chords. A Bezier interpolates
its endpoints, so no degree and no better tracing can rescue an edge whose ends
are not on the surface, and the miss is the 32-gon's own sagitta,
20(1 - cos(pi/32)) = 0.0963 - those ends sit on chord planes and were never
placed. That answers this item's open question, and it was the *fitter* and the
*tracing* that had been suspected.

**Two things had to change, in that order.**

**The tracing, so that a sample is on the sweep by construction.** Newton on two
implicits converges quadratically against a quadric, whose implicit is algebraic,
and linearly against a sweep, whose implicit is the tangent plane at a foot
point - open item 10's A2, *model-starved rather than iteration-starved*.
Measured: the traced samples sat on the quadric to 2.1e-13 and off the sweep by
8.2e-02. `crossInParameters` solves instead in the sweep's own parameters,

```text
F(S(u, v))           = 0     the quadric, stated algebraically
(S(u, v) - anchor).t = 0     the station along the curve, held
```

so every iterate is `S(u, v)` and the sweep is satisfied by construction. Samples
now reach 1e-14 on the quadric and ~0 off the sweep. The station term is not
optional: zeroing the quadric residual by free movement in the tangent plane lets
a sample slide along the curve, and that took 593 crossing curves down to 405.
Where the trace cannot converge - 208 of 15396 samples, the crossing lying off
the end of the patch - it falls back to the old Newton, which is no worse than
before.

**Then the endpoints.** A corner is placed where the surfaces of the two analytic
faces it bounds cross, which is the general statement of what the older rungs do
for special cases: whatever provenance says about which original made a vertex,
the faces written on it assert that it lies on their surfaces. Provenance cannot
answer this - it is per original solid, so it names a cap's cylinder (item 18)
and it names two coaxial cylinders 3.000 mm apart for these corners, which never
meet. Held to the ladder's own rules: solved to an absolute 1e-11, accepted only
where the result is on both surfaces to 1e-9, and no further than the corner's
nearest neighbour.

| `step-band-family` at `$fn` 32 | before | after |
| --- | --- | --- |
| edges written as the crossing curve | 593 | **630** |
| edges left as chords | 47 | **10** |
| B-spline corners off their own surface, p95 | 5.98e-02 | **3.50e-13** |
| the solid | valid, 12 faces | valid, 12 faces |
| OCCT volume | 19512.721301 | 19510.181245 |

28 corners moved, by 0.23 to 0.96 against nearest-neighbour bounds of 1.13 to
1.63 - large beside a sagitta, because in the run-out the mesh's own trim sits
that far from where the two surfaces truly cross. `step-cap-meets-wall`'s two
stray corners are fixed by the same rung, which is how its `WILL_FAIL` came off.

**Only `$fn` 32 moves**, and that is item 11 again: it is the one coupon where
the plane veto never fires, so it is the only one whose corners are free to be
placed at all. The other four hold every move.

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

### 11. The plane veto is blunt by 89 to 1 - closed 2026-09-16

**Closed.** `export-step-flagship-strict` was registered `WILL_FAIL` on the day it
was written, asserting that the corner placement never gives up the
crossing-curve boundary to protect a planar face; on 2026-09-16 ctest reported it
as a *failure*, which is the marking doing its job - the assertion had started
passing. The marking is off and it is a live test now.

What landed was two things, and only the first was the one this item predicted:

1. **A bent face with a hole is triangulated rather than refused.**
   `triangulateWithHoles` bridges each hole into the outer ring along a segment
   between two mutually visible corners and clips ears off what is left. A bridge
   is traversed once each way, so it ends up an interior diagonal shared by
   exactly two triangles, as a fan's diagonals are. This was the fix this item
   asked for, and on lid10 it triangulates 7 faces into 23.

2. **A triangle is not bent, and never was.** Splitting the veto's refusal
   counter by reason showed the nine corners lid10 still held were held by *five
   triangles*. Three points are coplanar wherever they go; what the test measured
   for one was its tilt away from the normal the **mesh** gave it before the
   placement ran. The writer had the matching flaw - it asserted
   `loop_normals[i]`, that same stale normal - so a tilted triangle really would
   have claimed a plane its corners had left, and refusing the move was the only
   thing keeping the file honest. Both halves are fixed: a triangle with no hole
   is never bent, and a planar face asserts the plane its own corners are on
   (Newell over the loop as written, kept to the mesh's sense so the shell's
   orientation does not change).

Measured, before and after, on the committed build against this one:

| | held by the veto | corners off a face's own surface | worst | code 7 |
| --- | --- | --- | --- | --- |
| lid10 before | 9 | 6 | 0.1050 | 0 |
| lid10 after | **0** | **1** | 0.1050 | 0 |
| bayonet after | 0 | 1 | 0.1050 | 0 |
| band family $fn 64 after | 0 | **0** | — | 0 |

The one corner left on lid10 and the bayonet is the same vertex in both - they
share the feature - at (-77.1194, -11.6539, 65.0000), between the sweep and a
cone, and it is open item 21.

**And it costs more than it looks, because SOLIDWORKS got worse.** Put to it with
the 2026-09-08 control in one session, both reference parts gained faults:

| | before | after |
| --- | --- | --- |
| lid10 | 7 faces, 1 edge, codes 7/17/21 | 7 faces, **2 edges**, codes **7/13/17/21/30** |
| bayonet | 4 faces, 1 edge, codes 7/17/21 | **5 faces**, 2 edges, codes 7/13/17/30 |

Code **13** is `swEdgeVerticesTouch`, and it is on edge `curve3001` at
(-77.1776, -11.5773, 64.9951) in both parts - which is the midpoint of our edge
`#8275`, a LINE **0.219 long running out of the one corner still 0.105 off its
cone**. The mechanism is the one this work has been circling all day: a vertex off
a surface its face is written on forces the reader to widen that vertex until it
covers the stray, and a short edge at it then reads as having touching vertices.
Freeing the corner's neighbours is what exposed it - while they sat off their
surfaces too, the reader's widening covered them all alike.

So the veto fix is right by every measure of our own and it hands SOLIDWORKS a
worse file, through one corner. That is not an argument for putting the veto
back; it is what makes item 21 the next thing rather than a footnote.

The original item follows, for the reasoning it records.

#### As it stood before

> **Landed 2026-09-16.** The veto now refuses only the corners a bent face
> implicates and settles, instead of refusing every move in the model. Root
> causes A, B, C and D below are what made that safe; the numbers it changes are
> at the end of this item. What is left of it is the faces that bend and cannot
> be fanned - 20 corners on lid10, 14 on `f04` - which `export-step-flagship-strict`
> still asserts against, and still fails on.

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

#### Landed 2026-09-16: refusing the implicated corners, and settling

All-or-nothing was right while a partial move broke the export, and root causes
A and B were what broke: an edge's geometry decided twice, and a vertex keyed by
mesh index. Both are closed at the root, so the refusal can be local.

| coupon | all-or-nothing | the settle loop |
| --- | --- | --- |
| lid10 | 795 corners refused for 11 bent faces | **20 dropped, 775 placed** |
| bayonet | the same shape | **20 dropped, 643 placed** |
| `f04` at `$fn` 64 | 1268 refused for 7 bent faces | **14 dropped, 1254 placed** |

It settles in one round on every coupon - refusals only grow, which is the
argument the section settle loop already uses.

The gate this item set for itself is met: `export-step-flagship-coupons` is green
over all six coupons under it, validator and kernel round trip both, with no
`EXPORT-ERROR`, one solid each and `BRepCheck` clean. The corner slack is still
red and still its own `WILL_FAIL` test, as it was.

And what it was for: `f04` reads `faults=0` in SOLIDWORKS with its boundary
written as 1223 crossing curves, where the same binary refusing every move reads
`faults=2 codes=17` on the swept B-spline and the bore. One model, one session,
one variable.

**What is left.** 20 corners on lid10 and 14 on `f04` are still held, each
touching a face that keeps a plane and would bend if it moved. A polygon that
bends can be fanned into triangles, and one *with a hole* cannot be fanned from a
single apex - that is the remaining case, and a real triangulation is what it
wants. `export-step-flagship-strict` asserts no corner is held this way and is
still `WILL_FAIL`, which is the honest statement: 795 to 20 is not 795 to 0.

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

#### Chased 2026-09-16: the outlier is a reader's repair, and the file's own defect is a seam

**The cone never carries that vertex.** In the written file the chamfer cone's
two loops have twenty corners, none of them at (79.5875, 0, 0), and the worst is
0.0770 off. OpenCASCADE reads twenty-one, because it widens three vertices to a
tolerance of **1.08**, merges them, and rebuilds the wires around them. The
0.7159 is `1.0125 * cos 45`, the drop from the sweep's corner above to z = 0 -
a distance in the reader's repaired topology, not in ours.

Refuted on the way, each named before it was measured: the T-junction weld
(item 16's slivers); `get_vertex` resolving by position, which keys on exact
coordinates and cannot merge two points 1.02 apart; a run substitution bringing
in a foreign endpoint; `patch.facets` naming different loops by write time; and
the corner placement, which in the normal build moves nothing at all - the veto
holds all 1094. `patchFromFacets` and the boundary runs are innocent: every
cycle vertex comes from the claim's own facets.

**Why the reader has to repair anything.** A B-spline sweep face holds facets on
both sides of its closed profile's *seam* - corners at v 0 to 0.037 and at
v 0.978 to 0.996 - on a surface written as the open rectangle 0 <= v <= 1. Every
boundary through the seam therefore runs the long way round the profile. Our own
pcurves say so (v 0.9775 -> 0.0585 against a 3D edge 0.099 long), and withholding
them changes nothing: OpenCASCADE re-projects the 3D curve and arrives at the
same place, which is what refuted "the pcurve writer is the defect".

**Why such a face is written.** `recogniseGridPatches` cuts a claim at the seam
only when it uses *every* span of the profile. lid10's boolean buries one span,
so the claim crosses the seam without wrapping, and nothing cuts it. A second
way in: one facet - an ear of the ridge's end cap, corners at the profile's last
point, first and second - lies across the seam by itself, so no cut could place
it either.

**Measured, by withholding just the three seam-crossing facets:**

| lid10, normal build | before | after |
| --- | --- | --- |
| cone's worst corner | 0.7159 | **0.1075** |
| OCCT worst tolerance | 1.0833 | **0.1249** (band 0.2527) |
| vertex at (79.5875, 0, 0) | tol 1.08, on the cone | tol 1e-07, on four planes |

**A fix is written and does not land**, and lives on the branch
`claude/step-export-seam-fix` rather than on this one, which stays green. Four
parts: `GridSurface::onSeam` and
`GridSurface::seamSide`, so the declaration answers where a facet sits against
its own seam; the recogniser refuses a facet lying across it and cuts any claim
that crosses it; the writer gives seam corners v = 1 on the closing side and
raises an `EXPORT-ERROR` for a face still on both sides; and the flagship check
asserts OCCT's slack stays inside the band the export publishes - all six
coupons pass that at 0.58 of their band or less, where pre-fix lid10 needs 1.08
against 0.25.

With it, lid10 and the bayonet part write four sweep faces instead of two, no
`EXPORT-ERROR`, tolerance 0.1249, cone worst corner 0.1075, volumes 227658.65
and 238846.98 against 227707.04 and 238911.27 - and **`BRepCheck` rejects one
face on each**, the thin closing-side piece, as `UnorientableShape`. The file is
consistent: senses, loop directions and pcurves all check. OpenCASCADE discards
the straight pcurve of a chord that sags off the helix, re-projects it, and the
chord's middle lands at **v = 1.0221** - across the seam of a surface it treats
as v-closed - so the rebuilt 2D wire leaves the rectangle. That is where it
stands; the suite caught it, and nothing was committed.

Two refutations worth keeping. A facet's *middle* does not say which side of the
seam it is on: beside the crease the centroid of a closing-strip facet projects
to v = 0.022 while its corners are at 0.98 to 1.0, which cost the first version
of the facet test three good facets and the cut it was meant to trigger. And the
face's orientation taken at that same centroid is equally wrong, though it is
not what inverts the face - changing it moved nothing.

#### Chased 2026-09-16: every way of *removing* the straddle makes SOLIDWORKS worse

Three variants were built and measured, and they agree with each other on every
local number and disagree with the one kernel that matters.

| variant | lid10's kernel slack | worst cone corner | `BRepCheck` | SOLIDWORKS |
| --- | --- | --- | --- | --- |
| as committed | 1.083252 | 0.7159 | clean | `faultyfaces=7 faultyedges=1`, codes 7/17/21 |
| cut at the seam + refuse the across facets (`claude/step-export-seam-fix`) | 0.124888 | 0.1075 | **1 face unorientable** | not run |
| refuse the across facets only | 0.109315 | 0.1050 | clean | `faultyfaces=8 faultyedges=2`, codes **7/13/17/21/30** |
| refuse them and re-seam the face | 0.109315 | 0.1050 | clean | the same, to the entity |

Every local measure says the last two are better: the slack that OpenCASCADE
needs falls by a factor of ten, item 15's 0.7159 outlier becomes 0.1050, every
surface kind's p95 corner is ~1e-13, and the solid is valid and closed. And
SOLIDWORKS reads *more* faults, including two codes - 13 `swEdgeVerticesTouch`
and 30 `swTopolNotG1Continuous` - which had been gone from both reference parts
since 2026-09-08.

**Isolated:** the refusal is what does it, not the re-seam. Refusal-only and
refusal-plus-re-seam produce the same fault list entity for entity, and the
re-seam alone changes lid10's volume in its last three digits. Two facets are
refused per part; what the fault detail then shows is the two sweep faces either
side of the notch at 13, a new faulty edge at (-77.1776, -11.5773, 64.9951) at
13, and the small B-spline of the r ~ 79 cluster turning from 17 to 30.

So **dropping the facet that lies across the seam leaves a notch the boundary
has to route around, and that is worse than the straddle it removes.** The
control reproduced `codes=17` in every one of these sessions, fourteen times
now, so the comparison is sound.

**What is left to try, in the order they are exact:**

1. **Split the facet at the seam** rather than dropping it: a triangle spanning
   the seam becomes two, one per side, and the face keeps its coverage with no
   notch and no straddle. The exporter already rewrites topology this way when it
   fans a bent polygon.
2. **Write the surface periodic in v**, which is what it is - the profile is
   closed - so that no seam exists in its parameter space at all and a face may
   wrap without any of this. The bigger change, and the one that would make the
   whole question disappear.

The re-seam itself - writing a face from the column after an unused span, so its
facets are contiguous in v - is implemented and harmless, and is kept in
`build/relax/` rather than committed, because on its own it fixes nothing: what
needs the seam moved is exactly the facet that straddles it.

#### What the reader builds there, measured 2026-09-16

Chasing the strays that appear only after a kernel has read the file back - the
ones the written text does not contain - ends here, and it ends with a number
that says what is wrong in one line. `scripts/step-diagnostics/seam-straddle.py`
prints the `v` each of a face's own pcurves runs between. For the sweep face of lid10,
`#1217` on the closed B-spline `#1045`:

```text
  edge #1054   LINE                       v 0.0000 0.0000
  edge #1070   LINE                       v 0.0000 0.0371
  edge #1086   B_SPLINE_CURVE_WITH_KNOTS  v 0.0371 0.0339
  edge #1103   B_SPLINE_CURVE_WITH_KNOTS  v 0.0339 0.0129
  edge #1118   LINE                       v 0.0129 0.0127
  edge #1133   LINE                       v 0.0127 0.0000
  edge #1148   LINE                       v 0.0000 0.9956     <- crosses the seam
  edge #1163   LINE                       v 0.9956 0.9866
  edge #1178   LINE                       v 0.9866 0.9775
  edge #1193   LINE                       v 0.9775 0.0585     <- crosses the seam
  edge #1206   LINE                       v 0.0585 0.0000
```

The loop crosses the seam **twice**, and each crossing is written as a pcurve
going the *long way*: a straight line in (u, v) from 0.0000 to 0.9956 sweeps the
entire profile instead of stepping the 0.0044 across the seam. Every control
point of every row of `#1045` has its first and last the same - the surface is
closed in v, measured, and its seam sits at (79.5875, 0, 0).

What OpenCASCADE then does with that is all of the rest:

- it **builds a seam edge we never wrote**, a LINE from (79.5875, 0, 0) to
  (79.5875, 0, 1.0125), and another at (79.2875, 0, 4.6250);
- it **inflates the vertex tolerance at (79.5875, 0, 0) to 1.08**, which is
  longer than the whole of our edge `#1206` there, so that everything can be
  made to meet;
- and our own `#1206` - a LINE whose two vertices are exactly its two ends, base
  (79.5173, 0, 1.0827), direction (0.0647, 0, -0.9979), length 1.0850 - then
  reads back as a vertex 0.7159 off its own curve. That number is the reader's
  repair, not our geometry. `scripts/step-diagnostics/vertex-on-curve.py` reports
  nothing for this file, and is right to.

**And it is what SOLIDWORKS has left.** Its remaining code 7 is on one edge, 594
at (79.3344, 0.9718, 2.6827), which is our ELLIPSE `#14816` from
(79.3525, 1.9435, 1.1863) to (79.3164, 0, 4.1792) - midpoint matched to 0.0001 -
with the two faults beside it on the cone and the plane that edge bounds. Both of
its vertices are on that ellipse **to 1e-11**, so the objection is not our
arithmetic; what the edge has instead is an endpoint at `y = 0`, on the seam
plane, in the neighbourhood the reader has just rebuilt. Everything SOLIDWORKS
still objects to on lid10 touches that seam.

So item 20's residue is this item, and the order of work is settled by it: the
two exact routes above - split at the seam, or write the surface periodic - are
no longer alternatives to weigh, they are the only thing left objecting. The
re-seam parked in `build/relax/` is a third and now looks better than when it was
shelved: the boundary uses `v` in 0.0000-0.0585 and 0.9775-1.0000 only, so
**0.0585 to 0.9775 is free** and a seam moved into that gap is crossed by
nothing. What was refuted on 2026-09-16 was re-seam *together with* the
across-seam facet refusal, which made SOLIDWORKS worse; re-seam on its own,
against a boundary that now has this much room, has not been tried.

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

---

### 17. Claim order is not a lever, and the contest is between duplicate records

**Asked and measured 2026-09-16.** The question was whether claiming in reverse
construction order - the last boolean's operands first - would sharpen
recognition. It would not, and the measurement says why.

Today's order is construction order: `mergeSurfaces` appends the left operand's
declarations and then the right's, and within a pass the first surface to accept
a facet takes it. Order can therefore only decide a facet **two** declared
surfaces both accept:

| export | facets offered to the quadric pass | contested | after provenance |
| --- | --- | --- | --- |
| `two-cylinders-cone` (below) | 116 | 26 | 26 |
| `lid10` | 2457 | 257 | 256 |
| `band-fn64` | 341 | 0 | 0 |

**Every contested group is two records of one surface.** On the model, surface 0
(r = 10 at z = 0, the first cylinder) against surface 3 (r = 10 at z = 20, the
cone primitive's bottom rim) - the same infinite cylinder declared twice. On
lid10, #9/#56 and #39/#43 the same way; #58/#66 a declared cone and a fitted
guess of it; #46/#67 a declared cone and a fitted guess **with the axis written
the other way round** (axis -z, slope +1, which is r = 82.4 at z = 75 again).

**So reversing changes nothing.** Behind a temporary switch, all three exported
both ways: identical face counts, identical volumes (lid10 227658.650755 either
way), identical tolerances. The files differ only in which record's *placement*
is written - reverse takes the fitted cone's origin at z = 79.4385 over the
declared z = 75 - and on the band family only in entity numbering.

It would also invert *declare first, recognise second*: fitted surfaces are
appended after declared ones, so last-first hands 120 of lid10's facets to a
guess instead of the declaration it duplicates.

**A declaration-side sharpening was tried and dropped.** `describesSameSurface`
already states the rule - same axis and radius, reference points differing along
the axis, one surface - and its comment names this very case, but only
`addSurfaceUnique` calls it; `containsSurface`, which the boolean path uses,
compares `refpt` exactly. Wiring it in takes the model from 5 declared surfaces
to 4 and lid10 from 25 to 22, and changes **nothing else**: same volumes, same
validity, same faces, and the 11 mm slide of item 18 unaffected. Nothing in the
suite fails without it, so it did not land. It is still the tidier rule, and the
cone case (same axis line, same half angle, radius agreeing) is not covered by
it at all.

**What the measurement leaves.** Which surface a facet lies on is never decided
by order on anything measured here. Where the recogniser is actually short is
provenance being silent - item 13 - and that is a question about ownership, not
about sequence.

#### Closed 2026-09-16: the duplicates are deliberate, and their consumers already collapse them

Two rules exist, and the difference between them is the answer:

- `Surface::sameAs`, with `describesSameSurface` beside it, deduplicates the
  *record list*, where being wrong loses a surface. It is conservative on
  purpose and keeps one infinite cylinder referred to from two heights as two
  entries - its own comment says so.
- `sameSurfaceGeometrically` in `export_step.cc` answers "how many *distinct
  surfaces* claim this vertex", and it already covers cones as well as
  cylinders - same axis line, radius compared at a shared point, slope in
  absolute value because reversing a cone's axis negates it. Nothing is dropped
  there, so it can afford to be liberal.

So the place where a duplicate would do harm - counting one surface twice as two
owners, which is what feeds the pair placement - collapses them already, and has
since `step-declare-cone`'s 32 junctions were found looking contested. That is
why applying the record rule on the boolean path changed nothing measurable, and
it is a reason not to apply it rather than an accident.

**And the duplicates cost nothing downstream.** Measured on lid10, which carries
588 quadric records, 24 of them declared and the rest guesses the approximation
pass added:

| duplicate pair | claims |
| --- | --- |
| declared cylinder r=80.7 x2 | 0 / 0 |
| declared cylinder r=78.3073 x2 | 76 / 76 - two separate regions of one infinite cylinder, not a contest |
| declared cylinder r=82.4 x2 | 0 / 0 |
| declared cone == **fitted** cone (x2 pairs) | 60 / **0** |

No fitted quadric claims a facet the declared record would have had. Item 17 is
closed: there is no sharpening here, by ordering or by deduplication.

**What it did turn up** is one thread, filed as item 19: the approximation fitted
two cones that the model had already declared.

### 18. A two-owner corner slides 11 mm along the curve its owners cross in

**Found 2026-09-16, while measuring item 17, and not yet chased.** Nine lines,
three primitives, and the approximation tier writes a broken shell:

```scad
$fn = 64;
intersection() {
  union() {
    cylinder(r = 10, h = 20);
    translate([0, 0, 20]) cylinder(r1 = 10, r2 = 4, h = 8);
  }
  translate([6, 0, -1]) cylinder(r = 10, h = 40);
}
```

| tier | result |
| --- | --- |
| faceted | valid, 1 solid, volume 4881.063273 - the mesh's own 4881.06 |
| `step-analytic-surfaces` | valid, 1 solid, 70 faces, volume 4885.633285 |
| both flags | **invalid**: 25 edges used by one face, OCCT reads 0 solids, 3 shells |

The export says `114 corners moved, by at most 11.0267` on a part of radius 10,
every one of them "where two declared owners cross". The travel test passed them
because its bound there is the mesh's own edges, and this mesh has 20-unit
vertical ones - which is the bound section 4 of the development document already
warns is not a bound.

`projectOntoBoth` steps only in the span of the two gradients, so it cannot
slide along a crossing curve in one step; two equations in three unknowns leave
the position along that curve unpinned, and the acceptance test is all that
stands between a junction vertex and a point on the far side of the part.

Refuted already: that the duplicate records of item 17 cause it. After deduping,
each original owns exactly one surface, provenance names two owners for 115 of
117 junction vertices as before, and the same 114 corners still move 11.0267.

Two more things this model is good for. Its exact tier is clean, so the defect
is the corner placement and nothing earlier. And in a variant where the veto
fires - the cone declared at `r1 = 9.9`, so no rim duplicates the wall - the
same model exports valid, the veto again protecting the file by accident, as in
item 11.

It is a fixture candidate the moment it is fixed, and a much smaller one than
lid10; see item 13 for why a model that still fails for another reason is kept
out of the suite until then.

#### Chased 2026-09-16, and the slide is closed at the root

**Which corners, and why.** Traced over every accepted two-owner move on that
model:

| owner pair | n | start off owner A | start off owner B | worst travel |
| --- | --- | --- | --- | --- |
| wall x cone | 25 | 0.0000 | 0.0000 | 0.0000 |
| cone x wall | 63 | 0.0086 | 0.0120 | 0.0192 |
| **wall x wall** | 26 | **up to 5.9285** | 0.0000 | **11.0267** |

0.0120 is the 64-gon's own sagitta, 10(1 - cos(pi/64)) - what the mesh concedes
about a junction vertex. The 26 are 5.93 off an owner they were never on: they
sit where the second cylinder's wall meets the first cylinder's *bottom plane*,
and the solver puts them on the line those two walls cross in, x = 3,
y = +/-9.5394. Newton is not at fault and neither is the bound: M1 (drift along
the curve), M2 (a distant branch) and M3 (the far line of the pair) are all
refuted - the crossing it finds is the true one, and the corner simply does not
belong on it.

**The premise is what fails.** Provenance is per original solid: a cylinder's
original owns its wall, and its *cap* facets belong to that same original
without being on it. The header of `Provenance` says as much - "this says which
surfaces a facet could be on" - and the pair placement reads "two declared
owners" as "the vertex lies on both".

**Fixed by asking the declaration**, with the rule section 4 already uses to
tell a chord facet from a cutting plane: a facet lying on a surface has its
normal parallel to the surface's own there, a cap cuts across and is
perpendicular, and a tessellation no coarser than a hexagon puts a chord facet's
normal within pi/6. A surface is an owner *of this corner* only where one of the
originals that named it has a facet here passing that test.

| the nine-line model | before | after |
| --- | --- | --- |
| corners moved | 114, worst **11.0267** | 89, worst **0.0192** |
| refused | - | 25, each 5.40 to 5.93 off the owner refused |
| OCCT shells | 3 | 1 |

**Two scopings were measured and both were needed.** Asking "any facet at this
corner" instead of "a facet of the original that named the surface" passes the
wall facet of the solid across, because two cylinders 6 apart run nearly
parallel where they cross: it refused 15 corners and left the 11.0267 standing.
And the test has to gate the *crossing placement only*, and only where both
owners are exact quadrics: where one is a fit, `build_tri_body` reroutes the
corner onto a plane a declaration vouches for, which is exactly what a
cap-meets-wall corner needs, and vetoing earlier took that reroute away -
`step-declare-grid-topcap` failed on its own APPROX line, `2 corners are placed
on a plane a declaration vouches for crossed with their exact owner`.

Every flagship coupon reads back identically after the fix - lid10 227658.650755
and bayonet 238846.977158, tolerances 0.124888, same face counts - because their
veto holds those moves anyway; 300 corners per part are refused the crossing and
none of them was going to move.

#### The rim is closed too, and the model is a fixture

The 25 edges used by one face were the second defect, and the same shape as the
first: at z = 28 the cone wrote its top rim as one `CIRCLE` while the cap across
it, bent by the corner moves, was fanned into triangles and still spelled that
rim as twenty-four chords. A fan writes one straight line per triangle side and
never reads `arc_subs`, so the handover was impossible rather than missed - root
cause A for *arcs*, where item 11 closed it for plane sections by binding on
`split_for_corners`. The rims never had that guard.

They have it now, and what happens next is better than chords on both sides: the
rim's edges go back to being ordinary shared edges, and the per-edge promotion
then writes each of them as the arc it is. 62 arcs become 86, the shell closes,
and the model reads back as one solid of 4890.790271 against a smooth volume
derived at 4891.296892.

**It is a fixture now, `step-cap-meets-wall`, registered `WILL_FAIL`.** Both
defects it was built for are fixed and it still fails its round trip, on a third
that is not: two corners sit 3.6254e-03 and 4.5317e-03 off the analytic faces
they bound, against the 3.4814e-07 a kernel allows. That is the slack of items 13
and 15, and marking the fixture rather than keeping it out of the suite - which
is what item 13's model still waits for - means the day that slack is fixed,
ctest reports *this* as the failure. Its two volume figures are derived twice
over: the smooth solid from the two-circle lens integral, and the mesh from the
same integral over OpenSCAD's own inscribed 64-gons, which agrees with what the
mesher reports to six decimals.

### 19. The approximation re-fits a cone the model declared, where a rim rule refused it

**Filed 2026-09-16, out of item 17, and not chased.** On lid10 the approximation
pass reports *took 18 of 35 uncovered regions - 0 as cylinders, 2 as cones, 16 as
rings of a turned surface*, and the two cones it fitted are the same surfaces as
declared cones already in the list: #67 is declared #46 with its axis written the
other way round, and #66 is declared #58 measured from another height. Both
fitted records claim nothing in the trimmed-quadric pass; the declared ones claim
60 facets each.

So a region lying on a cone the model declared was left uncovered, and the
approximation recovered the geometry by *fitting* it. The band pass says why it
gave up, in its own words, on the same radii:

```text
r=78.3073 band of 60 facets left faceted: the rim borders one face per facet
r=78.3073 band of 4 facets left faceted: a shared rim needs both bands to be the same shape
```

That the refused bands and the fitted cones are the same regions is consistent
with every number here but was not measured directly; that is the first thing to
do with this item.

It matters for the exact tier, which has no approximation to fall back on:
section 2's *a declaration that is not taken is a bug report*, and section 1's
"the third gate is where the work is" - eleven exact cylinder fits on the bayonet
lid, every one declared, every one refused by the rim rules. lid10 exports 16
cones with both flags and 9 with the analytic flag alone.

Item 10 surveys analytics that stop short and is the right home for the fix; this
entry records the one case where the shortfall is provable, because the surface
the approximation fitted is one the model had already stated.

#### Measured 2026-09-16: the refused bands and the fitted cones are the same regions

Facet for facet, and not merely the same radii:

| the approximation's cone fit | the band it was refused as | the rule |
| --- | --- | --- |
| 60 facets | r = 75.1, 60 facets | the band sharing this rim was dropped |
| 60 facets | r = 78.3073, 60 facets | the rim borders one face per facet |

All 120 facets the approximation fitted as cones are facets a band pass refused,
and the two sets coincide exactly. The chain of item 19 is therefore no longer
an inference.

**And the fit gains nothing.** Both fitted records duplicate a declared cone, and
both claim **zero** facets: the 120 are claimed by the *declared* records in the
trimmed-quadric pass, which spends the tessellation band under the approximation
flag and so takes what the band pass would not. That is also why lid10 writes 16
cones with both flags and 9 with the analytic flag alone - the declarations are
there either way, and only the looser tier can use them here.

So the `2 as cones` in the approximation's report counted work nobody used. It
now says so:

```text
EXPORT-WARNING: 2 regions are fitted to a surface the model had already declared,
so the fit gained nothing - a declaration was refused upstream and re-derived here
```

`describesSameSurface` covers cones for this, by the rule `sameSurfaceGeometrically`
already used next door - the same axis line, the slope in absolute value, the
radius compared at a shared point - so the record list and the owner count answer
that question the same way. It is item 17's leftover, spent here.

**And a fit the band pass never sees.** The re-run after fitting was conditional
on `fitted + turned + recovered`, leaving `coned` out: a model whose approximation
fits only cones added the record and never re-ran, so the fit could not become a
face at all. Latent on lid10, which fits sixteen rings as well; fixed with the
rest.

#### Exactness takes precedence, and it is now held rather than assumed

The standing rule: a surface the model declared beats one the approximation
fitted, in every combination, and a facet claimed under the exact tier is never
re-offered under a looser one. It is what a commercial kernel refuses - a fit
spends the tessellation band, a declaration states where the surface is.

The passes walk one list and the first surface to accept a facet takes it, so the
order *is* the precedence: the declarations are its head and every fit is
appended after them. Two `EXPORT-ERROR` guards hold it, both measured unreachable
before being made errors and each mutation-checked:

| guard | mutation | what it reports |
| --- | --- | --- |
| the declarations are still the head of the list | a fit inserted at the front | 66 of the model's own surfaces are no longer the first entries |
| the re-run costs the declarations nothing | every facet marked as claimed beforehand | 1943 facets they had already recognised |
| ... and moves nothing onto a fit | every band marked as fitted | 536 moved onto a fitted surface |

`Band::on_declared` is what the second reads, set where the recogniser already
knows it: a band is only accepted when a record describes it, and the caller says
how many of the records are the model's own. On lid10 536 facets and on the
bayonet part 628 go into the re-run claimed by a declaration and every one comes
out still claimed by one.

### 20. A substituted curve must pass through its own run's endpoints - landed

Read the fault codes as sentences rather than as names - the table is in
`doc/step-export-development.md` - and the two we actually get name two different
pairs of entities:

- **7** `swEdgeVertexNotLie`, "Vertex does not lie on edge's curve".
- **17** `swFaceBadEdge`, "Edge does not lie on face's surface".

Neither is what the corner-stray family measures. Those hold a corner against the
surface its face is written on, which is **16**, "Vertex does not lie on face's
surface" - and 16 has never fired on a file of ours. Every instrument built
before this week measured a relation SOLIDWORKS does not complain about.

**What was found.** `scripts/step-diagnostics/vertex-on-curve.py`, which reads
the STEP text with no kernel in the path, on the committed build's lid10:

```text
14 vertex/edge pairs off the curve
  1.619590  ELLIPSE  at (79.1848 3.3298 2.0324)
  1.141268  ELLIPSE  at (-75.1430 -20.9042 72.9263)
  ...        (14 ELLIPSE edges, 0.13 to 1.62)
```

Seventeen ELLIPSE edges in the file, fourteen of them missing their own
endpoints. The worst is the draft cone, half angle about 1.15deg, cut by a plane
whose normal makes `|n.axis| = 0.0645` with the axis. The section is a legitimate
ellipse - `planeSectionEllipse`'s guard is `|n.axis| > sin a ~ 0.020` and it
passes correctly - but it is **940.3176 x 57.6904**, a grazing cut, and there
`a ~ b / |n.axis|` amplifies along the major axis: a vertex a sagitta off the
cone is a millimetre and a half off the curve.

**Root cause, two deep.** `findSections` admitted an edge to a plane on one test
only - that both endpoints lie *in* the plane. A section's curve is the surface
met by the plane, so that is half of lying on the curve and the other half was
never asked. But the endpoints were off the cone because *nothing had put them on
it*: the corner placement's `faces_at` collected quadric, grid and Bezier patches
and bands, and **not the planar faces**, so a corner where a plane meets a cone -
which is every corner of a plane section - had one analytic surface at it and
never reached the crossing rung at all.

**What landed.**

1. **Planar faces are surfaces of the placement.** A face written as a PLANE
   asserts its corners lie in that plane exactly as a cone asserts its corners
   lie on the cone, so a corner between them goes on the conic where the two
   cross, and a corner between a quadric and *two* planes goes where all three
   meet (`projectOntoSurfaceAndPlanes`, the same solver the provenance rung
   uses). The planes come from `section_planes` - the plane of each face across
   the surface's boundary, with a **declared** plane preferred to the mesh's own
   - and not from the raw facets. That channel also answers, without a
   heuristic, which planes at a corner are the quadric's own chords: a chord is
   not a face across the boundary, so it is not in the list. The angular form of
   that question, `|n.nsurf| >= cos(pi/6)`, which the conic rung next door uses,
   reads a plane grazing a shallow cone as a chord - it misread *both* planes at
   (79.1848, 3.3298, 2.0324) and is why that corner sat 0.1045 inside its own
   cone.
2. **The ladder is ordered declared-first.** The new rung runs after the
   provenance conic and triple-point rungs, not before. Measured on
   step-exact-trim, where 32 corners are reachable both ways: run first, the new
   rung claimed all 32 and the declared rung reported none - which the fixture
   caught, and which is the wrong way round.
3. **`PlaneSurface` has an implicit.** `surfaceImplicit` had none, so no Newton
   could cross anything with a plane. It is exact and its residual *is* the
   distance. It is kept out of `quadricImplicit` because `isQuadric` decides
   other things.
4. **The invariant itself, as a backstop.** `findSections` measures every
   boundary vertex against the surface with the crossing pass's own tolerance and
   shortens a run whose end is off it, dropping it if fewer than two vertices
   remain. It binds at the *writer* and not where sections are decided: the
   decision runs before the placement, on vertices the mesh still holds a sagitta
   inside their surface, and binding it there dropped every section on lid10 for
   a staleness the placement then removed.
5. **The exporter audits itself.** Every `EDGE_CURVE` in the finished entity list
   is measured against its own two vertices before the file is written, for
   LINE, CIRCLE, ELLIPSE and B_SPLINE_CURVE alike, and says so as an
   EXPORT-WARNING. No substitution added later can escape it.
6. **The suite would fail without the fix.** `export-step-flagship-coupons` now
   asserts zero offenders over all six coupons, read from the text. It needs no
   derived expectation - zero is what any correct export has.

**Measured after.** lid10 written by this build: **0** vertex/edge pairs off
their curve, against 14 before, and 10 ELLIPSE edges still written rather than
given up. `step-band-family` at `$fn` 32 is 0 both before and after. The 54-test
suite passes.

**And what SOLIDWORKS ships itself, which is the calibration this whole chase was
missing.** Asked to repair `f02-band-fn032-analytic` in the Import Diagnostics
dialog, it can - and the file it saves has **a vertex 0.1015 off its own edge's
curve**: edge `#3394`, a clamped cubic (degree 3, four control points,
multiplicities 4/4, so it starts at its first control point by construction),
whose start vertex is at (-7.6537, 18.4776, 38.4343) while the curve starts at
(-7.6919, 18.5704, 38.4197). Verified by hand against the control points. The
0.000916 sliver at the thread run-in survives its repair untouched as well.

So code 7 is not a statement about exactness at the tier we hold ourselves to:
SOLIDWORKS tolerates a tenth of a millimetre of it in its own output. Our
1.6196 ellipses were worth fixing on their own merits and by our own measure -
but a 0.05 to 0.11 stray is inside what SOLIDWORKS itself ships, so it is not
what SOLIDWORKS is objecting to either, and the remaining faults on lid10 want a
different explanation.

**SOLIDWORKS, same session, same import settings, with the 2026-09-08 control.**

| file | verdict |
| --- | --- |
| `f02-band-fn032-analytic-control20260908` | `faults=2 faultyfaces=2 codes=17` - the control, reproduced |
| `f02-band-fn032-analytic` today | `faults=0` |
| `r01-lid10-analytic` before | `faults=1 faultyfaces=7 faultyedges=1 codes=7/17/21` |
| `r01-lid10-analytic` after | `faults=1 faultyfaces=7 faultyedges=1 codes=7/17/21` |

**So SOLIDWORKS's verdict on lid10 did not move.** The same seven faces and the
same edge, by index and by kind; only their centres shift by a millimetre or two.
That is worth stating plainly rather than filing the fix as a win: the 1.6 mm
ellipses were real, are gone from the file, and were not what SOLIDWORKS was
objecting to.

**What that leaves, and where to look next.** Read back through OpenCASCADE the
*new* lid10 still shows strays - 0.7159 on a LINE at (79.5875, 0, 0), 0.0200 on
two LINEs, 0.0137 on a CIRCLE - none of which exist in the file we wrote. The
reader made them, by splitting faces and rebuilding edges of its own, which is
the standing warning about kernel agreement in its sharpest form: OpenCASCADE
reported strays the file does not contain *and* said nothing about the fourteen
it did. That was chased the same day and the answer is in item 15: the sweep
face's loop crosses its own surface's seam twice, each crossing written the long
way round, and the reader repairs that by inventing a seam edge and inflating a
vertex's tolerance to 1.08. Every remaining SOLIDWORKS fault on lid10 touches it,
including the one code 7 left, which is on an ellipse whose vertices are on it to
1e-11.

### 21. One corner is left off its cone by the ladder's own travel bound

lid10 and the bayonet share a feature, and in both the last corner off a surface
its face is written on is the same vertex: (-77.1194, -11.6539, 65.0000), 0.1050
off the cone, bounded by two faces - the sweep `#12103` and the cone `#14571` -
and by two edges, both LINEs on the sweep's pcurve.

Two analytic faces at a corner is the case the general placement exists for, and
it computes the point. What refuses it is the bound the ladder holds every path
to: a corner may travel no further than its nearest neighbouring vertex, which
here is 0.22. The exact crossing of the sweep and the cone is further away than
that.

The bound is not wrong - it is what stops a corner folding the facet beside it,
and the trap it was written for (a 1.9976 move on step-partial-cylinder licensed
by the *longest* edge at the corner) is real. So the question is not whether to
loosen it but whether a corner whose target is on *both* its surfaces to 1e-9 has
earned an exception, and what the right bound for that case is - the distance to
the nearest neighbour is a bound on how far a corner may drag its facet, not on
how far a correct answer may lie.

Measured: 2 such corners on lid10 and the bayonet (one of them this one, the
other its mirror), 3 on the band family at `$fn` 64 at 0.0016 - so the size of
what is refused varies by three orders of magnitude between coupons, and the
`$fn` 64 ones are inside any tolerance that matters.

### 22. What SOLIDWORKS repairs is a curve off its surface by 1e-4, and ours are off by 0.1

`f04-band-fn064-analytic` reads `valid=1 faults=0 faultyfaces=0` from every API
check, and the Import Diagnostics dialog shows one faulty face: "General geometry
problem". On 2026-09-17 that face was repaired by hand and the result saved
beside the unrepaired import, both with knitting off, so the two differ by the
repair alone and by nothing else - same writer, same settings, same session.

**What the repair does, measured** (`build/item20/sw-diff.py`,
`curve-move.py`):

- every face matches by surface *and* by its whole set of corner coordinates, so
  **no vertex moved and no surface changed**;
- the entity census is identical - 69 faces, 1273 edges, 1206 vertices, one
  closed shell - so **no topology changed**;
- 215 B-spline curves were replaced, and each new one differs from the one it
  replaced by a median of **4.281e-07** and at most **9.583e-05** on an interior
  control point, with **both endpoints unmoved to all 9 digits**.

So the "general geometry problem" is a curve lying a ten-thousandth of a
millimetre off the surface it bounds, and the repair is a re-fit that puts it
back. Nothing structural is wrong with the face at all.

**And that is the relation we are worst at.** `scripts/step-diagnostics/edge-on-surface.py`
over our own exports, counting only edges off by more than 1e-4:

| file | line-on-bspline | line-on-cone | line-on-cylinder | line-on-plane |
| --- | --- | --- | --- | --- |
| lid10 | 21, worst **0.1089** | 73, worst 0.1068 | 4, worst 0.1070 | 1, 0.0015 |
| f02 | 10, worst **0.1015** | - | 9, worst 0.0456 | 0 |
| f04 | 25, worst 0.0304 | - | 29, worst 0.0114 | 0 |

Every *B-spline* edge we write is exactly on both its surfaces - `bspl-on-cyl`
and `bspl-on-bspl` are 0.000000 over 1223 edges on f04. What is off is straight
LINEs on curved surfaces, by up to **0.109**, which is a thousand times what
SOLIDWORKS thought worth repairing on f04 and is word for word what code 17
says: "edge does not lie on face's surface".

It also explains the oddity recorded in item 20 - that SOLIDWORKS ships a vertex
0.1015 off its own edge's curve in the file it saves after repairing `f02`. That
is not SOLIDWORKS being sloppy: **0.101451 is our own `line-on-bspl` worst for
that file**, re-expressed. It read our chord, wrote its own spline for it, and
carried our error across.

#### Half of it is done: the conic a plane cuts, whichever conic it is

**Landed 2026-09-17.** The arc promotion asks for a *circle* - both ends at one
height on the axis and at the surface's radius there - which is the section a
plane perpendicular to the axis cuts, and the only one it could reach. Every
other plane cuts a conic that is not a circle, and the chord across it was left
straight.

`planeSectionConic` answers for all three. The section is found algebraically
rather than geometrically, because that is the form that classifies itself: a
cone about `a` through apex `A` with half angle `alpha` is the quadric
`(X-A)' M (X-A) = 0` for `M = a a' - cos^2(alpha) I`, and substituting
`X = P + u E1 + v E2` leaves a conic in `(u, v)` whose determinant names it, whose
centre solves its linear part and whose eigenvectors are its axes.

And the answer is mostly **hyperbolas**. lid10's draft cone has a half angle of
about 1.15 degrees, so almost any plane down the wall is steeper than that and
cuts a hyperbola rather than an ellipse - 64 of its 68 promoted chords, against 4
ellipses. ISO 10303-42 has `HYPERBOLA` and `PARABOLA` entities, so there is no
reason to approximate either; both are written now, and OpenCASCADE reads the
file back with `Hyperbola 64` in its own edge census and one solid.

A hyperbola entity describes **one branch** - `a cosh(t)` is never less than `a` -
so the placement's reference direction has to point at the branch the edge is on.
The two branches are mirror images and the solver picks one arbitrarily; turning
it round rather than refusing is the difference between 37 promotions and 68.

Two traps, both caught by checks that already existed:

- **The edge keeps its vertices.** The first version swapped them where the curve
  ran the other way, which is what the arc promotion does not do - and every
  `ORIENTED_EDGE` in every loop using that edge was written against the old
  order, so the loops stopped closing. `validatestep.py` said so outright
  ("EDGE_LOOP #38502 does not close", two zero-area bounds, a PLANE normal
  against its winding) and the suite failed three tests. What turns instead is
  the placement's **axis**: a conic lies in its plane either way round and
  reversing the normal reverses the direction its parameter runs, which is the
  same trick `from x to` plays for a circle.
- **The audit had to learn the new curves first.** `vertexOffCurve` and
  `vertex-on-curve.py` both returned 0 for a kind they did not know, so writing
  64 hyperbolas would have made "0 vertices off their curve" vacuous for exactly
  the edges that had just changed. Both measure `C + a cosh(t) X + b sinh(t) Y`
  and the parabola now, by sampling and refining.

Measured on lid10, edges off a surface they bound by more than 1e-4:

| | before | after |
| --- | --- | --- |
| line-on-cone | 71, worst 0.1068 | **7**, worst 0.1068 |
| line-on-cylinder | 4 | 2 |
| line-on-bspline | 21, worst 0.1089 | 21, worst 0.1089 |
| distinct offending edges | 91 | **23** |

The bayonet promotes 8 the same way. `f02` promotes none, because none of its
strays is against a plane.

**What is left is the sweep**, and it is a different problem rather than more of
the same. An edge touching a sweep face carries a PCURVE, so its geometry is a
`SURFACE_CURVE` wrapping the line rather than the line itself, and the promotion
skips it - rightly, because replacing the 3D curve while leaving a pcurve that is
still a straight line in (u, v) would make the two disagree, which is what codes
11 and 12 are for. The crossing curve `intersectionArc` already computes is the
right geometry; what it needs is the pcurve to match, and to be decided at the
writer where the corners are final rather than at recognition time where they are
not.

#### The sweep half, 2026-09-18: parked, and a declaration problem rather than a curve problem

**Built, then parked** on `claude/step-sweep-chord-parked` (`1de808ff9`), because
the suite fails on it. A chord on a sweep face whose ends are on both surfaces is
replaced by the curve the sweep crosses its neighbour along, and its pcurve is
rebuilt and checked against that curve before either is written - replacing one
without the other is what codes 11 and 12 are for.

**Why it is parked, which is a gap in the gate and not a malformed file.** It
writes a combination this exporter never wrote before: a 3D B-spline edge with a
**2D B-spline PCURVE on a B-spline surface**. That is legal STEP, but
`tests/validatestep.py`'s pcurve rule maps the 2D curve through the surface's
*analytic* parametrisation, so it demands a placement and reports "PCURVE's
surface has no usable placement" on exactly the promoted edges - failing every
declared-grid fixture and the flagship. The fix is to teach the validator to
evaluate a tensor-product B-spline surface, so the new case is *checked* before
it lands. Relaxing the rule around it would be fixing the gate.

**Why it would not help much yet anyway.** On lid10 it promotes 1 of 27 sweep
chords. Two things stood in the way:

- **The gate measured against the band.** A grid's declared band is its
  tessellation's sagitta, so by that standard a chord 0.1 off the written sweep
  is on it; 19 of 27 were judged flat. Measuring against the written surface
  (kept on the parked branch) brought 21 to the fit.
- **Then 14 failed to fit, and every one meets an undeclared plane.** The error
  was all on the sweep side. A probe on one sample each settled it: the
  projection *succeeds* to 1e-14 but **moves 4.1 to 6.3 mm on an 8 mm chord**,
  because the two surfaces meet at **3.7 to 20 degrees** where the cones that fit
  meet at 36 to 41 - and **every one of those planes is undeclared**, a facet the
  boolean left, fitted to its own corners. A declared sweep crossed with an
  approximation of something else has no crossing curve worth writing.

Tracing a plane against the sweep in the sweep's own parameters (as a cone
already is) was tried as a fix for the second point and recovered none of the
14, as the angles predict; it is inert on the parked branch. The next question
is what those facets are in the model and why no declaration claimed them.

Of the rest: 6 have an end off the sweep itself - a code 16 on a B-spline
surface, which the census cannot see because its code-16 row measures analytic
surfaces only (`point-vs-faces.py` can) - and 3 straddle a crease of the
profile, where the crossing has a kink and wants a C0 knot rather than one
Bezier.

**The rest of the original plan**, for the record: the chord-vs-arc fix exists for
one case and does not cover these. `doc/step-export-development.md` records it for cylinders:
a ruling lies on a cylinder by construction, so the segments up and down the axis
need nothing, and the ones running *around* it are chords of a circle and are
promoted to arcs. What is left is lines on a **cone**, lines on a **sweep**, and
the cylinder chords that promotion does not reach. The sweep case is the one that
matters most - it is the largest number on both reference parts - and it is the
same question the crossing curve answered for edges between two declared
surfaces: an edge on one surface only still has to lie *on* it.

#### The undeclared planes are the thread's own facets, 2026-09-18

**Answered, and the fix it suggests is parked** on `claude/step-grid-junction-parked`
(`954a2c7d3`) because the suite fails five tests on it - for three reasons worth
more than the fix.

**What the planes are.** `scripts/step-diagnostics/planes-at-sweep.py` over lid10:
13 planes border the sweep, all at the thread's two ends - z 0.5 to 4.5 between
0 and 15 degrees, z 70 to 72 between 164 and 195 degrees, the ridge stopping at
918 degrees = 198. Each has every corner but at most one on the sweep to 1e-6,
the odd one within 0.18, and meets it at 1.4 to 20 degrees. They are not a facet the boolean left of something else:
an instrumented build matched all 13 to facets the grid claim **refused**, and
every one of those is a facet of the ridge's own flank. The sagitta that makes
them look like a second surface is the tessellation's, 79 (1 - cos 3) = 0.109,
which is the census worst of 0.1089.

**Why the sweep did not take them - five whys, with the branches:**

1. *Planes of an undeclared original, a hull or a polyhedron.* **Refuted**: the
   instrumented run maps each to a refused facet of original 134, the ridge.
2. *Refused by corner membership or by the centroid test.* **Refused by
   neither**: the log's "24 facets of the sweep are left faceted: a corner of each
   is further off the fit than four times the 0.0229 this claim is typically off,
   by up to 0.2184" is the outlier rule in `recogniseGridPatches`, and every one
   of the 13 planes sits on one of those 24. Whether any of the 93 centroid
   misses also ended up in them was not checked.
3. *The fit failing at the run-in and run-out, "where a cubic has least
   support"* - the code's own comment. **Refuted**: every corner the generator
   declared at the ends is on the fit to 0.0000; only corners the boolean made
   are off it. Each of the 25 corners more than 0.05 off the fit is nearer
   another declared surface than the sweep - 8 on a plane to 0.0000, the rest on
   a cylinder or cone within 0.083, inside its sagitta. They are where the thread
   dives into the wall: the mouth chamfer, the chamfer's top step at z = 1.2
   against the taper (r 79.4 and 79.376), the taper-to-bore crease at z = 65,
   r = 78.1, and the bore's facets near z = 73. On two tessellations, on neither
   surface. No corner nearer the sweep than any wall is further off than 0.0422,
   so the split has no overlap.
4. *The middle is clean because the thread's stations land on the wall's 6
   degree vertices* (153 steps over 918 degrees is 6.000). **Refuted**: 9 of 278
   middle corners are on a station. Why the middle stays at a median of 0.019
   while the ends reach 0.2 is **not explained** - the grazing entry is the
   obvious candidate and is unmeasured.

**The fix that was tried first, and why it is abandoned.** Excluding a corner
that is nearer another declaration from the stray measure claims the 24, and 13
planes bordering the sweep become 2. It is still a *threshold* standing in for a
computation - it decides by comparing distances what the declarations determine
outright - and the standing rule is that a magic number or an approximation band
is the last resort and needs explicit approval. Recorded here because its
measurement is what found the three defects below, not as the way forward. The
census gets *worse*:

| lid10 | before | parked |
| --- | --- | --- |
| code 17, count and worst | 62, 0.1089 | 67, **0.2615** |
| faces crossing their seam | 0 | **2** |
| sweep chords with an end off a surface | 8 | 14 |
| edges used by more than two faces | 0 | **60** |

and `validatestep.py` rejects lid10 and the bayonet outright. The refusal was
masking three defects, each its own item:

- **A fan across the sweep.** "Faces that keep a plane fanned into triangles"
  adds 196 faces where it added 23, and one of them, `#41289`, is a 16.6 mm
  triangle with all three corners on the taper cone, overlapping the sweep along
  an edge the sweep and another plane also use. That is the 60.
- **The claim wraps.** At the run-in the chamfer exposes the ridge's buried back
  span, so the claim covers all four spans, the re-seam no longer applies and
  both seam crossings come back.
- **Three-surface corners no placement reaches.** The worst, (-74.278, -24.134,
  65.000), is the thread meeting the taper-to-bore crease; the z = 1.2 ones meet
  the chamfer's step. Each is a junction of the sweep and two walls, left 0.13 to
  0.22 off the sweep. *Prefer the declaration*: the crease at z = 65 is the
  bore cylinder's declared cap plane, and the step at z = 1.2 the chamfer's, so
  each is sweep, plane and wall - well conditioned, where cone against cylinder
  at 1.15 degrees is not.

Order to take them: the three-surface corners first, since they are what the
refusal existed for; then the fan, then the wrap. The parked change is the
measurement that says when they are done - it should turn the table above
green without anything else changing.

#### Where a corner belongs is a closed form, and two rules were standing in for it

**Measured 2026-09-18, after the above.** A corner is determined by the surfaces
the file writes faces of it on: three of them meet in a *point*. The ladder had
no solver for that case and two rules deciding by distance instead.

**First: what lid10 actually leaves off.** Measured after the placement has run,
against each surface *as written* rather than against the band it publishes:
**4 corners** of lid10 are off a surface a face of them asserts - the pair of
item 21 (0.1051, 0.1035, cone and sweep) and two at the taper-to-bore crease
(0.0367, 0.0215, cone, cylinder and sweep). Not 300: that figure is before the
placement. The audit could not see the last two at all, because `off_its_faces`
excused a corner by `declaredBand`, which for a grid is its tessellation's
sagitta - 0.2527 on this thread. **Measuring against the band is what kept the
rung that could have placed them from ever being asked.** It now measures
against the surface.

**`projectOntoThree`, and that the point is real.** Newton on the three
surfaces' own implicits. On lid10's two crease corners it converges to residuals
of 0, 0 and 3.0e-14 - the cylinder and the cone exactly, the sweep to rounding -
and the point is *interior* to the sweep patch (u 0.9923, v 0.0938 against the
mesh vertex's u 0.9934, v 0.0904), so it is a genuine point of the surface and
not the patch edge a clamped projection would report as on it.

**The travel bound is direction-blind, and that is measurable.** A corner may
travel no further than its nearest neighbouring vertex. For the crease corner
that neighbour is 0.2356 away **perpendicular to the move**, while the first
vertex *along* the move is 4.64 away - the bound refuses 1.338 mm of travel that
has 4.6 mm of room. The move is a sixth of one station of the sweep; it is long
in millimetres only because the thread crosses the crease at **1.6 degrees**
(0.0367 off / 1.338 along = sin 1.57), the helix running at 2.9 degrees to a
horizontal crease. The bound is a proxy for "do not fold the facet beside it",
so ask that instead: for every facet at the corner, the corner stays on the
inner side of each edge it is not an end of, and the facet keeps its normal.
Exact, directional, and states no tolerance.

| lid10, census | scalar bound | fold test |
| --- | --- | --- |
| 16, vertex off its face's surface | 1, 0.1051 | **0** |
| 13, short edge at a strayed vertex | 1 | **0** |
| 17, edge off its face's surface | 62 | **54** |

Item 21's corner is placed by it, moving the 0.5503 that item records as
refused, and code 13 goes with it - which is the second-order rule working
forwards for once. The suite stays at 54 of 54.

**The first fold test was wrong, and the band family said so.** As first
written it moved 7 band-family corners and cost **code 17: 38 -> 46**. Tracing
each corner's fate under both bounds: 4 corners the scalar bound refused (and a
later rung then projected onto the cylinder *alone*) were now placed at the
exact crossing - better - while 3 the scalar bound had placed were now refused.
Every refusal, on both parts, was on a **consumed** facet: one an analytic face
absorbed, which is not in the file at all. That includes facet 1591 at lid10's
crease, so the reading recorded here the day before - that the triple point
"lies outside the corner's own star" and needs curve-level work - was wrong.
Where everything round a corner is a surface, nothing folds.

**What the file can actually get wrong** is narrower, and the test now asks
exactly that: an edge at the corner turning round (the corner passing the vertex
at its other end, wherever that has been placed), and a facet *written as
itself* folding or being bent out of its plane.

**And a written plane is a constraint, not an obstacle.** The narrowed test
then failed three declared-grid fixtures with EXPORT-ERROR "planar faces assert
a plane one of their own bounds is not in": corners of two analytic faces that
also belong to a face written as a plane, placed on the curve the two surfaces
share and off the plane. That is three constraints and one point - the plane
goes into the solve (`projectOntoThree` with it as the third surface), and two
written planes at such a corner over-determine it and leave it.

**Measured, 2026-09-19, the old binary against the new, like for like:**

| part | code 16 | code 13 | code 17 (count, worst) |
| --- | --- | --- | --- |
| lid10 | 1 -> **0** | 1 -> **0** | 62 -> **42** |
| bayonet | 1 -> **0** | 1 -> **0** | 62 -> **42** |
| f02, band family `$fn` 32 | 0 | 0 | 38 -> **8**, worst 0.1015 -> **0.0348** |
| f04, band family `$fn` 64 | 0 | 0 | 62 -> **38** |

No EXPORT-ERROR on any of them, the exporter's own audit reports **no** corner
left off a surface on lid10 or the band family, the three-surface rung places
both crease corners, and the suite is 54 of 54.

`export-step-flagship-corners`, registered WILL_FAIL because it failed on all
six coupons, now **passes on the four band-family coupons** and fails only on
lid10 and the bayonet: a cone corner 0.716 off and a plane corner 0.0137 off,
read through OpenCASCADE. The text census reads code 16 = 0 on both, and 0.72
is the worst this test recorded before any of this, so it is either OCCT's
healing or a relation the census does not measure. That is the next thing to pin
down, and the marking stays until it is.

**The standing rule this came out of** (2026-09-19): wherever a less precise
rung has claimed a corner, face or edge first, the more precise answer replaces
it - the low-precision rungs are historical, and first refusal kept them
winning. Instances found so far: the provenance rung's two-surface curve point
blocking the three-surface point, and the one-surface projection holding
corners the two-face crossing places exactly. The rest of the ladder is
surveyed in item 23, with a matrix that makes the precedence a table.

### 23. The precision ladder: a survey, a matrix, and what stands between it and landing

**Opened 2026-09-19**, under the standing rule that where a less precise rung
has claimed a corner, face or edge first, the more precise answer replaces it -
the low-precision rungs are historical, and first refusal kept them winning.

#### The survey

| rung | decides by | the more precise answer |
| --- | --- | --- |
| A. grid claim refusal (`recogniseGridPatches`) | 4x the median corner stray | claim, and let the placement put junction corners on the crossing |
| B. "three exact things meet" (singleOwner block) | the mesh's facet planes, filtered by a `cos(pi/6)` chord heuristic | the faces the file writes, declared planes first |
| C. conic with one plane | the mesh's plane, alternating projection | a declared section plane |
| D. provenance two-owner (`reportOwnership`) | the curve two owners share, gated by `travel <= bound` with the grid's tessellation band | every constraint the file asserts, solved together |
| E. own-quadric projection | one surface only | the crossing of all faces at the corner |

Two instances are fixed in `2b9d05efd` (item 22's last subsection): D holding
corners the three-surface point places, and E holding corners the two-face
crossing places.

**A, measured.** Removing the refusal outright, with the placement now precise,
takes f04's code 17 from 38 to **8** and dissolves the non-manifold fan that
blocked it when it was first parked. On lid10 it still costs the wrap (both seam
crossings back: the chamfer exposes the ridge's buried back span, the claim
covers all four spans, the re-seam cannot apply) and, with mesh planes soft, the
60 edges used by three faces. Restored until both are fixed.

The lid10 failures of `export-step-flagship-corners` - a cone corner 0.716 off
and a plane corner 0.0137 off - are **OpenCASCADE's**, not ours: in our text,
vertex #1049 at (79.5875, 0, 0) is used by the sweep and two planes and no cone
at all, and OCCT has widened it to 1.08. It is the run-in again, where A's
refused facets are.

#### The matrix, parked on `claude/step-corner-arbiter-parked` (`4f4411b94`)

Rungs propose instead of claiming; an arbiter chooses. Two tables at the top of
`StepKernel.cc` decide it, so reordering the ladder is editing a table:

- **`kCornerTiers`** ranks the kinds of constraint on a corner, with two
  columns. `rank` is precision: declared face, then fitted face, then
  provenance owner, then mesh plane. `hard` says a candidate that leaves such a
  constraint, where the corner was on it, is no candidate. Candidates are
  compared tier by tier from the most precise down - one constraint of a higher
  tier outweighs any number of a lower one.
- **`kCornerRungs`** breaks ties between rungs.

What building it taught, each measured:

- **Constraints are not all worth the same, so they cannot be counted.** Mesh
  planes ranked level with declarations let two chord planes outvote the sweep:
  61 corners left off on lid10.
- **What the file writes outranks what provenance says.** Provenance above the
  fitted faces took 60 lid10 corners off faces the file asserts, and the final
  check refused them with an EXPORT-ERROR.
- **Precision and safety are separate columns.** A mesh plane ranks last by
  precision, but it is held `hard` because the fan cannot yet take what bending
  it allows: soft, f04 fans 21 faces and opens along 12 edges, and lid10 writes
  60 edges used by three faces.
- **A far root is never a fallback.** Two planes and a quadric are solved for
  both closed-form roots along the planes' line - the implicit is exactly a
  quadratic in the line parameter, so three evaluations fix it - and within one
  score only the nearest candidate is eligible.

**Why it is parked.** Against `2b9d05efd`: code 17 on lid10 and the bayonet 42 ->
46, on f04 38 -> 44, f02 unchanged. The arbiter re-decides corners the old rungs
left alone, and downstream code tested only with the old moves breaks. On f04
two corners with the same constraints solve to the same point, 0.014 apart -
two mesh vertices where the model has one - and the sliver between them has a
plane an edge sits 2.27 off.

#### What stands between it and landing

1. **The fan** (`split_for_corners`) must survive bending a planar face beside a
   sweep without reusing an edge the sweep already has, and without opening the
   shell. Then `MeshPlane.hard` goes false.
2. **Coincident corners.** Two mesh vertices converging on one point of the
   model is a topological fact the placement has to act on - merge them - not a
   placement to refuse.
3. **The wrap** for rung A: a claim covering every span of a closed profile
   needs a `SEAM_CURVE`, or a cut, not the re-seam.

#### The three items, taken 2026-09-19: two were one, and the third parks

Work on `claude/step-ladder-precision`, which carries the matrix live
(`f0aff6156`); the claim work is parked on `claude/step-grid-claim-parked`
(`2bd2cfdf4`).

**The fan and the coincident corners were one defect: scoring chord planes.**
Every opened shell and every "fan that cannot be cut" traced to two mesh
vertices on one point, and the merge built for them was correct - snap corners
joined by an edge onto one point, key edges on the vertex *entity* rather than
the mesh index, drop a collapsed side from a loop and from the ring a fan is
cut from, keep a plane section off a collapsed run. But asked where the pairs
came from, the mesh had them **1.4e-12 to 1.9 apart**: never a duplicate the
mesh carried, always two points the solver had put on one root. A far root on a
bore's vertex line lies on *both* facet planes beside it, so with mesh planes
scored it outranked the near one. Taking mesh planes out of the arbiter
(`used = false`) and nothing merges anywhere; the merge was removed unused, and
is described here for the day a mesh does carry a duplicate. Three more
precision fixes fell out on the way, all landed:

- a written face on a plane the model declared is a declaration, not a chord -
  classed as a chord, lid10's z = 95 cap went soft and the corners of its hole
  were placed 0.65 below it, on the outer wall;
- a `declare_grid` sweep or a Bezier patch is a declared *interpolant*, below an
  exact declaration and above a fit;
- the faces the file writes rank above provenance's owners.

Also tried and removed as changing nothing measurable: recognising a cap as a
plane perpendicular to a declared quadric's axis (exact, but the fold test
already keeps those corners on it), and deriving the plane of the circle two
coaxial surfaces meet in to condition their crossing (the corners it was for are
not on the sweep at all - below).

**The wrap is solved, and what is left of rung A is a claim question.** With the
refusal removed, recognition and the writer disagreed about which profile span a
facet lies over: recognition asked the centroid, which beside the seam projects
onto the flank across the crease, and the writer asked the corners. Asking the
corners in both, recognition sees the claim wrap and cuts it into arcs of
spans, and no face crosses its seam. The cuts run along profile *columns*, and a
column of a tensor-product B-spline at a knot is itself a B-spline curve - the
surface's u-knots and that column of its control net - so those edges are
written as the surface's own curve, exact, where the chord between stations was
0.1 off it (155 of them on lid10, to 1). What stops it landing is five corners
at the thread's ends, each exactly on two exact declarations and 0.10 to 0.22
off the sweep: (79.4, 0, 1.2) is on the ridge's flat *end cap* - the sweep's
flank at that height is at r = 79.51 - so the facet attaching it to the sweep
face is not the sweep's, and the refusal was crudely right about it. The
precise replacement is a claim decided by solvability: a facet belongs to the
sweep when each of its corners can be placed on the sweep together with the
other declarations it lies on. That couples recognition to the placement and is
the next design step.

| code 17 | committed | the matrix | + claim work (parked) |
| --- | --- | --- | --- |
| lid10, bayonet | 42 | 42 | 52 |
| f02 | 8 | 8 | 8 |
| f04 | 38 | 38 | **8** |

**Two instruments were blind, and one of them was ours.** `vertex-on-curve.py`,
and through it the census's code-7 row, returned 0 for a `SURFACE_CURVE` - every
sweep edge - and measured a B-spline only at its end control points. It now
unwraps the one and evaluates the other by de Boor with its knots; mutation-
checked. `validatestep.py` rejected any curve that was not one Bezier span; it
now holds a knotted curve to the contract it already held a grid surface to
(parked with the claim work, which is the only thing that writes one).

#### SOLIDWORKS, 2026-09-19: codes 7 and 17 are gone from lid10

`claude/step-ladder-precision` at `93cafc572`, kit `build/sw-2026-09-19`,
SOLIDWORKS revision 34.0.0, `-Raw -RoundTrip -Heal`, labelled "raw (do-not-knit,
diagnostics off, analytic conversion off)"; the options were restored after.
The control reads as it must, `faults=2 faultyfaces=2 codes=17`, and is the one
file its repair alters (Δvolume -37.4145, Δarea +6.3295).

| file | census code 17 | SOLIDWORKS | repair |
| --- | --- | --- | --- |
| lid10 | 42 | solid, 803 faces, `faults=1 faultyfaces=3 faultyedges=0 codes=21/30` | 1 face split, nothing re-fitted |
| bayonet | 42 | solid, 65 faces, `faults=1 faultyfaces=1 faultyedges=0 codes=30` | 1 face split, nothing re-fitted |
| f02 | 8 | solid, valid, no faults | 34 curves re-fitted, worst 4.761e-05 |
| f04 | 38 | solid, valid, no faults | 215 curves re-fitted, worst 9.583e-05 |

Against the last recorded lid10 run (`faultyfaces=7 faultyedges=1
codes=7/17/21`): **codes 7 and 17 are gone, and so is the faulty edge**; faulty
faces 7 -> 3. What is left is two codes, and each is accounted for:

- **21**, self-intersecting face, on lid10 only - the census's three pinched
  loops (planes at z = 84.9), still never investigated; the bayonet has none
  and reports no 21.
- **30**, not G1 continuous, on both - and the healed pair names it. The repair
  adds exactly one face and one curve, moves the corners of one B-spline face,
  and re-fits nothing: SOLIDWORKS **splits a sweep face** at a crease. A
  `declare_grid` sweep is linear across its profile, so it is only C0 at every
  profile column, and a face spanning several spans carries a crease inside it.
  Splitting sweep faces at their profile columns, with the column's own curve as
  the edge, is exactly what `claude/step-grid-claim-parked` already does for a
  wrapping claim - a second reason, independent of the wrap, to finish it.

f02 and f04 are clean by every API check, and their repair is the familiar
re-fit at a ten-thousandth of a millimetre, touching no vertex, surface or
topology. The volume OpenCASCADE reads from SOLIDWORKS's re-export is 1.04%
below ours on lid10 and 0.81% on the bayonet, while SOLIDWORKS's own volume for
lid10 (224483.5622) is 0.06% from what OpenCASCADE reads of ours (224620.2322);
which side of the second read loses the 1% is not established.

**`-FaultDetail` named the entities**, which is why it is now part of every run.
Six rows, and they place both codes exactly:

| file | entity | kind | area mm2 | centre | codes |
| --- | --- | --- | --- | --- | --- |
| bayonet | face 19 | bspline | 14.005973 | 78.5601; 8.4690; 0.9727 | 30 |
| lid10 | face 34 | bspline | 14.007408 | 78.5601; 8.4690; 0.9727 | 30 |
| lid10 | face 550 | plane | 1.466064 | -56.9991; -55.8627; 89.7880 | 21 |
| lid10 | face 682 | plane | 1.466064 | -55.8627; 56.9991; 89.7880 | 21 |
| control | face 1 | bspline | 1027.844725 | 0.0298; 0.0882; 17.3483 | 17/17 |
| control | face 4 | cylinder | 2098.714282 | 0.1454; 0.0000; 20.0000 | 17/17 |

Code 30 is **one face, the same on both parts**: the sweep at the thread's
run-in, 14 mm2 at z = 0.97 - the small claim covering one span of the profile.
Code 21 is **two** planes of 1.466 mm2 at **z = 89.788**, not the three the
census counts and not the z = 84.9 recorded earlier in this file; they are a
mirror pair, which is what to expect of a feature the model places four times.

Also noted, not caused here: on the CGAL backend in the analytic-only mode,
`step-bored-cone` and `step-band-family` already raise "planar faces assert a
plane one of their own bounds is not in" on the old binary; the arbiter adds
`step-cap-meets-wall` and takes the band family from 11 faces to 13. No test
gates that mode's EXPORT-ERROR.

### 25. The claim must be revised after placement, not decided before it

**Measured 2026-09-21**, taking item 23's first follower. The conclusion is not
the one the follower expected, and it is worth more.

**What was tried.** The 4x-median refusal was replaced by a solver: a facet
belongs to the sweep when each of its corners can be placed on the sweep
together with the exact declarations it already lies on, asked with
`projectOntoThree` at 1e-9 and unbounded travel, "exact" being `declaredBand ==
0` rather than a tolerance chosen for the purpose. Measured on lid10: 6 facets
refused, code 17 **52 -> 44**, f04 held at **8**, `sweep-chords` 20 -> 17 with
the "an end off one of them" population 14 -> 11.

**Why it is wrong anyway.** `planes-at-sweep.py` reported two of the refused
facets as *the sweep's own* - every corner on it, meeting it at 1.4 and 2.7
degrees - which is the exact defect removing the refusal exists to cure. Chasing
it with `at-vertex.py`, the corner they were refused for, (76.7944 20.4008
1.0332), **does not exist in the exported file**: the placement had already
moved it.

So the test judged a corner *where the mesh left it*, and the mesh is the least
precise answer there is. The solver was sound - pairwise solves converge
(0.1074, 0.1080, 0.2189), and the triples that fail really have no root - but it
was given a provisional point. The 4x-median rule had the same flaw; replacing
its statistic with a solver kept its input and made the error look principled.

**This is the last ladder-order violation.** The arbiter was built so that where
a corner belongs is decided once, by the most precise constraint available. The
grid claim still decides earlier, by the tessellation, and wins by running
first - `moves.count(v)` in a new costume.

**And the arbiter already computes the answer.** On the unconditional claim it
reports, of its own accord:

```text
EXPORT-WARNING: 5 corners are left off a surface the file writes a face of them
  on, by 0.2184 at worst - 5 because no candidate satisfies every face there,
  0 because the one that does would fold a face or turn an edge round
```

Five corners, with the two reasons separated: no candidate satisfying every face
is the solvability answer, reached after placement, on the closed form. Those
five are item 23's five, named by the right component instead of guessed.

**What it therefore needs**, and it is a restructuring rather than a rule:
recognition claims unconditionally, the arbiter places, and a revision pass
un-claims any facet with a corner the arbiter could not put on the sweep. The
obstacle is ordering - recognition completes before any placement runs, and the
arbiter needs recognition's faces to know what meets at a corner - so the claim
has to become two-pass. Nothing may decide a corner's position anywhere else.

**Parked** on `claude/step-claim-after-placement-parked`, with the corner-stray
diagnostic below, because the claim alone leaves lid10 and the bayonet at 52
against 42 committed.

**A diagnostic came out of it**, and it is landed rather than parked: the corner
strays the old refusal consumed are now *reported* and decide nothing -
"the corners of the 776 facets the sweeps claim are within 0.2069 of the surface
as written, median 0.0128". Measured against the surface as written, not the
band. The median reproduces, independently, the 0.0128 item 22 records for the
middle of lid10's thread, and it is the measurement behind that item's
unexplained middle-versus-ends question.

### 26. A rim is written as a 45 degree ELLIPSE, and it costs 1.2 mm3 no kernel objects to

**Found 2026-09-21**, by asking why one declared surface written two ways bounds
two different solids. It is the first defect of this work adjudicated by the
*model's own arithmetic* rather than by a kernel, and no kernel would ever have
found it.

**The measurement.** `tests/data/scad/step-export/step-split-surface.scad`
exported two ways - the cylinder written as a band's wall, and the same cylinder
written as a trimmed patch (`OPENSCAD_STEP_YIELD_SURFACE=0`):

| | volume | against the model |
| --- | --- | --- |
| derived from the model, in closed form | **15647.634695** | - |
| trimmed patch | 15647.634695 | **-1.89e-11 relative** |
| band's wall | 15646.422089 | **-1.2126, -7.75e-5** |

The patch is exact to eleven digits. The band loses 1.2126 mm3.

**And neither reader notices.** SOLIDWORKS reads both as `valid=1 faults=0`,
with the control in the same run reading `faults=2 faultyfaces=2 codes=17` as it
must; OpenCASCADE reads both back as solids and reports the same two volumes to
four decimal places. Two kernels agree on both numbers, because both *files* are
well formed. What is wrong is which solid one of them describes, and only the
model can say.

**The defect.** The band file writes two `ELLIPSE`s - semi-axes 28.318 x 20 and
28.349 x 20 - whose four endpoints all lie at `z = 10.0`, radius exactly 20:
the crease circle where the cylinder meets the chamfer. Their placements give
them away:

```text
centre (0, 0, 29.956)  normal (-0.677,  0.207, -0.706)
centre (0, 0, 30.000)  normal (-0.678, -0.207, -0.705)
```

A centre at `z = 30` on a model 13 tall, and a normal 45 degrees off the axis. A
45-degree section of the r = 20 cylinder centred at `z = 30` runs from `z = 10`
to `z = 50`, so **`z = 10` is the ellipse's lowest extremity**: both endpoints
sit at that extremity and the curve arcs *upward* between them, off the rim,
shaving the sliver.

**The root cause is a deduction where a declaration was available.** The rim of a
cylinder at constant `z` is a circle by construction. The exporter instead
*fitted a plane* to the boundary run - and a run along a rim does not determine
one: infinitely many planes contain that line. The fit took a 45 degree plane,
which is the chamfer cone's own slope, so the neighbouring face's orientation
leaked into a curve belonging to the cylinder. Nothing here needed fitting.

**Why it stayed hidden.** It needs a rim run next to a 45 degree face, which
lid10 and the bayonet have and f02/f04 do not; the curve it produces is well
formed, so every validator and both kernels accept it; and the cost is 7.75e-5
of volume, which no face count, census row or fault list reports.

**What it explains.** Why the band representation loses volume; why the patch
representation is exact (it keeps the mesh vertices and writes real arcs, never
fitting a plane to a run); and why *representation-independence* was the right
property to assert even before the cause was known.

**Open:** lid10's own split shows 2.0818 mm3 with the area *identical to the
last digit*, where this fixture moves both area and volume. That is a different
signature and may be a second mechanism.

### 24. The flagship third of the suite does 18 exports where 6 would do

**Measured 2026-09-21**, while waiting an hour for a baseline run on a 16-core
machine. Not an exporter defect - a cost we pay on every verification, and the
verification loop is the thing this work runs most.

**Two independent multipliers**, both in `tests/step-flagship-check.py` and
`tests/CMakeLists.txt:1221-1256`:

1. **The six coupons are exported three times over.** `export-step-flagship-coupons`,
   `-strict` and `-corners` are the same script over the same `COUPONS` list;
   `--strict` and `--strict-corners` change *assertions applied to output already
   produced*, not what is exported. So lid10 and the bayonet - the two most
   expensive models in the project - are each exported three times per suite run.
2. **Within a run the six are serial.** `for name, source, extra in COUPONS:`
   exports, validates and round-trips one at a time. The coupons share no state
   and write separate files, so this is a sum where it could be a max.

**The fix, ctest-native rather than a cache.** Split export from assertion with
an `--export-dir`: one setup test exports the six once, the three assertion tests
consume that directory, wired with `FIXTURES_SETUP` / `FIXTURES_REQUIRED`. That
is what the property is for - no staleness heuristic, no lockfile, ctest owns the
ordering, and the three keep their own names and `WILL_FAIL` semantics. Then
parallelise the export loop in the setup with a thread pool (`subprocess.run`
releases the GIL), collecting results and printing in coupon order so the report
stays deterministic.

**The detail that would gut it if missed.** The checks do not read only the
`.stp`. `--strict` greps the export's **stdout** for `STRICT_MUST_NOT_SAY`, and
the round trip parses `tessellation band of ([\d.]+)` out of that same output to
set `fitted_band`. A split that persists only the `.stp` leaves `--strict`
asserting nothing and the round trip without its band - green for the wrong
reason, which is the failure mode this file is full of.

**Cost to land:** adding a test needs `cmake -B build`, which `CLAUDE.md` says
costs a full rebuild, against one unexplained 19-second measurement over an
existing cache. Unknown which applies here.

**Half of it landed 2026-09-21**, the half that needs no `cmake` reconfigure:
the coupon loop in `step-flagship-check.py` now exports concurrently on a thread
pool and checks serially in `COUPONS` order, so the wall clock is a max over the
exports instead of a sum, and the report stays deterministic. A flagship test
goes from about 300 s to **152 s** at Release. `scripts/step-dev.sh flagship`
also passes `-j` now, where it had been running the three tests one after
another.

What is left is the 3x duplication, and it is worth less than it looks: ctest
already runs the three tests **concurrently**, so removing the duplicate exports
saves CPU rather than wall clock. It needs a new `add_test`, hence a configure
and a full rebuild. Take it when something else already forces a configure.

**Priority, corrected the same day.** The numbers above were first taken on a
build with no `CMAKE_BUILD_TYPE` (see *A build with no build type*, below), where
the flagship set took 2h10m. On a Release build of the same commit the same six
coupons take about five minutes, so the 18-vs-6 redundancy is worth ~3 minutes a
run, not ~90. Still worth doing, and no longer urgent.

Banked rather than taken: it pays only across several full suite runs, and the
claim work's loop is `export` + `census`, which does not touch it.

### A build with no build type runs 26x slower and says nothing

**Measured 2026-09-21.** A fresh worktree's first build came out with
`CMAKE_BUILD_TYPE:STRING=` - empty. CMake defines `CMAKE_CXX_FLAGS_RELEASE` as
`-O3 -DNDEBUG` and then never applies it, so the binary is built with no
optimisation at all. Nothing in the build output, the smoke test or the suite
says so; the exports are **byte-for-byte identical**, and the only symptom is
time.

What it cost, same commit, same machine, six flagship coupons:

| coupon | Release | no build type |
| --- | --- | --- |
| band-fn048 | 29 s | ~14 min |
| band-fn064 | 64 s | ~30 min |
| band-fn096 | 97 s | ~49 min |
| lid10 | 56 s | ~19 min |
| bayonet | 44 s | ~17 min |
| **all six** | **~5 min** | **~2 h 10 min** |

and the full STEP suite 8437 s instead of a few minutes.

**The cause was in `scripts/msys2-build.sh`**, which auto-configures when there
is no cache and ran a bare `cmake -B build` with no build type, while
`CLAUDE.md`'s documented configure line passes `-DCMAKE_BUILD_TYPE=Release`.
Only a *fresh* tree reaches that branch - every other tree had a cache
configured by hand - so the one path nobody ran was the one a new worktree
always takes. Fixed: the script now passes it on both configure paths.

**The general shape**, worth more than the fix: a default that is *absent*
rather than wrong produces correct output and no diagnostic, so it can only be
found by noticing that a number is out of line with experience. Here it was
"this has never taken two hours before".

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

> Read `doc/step-export-wip.md` - "Read this first" and "Where it stands" before
> anything else, then open items 15, 11, 13 and 16 - and
> `doc/step-export-development.md`. Build and test mechanics are in `CLAUDE.md`.
> Work on `claude/solidworks-fault-code-17-b58858`, which is pushed and green at
> `16f29c5f`.
>
> The branch should be green. Verify with
> `ctest --test-dir build -R 'export-step-|mutations'`: **53 tests**, not
> `-R step`, which drops the mutation harnesses. Run it under the machine's own
> locale. Two of the 53 fail on purpose and are registered `WILL_FAIL`, so they
> report green: `export-step-flagship-strict` (the plane veto fires on all six
> coupons) and `export-step-flagship-corners` (corners of analytic faces off
> their surface). Do not read either green as the defect being gone, and do not
> delete them. Confirm the round trip actually runs (`cadquery-ocp` 7.x, not
> 8.0) before trusting any green result.
>
> Every STEP export needs `--enable=step-analytic-surfaces
> --enable=step-approximate-surfaces`; without them it writes facets and passes
> for the wrong reason. lid10 needs `-p examples/step_test/lid10.json -P "New set
> 1"` passed as an argv list, never through a shell function that can lose the
> quoting.
>
> **Where it stands.** Code 17 has now moved on two coupons. `f02-band-fn032`
> lost its bore's 17 when its boundary became the crossing curve; on 2026-09-14
> `f04-band-fn064` read **`faults=0`** in SOLIDWORKS under a relaxed veto, with
> its swept surface kept rather than discarded, where its normal build still
> reads 17. Root causes A, B, C and D of item 11 are all closed at the root, and
> the flagship check - which until 2026-09-14 could not fail its round trip at
> all - now enforces it.
>
> **Take item 15's outlier.** The corner slack is not cosmetic: lid10's inner
> wall between the thread turns is a run of cone faces, SOLIDWORKS knits them
> into one 19293.86 mm² face and rejects it with code 7 `swEdgeVertexNotLie`, so
> a CAD user sees the whole wall missing. Relaxing the veto takes those corners
> from 290 off their cone to 9. What survives is one vertex at
> (79.5875, 0, 0), 0.7159 off the bottom chamfer - and it is **not** corner
> placement: provenance never calls it a junction, no path proposes a move for
> it, and the claim that produced that face has a worst corner of 0.1089 over
> every facet it took. The written face's loop acquires a vertex the recogniser
> never accepted. Find which step adds it - `patchFromFacets`, the boundary
> runs, the writer's loop assembly - and on whose authority. That is root cause
> A's shape ("a boundary settled in two places") for vertices rather than for
> edge geometry.
>
> **How to work it, which is how A, B, C and D were found:**
>
> - Five whys with the branches named before each measurement, and the refuted
>   ones recorded. This investigation has refuted more hypotheses than it has
>   kept - including its own first account of C, and "the missing wall is a
>   regression" this session - and the refutations are part of the result.
> - Fix at the root, not at the gate. Do not relax the veto until item 15 is
>   closed, then the flagship check under the relaxation - read its *volumes*,
>   not only its verdict - and then SOLIDWORKS, with the `f02` control in the
>   same session.
> - Prefer the declaration to a deduction. Ask the surface - `splineForm`,
>   `membershipTolerance`, `isDeclaredPoint`, `declaredBand` - rather than
>   reaching around a protected member or inferring by projection. Where a
>   threshold is needed, derive it: D's backstop uses the mesher's own
>   `coplanarTolerance` rather than a number someone liked.
> - Verify a latent fix where the defect lives. A, B, C and D change nothing the
>   flagship coupons' normal build writes and were verified against the
>   relaxed-veto configuration (`b57c8e73b`'s settle loop, behind a temporary
>   switch that is never committed). Mutate by writing the working tree with
>   `git show <sha>:<path> > <path>`, never `git checkout <sha> -- <path>`,
>   which stages the file.
> - A defect the suite did not catch does not land its fix until something in
>   the suite fails without it. A unit test where the defect is in a solver; a
>   fixture where it is visible only in a whole export; an `EXPORT-ERROR` the
>   flagship check fails on where neither reaches.
> - Anything that should never be reached is loud, at the strength the
>   measurement supports: `EXPORT-ERROR` if unreachable today, `EXPORT-WARNING`
>   plus a `WILL_FAIL` assertion if reached today. Item 12 is the survey for
>   more.
> - Ask what a check rejects before quoting what it accepts, by handing it a
>   file known to be wrong. The flagship round trip passed a 25-shell file for
>   four days; a debug line printing four decimals hid 2.26e-06 as "0.0000"; and
>   a report printed before the loop it measures reads zero for ever.
> - When a CAD view suggests missing geometry, render our own file first. OCCT
>   plus `free_boundaries` and an STL render settled it in minutes this session,
>   where the view had suggested a regression that did not exist.
>
> **Standing rules.** Every expectation derived from the model, never captured
> from a run. Report how much of each sweep was recognised beside any fault
> count. For SOLIDWORKS: start it by hand, label every run with its import
> settings, never kill the driver mid-import, write its output to a file rather
> than through a buffering pipe, and put the 2026-09-08 control
> `build/interop-kit3/f02-band-fn032-analytic.stp` in the same session - it must
> read `faults=2 faultyfaces=2 codes=17`, or a page of clean rows means nothing.
> It has now reproduced that nine times. Read `-FaultDetail` before forming a
> theory, and compare it against the last run's table rather than reading it
> fresh.
