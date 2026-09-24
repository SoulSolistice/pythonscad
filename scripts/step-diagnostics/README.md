# STEP exporter tools — which one answers which question

Every tool the STEP work has built, grouped by the question it answers, so a new
session reaches for one rather than writing it again. Everything here reads the
STEP **text** unless marked **(OCP)**, which means it reads the file through
OpenCASCADE and needs `cadquery-ocp==7.8.1.1.post1` in the interpreter CMake
found (`scripts/step-dev.sh python` prints it). A text tool sees what we wrote; an
OCP tool sees what a kernel made of it after healing, which is sometimes the
point and sometimes a trap — see *Kernel agreement is not proof* in
`doc/step-export-development.md`.

## The development loop

| command | does |
| --- | --- |
| `scripts/step-dev.sh build` | incremental build and stage — never configures |
| `scripts/step-dev.sh test` | the STEP suite, `export-step-\|mutations`, 59 tests, staging DLLs on PATH |
| `scripts/step-dev.sh flagship` | the three flagship tests alone |
| `scripts/step-dev.sh export lid10\|bayonet\|f02\|f04\|<file.scad>` | export with both feature flags and the right customizer set, into `build/step-dev/` |
| `scripts/step-dev.sh census <file.stp>…` | the fault census, with the OCP-capable interpreter |
| `scripts/step-dev.sh measure <name>` | every reference part and fixture, both tiers, ledgers dumped, into `build/measure/<name>/` |
| `scripts/step-dev.sh compare <a> <b>` | the behaviour-neutral oracle: `measure-compare.py` - report less timings, entity census, sorted ledger and surfaces, per export. Self-compares to 0 and catches a one-field mutation |
| `scripts/step-dev.ps1 <same>` | the same from PowerShell, with MSYS2 set up and arguments passed untouched |

Use these rather than inline commands. A command passed from PowerShell to bash
crosses two quoting rules, and lid10's `-P "New set 1"` is exactly the argument
that does not survive it. **Never run two builds or two suites at once** in one
build directory: it corrupts ninja's state and the two ctests overwrite each
other's output files, so neither result means anything.

## First, on any export: the census

| tool | answers |
| --- | --- |
| `fault-census.py <file>…` | every property known to become a SOLIDWORKS fault, keyed by the code it becomes: 7, 16, 13, 21, 17 (OCP), seam crossings. Validated by the 2026-09-08 control (2420 edges off their surface, against 38–62 for current files). **Run this first.** |

## Why is this facet what it is — the claim ledger

| tool | answers |
| --- | --- |
| `OPENSCAD_STEP_LEDGER=<file.tsv>` on any export | every claim every recogniser made, one tab-separated row each: facet, origin, holder, claimant, surface, kind, precision, evidence class (D0-D4), fate (honoured / withdrawn / held / declined / refused), whether provenance says the maker owns that surface, and the rule behind the fate. Facet indices are the merged mesh's and match between the two tiers, so "what does one tier write that the other does not, and why" is a join of two dumps. The export's own report prints the summary: *why N facets are fitted or left to the tessellation*, then the rules by count. Read this before instrumenting a pass. |

## One relation at a time — our own file

| tool | fault | answers |
| --- | --- | --- |
| `vertex-on-curve.py <file>…` | 7 | every vertex against the curve its own edge is written as — LINE, CIRCLE, ELLIPSE, HYPERBOLA, PARABOLA, B-spline |
| `vertex-owners.py <file> [x y z]` | 16 | every vertex against the surface of every face using it; analytic surfaces only |
| `point-vs-faces.py <file> x y z` (OCP) | 16 | one point against every face using it, **B-spline surfaces included** — the census's blind spot |
| `pinched-loops.py <file>` | 21 | the faces behind the census's code-21 count, listed; legal seams of closed faces excluded |
| `edge-on-surface.py <file>…` (OCP) | 17 | every edge sampled along its length against both faces, by curve-on-surface pair |
| `sweep-chords.py <file>` (OCP) | 17 | line edges on a sweep face, split by whether their *ends* are on both surfaces (a crossing curve can replace them) or not (the corner is at fault) |
| `planes-at-sweep.py <file>` (OCP) | 17 | every plane bordering a sweep face: corners on the sweep, the angle they meet at — a plane at a few degrees with every corner on the sweep is the sweep's own facet left unclaimed, which is a claim question and not a curve one |
| `seam-straddle.py <file> [face]` | — | whether a face's loop crosses the seam of a surface closed in `v` — the long-way-round pcurve a reader has to repair |
| `edge-lengths.py <file>…` | 13 | edge-length distribution, closed edges apart; short edges are what a strayed vertex turns into code 13 |
| `face-bad-edge.py <file>` (OCP) | 11, 16, 17 | per face, every quantity SOLIDWORKS has a separate code for, with kind, area and centre in `-FaultDetail`'s units |
| `face-boundary-deviation.py <file>` (OCP) | 16 | per face, how far its boundary vertices sit from the surface it was given |

