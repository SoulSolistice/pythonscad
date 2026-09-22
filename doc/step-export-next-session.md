# STEP export: handover, 2026-09-22

Everything a new session needs: the doctrine as it now stands, what was
measured, what to do next, the tooling, the diagnostic method, and a priming
prompt to paste.

Branch `claude/step-claim-two-pass`, suite **56 of 56**, nothing pushed.

---

## 1. The doctrine

Six rules, each paid for. The last one is new today.

### 1.1 Catalogue first, arbitrate second

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
own form silently removes the option the arbiter would have chosen, and no
amount of arbitration downstream can recover it.

### 1.2 The representation fits the region, never the region the representation

> A recogniser decides *which* facets it takes - and a writer decides *which
> curve* it writes - by what the **declaration** covers, never by what its own
> form can express.

**Landed 2026-09-22 as R0**, for the band pass. A band measures whether the ring
it found is the whole of what its surface covers there, and gives the region up
rather than truncating it. Every split surface is gone from every reference
part; `step-split-surface` now exports the model's own derived volume to
1.9e-11; lid10 writes the same 913 facets analytically in fewer faces with 110
more exact circles.

### 1.3 Decide after the pass has finished claiming - claim, then revise

**New, and paid for on the first attempt at R0.** The region test asked inside
the band pass's seed loop judged each band against a state the pass had not
finished producing. A sphere is a *stack* of bands, one per ring of its
tessellation, so at the moment a ring is claimed its neighbours are siblings
that do not exist yet: every ring looked like a truncated region, every band was
given up, the drops cascaded through "the band sharing this rim was dropped",
and `step-sphere` exported as 482 planes with 448 corners off their own
surfaces. Moved to run once every band has claimed, a ring's neighbours are
*held* and the walk stops at them.

This is open item 25's conclusion for the grid claim, reached independently from
the other end, and it is the fifth time a provisional input has produced a
plausible wrong answer in this work. **If a test asks "what would this pass
leave behind", it may not run inside that pass.**

### 1.4 Qualification: rank by what *determines* the answer

`doc/step-rungs.md` is the survey. Classes, not accuracies:

| class | meaning |
| --- | --- |
| **D0** | fixed by declarations alone: closed form, no fit, no tolerance, no sample |
| **D1** | fixed by the model's stations through an interpolant that publishes a band |
| **D2** | fitted to declarations at a stated tolerance (a crossing arc at 1e-7) |
| **D3** | fitted to the mesh |
| **D4** | the tessellation itself - a chord, a facet plane |

Two consequences:

- **A D4 may never be an input to a D0-D2.** A facet's plane is not a plane.
- **"On the surface" is not "the right curve."** A section of a quadric by its
  own chord plane lies *exactly* on it and bounds nothing. Code 17 cannot see
  the difference.

**Adding a rung requires three things**: name its class; state what it is
measured against and make that something the model says; prove it fires *and*
that the suite fails without it.

### 1.5 Provenance is identity, not measurement; and a point carries whether it has moved

The model knows which original made each facet and which surfaces that original
owns. That needs no tolerance and does not care what order anything runs in. R0
asks it first, before any distance test.

`AnalyticFeatures::PointOrigin`: **Provisional** (it will be placed and has not
been - *no decision may be taken on it*), **Declared**, **Tessellation**,
**Placed**, **ReaderInvented**. Derived where reported, cached nowhere.

### 1.6 The standing rules that did not change

Kernel agreement is not proof. Exactness takes precedence. Magic numbers and
statistical thresholds need explicit approval - declared values and closed
algebraic solutions first. Half a fix parks, with its measurements. Five whys
with named branches, and record the refuted ones.

---

## 2. What was measured (do not re-derive)

### R0, landed

Same build before and after:

| | base | R0 |
| --- | --- | --- |
| lid10 split surfaces | 1 (cone 50) | **none** |
| lid10 regions given up whole | - | surfaces 9 (4 of 76), 50 (56 of 62), 52 (36 of 61) |
| lid10 facets covered analytically | 536 band + 377 patch = 913 | 480 + 433 = **913** |
| lid10 `CIRCLE` | 300 | **410** |
| lid10 census 7 / 16 / 13 / 21 / 17 | 0 / 0 / 0 / 3 / 162 | **identical**, same worst 0.108893, same breakdown |
| lid10 OCCT | 845 faces, 1 solid, 1 shell | **identical** |
| lid10 OCCT volume | 224683.668014 | 224711.283834 - *nothing adjudicates this* |
| `step-split-surface` volume | 15646.585807, **-1.0489 against the model** | **15647.634695, -1.9e-11** |
| `step-bored-cylinder` approx volume | 5298.405620 (derived 5298.405619) | **5298.405620** |
| `step-bored-cylinder` cylinder faces | 8 | **4** |

