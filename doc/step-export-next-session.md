# STEP export: handover, 2026-09-22

Everything a new session needs: the doctrine as it now stands, what was
measured, what to do next, the tooling, the diagnostic method, and a priming
prompt to paste.

Branch `claude/step-claim-two-pass`, suite **56 of 56**, nothing pushed.

---

## 1. The doctrine

Five rules, each paid for. The first three are new this session.

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

A *band* is not something the model declares; it is a way of writing a region
that happens to be a ring between two rims. Given a region that is a ring **plus
six facets a boolean left attached**, the band claims the ring and the remainder
falls to whoever runs next. One declared surface, two kinds of face, and a
boundary the model never asked for.

This generalises: any recogniser that decides which facets by what it can
express splits surfaces this way, on geometry nobody has looked at yet. **The
reference parts are specimens - they are where it showed, not where it lives.**

### 1.3 Qualification: rank by what *determines* the answer

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
  The corner arbiter enforces this (`kCornerTiers` marks MeshPlane
  `used = false`); nothing else did.
- **"On the surface" is not "the right curve."** A section of a quadric by its
  own chord plane lies *exactly* on it and bounds nothing. Code 17 cannot see
  the difference.

**Adding a rung requires three things**, each skipped by at least one rung this
session: name its class; state what it is measured against and make that
something the model says; prove it fires *and* that the suite fails without it.

### 1.4 Provenance is identity, not measurement

The model knows which original made each facet and which surfaces that original
owns. That needs no tolerance and does not care what order anything runs in.
`Provenance` exists and is used only as a *gate*; it answers per
`(facet, surface index)`, so a claim that cannot name its surface index cannot
be arbitrated by the model's own statement.

**Identity is carried, never inferred by proximity.** Three days went to one
mistake - a coordinate used as an identity - which is why a point now carries
its origin.

### 1.5 A point carries whether it has moved

`AnalyticFeatures::PointOrigin`: **Provisional** (it will be placed and has not
been - *no decision may be taken on it*), **Declared**, **Tessellation**,
**Placed**, **ReaderInvented**. Derived where reported, cached nowhere.

The first value is a defect written so it cannot recur: a proof of concept
judged corners at their *mesh* coordinates when the arbiter would move them, and
`at-vertex.py` found one refused corner absent from the exported file entirely.

### 1.6 The standing rules that did not change

Kernel agreement is not proof. Exactness takes precedence. Magic numbers and
statistical thresholds need explicit approval - declared values and closed
algebraic solutions first. Half a fix parks, with its measurements. Five whys
with named branches, and record the refuted ones.

---

## 2. What was measured (do not re-derive)

**Facet ownership.** The claim ledger reproduces the exporter exactly on all
four reference parts and all 45 fixtures (shadow check, `EXPORT-ERROR` when it
does not). Contested facets: lid10 452, bayonet 532, f02 32, f04 64.

**But the contention is not a contest.** *Every* contested facet is a precision
tie, and every one names the **same declared surface** as the claim holding it.
An ownership arbiter by precision would have been a **no-op**. Provenance
separates none of them, because there is nothing to separate.

**What is real is the split.** Per surface, not per facet:

```text
lid10    a cone, surface 50, 62 facets (56 band's wall, 6 quadric face)
bayonet  a cylinder, surface 6, 60 facets (52 / 8)
         a cone, surface 74, 62 facets (56 / 6)
f02, f04 none
```

Adjacent (2, 16, 2 mesh edges across the divide), so one connected region cut in
two - not a boolean leaving two pieces. Both sides from the **same original**,
which declared the surface. The quadric pass would have written all 62 as one
face; **the band's claim is the partial one**.

**A rim written as a 45 degree ELLIPSE.** `step-split-surface.scad`, derived
volume **15647.634695** (closed form, confirmed by independent numerical
integration):

