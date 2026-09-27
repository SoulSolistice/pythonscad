# STEP export: the design checklist

**Every change, PoC, design concept and hypothesis is held against this list
before it lands - and a commit that lands one states the result.** It is drawn
from `doc/step-recognition-design.md` and `doc/step-rungs.md`, which stay the
source and the argument; this is the same content as questions, so that no rule
depends on which paragraph a session happens to remember. Written 2026-09-24,
the day a landing was checked against *exchange, never surrender* and not
against *never manufacture a boundary* - and a systemic defect introduced early
is what later sessions end up patching round.

**Enforcement comes first.** Where a rule can be checked by the exporter, it is
an `EXPORT-ERROR`, and every run of every fixture fails on one
(`tests/stepexportsanitytest.py`; a fixture carrying a known defect names it with
`KNOWN-EXPORT-ERROR:`). Where it can be checked by a test, it is one. What
neither can reach stays here as a question - and the last column says which it
is, so that *none yet* reads as the work list it is: every such row is a rule
the system can currently break in silence.

How to use it: for a change, walk every row and answer it - *holds*, *breaks*
(then the change does not land), or *not touched*. For a hypothesis or a PoC,
the rows it would break are the first thing to measure. When a row's answer
needs an instrument that does not exist, that is a finding.

## A. The shape recognition runs in

| # | rule | ask of the change | enforced by |
| --- | --- | --- | --- |
| A1 | **Propose is pure**: a proposer reads the graph, the declarations and provenance - nothing any pass has claimed. *design: Three phases* | Does anything that proposes read `consumed`, `held()`, `taken` or another pass's result? | unit test `[purity]` - proposals identical whatever is held and in whichever order proposers run |
| A2 | **One arbiter grants**: a facet becomes held only through `Arbiter::settle`. *design: as built* | Does anything write `consumed` or an honoured claim outside the arbiter? | `EXPORT-ERROR` - the `consumed` cache against the ledger |
| A3 | **The ledger is the source of truth**, `consumed` a checked cache. *design: step 3* | Does anything decide from `consumed` rather than the ledger? | `EXPORT-ERROR` - as A2 |
| A4 | **Stages declare what they read and write**; the table's order is checked. *design: as built* | Is every new stage a row in `kStages`, with honest reads and writes? | `EXPORT-ERROR` - stage table order |
| A5 | **Every claim, won or lost, recorded once** through `recordClaim`, with precision and evidence class, where its fate is decided. *design: Step 1* | Does a new path decide a facet's fate without recording it? Does a claim name its surface? | `EXPORT-ERROR` - an honoured claim that names no surface, and unstated evidence on a held facet |
| A6 | **Reports are ledger queries**, in `Reports.cc`, never computed in the writer. *design: as built* | Is a new report a query over the ledger and what recognition handed on? | none yet - review |
| A7 | **Order is not policy**: the ledger must be identical whatever order recognisers run in. *design: Why purity is the whole point* | Does the result depend on which pass or which surface runs first? | partly - unit test `[purity]`: the D0 regions and their representations are the same whichever order the declarations arrive in; test `export-step-numbering`: the file is the same byte for byte whatever the mesh's vertex and triangle numbering (`canonicalNumbering`, item 37); none yet for the grant, which is still call order |
| A8 | **Refactor in the plan's steps**, each proven: neutral by the oracle, behavioural by a fixture that asserts it first; no wholesale rewrite validated against the suite alone. *design: Migrating without a big bang; The structural work is sanctioned* | Neutral: did `measure` / `compare` give 0 differences? Behavioural: which fixture asserted it before the code changed? | `scripts/step-dev.sh compare`; process |
| A9 | **Structure that stays clean, driven by data between the steps**: one concept per unit (a pure product, a recogniser, a policy, a family of reports); a function that makes more than one decision becomes named functions returning data; logic written a second time is extracted then; stages are glue - read declared products, call recognisers, hand results to the arbiter - and hold no geometry; policies are tables, not control flow. *author, 2026-09-24; design: The structural work is sanctioned* | Does the change grow a function past one decision, copy logic, put geometry in a stage, or encode a policy as branches? Where would it live as its own unit, and what data crosses to the next step? | review - the trap the 4000-line function was, and `Bands.cc` still is |
| A10 | **Derive from the most basic fact, don't enumerate cases**: each output is derived from the one fact that decides it, independently of the others; a degenerate case is what the general formula gives, not a row of its own; inputs are filtered by kind once, before the derivation, not inside each case. *author, 2026-09-25: interconnected when/if/then classifications are inherently error prone - two rows of the revolve table had forgotten the chord condition* | Does the change add a case to a switch whose rows each re-check a condition, or special-case what a general formula already covers? | review - `SweptSurfaces.cc` is the worked example |