**Read the facet count first.** The same facets are written analytically, in
fewer faces, with more exact curves; nothing fell back. A face count moves
either way and says nothing on its own.

### Three things that turned out not to be true

- **C0 and R0 are not one defect.** The rungs survey said the seam-conics would
  go with the split. They did not move at all: 78 on lid10, 18 on the bayonet,
  31 on f04. R0's facets were ones a *band* could not hold; C0's are ones the
  *general pass refused*, each with a stated reason. Open item 27.
- **The split only exists in the approximation tier.** Exported without
  `--enable=step-approximate-surfaces`, no surface on lid10, the bayonet or
  `step-split-surface` is split at all. At 1e-7 the region of a declared surface
  *is* the ring, so R0 is correctly silent there.
- **The residual 1.0489 mm3 was not another ellipse.** Item 26's rim fix took
  1.2126 to 1.0489, and the rest was three facets of a chamfer **cone** left
  planar by the truncated region - a chord plane sits inside the cone it
  replaces. The two `ELLIPSE`s still in that file are semi-axes 20 by 20, which
  is a circle spelled oddly and worth nothing to the volume.

### Still true from the previous session

- **Facet ownership is not a contest.** Every contested facet is a precision tie
  naming the same declared surface as the claim holding it. An ownership arbiter
  by precision would have been a no-op.
- **Refusing the facet-plane conics makes it worse**: code 17 went 42 -> 198 on
  lid10, because the chord they fall back to is further off the face.
- **Neither kernel can adjudicate geometry.** SOLIDWORKS and OpenCASCADE both
  read a file with a 1.2 mm3 error as `valid=1 faults=0`.

---

## 3. Next steps, in order

1. **Open item 27 - C0, and it is a refusal problem.** Work down lid10's own
   list of refusals: a claim whose boundary strays 0.1094 where the tessellation
   allows 0.0067 (192 facets); 20 facets reaching further round the axis than
   four times the typical span; 24 sweep facets off their fit. Two of those are
   4x-median statistics, which is the shape open item 25 refused on its own rung.
2. **Then C2 before C5.** With regions whole, a section plane is either declared
   or a genuine flat face. **Do not retire C5 first.**
3. **Close the ledger's own gap**: the smooth-region pass takes facets without
   recording a claim. A recogniser absent from the ledger cannot lose a contest
   it should win.
4. **Open item 28 - the exporter is not byte-deterministic.** Cheap and worth
   doing: export one model a hundred times and diff the *reports*. If any
   decision is "first surface to accept it wins" over a pointer-keyed container,
   the geometry itself is non-deterministic and nothing today would notice.
5. **lid10's two code-21 faces** - planes of area 1.466 at
   `(-56.999, -55.863, 89.788)` and `(-55.863, 56.999, 89.788)`, never
   investigated.
6. **Open item 21's corner**, and the 1% volume question in item 23's
   SOLIDWORKS run.
7. **Banked**: item 24, the flagship third of the suite doing 18 exports where 6
   would do. Worth ~3 minutes a run; take it when something else forces a
   configure.

---

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

**Environment facts, all measured on this machine:**

- 16 cores, 64 GB. A **cold** build of a fresh worktree is ~10 minutes of
  compiling plus the bundle and install; an incremental one ~2 minutes; the
  suite ~4 minutes; a **configure over an existing cache is cheap** - measured
  again 2026-09-22, about 12 targets rebuilt after a `tests/CMakeLists.txt`
  change. What costs an hour is a cache whose *settings* changed.
- **A worktree has no submodules and no `libraries/MCAD`.** Copy them from the
  main checkout and repoint each `.git` file at
  `<main>/.git/modules/<path>`; do not re-clone.
- **`CMAKE_BUILD_TYPE` must be Release.** An empty build type runs **26x**
  slower with byte-identical output. `msys2-build.sh` passes it on both paths.
- Adding a fixture or test needs `--configure`, or ctest never sees it.
- A STEP fixture needs an empty `-expected.txt` in
  `tests/regression/export-step-sanitytest/`.
- `scripts/step-dev.ps1 build` must be invoked from PowerShell with backslashes
  (`scripts\step-dev.ps1`); a bash tool eats them and PowerShell then says the
  file does not exist.

**Diagnostics are catalogued by the question they answer** in
`scripts/step-diagnostics/README.md`. Read it before writing one.

---

## 5. The diagnostic method

In this order. Each step exists because skipping it cost a day.

1. **The census, on our own file.** `fault-census.py` keys every property known
   to become a CAD fault by the code it becomes. Our file is the part we
   control; a reader's verdict is the last thing to ask.