## Locating a fault once it is counted

| tool | answers |
| --- | --- |
| `near-edge.py <file> x y z [radius]` | our edges whose midpoint is near a point — how a SOLIDWORKS fault centre (`-FaultDetail`) is matched back to an entity of ours |
| `at-vertex.py <file> x y z [tol]` | what we wrote at a point: the vertex, its edges, and the faces using each |
| `face-corners.py <file> <face>` | a face's loops, edge by edge, with curve kind and length |
| `reader-built.py <file>` (OCP) | edges the reader holds that we never wrote — how OpenCASCADE's invented seam edge and 1.08 vertex were shown to be its own |

## Interrogating a reader

The method is in `doc/step-export-development.md`, *How to interrogate a
reader*: import raw, round-trip always, round-trip twice to isolate a repair,
read the fault list last.

| tool | answers |
| --- | --- |
| `../step-interop-solidworks.ps1 -KitDir <d> -ImportSettings <label> -Raw -RoundTrip [-Heal] [-FaultDetail <tsv>]` | SOLIDWORKS's verdict per file; `-Raw` turns knitting, healing and analytic conversion off and restores them; `-RoundTrip` saves its model back as `_SW.STEP`; `-Heal` adds `_SW_healed.STEP` and a before/after delta; `-FaultDetail` one row per faulty entity. Always include the control `build/interop-kit3/f02-band-fn032-analytic.stp` (`faults=2 faultyfaces=2 codes=17`) |
| `../step-interop-sw-roundtrip.py --kitdir <d>` (OCP) | both comparisons in one command: ours against its round trip (substitution, degradation, volume drift), and the round trip against its healed twin (what the repair changed, exactly) |
| `reader-diff.py <a.stp> <b.stp>` | what changed between two files whose ids do not correspond — faces by surface and corners, curves by control points, with every replaced curve's displacement |
| `../step-interop-faultmap.py` | the faces a CAD system calls faulty, mapped back onto the geometry we wrote |
| `../step-interop-crosscheck.py` | derived, OpenCASCADE and CAD volumes side by side, disagreements flagged |
| `../step-interop-roundtrip.ps1` | a single file into SOLIDWORKS and straight back out |
| `../fusion/step-interop-fusion.py` | the Fusion equivalent; runs *inside* Fusion via Scripts and Add-Ins |

## The kit, and the older corner instruments

| tool | answers |
| --- | --- |
| `../step-interop-kit.py --binary <abs staged binary> --outdir <d>` | builds the 24 coupons as 48 files, analytic and faceted control each |
| `../step-occt-strict.py [--kitdir <d>]` (OCP) | how far each face's corners are from its own surface, p95 per file |
| `../step-corner-stray.py <file>…` (OCP) | the same, broken down by surface kind |
| `../step-analytic-probe.py` | replays the recogniser over a faceted export: the geometric ceiling and why each band was dropped |
| `band-family-controls.scad`, `band-family-volumes.py` | the band family with its controls exposed, and its derivable volumes |

## Tests, for completeness

`tests/validatestep.py` (the file is well formed), `tests/steproundtrip.py`
(OCP; a kernel reads it back as a solid), and `tests/step-flagship-check.py`
(both, plus vertex-on-curve and seam-straddle, over the six flagship coupons)
are what the suite runs. They are the floor, not the method.

`tests/stepexportsanitytest.py` also holds every fixture's approximation tier
against its exact tier through both tiers' ledger dumps (`surrendered`): a
facet the exact tier writes on a declared surface that the approximation tier
writes on nothing fails the test - a closed form surrendered, not exchanged.
The same function answers it for any two ledger dumps of one model, which is
how it was first run over a whole `measure` directory.
