# STEP export: handover, 2026-09-22

Everything a new session needs: the doctrine as it now stands, what was
measured, what to do next, the tooling, the diagnostic method, and a priming
prompt to paste.

Branch `claude/step-claim-two-pass`, suite **58 of 58**, nothing pushed.

**This session attempted R0, reverted it, and then spent the rest of itself
rediscovering things `doc/step-recognition-design.md` already said.** That is
the session's main lesson and it is worth more than its output:

> Before building anything in recognition, find the paragraph in
> `doc/step-recognition-design.md` that already covers it. R0's region test was
> specified there (*"the facets whose maker owns this surface and which lie on
> it, connected"*) and was implemented with a tessellation band instead. The
> declaration census re-derived *"Why is this facet a plane? - read the
> ledger"*, *"order is policy"*, and the claim's own `evidence` field - the
> document's consequence 5 names that exact temptation: *"Every new answer costs
> plumbing... the reason this document exists rather than a sixth feature."*

The plan in section 3 is therefore expressed in **that document's own numbered
migration steps**, not in parallel ones. The exporter's behaviour is what it was
at `006ab3b45` plus one behaviour-neutral refactor and the census; what else is
new is measurement and a sharper statement of the ladder's philosophy.

---

## 1. The doctrine

### 1.1 The ladder is a covering order, and nothing falls down it

**This is the core, and everything else is a consequence of it:**

> A closed form is exact by definition and has to stay.

So the classes are a **covering order**, not a tie-break. Every D0
representation is covered first, then D1 takes what is left, then D2, then D3,
and D4 takes only what nothing above it could. A lower rank never drives a
higher one. An entity already written in closed form may be **exchanged for
another closed form and never surrendered** to a chord or a facet plane.

Days of debugging are in that sentence. `doc/step-rungs.md` is the survey that
applies it entity by entity; read it before adding or changing a rung.

### 1.2 The representation fits the region, never the region the representation

> A recogniser decides *which* facets it takes - and a writer decides *which
> curve* it writes - by what the **declaration** covers, never by what its own
> form can express.

**Attempted as R0 on 2026-09-22 and reverted.** It is still the right rule and
the implementation was wrong in two ways, both of them instances of 1.1. Read
`doc/step-recognition-design.md`, *R0, attempted and reverted*, in full before
the next attempt - the measurements are worth more than the feature would have
been.

### 1.3 Catalogue first, arbitrate second

A decision with more than one possible claimant is taken in two phases. Phase
one **catalogues** who claims what and on what evidence; phase two
**arbitrates**. Precedence lives in a table, not in the order passes happen to
run in.

**The detector is mechanical.** Any flag that makes a claimed thing invisible to
a later claimant is an arbiter with a one-line policy and no record:
`consumed[f]`, `taken[f]`, `moves.count(v)`. Four such sites remain in
recognition: `AnalyticFeatures.cc:1170, 1806, 2282, 2652`.

**And phase one is the half that matters.** An arbiter can only choose between
claims that were *made*. A recogniser that truncates its own claim to fit its
own form silently removes the option the arbiter would have chosen.

### 1.4 Decide after the pass has finished claiming - claim, then revise

**Paid for twice now.** A region test asked *inside* the band pass's seed loop
judges each band against a state the pass has not finished producing. A sphere
is a *stack* of bands, one per ring of its tessellation, so at the moment a ring
is claimed its neighbours are siblings that do not exist yet: every ring looked
truncated, every band was given up, the drops cascaded through "the band sharing
this rim was dropped", and `step-sphere` exported as 482 planes with 448 corners
off their own surfaces. Moved to run once every band has claimed, a ring's
neighbours are *held* and the walk stops at them.

This is open item 25's conclusion for the grid claim, reached from the other
end. **If a test asks "what would this pass leave behind", it may not run inside
that pass.**

### 1.5 Qualification: rank by what *determines* the answer

| class | meaning |
| --- | --- |
| **D0** | fixed by declarations alone: closed form, no fit, no tolerance, no sample |
| **D1** | fixed by the model's stations through an interpolant that publishes a band |
| **D2** | fitted to declarations at a stated tolerance (a crossing arc at 1e-7) |
| **D3** | fitted to the mesh |
| **D4** | the tessellation itself - a chord, a facet plane |

- **A D4 may never be an input to a D0-D2.** A facet's plane is not a plane. The
  R0 attempt broke this and it is what disqualified it.
- **"On the surface" is not "the right curve."** A section of a quadric by its
  own chord plane lies *exactly* on it and bounds nothing.
- **A D0 may not carry an accuracy threshold**; it is exact by construction.
  Float equality for a declared point against the surface that emitted it -
  `runsThrough`'s 1e-7 - is not a threshold in this sense. A *band* that admits
  points known to be off the surface is.
- **Each primitive has its own ladder.** Curves C0-C6, surfaces S0-S3, regions
  R0-R2, corners their own two tables. Each is resolved on its own ladder, exact
  first; they do not borrow one another's evidence, and a decision on one may
  not demote an entity on another. The R0 attempt crossed both: a *region*
  question decided by a *surface-membership distance*, whose answer then took a
  *surface* from S0 to S3.
- **A rung may never manufacture a boundary.** A curve is written *because* a
  boundary exists, not the other way round. C5 - the conic a *facet* plane cuts
  from the quadric beside it - splits one declared surface against itself 78
  times on lid10. That is a curve rung reaching into the surface ladder, and it
  is what C0 is for.
- **Consuming less is not a fault when the reason is exactness.** A smaller
  exact claim beats a larger inexact one. Ask *why* a rung declined, not how
  much - a form that could not express the region, an order that got there
  first, and a threshold that happened to bite are three different answers.

**Adding a rung requires three things**: name its class; state what it is
measured against and make that something the model says; prove it fires *and*
that the suite fails without it.

### 1.6 Provenance is identity, not measurement

The model knows which original made each facet and which surfaces that original
owns. That needs no tolerance and does not care what order anything runs in.
`Provenance` today answers only `excludes` - *this facet's maker does not own
that surface*. Its **positive** form is what R0 needs and does not exist; the
answer is already derived in `export_step.cc` as `covered` / `covered_cut`.

`AnalyticFeatures::PointOrigin`: **Provisional** (it will be placed and has not
been - *no decision may be taken on it*), **Declared**, **Tessellation**,
**Placed**, **ReaderInvented**. Derived where reported, cached nowhere.

### 1.7 The standing rules that did not change

Kernel agreement is not proof. Exactness takes precedence. Magic numbers and
statistical thresholds need explicit approval - declared values and closed
algebraic solutions first. Half a fix parks, with its measurements. Five whys
with named branches, and record the refuted ones.

---

## 2. What was measured (do not re-derive)

### The R0 attempt, in both directions

Parked on `claude/step-region-r0-optimistic-parked`. The band pass measured
whether its ring was the whole of what its surface covers there and gave the
region up if not, deciding membership with `min(bandOf(facet), max_off)`.

**What it bought**, approximation tier, same build before and after:

| | base | attempt |
| --- | --- | --- |
| lid10 split surfaces | 1 (cone 50) | none |
| lid10 facets covered analytically | 913 (536 band + 377 patch) | 913 (480 + 433) |
| lid10 `CIRCLE` | 300 | 410 |
| lid10 census 7/16/13/21/17 | 0/0/0/3/162 | identical, same worst 0.108893 |
| lid10 OCCT | 845 faces, 1 solid, 1 shell | identical |
| `step-split-surface` volume | 15646.585807, **-1.0489 against the model** | **15647.634695, -1.9e-11** |
| `step-bored-cylinder` cylinder faces | 8 | 4, same derived volume |

**What it cost**, exact tier (`--enable=step-analytic-surfaces` alone), lid10:

| | base | attempt |
| --- | --- | --- |
| `Cone` faces, OCCT | 9 | **8** |
| `Plane` faces | 1574 | 1630 |
| `CIRCLE` | 413 | 411 |
| `LINE` | 3466 | **3633** |

Two regions given up; the trimmed-quadric writer refused them under *"half a
surface is worse than none"*; 56 facets that had been an exact conical face came
out as planes. **A closed form was surrendered, not exchanged**, which is 1.1.

### Three corrections to things previously believed

- **"No surface is split at the exact tier" was an artefact of the report.** The
  split warning counts only facets *another recogniser claimed*; facets of a
  declared surface left planar are invisible to it. Measured directly, lid10's
  cone 50 has 59 facets exactly on it against a ring of 56, and cylinder 52 has
  38 against 36 - at 1e-7, with no approximation flag. The regions do exceed the
  rings in the exact tier. **The instrument under-reports; do not read its
  silence as absence.**
- **C0 and R0 are not one defect.** `doc/step-rungs.md` had them as one from two
  ends. The attempt removed every split surface and left C0's seam edges exactly
  where they were - 78 on lid10, 18 on the bayonet, 31 on f04. R0's facets were
  ones a *band* could not hold; C0's are ones the *general pass refused*, each
  with a stated reason. **But the attempt took its region from the tessellation
  band**; the rule leaving C0's facets planar is `boundary_lies_on_surface`, all
  of a surface or none, and a region taken from *provenance* would reach the
  natural boundary instead of a ragged one. Whether that moves C0 is
  **untested**, and it is what settles the order of the next two pieces of
  work.
- **The residual 1.0489 mm3 of open item 26 was not another ellipse.** The rim
  fix took 1.2126 to 1.0489; `reader-diff.py` showed the rest was three facets
  of the chamfer **cone** left planar, a chord plane sitting inside the cone it
  replaces. The two `ELLIPSE`s still in that file have semi-axes 20 by 20 - a
  circle spelled oddly, worth nothing to the volume.

### Two gaps in the instruments

- **The suite could not see an exact-tier regression**, and the whole regression
  above passed the suite. **Closed 2026-09-22** by `step-region-beyond-the-band`
  and `step-region-pocket`, and by `step-dev.sh export` writing both tiers.
- **The fault census is not the exactness metric.** It was *identical* across
  the R0 attempt - 0/0/0/3/162, same worst, same breakdown - while the exact
  tier lost a conical face and 56 facets to planes. And in the other direction,
  a *rising* code 17 is not by itself a regression: greedy consumption hides
  edges that do not lie on their face by absorbing them into something
  tolerant, and every step towards exactness exposes them. Watch closed-form
  coverage per declared surface and the derived volume, not the census.
- **The exporter is not byte-deterministic.** The same binary exports the same
  model twice with different entity numbering, most likely a `std::map` keyed on
  a pointer. Verify a behaviour-neutral change by *content* - the report text,
  the entity census, the derived volumes - never by diffing files. Open item 28.

### Still true from the previous session

- **Facet ownership is not a contest.** Every contested facet is a precision tie
  naming the same declared surface as the claim holding it.
- **Refusing the facet-plane conics makes it worse**: code 17 went 42 to 198 on
  lid10, because the chord they fall back to is further off the face.
- **Neither kernel can adjudicate geometry.** SOLIDWORKS and OpenCASCADE both
  read a file with a 1.2 mm3 error as `valid=1 faults=0`.

---

## 3. Next steps, in order

**The goal is the rung system, not the reference parts.** lid10 and the bayonet
are specimens - they are where the ugly geometry was found, not the thing being
optimised - and a finding on one counts when it has become a synthetic fixture
that isolates it. If the ladder is complete and correct they fall out of it.
`doc/step-rungs.md`, *What completing the system means*, is the shape of the
work; the list below is that shape in order.

1. ~~The declaration-utilisation census.~~ **Built 2026-09-22.** Every export
   now reports, per declaration: written as a face / read to identify a surface
   without one standing on it / no mesh left on it (the booleans took it, which
   provenance decides) / **unexplained**, which is the bug report. Both
   discriminators were mutation-checked, and a fifth bucket - *a duplicate of
   one already written* - was tried, fired zero times on every model, and was
   removed rather than landed.

   **The work list it produced**, and the headline was mostly not a defect. The
   standing figure was *"lid10 declares 41 planes and writes 27 planar faces on
   a declared plane"*, fourteen unaccounted for. Of those 41 planes: 10 written,
   31 with no mesh left, **none unexplained** - the plane channel is clean. What
   remains is small, named and curved:

   ```text
   lid10   exact       25 surfaces: 13 written, 1 evidence, 7 no mesh, 4 unexplained
                       cylinder 43, cone 54, cylinder 56, sweep 65
           both flags  3 unexplained - cylinder 12, cylinder 43, cylinder 56
   bayonet exact       23 surfaces: 11 written, 0 evidence, 7 no mesh, 5 unexplained
                       cylinder 24, cylinder 60, cylinder 67, cone 78, sweep 87
   ```

   `step-cube`, `step-bored-cylinder` and `step-region-pocket` report **zero**
   unexplained, which is the census passing its own sanity check: the bore's two
   end discs and the pocket's r = 20 wall land in *no mesh left*, exactly as
   `step-region-pocket.scad` derives in prose.

   **And it reports per facet too, because per declaration it cannot see the
   worst case.** *Written* means a face stands on the declaration, not that it
   holds everything it should. And a fitted surface can consume a region that
   was really a combination of exact primitives and leave a few facets behind:
   the declarations that should have covered them had their ground taken, their
   recognisers never proposed, **no losing claim exists to count**, and the
   stranded facets have no route back. `consumed` is not *consumed by the most
   exact claimant*, and once a facet is consumed every later pass is blind to
   it.

   ```text
   lid10  exact   909 declared surface, 27 declared plane, 0 fitted, 1521 tessellation
          approx 1671 declared surface, 27 declared plane, 0 fitted,  759 tessellation
   step-approximate-report  exact 0/2/0/2048   approx 0/2/2048/0
   ```

   That last row is the tier in one line and is what proves the fitted bucket
   fires, so lid10's zero there is a true zero.

   **One number is deliberately absent**: how many fitted or tessellated facets
   had a declaration *available*. `owned` names an original's surfaces, not a
   facet's, so on any model that declares anything it answers "all of them" -
   measured, and taken back out rather than shipped. It needs the positive
   per-facet query, which is the next item.

   **Then**: take the nine unexplained declarations one at a time and answer
   *why*, of **both** tiers - the approximation writes lid10's cone 54 and sweep
   65 and loses cylinder 12, so they fail on different declarations. Two of
   lid10's four and three of the bayonet's five already have their answer
   narrowed: their mesh is held under another surface's name.

2. **Finish step 1 of `doc/step-recognition-design.md`'s migration plan**, and
   read that plan rather than this line. *Every pass records its claims into the
   ledger while `consumed` still decides everything* - additive,
   behaviour-neutral, and the suite must not move a number.

   What is missing, measured: `recogniseQuadricPatches` proposes over held
   facets but records a claim only when someone **else** holds them, so a facet
   it claims and then drops to its own span rule or `worst > band` leaves no
   entry; and the band pass, which runs first and takes 536 facets on lid10,
   records only what it honours. Extend it from *what a pass lost* to **every
   claim a pass would make, honoured or not**.

   That is what makes the census a read of the catalogue instead of a
   reconstruction of `consumed`, and it is what supplies the one number the
   census cannot: under a pure Propose, a claim naming a declared surface *is*
   the evidence that a declaration was available on that facet, so the positive
   `Provenance` query stops being needed for it. Then **step 3** - flip the
   source of truth from `consumed` to the ledger - and only then step 4.

3. **R0 properly** (open item 29), if step 2 says the region is larger than what
   is written. Two parts: the region from step 2's query, carrying no tolerance;
   and *exchanged, never surrendered* - `boundary_lies_on_surface` is computable
   on a candidate patch **before** the band is dropped, so the band gives up
   only when the replacement will actually be written. That is the check whose
   absence caused the reverted regression, and it does not need the full
   Propose / Arbitrate / Assemble rewrite to exist. Judged by
   `step-region-pocket`'s derived `4185*pi` and `step-region-beyond-the-band`'s
   exact-tier coverage.
4. **C0 and the refusals** (open item 27). The facets left planar at the exact
   tier are refused by `boundary_lies_on_surface` - all of a surface or none,
   which is right - and by two **4x-median statistics**, which are a yardstick
   made of the error they grade and are the ladder's own forbidden shape. Take
   the statistics first.
5. **Enforcement, so the classes stop being an honour system.** The ledger
   carries a claim's own `ClaimPrecision` but not the class of the *evidence*
   behind it, so a claim can call itself DeclaredExact while a tessellation band
   decided it. Carry both, make the mismatch an `EXPORT-ERROR`. It would have
   refused the reverted attempt on its first export.
6. **Feedstock: finish the 2D channel, one derivation instead of five guards.**
   This is the largest D0 unlock there is and it is already surveyed - see
   `doc/step-export-wip.md`, *One derivation, not five guards*. `Outline2d`
   half-carries the 2D representation already (arcs survive, which is why
   `offset(r = 2)` corners come out as cylinders); finishing it lets both
   extruders answer from **one total function** - segment class and operation
   in, surface out, every class enumerated, no silent `continue`.
   `linear_extrude(twist=)` becomes one branch rather than a refused wall; the
   ellipse item dissolves, since an extruded ellipse is a
   `SURFACE_OF_LINEAR_EXTRUSION` over a curve the exporter already writes; the
   revolved segment that meets its own axis stops being a wrong refusal that
   read as a decision because it carried a comment. And
   `cylinder(r1 = 10, r2 = 0, h = 20)` writing a cone while the identical solid
   revolved from a profile writes 96 planes stops being possible, because both
   answer from the same table - **which is a property assertion needing no
   ground truth: the same solid by two routes must declare the same surfaces.**
   `polyhedron()`, `import()`, `surface()` and `projection()` are *not* on this
   list: they have no intent to read, `declare_grid` and `declare_sweep` are the
   channel for them, and that is a boundary rather than a gap.
7. **Survey the boundary run.** Which stretch of a loop becomes one curve is
   decided by case analysis (`RimRef`'s four kinds, `Patch::Run`) with no
   classed ladder behind it. It is the one entity the survey does not cover.
8. **Open item 28 - determinism.** Export one model a hundred times and diff the
   *reports*. If any decision is "first surface to accept it wins" over a
   pointer-keyed container, the geometry is non-deterministic and nothing today
   would notice.
9. **The rest, unchanged**: lid10's two code-21 faces; open item 21's corner and
   item 23's 1% volume question; item 24's flagship duplication, banked.

## 4. Tooling and setup

**The dev loop is committed. Do not write build or export scripts.**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 build
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 test
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 export lid10
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 census <file.stp>
```

`scripts/step-dev.sh` is the same from bash. Verify with `test` before and after
any change. **Never run two builds or two suites at once.**

**The exact tier is a separate measurement and the dev loop does not make it.**
`step-dev export` always passes both feature flags. To see what the exact tier
does, run the staged binary with `--enable=step-analytic-surfaces` alone.

**Environment facts, all measured on this machine:**

- 16 cores, 64 GB. A **cold** build of a fresh worktree is ~10 minutes of
  compiling plus the bundle and install; an incremental one ~2 minutes; the
  suite ~4 minutes; a **configure over an existing cache is cheap** - about 12
  targets after a `tests/CMakeLists.txt` change.
- **A worktree has no submodules and no `libraries/MCAD`.** Copy them from the
  main checkout and repoint each `.git` file at
  `<main>/.git/modules/<path>`; do not re-clone.
- **`CMAKE_BUILD_TYPE` must be Release.** An empty build type runs **26x**
  slower with byte-identical output.
- Adding a fixture or test needs `--configure`, or ctest never sees it, plus an
  empty `-expected.txt` in `tests/regression/export-step-sanitytest/`.
- Invoke `scripts\step-dev.ps1` from PowerShell with backslashes; a bash tool
  eats them and PowerShell then reports the file as missing.

**Diagnostics are catalogued by the question they answer** in
`scripts/step-diagnostics/README.md`. Read it before writing one.

---

## 5. The diagnostic method

In this order. Each step exists because skipping it cost a day.

1. **The census, on our own file.** `fault-census.py` keys every property known
   to become a CAD fault by the code it becomes.
2. **The export's own report** - **the declaration census first**: of everything
   the model declared, how much is written as a face, how much the booleans
   removed, and how much is unexplained. That is *is everything that could be
   exact written as exact*, which is the question the rungs exist to answer.
   Then the claim ledger's agreement, any declared surface split between
   recogniser kinds, how many section planes were facets of the surface being
   cut, how many rims the declaration settled. **But know what each line cannot
   see**: the split warning counts only facets another recogniser claimed, so a
   region left planar does not appear in it.
3. **The catalogued instruments**, by question - `reader-diff.py` (what do these
   two files differ *by*? - it found the three planar chamfer facets behind the
   last 1.0489 mm3), `sweep-chords.py`, `planes-at-sweep.py`, `at-vertex.py`,
   `point-vs-faces.py`.
4. **Measure both tiers.** A change that is a pure win with both feature flags
   can be a regression with only `--enable=step-analytic-surfaces`, and the
   suite will not say so.
5. **Derive the expectation from the model.** Never integrate the tessellation
   when the exporter writes the analytic surface. Where no absolute is
   available, assert a **property**.
6. **A kernel, last.** Interrogate rather than read a verdict: `-Raw -RoundTrip
   [-Heal] -FaultDetail`, always with the control `f02-band-fn032-analytic.stp`
   (`faults=2 faultyfaces=2 codes=17`).
7. **Mutation-check every zero and every total**, and never verify a
   behaviour-neutral change by diffing exports.

**The trap, in yet another costume**: *a nearly-right quantity answering a
question it was not asked.* This session it was the tessellation band standing
in for "what this surface covers", and a split report's silence standing in for
"nothing is split".

---

## 6. Priming prompt

Paste this into a new session, adjusting only the worktree path.

```text
You are continuing work on the PythonSCAD STEP exporter, in a worktree of
E:\29_Git_Soulsolistice\pythonscad. Branch: claude/step-claim-two-pass, suite
58 of 58, nothing pushed.

Read first, in this order: CLAUDE.md (build, test, the dev loop), then
doc/step-export-next-session.md in full, then doc/step-rungs.md and
doc/step-recognition-design.md. Those last two are the core of this work and are
earned through days of debugging - read them carefully and do not weaken them.
doc/step-export-wip.md open items 21, 23, 24, 25, 27, 28 and 29 are the detail.

The philosophy everything rests on: a closed form is exact by definition and has
to stay. The classes are a covering order - every D0 representation is covered
first, then D1 takes what is left, then D2, D3, and D4 only what nothing above
it could. A lower rank never drives a higher one, and nothing falls down the
ladder: a closed form may be exchanged for another closed form, never
surrendered to a chord or a facet plane. A D0 carries no accuracy threshold; it
is exact by construction.

The previous session attempted R0 - claim a declared surface's region whole -
and reverted it. It removed every split surface, bought 110 exact circles on
lid10 and made step-split-surface exact against its derived volume, and it still
had to come out: membership was decided by the tessellation band, a D4, and in
the exact tier a declared cone fell to planes (Cone faces 9 to 8, LINE 3466 to
3633). Read doc/step-recognition-design.md, *R0, attempted and reverted*, before
going near it again. The code is on claude/step-region-r0-optimistic-parked.

The development loop is committed; do not write your own build or export
scripts:

  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 build
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 test
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 export lid10
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 census <file.stp>

It always passes both feature flags, so it does not measure the exact tier. Run
the staged binary with --enable=step-analytic-surfaces alone for that, and
measure both tiers on any change to recognition - the suite cannot see an
exact-tier loss on a reference part. A worktree has no submodules: copy them
from the main checkout and repoint each .git file at <main>/.git/modules/<path>.
Verify with `test` (58 of 58) before and after any change. Never run two builds
or two suites at once. Every diagnostic is catalogued by the question it answers
in scripts/step-diagnostics/README.md.

The goal is the rung system itself, not the reference parts. lid10 and the
bayonet are specimens - where the ugly geometry was found, not the thing being
optimised - and if the ladder is complete and correct they fall out of it. Read
doc/step-rungs.md, *What completing the system means*: breadth (every entity has
a ladder - the boundary run does not), depth (every rung names its class and its
evidence's class - nothing checks this), feedstock (every primitive declares -
minkowski, hull's blend, linear_extrude(twist=), PathExtrude and Skin do not).

Where to start, and it changes no behaviour: **is everything that could be D0
actually D0?** Ask it from the declarations, not from the written entities.
doc/step-export-development.md already carries the doctrine - "a declaration
that is not taken is a bug report" - and it has never been measured. Current in
both tiers: lid10 declares 41 planes and writes 27 planar faces on a declared
plane, so fourteen declarations never become a face and nothing says why; for
the 25 declared analytic surfaces the number is not reported at all; and 2
regions are fitted to a surface the model had already declared, which is a D0
that ended as a D3. Separate three outcomes, not two - written, used as evidence
and never written (hull's two rims identify a cone and are never faces), and
unused, which is the bug report. Then attribution (60 facets on lid10 whose
losing claim names a different surface), then R0 properly, then C0's refusals,
starting with the two 4x-median statistics, which are a yardstick made of the
error they grade.

Do NOT start by arbitrating facet ownership, and do NOT start by refusing the
wrong curves. Both were measured: every contested facet is a precision tie
naming the same surface as the claim holding it; and refusing facet-plane conics
takes code 17 from 42 to 198 on lid10 because the chord they fall back to is
worse.

Derive expectations from the model, never from a kernel: both OpenCASCADE and
SOLIDWORKS called a file with a 1.2 mm3 error valid with zero faults. Mutation-
check every zero an instrument reports, and know what each instrument cannot
see - the split-surface warning counts only facets another recogniser claimed,
so a region left planar never appears in it. Do not verify a behaviour-neutral
change by diffing two exports; the exporter is not byte-deterministic.
```