| | volume | against the model |
| --- | --- | --- |
| trimmed patch | 15647.634695 | -1.89e-11 |
| band's wall (before fix) | 15646.422089 | -1.2126 |
| band's wall (after fix) | 15646.585807 | -1.0489 |

Cause: the rim's section plane was a **facet of the chamfer cone**, 45 degrees,
centred at z = 30 on a model 13 tall. Fixed by asking the declaration - 60 edges
on lid10 now keep the quadric's own circle.

**Neither kernel can adjudicate.** SOLIDWORKS reads both files `valid=1
faults=0` (control `faults=2 faultyfaces=2 codes=17` in the same run, as it
must), and OpenCASCADE agrees with its volumes. Only the model's arithmetic
found it.

**And the commonest "conic" is not a boundary.** Of the facet-plane sections,
**78 of 78 on lid10, 18 of 18 on the bayonet, 31 of 31 on f04** have the facet
on the *same surface being cut*. One surface, part claimed and part faceted,
with the seam written as a section of itself.

**Refusing them makes it worse**: code 17 went 42 -> 198 (lid10), 42 -> 78
(bayonet), 38 -> 100 (f04), because the chord they fall back to is further off
the face than the wrong curve is.

---

## 3. Next steps, in order

1. **C0 + R0 - claim a declared surface's region whole.** The split and the 78
   seam-conics are one defect from two ends. Everything else is downstream.
   `OPENSCAD_STEP_YIELD_SURFACE=<index>` gives the other representation for
   comparison; `tests/step-representation-check.py` is the assertion
   (`WILL_FAIL` today at 1.0489 mm3).
2. **Then C2 before C5.** With regions whole, a section plane is either declared
   or a genuine flat face and the facet case disappears rather than being
   refused. **Do not retire C5 first.**
3. **Close the ledger's own gap**: the smooth-region pass takes facets without
   recording a claim. The shadow check skips those rather than counting them as
   agreement, and says so. A recogniser absent from the ledger cannot lose a
   contest it should win.
4. **lid10's two code-21 faces** - planes of area 1.466 at
   `(-56.999, -55.863, 89.788)` and `(-55.863, 56.999, 89.788)`, never
   investigated.
5. **Open item 21's corner**, and the 1% volume question in item 23's
   SOLIDWORKS run.
6. **Banked**: item 24, the flagship third of the suite doing 18 exports where 6
   would do. Worth ~3 minutes a run at Release speed; take it when something
   else already forces a configure.

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

- 16 cores, 64 GB. `step-dev.sh test` uses every core; the build keeps three
  quarters because a CGAL translation unit peaks near 2 GB.
- A full Release build is ~20 min; an incremental one ~2 min; a **configure over
  an unchanged cache is ~110 s**, not the hour `CLAUDE.md` used to warn about.
  What costs an hour is a cache whose *settings* changed.
- **`CMAKE_BUILD_TYPE` must be Release.** An empty build type runs **26x**
  slower with byte-identical output; the suite went 8437 s to 229 s when fixed.
  `msys2-build.sh` now passes it on both configure paths.
- The bundle step can fail on `mv: Permission denied` when something runs out of
  the build tree. `msys2-build.sh` now kills only such processes, and refuses to
  exit 0 with a staged binary older than the build.
- Adding a fixture or test needs `--configure`, or ctest never sees it.
- A STEP fixture needs an empty `-expected.txt` in
  `tests/regression/export-step-sanitytest/`.

**Diagnostics are catalogued by the question they answer** in
`scripts/step-diagnostics/README.md`. Read it before writing one.

---

## 5. The diagnostic method

In this order. Each step exists because skipping it cost a day.

1. **The census, on our own file.** `fault-census.py` keys every property known
   to become a CAD fault by the code it becomes. Our file is the part we
   control; a reader's verdict is the last thing to ask.
2. **The export's own report.** It now says: the claim ledger's agreement and
   its contested / tied counts; any declared surface split between recogniser
   kinds, with the division, the originals on each side and the edges across the
   divide; how many section planes were facets of the surface being cut; how
   many rims the declaration settled. Most questions asked this session were
   answered here, not by an instrument.
