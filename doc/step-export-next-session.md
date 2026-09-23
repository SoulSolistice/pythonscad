# STEP export: handover, 2026-09-23

Branch `claude/step-claim-ledger`, suite **58 of 58**, nothing pushed, working
tree clean. It continues `claude/step-claim-two-pass-cf4565` with the session of
2026-09-23 - read **"The session of 2026-09-23"** directly below first; sections
0-8 underneath are the previous handover and still hold.

## The session of 2026-09-23 - read this first

**What landed** (each behaviour-neutral, proven on 104 exports - lid10, bayonet,
f02, f04 and all 48 fixtures, both tiers - by identical report and entity
census, suite 58 of 58):

| commit | what |
| --- | --- |
| `18ba4071b` | **Migration step 1 finished.** Every pass records every claim it would make, once, with a `ClaimFate` (honoured / withdrawn / held / declined / refused, with the rule) and an `EvidenceClass` (D0-D4, weakest of what decided it). The band pass names its surface as soon as its walk is confined and records every shape refusal; the Bezier pass examines held facets; each writer records every patch it does not write. The census now reads the catalogue: *why N facets are fitted or left to the tessellation*, by rule. `OPENSCAD_STEP_LEDGER=<file>` dumps the ledger as a table |
| `b72449eb3` | **Provenance carried, not measured.** `Surface::owners` is stamped with the Manifold original ids where they are minted and carried through every boolean; reported against the old position-derived `owned`, which still decides |
| `de85ea6f0` | **Face or evidence.** A record now says whether its declarer is made of it (`owners`) or offers it as evidence (`witnesses`) - the frustum's rims, a sloped revolve edge's radii, a taper's rims |

**The foundation, recorded in `doc/step-recognition-design.md`, *Determinacy*:**
a D0 with **correct provenance** is the anchor; an entity is as precise as the
**independent** closed forms that fix it (weakest needed class, freedom left,
corroboration - algebraic independence, not a count of names); determinacy is
judged **globally** over the whole cover, stranded facets included; and wherever
a less precise rung holds ground the question is *why*. Claims that follow the
rung from D0 upward, decided by provenance, evidence and precision, dissolve the
boundary violations.

**What the catalogue showed** (lid10 until fixtures isolate it):

- **Open item 30** - the approximation tier surrenders 188 facets of declared
  cylinder 12 that the exact tier writes as a quadric face; proven by joining the
  two tiers' ledger dumps facet by facet.
- **Open item 31** - provenance's ownership was measured. Identity overturns it
  on 180 held band walls; `step-cylinder-cross` isolates the measured rule's
  failure (crease slivers "own" the other cylinder). The switch to identity is
  parked on `claude/step-provenance-identity-parked`: it fails
  `flagship-strict`, because identity says *declared* and junctions need
  *present* too - a D0 presence test, the other half of the positive query R0
  needs.
- **Open item 32** - the fixture sanity test never checks `EXPORT-ERROR`.
- The code as it stands breaks the two mandatory documents in eight named ways,
  each now counted by the ledger; the list and its numbers are in
  `doc/step-recognition-design.md`, *Step 1, finished*.

**Next, in order:**

1. **The D0 presence test**, then the provenance switch again, judged by
   `flagship-strict`, `step-cylinder-cross`, `step-declare-cone` and
   `step-shared-arc`.