## B. Regions: which facets a surface gets

| # | rule | ask of the change | enforced by |
| --- | --- | --- | --- |
| B1 | **The representation fits the region, never the region the representation**: a recogniser takes facets by what the declaration covers, not by what its form can express. *design: Three phases; development: same title* | Does a recogniser drop facets because its own form cannot hold them? | `EXPORT-ERROR` - D0 region split (`reportRegions`) |
| B2 | **Region first, and the region is sequence-agnostic**: a pure function of provenance and exact membership; the representation is chosen per region, second. *design: R0 is region first* | Is any region decided by which claimant reached it first, or carved by a representation? | `EXPORT-ERROR` - D0 region split; `KNOWN-EXPORT-ERROR` on the fixtures R0 has yet to fix |
| B3 | **Never split a surface on a boundary the model does not have** - it strands facets no later pass can attribute. *design: R0 is region first* | Does the change introduce, or leave, two faces meeting inside one connected D0 region? | `EXPORT-ERROR` - D0 region split, with the mesh edges inside it |
| B4 | **One declared surface, one kind of face, the whole connected region.** *design: Three phases* | Is any surface written partly as one kind and partly as another? | `EXPORT-WARNING` per kind (Reports); the D0-region `EXPORT-ERROR` where the region is whole facets |
| B5 | **A facet is attributed to the surface its maker declared as a face** - never to a witness, never to another original's surface by position. *wip 31, 34* | Does a claim name a surface its maker declared only as evidence, or not at all? | `EXPORT-ERROR` - D0 facets held for a different surface |
| B6 | **D0 ground is not left to the tessellation** without a proof that no closed form exists. *rungs: The qualification; author, 2026-09-24* | Does the change leave facets of a D0 region as planes? What proves nothing closed could write them? | `EXPORT-ERROR` - D0 facets left to the tessellation |
| B7 | **Cut facets** belong to their maker's surface; where a cut corner belongs is the corner ladder's question. *rungs: R0; wip 34* | Does the change treat a boolean-cut facet as not the surface's because a corner is off it? | none yet - the D0 region holds whole facets only (`Membership.h`) |
| B8 | **Globally, never locally**: judged by what it leaves on every facet; stranding is visible before choosing. *design: Determinacy 3* | Does a locally good claim strand facets elsewhere? | partly - B6's error; none yet for stranding across surfaces |
| B9 | **A face's surface is G1 inside it**: where a declared surface is only C0 along a line - a corner of a sweep's laws - its region ends there, and the face carries only its own stretch of the surface. The crease is the declaration's, so the cut is not a manufactured boundary (D1); it runs along the mesh's own edges there, which the generator provides by putting a station at each corner. *2026-09-26: SOLIDWORKS's Import Diagnostics lists every face spanning such a crease as faulty, and none once it is split* | Does a face span a crease of its own surface? Does a generator leave a corner of its laws without a station? | `formGridPatches` splits at the u creases; `EXPORT-WARNING` names a corner with no station; `step-sweep-law-corners`. Profile creases (across v) are left inside faces: measured harmless (the control coupon, 0 faults), not proven so |
| B10 | **A face exists where the exact solid has one, not where the mesh does**: "made on S" (identity) says which surface a facet belongs to; whether S has a face there is decided by the model's boolean over its exact primitives. The mesh boolean ran on chords and is wrong wherever a feature is smaller than a sagitta - an `eps` of 0.1 against a 0.107 sagitta leaves slivers of a surface the model never shows. *design: Existence is a D0 question too; 2026-09-26, lid10* | Does the change treat a facet's presence in the mesh as proof that its surface has a face there? Does it constrain a corner by surfaces that cannot meet? | `EXPORT-ERROR` - a facet made on a declared surface that the model's solid rules out at every point of it, a phantom (`reportExistence`; `existence_test.cc`; the specimen `examples/step_test/phantom-sliver.scad` joins the fixtures when its corners are placed); decided only where no operand that states nothing of its solid - not even an envelope (a polyhedron's, a hull's) - is unioned above it. The corner contradiction is the design's step 5 |