3. **The catalogued instruments**, by question - `sweep-chords.py` (is the
   corner at fault, or can a curve replace it?), `planes-at-sweep.py` (is this
   plane the sweep's own facet?), `at-vertex.py` (what did we write at this
   point?), `point-vs-faces.py` (how far off, B-splines included).
4. **Derive the expectation from the model.** For a boolean cut of a declared
   surface the volume often *is* available in closed form - the mistake to avoid
   is integrating the *tessellation* when the exporter writes the *analytic*
   surface (`cylinder(h=12, r=20, $fn=32)` exports at `pi*400*12`, not the
   32-gon's area times 12). Where no absolute is available, assert a
   **property**: representation-independence needs no ground truth at all.
5. **A kernel, last.** OpenCASCADE heals silently; SOLIDWORKS reports faults
   that are orthogonal to correctness. Both called a file with a 1.2 mm3 error
   `valid=1 faults=0`. **A reader cannot adjudicate geometry; only the model
   can.** Interrogate rather than read a verdict: `-Raw -RoundTrip [-Heal]
   -FaultDetail`, always with the control `f02-band-fn032-analytic.stp`
   (`faults=2 faultyfaces=2 codes=17`).
6. **Mutation-check every zero and every total.** A zero from a new instrument
   is not a result. This session: `misheld` read 0 (mutated to 296 to prove it
   reads), `tied` equalled the whole population (mutated to 0), and a readout
   grep silently dropped a line reporting 288 over-claims for two builds.

**The trap this session repeated four times**, in different costumes: *a
nearly-right quantity answering a question it was not asked.* A corner judged at
its mesh coordinate; "off any face here" used where "off **this** surface" was
meant; a ledger naming a holder that had already let go; a count of disagreement
read as a count of damage. Each was caught by a check, none by reading the code.

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
and doc/step-recognition-design.md (the claim ledger, and region before
representation). doc/step-export-wip.md open items 21, 23, 24, 25 and 26 are the
detail.

The development loop is committed; do not write your own build or export
scripts:

  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 build
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 test
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 export lid10
  powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 census <file.stp>

Verify with `test` (56 of 56) before and after any change. Never run two builds
or two suites at once. Every diagnostic is catalogued by the question it answers
in scripts/step-diagnostics/README.md - read it before writing an instrument.

Where to start: claim a declared surface's region whole (C0 and R0 of
doc/step-rungs.md). lid10's cone 50 is one connected region of one declared cone
written as two kinds of face - 56 facets to a band's wall, 6 to a quadric face,
touching along 2 mesh edges, both sides from original 78, which declared it -
and the bayonet has a cylinder and a cone the same way.
tests/data/scad/step-export/step-split-surface.scad reproduces it in ten lines,
and tests/step-representation-check.py is the assertion (WILL_FAIL today at
1.0489 mm3; OPENSCAD_STEP_YIELD_SURFACE=<index> gives the other
representation).

Do NOT start by arbitrating facet ownership, and do NOT start by refusing the
wrong curves. Both were measured this session: every contested facet is a
precision tie naming the same surface as the claim holding it, so an ownership
arbiter is a no-op; and refusing facet-plane conics takes code 17 from 42 to 198
on lid10 because the chord they fall back to is worse. Both are downstream of
claiming the region whole.

The three doctrines that bind: catalogue first and arbitrate second, with phase
one never truncating a claim to fit a form; the representation fits the region,
never the region the representation; and rank rungs by what determines the
answer (D0 declarations alone, down to D4 the tessellation), where a D4 may
never be an input to anything above it. A rung needs its class named, its
yardstick to be something the model states, and proof that it fires and that the
suite fails without it.

Derive expectations from the model, never from a kernel: both OpenCASCADE and
SOLIDWORKS called a file with a 1.2 mm3 error valid with zero faults. And
mutation-check every zero an instrument reports.
```
