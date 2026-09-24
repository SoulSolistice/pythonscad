# STEP export: handover, 2026-09-24

Branch **`claude/step-claim-ledger`**, the commits in section 5 on top of
`claude/step-claim-two-pass-cf4565` (`f5fb30c74`), nothing pushed, working tree
clean. Suite **58 of 58**; unit tests 3974 passing plus 1 expected failure.
Parked beside it: `claude/step-provenance-identity-parked`.

This document is the whole handover. It says what the rules are, what was
understood in the session of 2026-09-23/24 so that it is not rediscovered, what
was built, how to work, and what to do next. The priming prompt is at the end.

---

## 0. Read these first. This is not negotiable

| document | status |
| --- | --- |
| **`doc/step-recognition-design.md`** | **MANDATORY.** The claim ledger, Propose / Arbitrate / Assemble, region before representation, the migration plan (steps 1-3 now done), *Determinacy: the ground this design stands on*, *Step 1, finished*, and **The recognition module, as built** - the architecture you are working in, how to add a recogniser, and what is not done |
| **`doc/step-rungs.md`** | **MANDATORY.** What determines each entity and therefore which rung it is: the D0-D4 classes, the covering order, the ladders for curves (C0-C6), surfaces (S0-S3), regions (R0-R2) and corners, and how to read a result |
| `doc/step-export-development.md` | what is settled: the declaration channel (now with *who declared a surface, and in which role*), boundary machinery, corner arbiter, kernels, testing doctrine (now with *behaviour-neutral is proven by the oracle*), traps (now with this session's process traps) |
| `doc/step-export-wip.md` | open items - read **30, 31, 32** (new), 27, 29, and 17 (superseded) |
| `scripts/step-diagnostics/README.md` | every instrument, by the question it answers |

**The two mandatory documents are not background reading and not advisory.**
They were earned over many sessions of debugging. Every change to recognition,
to a rung, or to what the exporter writes is justified against them, and work
goes *inside* the design's numbered migration steps. Before building anything,
find the paragraph that already covers it: two sessions running have lost most
of a day rediscovering what those documents already said.

---

## 1. The rules that are not negotiable

### 1.1 Purity, and the recognition shape it is built in

Recognition now runs in the shape the design asked for (section 3 below).
**Continuing in that shape is not negotiable**:

- **Propose is pure.** A proposer reads the graph, the declarations and
  provenance - and nothing any pass has claimed. If it needs to know what is
  held, it is not a proposal.
- **One arbiter grants.** A facet becomes held only through
  `recognition::Arbiter`. **The ledger is the source of truth** (migration step
  3); `consumed` is a cache, checked against the ledger at the end of every run.
- **Stages declare what they read and write**, and the pipeline checks the
  table's order against those declarations every run.
- **Every claim - won or lost - goes through one rule** (`recordClaim` and a
  `Claimant`), and states its precision *and* the class of its evidence.
- **Reports are queries over the ledger**, in `recognition/Reports.cc`, never
  computed inside the writer.

Why it is not negotiable: this is what makes "why is this facet a plane?" a
lookup; what lets policy change in one table rather than across four passes;
and what makes the covering order enforceable. A change that reaches round it -
a pass reading `consumed`, a grant outside the arbiter, an undeclared stage, a
report inside `StepKernel` - is refused, whatever it buys.

**Refactoring is endorsed and wanted when it serves this purpose.** The
constraint is only that each step is proven: by the oracle (section 4) when it
claims to change no behaviour, and by a fixture that asserts the change first
when it does. A wholesale rewrite validated against the suite alone is refused -
the reverted R0 passed 56 of 56 while turning a declared cone into planes.

### 1.2 The ladder

- **A closed form is exact by definition and has to stay.** The classes D0-D4
  are a **covering order**: D0 is covered first, then D1 takes what is left,
  then D2, D3, and D4 only what nothing above could. A closed form may be
  **exchanged** for another closed form, **never surrendered** to a chord or a
  facet plane. Nothing falls down the ladder.
- **A rung is as exact as its evidence.** A declared surface in the sentence
  does not make a rule D0; what *decided* it fixes its class.
- **A D4 may never be an input to a D0-D2.** A D0 carries no accuracy
  threshold (float equality for a generator's point against the surface that
  emitted it is not a threshold; a band is).
- **Each primitive has its own ladder**; a decision on one may not demote an
  entity on another. **A rung may never manufacture a boundary.**
- **Coverage is not the criterion, and a rising census is not by itself a
  regression.** Ask *why* a rung declined, not how much.

### 1.3 Determinacy, provenance and root causes - the understanding of this session

Recorded in full in `doc/step-recognition-design.md`, *Determinacy*. In short:

1. **A D0 with correct provenance is THE key.** A region belongs to the surface
   its *creating function* declared and ends where that function's surfaces end;
   such a region strands nothing.
2. **`consumed` is not correct**, in three ways, each of which cost weeks:
   consumed is not consumed by the most precise claimant; consumed even by a D0
   is not necessarily the *right* D0; and a wrong holder can strand facets that
   nothing can allocate afterwards. So held facets are always still examined.
3. **An entity is as precise as the independent closed forms that fix it** -
   for every kind of entity, not only corners: first the weakest class its
   determining set needs, then the freedom left (leftover freedom is filled from
   the mesh), then independent corroboration. Independence is algebraic (gradient
   rank), not a count of names: a surface declared twice, or two coaxial
   cylinders, fix no more than one.
4. **Globally, never locally.** What is maximised is the determinacy of the whole
   cover. A region solved well locally (a nine-knot Bezier) that strands two
   facets is worse than it looks, and no census recovers those facets. Stranding
   must be visible *before* the arbiter chooses.
5. **Wherever a less precise rung holds ground, ask why**: wrong
   classification? missing declaration? consumed by a misfitting claimant?
   underdetermined? A more precise rung may not build on a less precise one unless
   thoroughly validated.
6. **Root-cause fixes only.** No policy gate, threshold or exclusion rule where a
   cause can be chased to its origin. This session's example: lid10's disputed
   facets were traced to an undeclared `polyhedron()` in the model and fixed in the
   model (section 2.4).
7. **A closed-form truth can look like a bug.** An edge once had to move over
   1 mm to where provenance put it, and that cured SOLIDWORKS faults. Never refuse
   a correction for its size; ask what determines it.
8. **Intent cannot be recovered from geometry.** Minkowski decomposition, the
   H-representation or an Ehrhart polynomial cannot turn a polyhedron back into
   the cylinder it approximates; points satisfying a declared closed form are
   *corroboration*, not ownership. Missing intent is fixed by declaring it.

### 1.4 The standing rules

Kernel agreement is not proof. Exactness takes precedence. Magic numbers and
statistical thresholds need explicit approval. Half a fix parks, with its
measurements, on a `-parked` branch. Five whys with named branches; record the
refuted ones. Derive expectations from the model, never from a kernel, and never
calculate anything from a faceted input. Mutation-check every new assertion and
every zero. **lid10 is a specimen, not a deliverable** - a finding on it counts
only once a synthetic fixture isolates it. Never `--no-verify`.

---

## 2. What this session established - do not rediscover it

### 2.1 Migration step 1 is finished: every claim a pass would make is recorded

`Claim` carries a **`ClaimFate`** - honoured, withdrawn (a band given back),
held (something had it first), declined (the claimant's own rule, named in
`why`), refused (the writer would not write the region) - and an
**`EvidenceClass`**, the weakest class of what decided membership. Every pass
records every claim, once, where its fate is decided: the band pass names its
surface as soon as its walk is confined and records every *shape* refusal; the
Bezier pass examines held facets; the grid and quadric passes record their floor,
median and region refusals; every writer records every patch it does not write.

**The evidence classes, measured, and the one that surprises**: a band's wall
D0; a band's polar cap D4; a declared sweep or Bezier patch D1; a fit D3; and
**a trimmed quadric face D4 in both tiers** - `OnQuadric::covers` holds corners to
`min(bandOf(f), max_off)` and the interior to 1.25x the facet's sagitta, so the
tessellation decides even at `max_off = 1e-7`. lid10 writes 377 facets on a
declared exact surface by D4 evidence, the bayonet 265. Counted, not refused yet.

### 2.2 The census reads the catalogue

Every export prints *why N facets are fitted or left to the tessellation* -
declined (rule named), refused, held first by something less precise, withdrawn,
or claimed for no declaration (split by whether the maker declares anything) -
and *what decided each written facet* (D0-D4). The "no declaration, maker
declares nothing" branch fires where it must: `step-approximate-report` 2048,
`step-extrude-refusals` 2112, `step-revolve-axis-point` 32.

**Current figures** (`build/measure/split`, the committed code):

```text
lid10    exact   22 declared: 13 written, 1 evidence, 6 no mesh, 2 unexplained (cone 52, sweep 62)
                 by facet 909 declared / 27 declared plane / 0 fitted / 1521 tessellation
                 decided by: 536 D0, 373 D4
         approx  1 unexplained (cylinder 12 - open item 30)
                 by facet 1671 / 27 / 0 / 759; 306 of the 759 declined by a rule
bayonet  exact   20 declared: 11 written, 7 no mesh, 2 unexplained (cone 75, sweep 84)
         approx  0 unexplained; 18 facets below a declaration, all the sweep's 4x-median rule
```

### 2.3 What the catalogue showed

- **Purity's defect, counted.** 188 facets on lid10 are walks the band pass
  confined to a declared surface and then refused *for their shape* - "a claim
  truncated to fit a form", which was in nobody's claim before.
- **Open item 30 - the approximation tier surrenders a closed form.** Joined
  facet by facet across the two tiers' ledger dumps: those same 188 facets of
  declared cylinder 12 are a quadric face at the exact tier and planes at the
  approximation tier, where `max_off = inf` widens the quadric claim to fringe
  facets, the region's worst corner then exceeds `bandOf(region)`, and the whole
  region is declined. A D4 widening and a D4 all-or-nothing test costing D0
  ground.
- **Order is policy, seen.** 6 facets whose maker owns the sweep were refused by
  the sweep's own 4x-median statistic and then taken by a quadric face.
- **Open item 32.** `tests/stepexportsanitytest.py` never checks for
  `EXPORT-ERROR`, so on the fixtures the ledger's own checks are loud to nobody.
  Measured zero errors everywhere; enforcing it wants one suite run of its own.

### 2.4 Provenance: identity, not measurement - open item 31

- **The ownership half of `Provenance` was a measurement**: `export_step.cc`
  re-derived "original X owns surface S" from corners on the surface, a 60 degree
  facing band and `least = 3`, because every boolean merged the operands' records
  into one flat list and forgot who declared what. It fails exactly where two
  declarations meet: on `step-cylinder-cross` the crease slivers "own" the other
  cylinder; on `step-declare-cone` a cap "owns" the cone it closes.
- **Now carried**: `Surface::owners` (face declarers) and `witnesses` (evidence
  declarers - `cylinder(r1, r2)`'s rims, a sloped revolve edge's radii, a taper's
  rims), stamped where Manifold mints ids, carried by every clone and transform,
  united on every deduplication; a `declare_*` over an already-Manifold subtree is
  stamped with that subtree's originals. **One geometric surface is one record**
  (`describesSameSurface`); two records of one infinite cylinder had been counted
  twice by ownership, the ledger and the census.
- **The decision still uses the measured `owned`.** The switch was tried twice
  and parked (`claude/step-provenance-identity-parked`): identity alone offered
  a junction candidate 132.9 mm away (a tool whose declared wall the boolean
  removed); **identity plus a D0 presence test** (a facet of the maker on the
  surface by `runsThrough`, no band, no count) fixed that and failed
  `flagship-strict` on lid10 for one reason: originals 26-32.
- **Originals 26-32, traced to the model** with the ledger dump's centroids:
  the four copies of `bayonetChannel`, a `polyhedron()` whose walls are point
  lists at the same radii the entry slots declare. **Fixed at the origin**: both
  specimens now wrap the channel in `declare_cylinder(r = rOuter)` and
  `declare_cylinder(r = rInner)`. lid10's measured-only ownership is now 0. **The
  switch has not been retried since** - see section 6.
- The bayonet specimen exports `_part = base`, which has no channel: its channel
  declaration is correct and not exercised by that export.

---

## 3. The recognition module, as built

All in `src/geometry/recognition/`, STEP-agnostic. Full table, the recipe for
adding a recogniser and the not-done list: `doc/step-recognition-design.md`,
*The recognition module, as built*.

```text
band -> fit -> propose (pure) -> Bezier -> grid -> quadric -> leftover
```

| file | what |
| --- | --- |
| `FacetGraph.h/.cc` | the mesh and its edge -> loops adjacency, built once |
| `Provenance.h` | facet -> original; the `excludes` gate |
| `Ledger.h` | `Claim`, `ClaimFate`, `EvidenceClass`, `ClaimLedger`, `Claimant`, `recordClaim` - the one rule every claim is recorded through |
| `Arbiter.h` | `settle` - the only place a facet becomes held; `held()` read from the ledger; `cacheAgrees()` |
| `Pipeline.h/.cc` | `Context`, six stage functions, `kStages` with reads/writes, the order check, `Writability` (the writer's sections, asked by the quadric stage) |
| `Reports.h/.cc` | `dumpLedger`, `reportArbitration`, `reportDeclarationCensus` |
| `Bands.cc` `Bezier.cc` `Grids.cc` `Quadrics.cc` `Fits.cc` `Regions.cc` | one file per recogniser; `propose*` pure, `form*` over what is granted; shared helpers in `Internal.h` |
| `../AnalyticFeatures.cc` | what the writer calls: projections, implicit forms, fitting primitives (696 lines) |

Assertions, all mutation-checked: the stage table's order against its
declarations (swap grid and quadric and it fires), the `consumed` cache against
the ledger (flip one facet and it fires), and the `[purity]` unit test
(proposals identical whatever is held and whichever order proposers run in).

**What is not done** (also in the design doc): step 4 - the grant is still
today's order, `held() || taken` in the `form*` functions; the band pass grants
inside itself and is split into propose/form with step 4 (it matters once the
arbiter weighs a band against a whole-region claim - R0); report strings still
say `STEP export:`.

---

## 4. Tooling: how to build, test and measure

### The dev loop is committed - do not write build or export scripts

```powershell
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 build
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 test
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 export lid10
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 census <file.stp>
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 measure <name>
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 compare <a> <b>
```

- **`measure` / `compare` - the oracle.** `measure` exports lid10, bayonet,
  f02, f04 and every fixture in both tiers with the ledger dumped into
  `build/measure/<name>/`; `compare` runs
  `scripts/step-diagnostics/measure-compare.py`: report less timings, entity
  census, ledger and surface tables sorted, per export, differences shown as an
  ordered diff. **0 differences over 104 exports is the bar for a refactor.**
  `build/measure/base` and `split` exist on this worktree as references.
- **`export`** writes both tiers - `<name>.stp` (both flags) and
  `<name>-exact.stp` (`--enable=step-analytic-surfaces` alone). Measure both; the
  suite is weakest at the exact tier. `STEP_DEV_LEDGER=1` dumps ledgers.
- **The ledger dump**: `OPENSCAD_STEP_LEDGER=<file.tsv>` on any export - one row
  per claim (facet, origin, holder, claimant, surface, kind, precision, evidence,
  fate, measured and carried ownership, centroid, why) and `<file>.surfaces.tsv`
  (every declared surface's parameters, owners and witnesses). Facet indices match
  between tiers, so tier differences are a join of two files.
- **The suite**: `ctest --test-dir build -R 'export-step-|mutations'`, 59 tests
  (via `step-dev.ps1 test`). Two are **`WILL_FAIL` on purpose** and report
  green while failing: `export-step-flagship-corners`,
  `export-step-representation`. (`step-region-pocket` was the third until
  2026-09-24, when it started passing - open item 30.)
  Unit tests: `build/OpenSCADUnitTests.exe` with `build/staging` on PATH, from
  the MSYS2 shell.
- **The OCP instruments and the round trip need `cadquery-ocp==7.8.1.1.post1`**
  in the interpreter CMake found (`step-dev.sh python`); without it they skip in
  silence and the suite goes green anyway.
- **Diagnostics**: `scripts/step-diagnostics/README.md`. First `fault-census.py`
  on our own file; `reader-diff.py` to compare two exports; `at-vertex.py` for
  what we wrote at a point; the SOLIDWORKS interop scripts with a known-faulty
  control in every run.

### Environment and traps, measured on this machine

- 16 cores, 64 GB; build with `-j12`. Cold build of a fresh worktree ~10 min;
  incremental ~2 min; touching `Surface.h` / `PolySet.h` rebuilds more.
- **A worktree has no submodules**: copy them from the main checkout and write
  each `.git` file as `gitdir: <the main checkout's own gitdir for it>` (module
  names are not always the path - read the main checkout's `.git` file).
- **`CMAKE_BUILD_TYPE` must be Release** (msys2-build.sh defaults to it).
  **Adding a source file or a fixture needs `msys2-build.sh --configure`**
  (cheap over an existing cache), plus an empty `-expected.txt` for a fixture.
- A freshly linked `build/pythonscad.exe` cannot run; the staged
  `build/staging/pythonscad.com` can.
- **Measure only after a build that succeeded** - gate on the exit status.
  `step-dev.ps1 build` shows three lines; on a failure run
  `scripts/msys2-build.sh` with the log kept and grep `error:`.
- **Git Bash has no `ctest`; MSYS2 has no `diff`.** From PowerShell pass file
  arguments to `step-dev` with forward slashes. C++ string literals: edit with
  the Edit tool, not a shell heredoc (it turns `\n` into a newline).
- **clang-format fails the first commit** after reformatting: stage and commit
  again. Never `--no-verify`.
- **The exporter is not byte-deterministic**: verify by content (the oracle),
  never by diffing files.
- A transient "Permission denied" moving `pythonscad-bundled-py` during staging
  is a file lock; rebuild.

---

## 5. What landed, in order

| commit | what |
| --- | --- |
| `18ba4071b` | migration step 1: every claim with fate and evidence; the census reads the catalogue; the ledger dump |
| `b72449eb3` `de85ea6f0` | ownership carried on `Surface` (`owners`), face vs evidence roles (`witnesses`) |
| `45f1b5f28` | ledger dump: centroids and `.surfaces.tsv` |
| `e71fcbbbf` | specimens declare the channel walls; one geometric surface is one record; `declare_*` over a Manifold is stamped |
| `3f61c9768` | the `measure` / `compare` oracle |
| `39dd119a3` | stage A: the recognition module, `FacetGraph` built once |
| `ab7ba551d` | stage C1: Bezier, grid, quadric propose purely |
| `b88adaf25` | stage B: recognition as a pipeline, out of the writer; `Writability` |
| `2d23146a7` | stage C2: stages with a checked table; the `Arbiter` |
| `80cd4e99f` | stage C3: propose as its own stage; `[purity]` unit test |
| `21888b1ba` | stage D: reports as ledger queries in `Reports.cc` |
| `d64877cad` | **migration step 3: the ledger is the source of truth** |
| `fefe50911` | one rule for what every claim carries (`recordClaim`, `Claimant`) |
| `5c87fdfe9` | one file per recogniser |
| docs | `a0edbf989` `45f7ef23f` `ecee6b77a` `c150acc43` `4c45a44d9` `f5dfabfec` `1698d3779`, and this handover |

Every structural commit: 0 differences over 104 exports by the oracle, suite 58
of 58, unit tests unchanged.

---

## 6. Next steps, in order

1. **Retry the parked provenance switch** (`73a42f3aa`, identity plus D0
   presence) on top of this branch. Its only known failure - originals 26-32 -
   is fixed at the origin. Judge it by `flagship-strict`, `step-cylinder-cross`,
   `step-declare-cone`, `step-shared-arc` and `compare`. It changes behaviour:
   it lands with its numbers or parks again. Open item 31.
2. **Migration step 4 - the policy, in the arbiter's table.** Move the grant
   decision (`held() || taken` in the `form*` functions) into the arbiter,
   keyed on provenance first, then determinacy (weakest needed class, freedom
   left, corroboration), judged globally with stranding priced in. One landing at
   a time, each with a fixture that asserts it first. The design's
   shuffled-order test ("run the recognisers in a shuffled order; the ledger must
   be identical") becomes writable here - write it.
   - **First candidate: open item 30**, exchange-never-surrender: a claim may
     not take D0 ground below D0. Fixture: a declared cylinder whose exact-tier
     region is a clean sheet and whose band-widened region has a fringe corner
     past the region band; assert the quadric face at both tiers.
   - **Then R0** (open item 29): the region from provenance's positive answer
     (carried ownership plus presence), with the band pass split into propose and
     form so a whole-region claim can beat a ring only among claims that will be
     written. Judged by `step-region-pocket`'s derived `4185*pi` and
     `step-region-beyond-the-band`'s exact-tier coverage.
3. **C0 and the refusals** (open item 27), the two 4x-median statistics first.
4. **Enforcement**: make an honoured claim whose evidence class is below its
   precision an `EXPORT-ERROR` once policy stops producing them; open item 32
   (the sanity test checks `EXPORT-ERROR`).
5. **Feedstock**: the 2D channel - one derivation for both extruders.
6. The boundary-run survey; determinism (item 28); items 21, 23, 24.

---

## 7. The specimens

lid10 and the bayonet are **specimens, not deliverables**: where the ugly
geometry was found, not the thing optimised. If the ladder is right, they fall
out of it.

| part | how |
| --- | --- |
| `lid10` | `examples/step_test/lid10.scad` **with its customizer**: `-p examples/step_test/lid10.json -P "New set 1"` (as an argv list; `step-dev export lid10` does it). Now declares its bayonet channel's walls |
| `bayonet` | `examples/step_test/bayonet_container_v1-2.scad`, same treatment; exports `_part = base` |
| `f02` / `f04` | `step-band-family.scad` at `-D FN=32` / `-D FN=64` |

`base_coupler` is the user's own part, outside the repo, in declared and
undeclared variants.

---

## 8. Priming prompt

Paste this into a new session, adjusting only the worktree path.

```text
You are continuing work on the PythonSCAD STEP exporter, in a worktree of
E:\29_Git_Soulsolistice\pythonscad. Branch: claude/step-claim-ledger (built
on claude/step-claim-two-pass-cf4565), nothing pushed, tree clean, suite 58 of
58. If your worktree was cut from master, branch from claude/step-claim-ledger
(do not reset); copy the submodules from the main checkout, writing each .git
file as the main checkout's own gitdir for it.

Read first, in this order, and in full: doc/step-export-next-session.md (the
handover - rules, what was understood, what was built, tools, next steps), then
doc/step-recognition-design.md and doc/step-rungs.md. Those two are MANDATORY
and NOT NEGOTIABLE: they are the foundation of this work, earned over many
sessions, and every change to recognition or to a rung is justified against
them. Before building anything, find the paragraph that already covers it - two
sessions running lost most of a day rediscovering what they already said. Then
doc/step-export-development.md (what is settled, testing doctrine, traps) and
doc/step-export-wip.md (open items 30, 31, 32, 27, 29).

PURITY IS NOT NEGOTIABLE. Recognition now runs as a pipeline in
src/geometry/recognition/: band -> fit -> propose (pure) -> Bezier -> grid ->
quadric -> leftover, with a stage table whose declared reads/writes are checked
every run, one Arbiter that is the only place a facet becomes held, the ledger
as the source of truth (migration step 3 done; `consumed` is a checked cache),
every claim recorded through one rule with its precision and evidence class,
and reports as ledger queries. Continue in that shape: no pass reads
`consumed`, no grant outside the arbiter, no undeclared stage, no report inside
the writer. Refactoring is endorsed and wanted when it serves this purpose; each
step is proven by the oracle when neutral and by a fixture when not.

The foundation: a closed form is exact and has to stay - D0-D4 is a covering
order, a closed form may be exchanged, never surrendered, a rung is as exact as
its evidence, a D4 never feeds a D0-D2. A D0 with CORRECT PROVENANCE is the key.
`consumed` is not correct: not the most precise, not necessarily the right D0,
and a wrong holder strands facets. An entity is as precise as the independent
closed forms that fix it (weakest needed class, freedom left, corroboration),
judged GLOBALLY over the whole cover. Wherever a less precise rung holds ground,
ask why. Root-cause fixes only. Intent cannot be recovered from geometry - it is
declared.

Tooling - the dev loop is committed, do not write build or export scripts:

  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 build
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 test
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 export lid10
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 census <file.stp>
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 measure <name>
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 compare <a> <b>

measure + compare is the behaviour-neutral oracle: 0 differences over 104
exports (report, entity census, sorted ledger) is the bar for a refactor.
OPENSCAD_STEP_LEDGER=<file.tsv> dumps every claim with fate, evidence class,
ownership and centroid, plus the declared surfaces. export writes both tiers;
measure both. Measure only after a build that succeeded. Never run two builds
or two suites at once. Two suite tests are WILL_FAIL on purpose. Every
diagnostic is catalogued in scripts/step-diagnostics/README.md; the OCP tools
need cadquery-ocp==7.8.1.1.post1 and skip in silence without it.

lid10 is a SPECIMEN, not a deliverable; a finding counts once a synthetic
fixture isolates it. It needs its customizer: -p examples/step_test/lid10.json
-P "New set 1".

Where to start: (1) retry the parked provenance switch (identity plus D0
presence, claude/step-provenance-identity-parked 73a42f3aa) on top of this
branch - its only known failure, lid10's undeclared bayonet channel, is fixed in
the model now. (2) Then migration step 4: move the grant from the form*
functions into the arbiter's table, provenance first then determinacy, one
landing at a time with a fixture first, starting with open item 30
(exchange-never-surrender), then R0 with the band pass split into propose and
form. Write the design's shuffled-order test when the policy stops being call
order.

Derive expectations from the model, never from a kernel, and never calculate
anything from a faceted input. Mutation-check every new assertion and every
zero. Know what each instrument cannot see. Never --no-verify.
```