## C. Classes, evidence and exactness

| # | rule | ask of the change | enforced by |
| --- | --- | --- | --- |
| C1 | **Rank by what determines the answer, not by how close it lands**: D0 declarations alone; D1 the model's stations through an interpolant; D2 fitted to declarations at a stated tolerance; D3 fitted to the mesh; D4 the tessellation. *rungs: The qualification* | Which class decides what this change decides - honestly, by its evidence? | review |
| C2 | **In D0 only machine precision applies** - no threshold, no deviation; the bound is the arithmetic's forward error. *author, 2026-09-24; `recognition/Exact.h`* | Does a D0 test compare against a chosen number (1e-7, a band, a fraction of a radius)? | none yet in the band pass's measured-parameter matching and a grid's 1e-9 key; `onExactly` elsewhere |
| C3 | **A D4 may never be an input to a D0-D2**, as admission or as veto. *rungs: The qualification* | Does a band, a sagitta, a median or any statistic over facets admit or refuse D0 ground? | partly - quadric region rules decide only D4 members (`QuadricProposal::decided`); the corner arbiter's `kCornerTiers`; none yet for the band pass |
| C4 | **A rung is as exact as its evidence**: a declared surface in the sentence does not make it D0. *rungs; design: Step 1* | Is the evidence class stated for what *decided* membership, facet by facet where it differs? | none yet - an honoured claim whose evidence is below its precision as an `EXPORT-ERROR` is the planned enforcement |
| C5 | **Covering order**: D0 covered first, then D1, D2, D3, D4 only what nothing above could. **Nothing falls down the ladder.** *rungs* | Can the change make ground a closed form held fall to a lower class? | test - `surrendered()`: every fixture's approximation tier against its exact tier, facet by facet; none yet within one tier |
| C6 | **A closed form may be exchanged, never surrendered**; a whole-region claim wins only among claims that will actually be written. *rungs: R0; design: R0 attempted* | If a representation gives ground up, is the replacement certain to be written (formed, and at the exact tier bounded)? | test - C5's; review for within-tier exchanges |
| C7 | **Each primitive has its own ladder** (curves C0-C6, surfaces S0-S3, regions R0-R2, corners); ladders do not borrow each other's evidence, and a decision on one may not demote an entity on another. *rungs: The inventory* | Does a region, curve or corner decision move an entity on another ladder down? | review |
| C8 | **Determinacy**: an entity is as precise as the independent closed forms that fix it - weakest class needed, then freedom left, then corroboration; independence is algebraic, not a count of names. *design: Determinacy 2* | What fixes this entity, how many independent constraints, what is left to the mesh? | review; the corner arbiter's rungs |
| C9 | **Provenance first, then the rung**; at D0, where two closed forms both satisfy the construct, only provenance and membership discriminate; precision may rank only below D2. *design: The arbitration key; author, 2026-09-24* | On a tie, what decides - identity, or call order? | partly - B5's error; none yet for the grant, which is still pass order |

## D. Curves and boundaries

