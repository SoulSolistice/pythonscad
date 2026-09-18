# Priming prompt for the next STEP-export session

Paste the block below as the first message of a new session. It is written to be
self-sufficient; everything it points at is in the repository.

---

You are continuing work on the PythonSCAD STEP exporter, in the worktree at
`E:\29_Git_Soulsolistice\pythonscad\.claude\worktrees\solidworks-fault-code-17-b58858`
on branch `claude/solidworks-fault-code-17-b58858`. Nothing has been pushed.

**Read first, in this order:** `CLAUDE.md` (build, test, the interop driver),
then `doc/step-export-development.md` sections *The census*, *How to interrogate
a reader*, *Fault codes* and *Entities: what ISO 10303-42 has, and what we
write*, then `doc/step-export-wip.md` *The session of 2026-09-16 to 18 in one
page* and open items 20, 21 and 22.

**The development loop is committed - do not write your own build or export
scripts.** From PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 build         # incremental, never configures
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 test          # 54 tests, all must pass
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 export lid10  # also bayonet, f02, f04, <file.scad>
powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 census build/step-dev/lid10.stp
```

`scripts/step-dev.sh` is the same from bash. It sets MSYS2 up, passes arguments
through untouched (lid10's `-P "New set 1"` is what an inline command mangles),
exports with both feature flags and the right customizer set, and runs the suite
with the staging DLLs on PATH. Verify before and after any change with `test`.
**Never run two builds or two suites at once**: they corrupt ninja's state and
overwrite each other's outputs, and neither result then means anything.

**Every diagnostic tool is catalogued in `scripts/step-diagnostics/README.md`,
by the question it answers** - the census, one check per fault relation, the
tools that locate a fault in the file, and the reader interrogation. Read it
before writing any instrument; the question has probably been answered. The
complete list of SOLIDWORKS fault codes with its own descriptions, which ones we
have produced and which census row watches for each, is the *Fault codes* table
in `doc/step-export-development.md`.

**The first thing to run on any export** is the census, on our own file:

```bash
python scripts/step-diagnostics/fault-census.py <file.stp>
```

It reports every property known to become a SOLIDWORKS fault, keyed by the code
it becomes (7, 16, 13, 21, 17, seam crossings). Our file is the part we control;
a reader's fault list is the last thing to ask.

**Standing rules, all paid for:**

- *Exactness takes precedence*: a declared surface always beats a fitted one,
  and every substituted curve must pass through its run's own endpoints.
- *Prefer the declaration to a deduction*: when two things meet, ask whether
  both are declared before trying to compute their crossing. If one is not, the
  fault is upstream in recognition or provenance, not in the solver.
- *Measure against the surface as written*, never against a grid's declared
  band - the band is the tessellation's sagitta.
- *Five whys with named branches*; record the refuted ones. Land no fix until
  something in the suite fails without it. Make should-never-happen loud.
- *Kernel agreement is not proof*: OpenCASCADE heals on read. New checks read the
  STEP text with no kernel in the path.
- **SOLIDWORKS:** start it by hand; never kill the driver mid-import; always run
  `scripts\\step-interop-solidworks.ps1 -Raw -RoundTrip` (knitting, healing and
  analytic conversion off, restored afterwards) and include the control
  `build/interop-kit3/f02-band-fn032-analytic.stp`, which must read
  `faults=2 faultyfaces=2 codes=17`. Add `-Heal` only to *isolate* a repair,
  and diff the pair with `scripts/step-interop-sw-roundtrip.py`. Count faults
  before any heal - a failed repair once took a 64-face solid to 2 faces.
- The API's fault checks and the Import Diagnostics dialog are different
  analyses and neither contains the other.
- We write basic entities and make them exactly right before richer ones;
  SOLIDWORKS's strictness is the asset that keeps that honest.

**Where to start.** Item 22's sweep half is a *declaration* problem: every plane
that failed to cross lid10's sweep is undeclared - a facet the boolean left,
meeting a declared sweep at 3.7-20 degrees, so the crossing is ill-conditioned
and not the edge's geometry. Find out what those facets are in the model and why
no declaration claimed them (items 13 and 14 are the provenance background).
The promotion itself is **parked** on `claude/step-sweep-chord-parked`: it
writes a 2D B-spline pcurve on a B-spline surface, which `tests/validatestep.py`
cannot yet evaluate ("PCURVE's surface has no usable placement"). Teach the
validator to evaluate a B-spline surface first, then land it - never relax the
check around it.
Then: the three pinched loops on lid10 (code 21, never investigated; SOLIDWORKS
reports negative face areas there). `pinched-loops.py` lists them - faces
`#38510`, `#38511`, `#38513`, all **planes at z = 84.9** - and `face-corners.py`
shows each one's loop. Then item 21's single corner, which wants to travel
0.5503 where its nearest neighbour is 0.1927.

To pin any fault to an entity: `near-edge.py` matches a SOLIDWORKS fault centre
back to our edge, `at-vertex.py` shows what we wrote at a point, and
`sweep-chords.py` splits the sweep's chords by whether their ends are on both
surfaces. All live in `scripts/step-diagnostics/`; the development doc lists
them under *Locating a fault*.

Two gaps in the census to close while there: its code-16 row measures analytic
surfaces only, so a vertex off a *B-spline* surface is invisible to it (f02 has
nine, 0.08-0.10 off the sweep); and the richer entities
`SURFACE_OF_LINEAR_EXTRUSION` / `SURFACE_OF_REVOLUTION` are measured gaps
(an elliptical cylinder exports as 66 planes) to take on once the basic ones are
clean.