2. **The structural refactor** - wanted, and warranted by what step 1 cost (a
   claim's fate is decided in three places): a `src/geometry/recognition/`
   module of `FacetGraph`, `Provenance`, pure `Proposer`s, an `Assembler` that
   asks writability *before* arbitration, and an `Arbiter` whose first policy is
   today's call order as a table. Oracle for every structural step: the
   **honoured assignment per facet identical on all 104 exports**, from the
   ledger dump - far stronger than the suite.
3. Then policy, one landing each with a fixture first: provenance-first regions
   (R0), exchange-never-surrender (item 30), D4 tests out of D0 membership, the
   4x-median statistics (item 27), the global objective.

**Tooling added this session:** the ledger dump; a measurement loop that exports
the four parts and every fixture in both tiers into `build/<name>/` and compares
report (minus the ledger's own and timing lines) and entity census - a scratch
script, worth committing as `scripts/step-dev.sh measure` next.

---

## 0. Read these first, and build on them. This is not negotiable

Two documents are the foundation of this work. They were earned over many
sessions of debugging, and **every change to recognition, to a rung, or to what
the exporter writes must be justified against them**:

| | |
| --- | --- |
| **`doc/step-rungs.md`** | **MANDATORY.** What determines each entity, and therefore which rung it is. The D0-D4 classes, the covering order, and the inventory of curves, surfaces, regions and corners. |
| **`doc/step-recognition-design.md`** | **MANDATORY.** The claim ledger, Propose / Arbitrate / Assemble, region before representation, and the five-step migration plan. |

**They are not background reading and they are not advisory.** Work goes
*inside* the design's numbered migration steps, not beside them. Before building
anything, find the paragraph that already covers it - and if a result does not
fit the system, **the question that always has to be answered is *why***,
because getting here has repeatedly shown that an unanswered "why" becomes a
workaround where an exact solution existed.

Also worth reading, in this order:

- **`doc/step-export-development.md`** - what is settled: the declaration
  channel, the boundary machinery, the corner arbiter, kernel behaviour, the
  testing doctrine, and the traps and refuted claims.
- **`doc/step-export-wip.md`** - open items (21, 23, 24, 25, 26, 27, 28, 29),
  unexplained results, the roadmap, and the standing rules.

**The last session's own lesson, stated because it cost most of a day:** it
attempted R0, reverted it, and then rediscovered things
`doc/step-recognition-design.md` already said. R0's region test was specified
there - *"the facets whose maker owns this surface and which lie on it,
connected"* - and was implemented with a tessellation band instead. A
declaration census was then built which re-derived *"Why is this facet a
plane? read the ledger"*, *"order is policy"*, and the claim's own `evidence`
field.
That document's consequence 5 names the exact temptation: *"Every new answer
costs plumbing... the reason this document exists rather than a sixth feature."*

### Big refactors are welcome

**This is explicit permission.** Turning the recognisers into classes, building a
graph-shaped recognition pipeline, replacing the 4000-line function, making
Propose pure - **all of it is accepted, and wanted, where it serves
`doc/step-recognition-design.md`.** The design asks for exactly that in its step
5, and the four free functions sharing a mutable `consumed` are the reason five
named consequences exist.

What is *not* accepted is a wholesale rewrite validated only against the suite:
the document says why - *"a wholesale rewrite validated against it is how a
subtle regression ships"* - and the reverted R0 proved it by passing 56 of 56
while turning a declared cone into planes. Refactor in the plan's steps, each
one verifiable on its own, each landing with its numbers.

---

## 1. The doctrine

### 1.1 The ladder is a covering order, and nothing falls down it

**The core, and everything else is a consequence:**

> A closed form is exact by definition and has to stay.

The classes are a **covering order**, not a tie-break. Every D0 representation is
covered first, then D1 takes what is left, then D2, D3, and D4 takes only what
nothing above it could. A lower rank never drives a higher one. An entity already
written in closed form may be **exchanged for another closed form and never
surrendered** to a chord or a facet plane.

### 1.2 The representation fits the region, never the region the representation

> A recogniser decides *which* facets it takes - and a writer decides *which
> curve* it writes - by what the **declaration** covers, never by what its own
> form can express.

Attempted as R0 on 2026-09-22 and reverted. Still the right rule; the
implementation was wrong in two ways, both instances of 1.1.

### 1.3 Catalogue first, arbitrate second

Phase one **catalogues** who claims what and on what evidence; phase two
**arbitrates**. Precedence lives in a table, not in the order passes run in.

**The detector is mechanical.** Any flag that makes a claimed thing invisible to
a later claimant is an arbiter with a one-line policy and no record:
`consumed[f]`, `taken[f]`, `moves.count(v)`. Four such sites remain in
recognition: `AnalyticFeatures.cc:1170, 1806, 2282, 2652`.

**Phase one is the half that matters.** An arbiter can only choose between claims
that were *made*.

### 1.4 Decide after the pass has finished claiming - claim, then revise

A region test asked *inside* the band pass's seed loop judges each band against a
state the pass has not finished producing. A sphere is a *stack* of bands, so at
the moment a ring is claimed its neighbours are siblings that do not exist yet:
every ring looked truncated, the drops cascaded, and `step-sphere` exported as
482 planes with 448 corners off their own surfaces. **If a test asks "what would
this pass leave behind", it may not run inside that pass.**

### 1.5 Qualification: rank by what *determines* the answer

| class | meaning |
| --- | --- |
| **D0** | fixed by declarations alone: closed form, no fit, no tolerance, no sample |
| **D1** | the model's stations through an interpolant that publishes a band |
| **D2** | fitted to declarations at a stated tolerance |
| **D3** | fitted to the mesh |
| **D4** | the tessellation itself - a chord, a facet plane |

- **A D4 may never be an input to a D0-D2.** Outside the corner arbiter this is
  an honour system, and it has now failed once, expensively.
- **A rung is as exact as its evidence.** Deciding *about* a declared surface
  does not make a rule D0. R0 was called D0 because a declared surface was in
  the sentence; what decided it was `bandOf`, and that fixed its class.
- **A D0 carries no accuracy threshold.** Float equality for a declared point
  against the surface that emitted it (`runsThrough`'s 1e-7) is not a threshold;
  a *band* admitting points known to be off the surface is.
- **Each primitive has its own ladder** - C0-C6, S0-S3, R0-R2, corners' two
  tables. They do not borrow one another's evidence, and a decision on one may
  not demote an entity on another.
- **A rung may never manufacture a boundary.** C5 splits one declared surface
  against itself 78 times on lid10 - a curve rung reaching into the surface
  ladder. That is what C0 is for.
- **Consuming less is not a fault when the reason is exactness.** Ask *why* a
  rung declined, not how much.

### 1.6 Provenance is identity, not measurement

`Provenance` answers only `excludes` today. Its **positive** form is what R0
needs and does not exist; the answer is already derived in `export_step.cc` as
`covered` / `covered_cut`.

### 1.7 The standing rules

Kernel agreement is not proof. Exactness takes precedence. Magic numbers and
statistical thresholds need explicit approval. Half a fix parks, with its
measurements. Five whys with named branches, and record the refuted ones.

---

## 2. lid10 and the other specimens

**lid10 is a specimen, not a deliverable.** It is where the ugly geometry was
found, not the thing being optimised, and `doc/step-export-development.md`,
*Scope*, has said so since it was written. **A finding on it counts only once it
has become a synthetic fixture that isolates it** - which is why
`step-t-junction`, `step-oblique-trim`, `step-declare-cone`,
`step-region-beyond-the-band` and `step-region-pocket` exist.

If the ladder is complete and correct, **the reference parts fall out of it**.
There is nothing about them to hunt. Do not spend a session fixing lid10.

The four reference parts, none of which is a fixture:

| part | what it is |
| --- | --- |
| `lid10` | `examples/step_test/lid10.scad`. **Needs its customizer**: `-p examples/step_test/lid10.json -P "New set 1"`, passed as an argv list. Without it you export the default component and every number is incomparable with anything recorded. |
| `bayonet` | `examples/step_test/bayonet_container_v1-2.scad`, same customizer treatment |
| `f02` / `f04` | `step-band-family.scad` at `-D FN=32` / `-D FN=64`. The *fixture* only ever runs at its declared `FN = 32`. |

`base_coupler` is the user's own part, outside the repo, kept in declared and
undeclared variants.

---

## 3. Tooling: how to compile, test and measure

### The dev loop is committed. Do not write build or export scripts

```powershell
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 build
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 test
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 export lid10
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 census <file.stp>
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 flagship
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 python
```

`scripts/step-dev.sh` is the same from bash; the `.ps1` sets MSYS2 up and passes
arguments untouched, which matters because lid10's `-P "New set 1"` does not
survive PowerShell's quoting and then bash's.

- **Invoke the `.ps1` from PowerShell with backslashes.** A bash tool eats them
  and PowerShell then reports the file as missing.
- **Verify with `test` before and after any change.** 58 of 58.
- **Never run two builds or two suites at once** in one build directory.
- `export <part>` writes **both tiers**: `<name>.stp` with both feature flags
  and `<name>-exact.stp` with `--enable=step-analytic-surfaces` alone.
  `STEP_DEV_TIER=approx` skips the second. **Measure both** - a change can be a
  pure win with both flags and a regression with only the analytic one, which is
  exactly how the reverted R0 passed the suite.

### What the loop does underneath

`scripts/msys2-build.sh` - configure (only when asked or when there is no
cache), build, bundle the runtime Python, `cmake --install` to `build/staging`,
then test with the staging DLLs on `PATH`. A freshly linked
`build/pythonscad.exe` **cannot run**; the staged one at `build/staging/` can.
Run it from the staging root, never `staging/bin`.

Non-interactively from PowerShell:

```powershell
$env:MSYSTEM='UCRT64'; $env:CHERE_INVOKING='1'
C:\msys64\usr\bin\bash.exe -lc "cd /e/path/to/pythonscad && ./scripts/msys2-build.sh --configure"
```

`-l` matters: the login profile is what puts `/ucrt64/bin` on PATH.

### Environment facts, measured on this machine

- 16 cores, 64 GB. A **cold** build of a fresh worktree is ~10 minutes of
  compiling plus bundle and install; incremental ~2 minutes; the suite
  ~4 minutes.
- **A configure over an existing cache is cheap** - about 12 targets after a
  `tests/CMakeLists.txt` change. What costs an hour is a cold cache or one whose
  *settings* changed.
- **`CMAKE_BUILD_TYPE` must be Release.** An empty build type runs 26x slower
  with byte-identical output.
- **A worktree has no submodules and no `libraries/MCAD`.** Copy them from the
  main checkout and repoint each `.git` file at `<main>/.git/modules/<path>`;
  do not re-clone.
- **Adding a fixture or test needs `--configure`**, or ctest never sees it, plus
  an empty `-expected.txt` in `tests/regression/export-step-sanitytest/`.
- **`cadquery-ocp==7.8.1.1.post1`** must be in the interpreter CMake found
  (`step-dev.sh python` prints it). 8.0 no longer exposes
  `TopTools_IndexedMapOfShape`, so the round trip **skips in silence** and the
  suite goes green without it.
- The exporter is **not byte-deterministic**: the same binary exports the same
  model twice with different entity numbering. Verify a behaviour-neutral change
  by *content* - the report, the entity census, the derived volumes - never by
  diffing files.

### The suite

```bash
ctest --test-dir build -R 'export-step-|mutations'    # 58 tests
```

That regex and not `-R step`, which drops `bspline-check-mutations` and
`closed-sphere-check-mutations` - the harnesses that prove the other tests would
fail if the defect came back.

**Two of the 58 fail on purpose and report green**, registered `WILL_FAIL`:
`export-step-flagship-corners` and `export-step-sanitytest_step-region-pocket`.
ctest reports them *passed* while they fail and will report *failed* the day
they start passing, which is how those fixes announce themselves. Do not delete
them and do not read their green as the defect being gone.

---

## 4. The analytic helpers in `scripts/step-diagnostics`

**Every diagnostic is catalogued by the question it answers** in
`scripts/step-diagnostics/README.md`. **Read it before writing a new one** - a
new instrument completes the committed catalogue, it is not a session snippet.
`(OCP)` means it reads the file back through OpenCASCADE and needs
`cadquery-ocp`; a text tool sees what we wrote, an OCP tool sees what a kernel
made of it after healing.

**Run first, on our own file:**

| tool | answers |
| --- | --- |
| `fault-census.py <file>…` | every property known to become a SOLIDWORKS fault, keyed by the code it becomes: 7, 16, 13, 21, 17 (OCP), seam crossings |

**One relation at a time:**

| tool | fault | answers |
| --- | --- | --- |
| `vertex-on-curve.py` | 7 | every vertex against the curve its own edge is written as |
| `vertex-owners.py` | 16 | every vertex against the surface of every face using it |
| `point-vs-faces.py` (OCP) | 16 | one point against every face using it, **B-splines included** |
| `pinched-loops.py` | 21 | the faces behind the census's code-21 count |
| `edge-on-surface.py` (OCP) | 17 | every edge sampled along its length against both faces |
| `sweep-chords.py` (OCP) | 17 | line edges on a sweep face, split by whether a curve could replace them |
| `planes-at-sweep.py` (OCP) | 17 | is this plane the sweep's own facet left unclaimed? |
| `seam-straddle.py` | — | does a face's loop cross the seam of a surface closed in `v` |
| `edge-lengths.py` | 13 | edge-length distribution |
| `face-bad-edge.py` (OCP) | 11/16/17 | per face, every quantity SOLIDWORKS codes separately |
| `face-boundary-deviation.py` (OCP) | 16 | per face, how far its boundary sits from its surface |

**Locating a fault once counted:** `near-edge.py`, `at-vertex.py` (what did we
write at this point?), `face-corners.py`, `reader-built.py` (OCP - edges the
reader invented).

**Comparing two files:** `reader-diff.py` - what changed between two STEP files
whose ids do not correspond. This is the one that found the three planar chamfer
facets behind open item 26's residual 1.0489 mm3.

**Interrogating a reader:** `../step-interop-solidworks.ps1` with `-Raw
-RoundTrip [-Heal] -FaultDetail`, `../step-interop-sw-roundtrip.py`,
`../step-interop-faultmap.py`, `../step-interop-crosscheck.py`. Always include
the control `f02-band-fn032-analytic.stp` (`faults=2 faultyfaces=2 codes=17`).
Import **raw** and round-trip as a matter of course; read the fault list last.

**Fixtures' own harnesses:** `band-family-controls.scad`,
`band-family-volumes.py`. And the suite's: `tests/validatestep.py` (well
formed), `tests/steproundtrip.py` (OCP, a kernel reads it back as a solid),
`tests/step-flagship-check.py`, `tests/step-representation-check.py`.

---

## 5. The diagnostic method

1. **The census on our own file** - `fault-census.py`.
2. **The export's own report, and the declaration census first**: of everything
   the model declared, how much is written as a face, how much the booleans
   removed, how much is unexplained; then the per-facet class line; then the
   ledger's agreement and any split surface. **Know what each line cannot see** -
   the split warning counts only facets another recogniser *claimed*, so a
   region left planar never appears in it.
3. **The catalogued instruments**, by question.
4. **Measure both tiers.** The suite cannot see an exact-tier loss on a
   reference part.
5. **Derive the expectation from the model.** Never integrate the tessellation
   when the exporter writes the analytic surface. **Never assert or calculate
   anything from a faceted input** - it is wrong however carefully it is taken.
   Where no absolute is available, assert a *property*.
6. **A kernel, last.** Both OpenCASCADE and SOLIDWORKS called a file with a
   1.2 mm3 error `valid=1 faults=0`.
7. **Mutation-check every zero and every total.** A zero from a new instrument
   is not a result.

**How to read a result** - `doc/step-rungs.md` has the section, and it matters:
a *rising* fault census is not by itself a regression (greedy consumption hides
edges that never lay on their face, and exactness exposes them), a census that
does **not** move is not a statement that nothing moved, and coverage is not the
criterion. Watch closed-form coverage per declared surface and the derived
volume.

---

## 6. What was measured (do not re-derive)

### The R0 attempt, both directions

Parked on `claude/step-region-r0-optimistic-parked`. Membership decided with
`min(bandOf(facet), max_off)` - a D4 yardstick for a D0 rung.

**Bought**, approximation tier: lid10's one split surface gone; 913 facets
covered analytically before and after; `CIRCLE` 300 → 410; census identical;
`step-split-surface` volume 15646.585807 → **15647.634695**, the model's own
closed form to 1.9e-11; `step-bored-cylinder` 8 → 4 cylinder faces at the same
derived volume.

**Cost**, exact tier on lid10: `Cone` faces 9 → **8**, `Plane` 1574 → 1630,
`CIRCLE` 413 → 411, `LINE` 3466 → **3633**. Two regions given up, the
trimmed-quadric writer refused them under *"half a surface is worse than
none"*, and 56 facets that had been an exact conical face came out as planes.
**A closed form was surrendered, not exchanged.**

### The declaration census, built 2026-09-22

Four fates per declaration - written / evidence / no mesh left / **unexplained**,
which is the bug report - plus a per-facet class line. Current:

```text
lid10   exact   25 surfaces: 13 written, 1 evidence, 7 no mesh, 4 unexplained
                cylinder 43, cone 54, cylinder 56, sweep 65
                of those 4, 2 have mesh held under another surface's name
        approx  3 unexplained - cylinder 12, cylinder 43, cylinder 56
bayonet exact   23 surfaces: 11 written, 0 evidence, 7 no mesh, 5 unexplained
                cylinder 24, cylinder 60, cylinder 67, cone 78, sweep 87
                of those 5, 3 have mesh held under another surface's name

by facet, lid10  exact   909 declared surface / 27 declared plane / 0 fitted / 1521 tessellation
                 approx 1671 / 27 / 0 / 759
step-approximate-report  exact 0/2/0/2048     approx 0/2/2048/0
```

The last row is the tier in one line and is what proves the fitted bucket fires.
`step-cube`, `step-bored-cylinder` and `step-region-pocket` report **zero**
unexplained.

**The old headline was mostly not a defect.** *"lid10 declares 41 planes and
writes 27 planar faces on a declared plane"* - of those 41, 10 are written, 31
have no mesh left, **none is unexplained**. The plane channel is clean.

**One number is deliberately absent**: how many fitted or tessellated facets had
a declaration *available*. `owned` names an original's surfaces, not a facet's,
so it answers "all of them" - measured, and taken back out rather than shipped.

### The claim trinity was malformed

`Claim { facet, surface, precision, by, evidence }` is worth what its fields are
worth, and two were wrong at the **honoured** sites - the only ones anything acts
on. A band's cap named no surface at all, taking `surface_index`'s
`std::size_t(-1)` default, which nothing can tell from a claim on a *fitted*
surface; the census duly reported step-sphere's polar caps as *"on a surface the
approximation fitted"* at the **exact tier**, where nothing can be fitted. And
the honoured claims hardcoded their precision, so **a fit that won was recorded
as a declaration** - which is why `ClaimLedger::misheld` has read 0 on every part
ever measured. Both fixed; an `EXPORT-ERROR` now fires on any honoured claim
that cannot name its surface.

**What it did not change:** lid10's 452 contested facets are still 452 ties of
equal precision. A tie means the rung system has not distinguished the two
claimants, and no labelling will - a band's wall and a quadric face are the same
precision on the same declared surface because it is not a precision question.
It is the representation question.

### Other standing results

- **C0 and R0 are not one defect** - as far as the *attempt* goes. It removed
  every split surface and left C0's seam edges where they were: 78 on lid10, 18
  on the bayonet, 31 on f04. But that attempt's region came from the
  tessellation band; whether a **provenance** region moves C0 is untested, and
  that measurement settles the order of the next two pieces of work.
- **Refusing the facet-plane conics makes it worse**: code 17 went 42 → 198 on
  lid10, because the chord they fall back to is further off the face.
- **Neither kernel can adjudicate geometry.**

---

## 7. Next steps, to advance the rung system

**The goal is the ladder, complete across every entity and every class.**
`doc/step-rungs.md`, *What completing the system means*, is the shape: breadth
(every entity has a ladder - the **boundary run** does not), depth (every rung
names its class *and its evidence's class* - nothing checks this), feedstock
(every primitive declares - the 2D channel is the gap).

1. **Finish step 1 of the design's migration plan.** *Every pass records its
   claims into the ledger while `consumed` still decides everything* - additive,
   behaviour-neutral, and the suite must not move a number. What is missing,
   measured: `recogniseQuadricPatches` records a claim only when someone **else**
   holds the facet, so one it claims and then drops to its own span rule or
   `worst > band` leaves no entry; and the band pass, which runs first and takes
   536 facets on lid10, records only what it honours. Extend it from *what a
   pass lost* to **every claim a pass would make, honoured or not**.

   This is what turns the census from a reconstruction of `consumed` into a read
   of the catalogue, and it supplies the number the census cannot: under a pure
   Propose, a claim naming a declared surface *is* the evidence that a
   declaration was available on that facet.
2. **Step 3 - flip the source of truth** from `consumed` to the ledger. Not one
   number in the suite may move.
3. **R0 properly** (open item 29). The region from provenance's positive query,
   carrying no tolerance; and *exchanged, never surrendered* -
   `boundary_lies_on_surface` is computable on a candidate patch **before** the
   band is dropped. Judged by `step-region-pocket`'s derived `4185*pi` and
   `step-region-beyond-the-band`'s exact-tier coverage.
4. **C0 and the refusals** (open item 27). Two of the refusals are **4x-median
   statistics** - a yardstick made of the error it grades, the ladder's own
   forbidden shape. Take those first.
5. **Enforcement.** The ledger carries a claim's `ClaimPrecision` but not the
   class of its *evidence*, so a claim can call itself DeclaredExact while a
   tessellation band decided it. Carry both; make the mismatch an
   `EXPORT-ERROR`. It would have refused the reverted attempt on its first
   export.
6. **Feedstock: the 2D channel, one derivation instead of five guards.** See
   `doc/step-export-wip.md`. `Outline2d` half-carries it already (arcs survive,
   which is why `offset(r = 2)` corners come out as cylinders); finishing it
   lets both extruders answer from **one total function** - segment class and
   operation in, surface out, every class enumerated, no silent `continue`.
   `linear_extrude(twist=)` becomes one branch rather than a refusal; the
   ellipse item dissolves (an extruded ellipse is a
   `SURFACE_OF_LINEAR_EXTRUSION` over a curve we already write); the revolved
   segment meeting its own axis stops being a wrong refusal. And
   `cylinder(r1=10, r2=0, h=20)` writing a cone while the identical solid
   revolved writes 96 planes becomes impossible - **a property assertion needing
   no ground truth: the same solid by two routes must declare the same
   surfaces.** `polyhedron()`, `import()`, `surface()`, `projection()` are *not*
   on this list: no intent to read, a boundary rather than a gap.
7. **Survey the boundary run**, the one entity with no classed ladder.
8. **Open item 28 - determinism.** Export one model a hundred times, diff the
   *reports*.
9. **The rest**: lid10's two code-21 faces; item 21's corner; item 23's 1%
   volume question; item 24's flagship duplication, banked.

---

## 8. Priming prompt

Paste this into a new session, adjusting only the worktree path.

```text
You are continuing work on the PythonSCAD STEP exporter, in a worktree of
E:\29_Git_Soulsolistice\pythonscad. Branch: claude/step-claim-two-pass, suite
58 of 58, nothing pushed, tree clean.

Read first, in this order: CLAUDE.md (build, test, the dev loop), then
doc/step-export-next-session.md in full, then **doc/step-rungs.md and
doc/step-recognition-design.md**. Those last two are MANDATORY and NOT
NEGOTIABLE: they are the foundation of this work, earned over many sessions,
and every change to recognition or to a rung must be justified against them.
Work inside the design's numbered migration steps, not beside them. Before
building anything, find the paragraph that already covers it - a previous
session lost most of a day rediscovering what those documents already said.
Then doc/step-export-development.md (what is settled) and
doc/step-export-wip.md (open items 21, 23, 24, 25, 26, 27, 28, 29).

Big refactors are welcome: recognisers as classes, a graph-shaped pipeline, a
pure Propose phase, replacing the 4000-line function - all accepted and wanted
where it serves doc/step-recognition-design.md, which asks for exactly that in
its step 5. What is not accepted is a wholesale rewrite validated only against
the suite; the reverted R0 passed 56 of 56 while turning a declared cone into
planes.

The philosophy everything rests on: a closed form is exact by definition and
has to stay. The D0-D4 classes are a covering order - D0 is covered first, then
D1 takes what is left, then D2, D3, and D4 only what nothing above it could. A
lower rank never drives a higher one and nothing falls down the ladder: a
closed form may be exchanged for another closed form, never surrendered. A rung
is as exact as its evidence, and a D0 carries no accuracy threshold.

Tooling - the dev loop is committed, do not write build or export scripts:

  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 build
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 test
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 export lid10
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 census <file.stp>

Invoke the .ps1 from PowerShell with backslashes. `export` writes both tiers -
<name>.stp with both feature flags and <name>-exact.stp with the analytic flag
alone - and you must measure both: the suite cannot see an exact-tier loss on a
reference part. Verify with `test` (58 of 58) before and after any change.
Never run two builds or two suites at once. A worktree has no submodules: copy
them from the main checkout and repoint each .git file at
<main>/.git/modules/<path>.

Every diagnostic is catalogued by the question it answers in
scripts/step-diagnostics/README.md - read it before writing an instrument.
fault-census.py first on our own file; reader-diff.py to compare two exports;
at-vertex.py for what we wrote at a point; the (OCP) tools need
cadquery-ocp==7.8.1.1.post1 in the interpreter CMake found, and skip in
silence without it.

lid10 is a SPECIMEN, not a deliverable - where the ugly geometry was found, not
the thing being optimised. A finding on it counts only once it is a synthetic
fixture that isolates it. If the ladder is right, lid10 falls out of it. It
needs its customizer: -p examples/step_test/lid10.json -P "New set 1".

Where to start: finish step 1 of the design's migration plan - every pass
records its claims into the ledger while `consumed` still decides everything,
extended from "what a pass lost" to every claim a pass would make, honoured or
not. Measured gap: recogniseQuadricPatches records a claim only when someone
else holds the facet, and the band pass records only what it honours. That is
additive and behaviour-neutral, and it is what turns the declaration census
from a reconstruction of `consumed` into a read of the catalogue. Then step 3
(flip the source of truth), then R0 properly (open item 29), then C0's refusals
(open item 27), starting with the two 4x-median statistics.

Derive expectations from the model, never from a kernel, and never calculate
anything from a faceted input. Mutation-check every zero an instrument reports.
Know what each instrument cannot see. Do not verify a behaviour-neutral change
by diffing two exports - the exporter is not byte-deterministic.
```