| # | rule | ask of the change | enforced by |
| --- | --- | --- | --- |
| D1 | **A rung may never manufacture a boundary**: a curve is written because a boundary exists. *rungs: How to read a result* | Does the change write an edge between two faces of one declared surface, or a seam of a surface against itself? | report line (C5's "wrong curve, kept"); B3's error inside a D0 region; none yet as an error for C0 edges |
| D2 | **"On the surface" is not "the right curve"**: a section by a chord plane lies on the quadric and bounds nothing. *rungs: The qualification* | Is a curve accepted because it lies on the surface rather than because it is the boundary? | review; `vertex-on-curve.py`, `edge-on-surface.py` |
| D3 | **C2 before C5, C0 first**: a declared plane's conic, never a facet plane's; no edge where both sides are one surface. *rungs: What the survey says to do* | Does the change add or keep a conic cut by a plane nothing declared? | report line only |
| D4 | **Retire C5 only once its inputs are fixed** - the chord it falls back to is further off. *rungs* | Does removing a curve rung leave a worse one in its place? | review; the census |
| D5 | **One curve between two faces is one edge**, from a vertex where something else meets it to the next; **a corner of the curve - where it crosses a crease of either surface - is a vertex, never a C0 knot**: a kernel wants every edge's curve G1. *author, 2026-09-25: the lead-in's boundary should be a clean curve; 2026-09-26: a C0 knot inside an edge, and a face spanning a crease of its own surface, are what SOLIDWORKS's Import Diagnostics lists as faulty* | Does the change write a boundary curve in pieces joined where nothing else meets them, cut one at an arbitrary vertex, or carry a corner inside one edge? | `validatestep.py` `check_curve_runs` (polynomial B-splines; a vertex on a declared crease, read from the pcurve, is exempt); circles and conics not yet - a rim is still written arc by arc |

## E. Declarations and feedstock

| # | rule | ask of the change | enforced by |
| --- | --- | --- | --- |
| E1 | **Declarations are the foundation of D0; a missing one is added** - in the primitive that makes the geometry, or in the model - never inferred from geometry. *author; rungs: Feedstock; design: Determinacy 1.8* | Is intent being recovered from positions where a declaration should state it? | review; the census's *unexplained* declarations and *claimed for no declaration* facets |
| E2 | **A declaration not taken is a bug report.** *development: the declaration channel* | Does the change leave a declaration with mesh on it and no face? | census line (*unexplained*) - none yet as an error |
| E3 | **A generator's points lie on its own declaration** at machine precision. *2026-09-24: offset()'s grid* | Does anything between the generator and the exporter move a point off the surface it declared? | fixtures step-rounded-profile(-notched) - D0 tests at machine precision fail when it does |
| E4 | **Faces and witnesses are different roles**: a face declarer owns the surface; a witness offers evidence. *development: who declared a surface* | Does the change treat evidence as a face, or the reverse? | B5's error |
| E5 | **No intent to read is a boundary, not a gap**: `polyhedron()`, `import()` declare nothing; `declare_*` is the channel. *rungs: Feedstock* | Is D4 here because nothing could be declared, or because nothing was? | census split by whether the maker declares anything |
| E6 | **An edge's kind is its maker's word**: what a 2D edge is - straight, or a chord of which record - is named where it is made and carried through Clipper by identity (`EdgeIdentity.h`); measuring its ends is the fallback for an edge nothing named, never a second opinion. *2026-09-25: survey item 2, the D-shaft* | Does the change make 2D edges without naming them, drop or reorder `edge_ids` without keeping them in step with `vertices`, or decide an edge's kind by position where a name exists? | `sweep_table_test.cc` (the D-shaft through Clipper); a producer that names nothing reads as measured, not as an error |

## F. Evidence, testing and process

| # | rule | ask of the change | enforced by |
| --- | --- | --- | --- |
| F1 | **Derive expectations from the model, never from a kernel, never from a faceted input**; nothing grounds on tessellations. *development: Derived, not captured; author* | Where did every number in the new assertion come from? | review |
| F2 | **Mutation-check every new assertion and every zero.** *development* | Which planted fault did the new check fail on? | process - stated in the commit |
| F3 | **Kernel agreement is not proof.** *development* | Is a kernel's reading offered as the evidence? | review |
| F4 | **Behaviour-neutral is proven by the oracle, never argued**; measure both tiers. *development; design* | `compare` result, both tiers? | `scripts/step-dev.sh measure` / `compare` |
| F5 | **A census that moves is not a regression, one that does not move is not proof**; watch closed forms per surface, and ask why a rung declined, not how much. *rungs: How to read a result* | Is a count being read as a verdict? | review |
| F6 | **Magic numbers and statistical thresholds need explicit approval**; a statistic over the tessellation is the yardstick made of the error it grades. *rungs: How to add a rung* | Does the change introduce a number nobody derived? | review |
| F7 | **Root-cause fixes only**; five whys with the branches named; half a fix parks on a `-parked` branch with its measurements. *development; handover* | Is this a gate or exclusion standing in for a cause? | review |
| F8 | **A new rung names its class and its inputs' class, is measured against something the model states, sits in the module's shape, fires, and the suite fails without it.** *rungs: How to add a rung* | All five? | review; F2 |
| F9 | **Exports are deterministic**, and a function of the model, not of the order its mesh is numbered or allocated in. *wip 28, 37* | Does the change walk a pointer-ordered container, or choose by index where no fact decides? | test `export-step-numbering` - four fixtures byte for byte under other numberings; measured 2026-09-27 on both backends (wip 28) - no test yet runs every fixture twice |
| F10 | **Known defects are named, never hidden**: `KNOWN-EXPORT-ERROR` over `WILL_FAIL`, which passes whatever a test fails on. *development: The directives* | Is a failing check silenced rather than named? | review; the directive fails when its error goes |