2. **The export's own report.** It says: the claim ledger's agreement and its
   contested / tied counts; any declared surface split between recogniser kinds,
   with the division, the originals on each side and the edges across the
   divide; **any region given up whole rather than written as a band**, with the
   ring and the region; how many section planes were facets of the surface being
   cut; how many rims the declaration settled. Most questions are answered here.
3. **The catalogued instruments**, by question - `reader-diff.py` (what do these
   two files differ *by*? - it is what found the three planar chamfer facets
   behind the last 1.0489 mm3), `sweep-chords.py`, `planes-at-sweep.py`,
   `at-vertex.py`, `point-vs-faces.py`.
4. **Derive the expectation from the model.** The mistake to avoid is
   integrating the *tessellation* when the exporter writes the *analytic*
   surface. Where no absolute is available, assert a **property**:
   representation-independence needs no ground truth at all.
5. **A kernel, last.** OpenCASCADE heals silently; SOLIDWORKS reports faults
   orthogonal to correctness. Interrogate rather than read a verdict: `-Raw
   -RoundTrip [-Heal] -FaultDetail`, always with the control
   `f02-band-fn032-analytic.stp` (`faults=2 faultyfaces=2 codes=17`).
6. **Mutation-check every zero and every total.** A zero from a new instrument
   is not a result. Today: the new derived-volume assertion was mutated to the
   old wrong figure and confirmed to fail.
7. **Do not verify a behaviour-neutral change by diffing files.** The exporter
   is not byte-deterministic (open item 28). Compare the report text, the entity
   census and the derived volumes.

**The trap, in yet another costume**: *a nearly-right quantity answering a
question it was not asked.* Today it was a region test asked before the pass
that makes the regions had finished.

---

## 6. Priming prompt

Paste this into a new session, adjusting only the worktree path.

```text
You are continuing work on the PythonSCAD STEP exporter, in a worktree of
E:\29_Git_Soulsolistice\pythonscad. Branch: claude/step-claim-two-pass, suite
56 of 56, nothing pushed.

Read first, in this order: CLAUDE.md (build, test, the dev loop), then
doc/step-export-next-session.md in full - it carries the doctrine, what has
already been measured, the tooling and the diagnostic method - then
doc/step-rungs.md (what determines each entity, and therefore which rung it is)
and doc/step-recognition-design.md (the claim ledger, region before
representation, and R0 as landed). doc/step-export-wip.md open items 21, 23, 24,
25, 27 and 28 are the detail.

The development loop is committed; do not write your own build or export
scripts:

  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 build
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 test
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 export lid10
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 census <file.stp>

A worktree has no submodules: copy them from the main checkout and repoint each
.git file at <main>/.git/modules/<path>. Verify with `test` (56 of 56) before
and after any change. Never run two builds or two suites at once. Every
diagnostic is catalogued by the question it answers in
scripts/step-diagnostics/README.md - read it before writing an instrument.

Where to start: open item 27, C0 - the 78 edges on lid10 (18 on the bayonet, 31
on f04) written as a section of a surface by its own chord plane, which is a
boundary of nothing. R0 landed on 2026-09-22 and did NOT move that count, which
is the finding: those facets are not ones a band truncated away, they are ones
the general pass refused, each with a reason the export already prints - a
claim whose boundary strays 0.1094 where the tessellation allows 0.0067, 20
facets reaching further round the axis than four times the typical span, 24
sweep facets off their fit. Two of those refusals are 4x-median statistics,
which is a yardstick made of the error it grades.

Do NOT start by arbitrating facet ownership, and do NOT start by refusing the
wrong curves. Both were measured: every contested facet is a precision tie
naming the same surface as the claim holding it, so an ownership arbiter is a
no-op; and refusing facet-plane conics takes code 17 from 42 to 198 on lid10
because the chord they fall back to is worse.

Four doctrines bind. Catalogue first and arbitrate second, with phase one never
truncating a claim to fit a form. The representation fits the region, never the
region the representation. Decide after the pass has finished claiming, never
inside it - a region test asked inside the band pass judged each band against a
state that did not exist yet and exported a sphere as 482 planes. And rank rungs
by what determines the answer (D0 declarations alone, down to D4 the
tessellation), where a D4 may never be an input to anything above it.

Derive expectations from the model, never from a kernel: both OpenCASCADE and
SOLIDWORKS called a file with a 1.2 mm3 error valid with zero faults. Mutation-
check every zero an instrument reports. And do not verify a behaviour-neutral
change by diffing two exports - the exporter is not byte-deterministic (open
item 28); compare the report, the entity census and the derived volumes.
```
