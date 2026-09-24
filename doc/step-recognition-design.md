# Recognition: a design for transparency

**Written 2026-09-21**, after a session in which answering "who claimed this
facet?" needed a signature change, a new vector, a flag, a diverted claim site
and a fifteen-line report - *per pass*, with two passes still to go. The answer
it produced is worth having (lid10: **56 facets a quadric covers are held by a
band's wall**, never visible before), and the cost of getting it is the problem
this document exists to remove.

**Every rule here is also a row of `doc/step-design-checklist.md`**, which every
change is held against before it lands.

This is a design, not a record of what is built. What is built is in
`doc/step-export-development.md`; what is open is in `doc/step-export-wip.md`.

`doc/step-rungs.md` is the companion survey: what determines each entity the
exporter writes, and therefore which rung it belongs on. Read it before adding
a rung - four were added by hand in one session, and one fired zero times.

## What is wrong, stated as properties rather than complaints

Recognition is four free functions called in a fixed order from a 4000-line
function, sharing a `std::vector<char> consumed` that each mutates as it goes.
Five consequences, every one of them measured this session:

1. **Order is policy.** Whoever runs first takes a facet and it becomes
   invisible to everyone after. Bezier, then grid, then quadric, then smooth
   regions - so the sweep never loses, and the quadric loses 56 facets on lid10
   without anything saying so.
2. **The first pass cannot observe contention at all.** `recogniseGridPatches`
   reported zero contested facets, correctly: almost nothing is held when it
   runs. A number that means "I ran first" reads exactly like a number that
   means "there is no conflict".
3. **Dependencies are discovered by breakage.** `crossing_edges` is derived from
   `grid_faces`; the only way to learn that was to revise the claim and watch
   the shell open. The code carries a comment saying the same mistake once cost
   420 edges.
4. **Nothing is re-runnable.** Open item 25 needs "re-evaluate the claim and
   everything downstream". There is no unit to re-evaluate, so the attempt
   became an in-place edit late in the pipeline, and it failed on both counts a
   late edit fails on: bent planar faces and an unreadable solid.
5. **Every new answer costs plumbing.** The contention number above is the
   worked example, and it is the reason this document exists rather than a
   sixth feature.

## The design

### Three phases, and the first one is pure

**The rule that governs phase one**, found the hard way on 2026-09-21 and
carried as a doctrine of its own in `doc/step-export-development.md`, *The
representation fits the region, never the region the representation*:

> A recogniser decides *which* facets it claims by what the **declaration**
> covers, never by what its own form can express.

A claim truncated to fit a form is the defect no amount of arbitration can
repair, because the arbiter never sees the facets that were dropped - they are
in nobody's claim. Phase two can only choose between claims that were made.

The ledger makes it checkable rather than merely stated: *for every declared
surface, the facets held for it are held by one kind of recogniser, and are the
whole connected region the declaration covers.* An export that breaks it says
so, naming the surface, the division, the originals on each side, and how many
mesh edges cross the divide.

**Propose.** Each recogniser reads the mesh and the declarations and *writes
claims*. It mutates nothing shared. A claim is data:

```text
Claim { facet, surface, precision, by, evidence }
```

`precision` is the same idea as the corner arbiter's `kCornerTiers` - a declared
exact surface outranks a declared interpolant outranks a fit - and `evidence` is
the measurement that justified it, so a report can say *why* and not only *who*.

**Arbitrate.** One component takes the ledger and assigns each facet an owner by
precision, recording the winner *and the losers*. Contention is then
`claims_on[f].size() > 1`, computed once, reported once, by the component whose
job it is.

**Assemble.** Patches are built from the assignment, corners are placed by the
existing arbiter, and - because Propose is pure - a claim can be revised and the
affected part re-evaluated. Open item 25 stops needing a trial export.

### Why purity is the whole point

A pure Propose phase makes a property *testable* that today can only be asserted
in prose:

> Run the recognisers in a shuffled order. The ledger must be identical.

That single test encodes "order is not policy". It cannot be written against the
current code at all, because the current code's answer depends on the order by
construction. It is also exactly the kind of mutation check this project already
demands of every new assertion.

### What becomes cheap

- *Why is this facet a plane?* - read the ledger.
- *What did the sweep want and lose?* - read the ledger.
- *Does a new geometry type conflict with an old one?* - it proposes, and the
  arbiter reports the contest. No pass is edited.
- *Re-run recognition with one facet excluded* - call Propose again.

Each of those has cost an afternoon at least once.

## The arbitration key: precision is not enough, and the matcher needs a rung

**Measured 2026-09-21, and it changes step 4.** With the ledger in place, the
first question asked of it was whether the *right* recogniser holds each
contested facet - not merely whether there was a contest. Counting disagreement
says one thing; counting damage says another:

| part | contested | held by a less precise claimant | **a tie of equal precision** |
| --- | --- | --- | --- |
| lid10 | 452 | 0 | **452** |
| bayonet | 532 | 0 | **524** |
| f02 | 32 | 0 | **32** |
| f04 | 64 | 0 | **64** |

**Every contested facet is a tie.** The precision ranking carried over from the
corner arbiter - declared exact, declared interpolant, fitted - does not
discriminate here at all: a band's wall and a quadric face are both declared
exact, and the whole contest is settled by which function runs first.

So **arbitrating by precision alone would have been a no-op**, and shipping it
green would have retired the question with the contention untouched. The zero in
the middle column is the trap: it reads as "no damage" and means "my key cannot
see the damage".

### What discriminates instead

Two keys, in order, and the second is the one this section adds.

**1. Provenance, which is identity rather than measurement.** The model knows
which original made a facet and which surfaces that original owns. A facet
whose maker owns the surface claiming it is not in the same position as one
that merely lies near it, and no distance test can substitute - the same
principle as *diagnose at the origin*: carry the fact from where the geometry
was made rather than inferring it by proximity.

**2. A rung of the matchers, ordered by the strength of the evidence.** The
corner arbiter already has this as `kCornerRungs`, and the reason "three
surfaces meeting in a point beats two meeting in a curve" is not a preference
but a *dimensionality* argument: three constraints determine the answer, two
leave a freedom. A facet rung has to be justified the same way, and each step
below has such an argument:

| rung | evidence | why it outranks the next |
| --- | --- | --- |
| provenance owns it | the generator says this facet's maker owns this surface | a declaration, not a measurement; nothing derived can outrank it |
| declared points | the facet's corners are points the generator emitted on the surface | exact by position, with no tolerance to choose |
| area membership | corners *and* interior lie on the surface | a facet closing a declared profile has every corner on the surface by position alone - the interior is what distinguishes the region from its lid, and `recogniseGridPatches` already asks the centroid for exactly this reason |
| corners only | position of the corners alone | the weakest, and the one that claims a cap over a bore because every vertex is on the rim |

Reordering is editing the table, as it is for corners. And measured
satisfaction decides before the table does, so a rung cannot let weaker
evidence beat stronger - the rule the corner arbiter already holds to.

### What the contention actually is, measured 2026-09-21 - and a retraction

The tie table above was published as the cost of deciding by call order. **It is
not, and the correction matters more than the original number.**

Nothing is double-consumed: `consumed` does its job, and the shadow check proves
exactly one honoured claim per facet. What the ledger counts as contested is a
*losing* claim - a later pass covering a facet already held. Asked what those
claims are for, the answer is almost always the surface that already holds it:

| part | contested | naming the *same* declared surface | a different surface |
| --- | --- | --- | --- |
| lid10 | 452 | 452 | 60 (pair instances) |
| bayonet | 532 | 524 | 8 |
| f02 | 32 | 32 | 0 |
| f04 | 64 | 64 | 0 |

and the pairs are exclusively one meeting:

```text
lid10:  a band's wall holds, a quadric face also covered (the same surface)   296
        a quadric face holds, a band's wall also covered (the same surface)   156
        a band's wall holds, a quadric face also covered (a different one)     60
```

So the bulk of it is **two recognisers with overlapping capability** - a declared
cylinder or cone is reachable by the band pass and by the quadric pass alike -
and not a dispute about who owns a facet. Provenance cannot separate them
because there is nothing to separate: it is the same surface of the same
original. An ownership arbiter, by precision or by provenance or by a rung of
evidence, would have had nothing to decide on 452 of them.

**And one surface is genuinely split, which is the defect under all of it.**
Asking per *surface* rather than per facet - how many kinds of recogniser hold
it - finds the case a count of contention cannot:

```text
lid10:   a cone, surface 50, 62 facets, held by a band's wall and a quadric face
bayonet: a cylinder, surface 6,  60 facets - the same
         a cone,     surface 74, 62 facets - the same
f02, f04: no surface is split
```

lid10's own provenance report names it: *original 78, 252 facets, owns cone 50
(120 facets whole, 71 cut)*. One declared cone of one original solid, written as
**two kinds of face**, with the division decided by which pass reached which
facet first. Neither holder is wrong about the surface and neither is right
about the face, and the boundary between them is an edge the model never asked
for.

This is what "is the right consumer holding the claim?" means once it is
measured, and it is why the question had to be asked per surface. Per facet,
every answer looked benign: one honoured claim each, no precision inversion, no
provenance conflict.

#### The split is gratuitous, and the band's claim is the partial one

Measured further, because a cone a boolean left in two pieces legitimately needs
two faces and "fixing" that would be damage:

```text
lid10   cone 50, 62 facets (56 to a band's wall, 6 to a quadric face)
        2 mesh edges have a differently-held facet on each side
bayonet cylinder 6, 60 facets (52 band, 8 quadric), 16 such edges
        cone 74, 62 facets (56 band, 6 quadric), 2 such edges
```

Facets held by the two kinds **touch**, so this is one connected region cut in
two, not two disconnected pieces. And the quadric pass says what it wanted:
*"56 facets this quadric also covers are held by something that claimed them
first"* - it would have written all 62 as one face. **The band's claim is the
partial one.**

That is not the band misbehaving. A band *is* a ring between two rims; this
region is such a ring plus 6 facets a boolean left attached, and the band has no
shape for them.

**Both obvious fixes are wrong, and the framing was the problem.** "The band
yields" destroys geometry; "the band absorbs the remainder" puts facets where
they do not belong. A choice between two damaging options usually means the
question is posed wrongly, and it was.

**The root cause: the region is chosen to fit the representation.** The
*declared* thing is cone 50. A band is not declared by anything - it is a way of
*writing* a region that happens to be a ring between two rims. The band pass
decides which facets it takes by what its own form can express, so a region that
is a ring **plus six facets** gets truncated to the ring, and the remainder
looks like someone else's problem.

That generalises, which is what matters: **any** recogniser that decides *which
facets* by *what it can express* will split surfaces this way, on models nobody
has seen. lid10 and the bayonet are not the bug; they are the two specimens
where it shows.

**And the model settles where the facets belong.** Asked of provenance, every
facet on both sides of every split comes from *one* original, and that original
declared the surface:

```text
lid10   cone 50:    a band's wall from original 78; a quadric face from original 78
bayonet cylinder 6: both from original 41
bayonet cone 74:    both from original 74
```

One original, one declared surface, one connected region. Nothing is ambiguous
about ownership at all - only about *how the region is written*.

**So the order is: region first, representation second.** *(Landed 2026-09-22
as R0; the measurements are below, under "R0, as landed".)*

1. Determine the region from the declaration and provenance - the facets whose
   maker owns this surface and which lie on it, connected.
2. Choose the representation to fit *that* region: a band with rims where the
   region really is a ring, a trimmed patch otherwise.
3. Keep exact curves as a property of the **boundary**, not of the
   representation.

Step 3 is already true and was the reason to fear step 2: `StepKernel.cc` writes
a `CIRCLE` for a circular run on a *quadric face* (`circular_runs` /
`runCircle`), so a trimmed patch keeps exact circular edges where its boundary
is circular. Rim exactness is not a band privilege, and the objection that
yielding the surface would lose it does not hold.

**The proof of concept, run 2026-09-21.** A temporary switch made the band pass
yield one named declared surface, so the pass after it covered the whole region.
Never committed; the point was to measure the cost before building anything.

On lid10, yielding cone 50:

| | base | yielded |
| --- | --- | --- |
| census 7 / 16 / 13 / 21 | 0 / 0 / 0 / 3 | identical |
| census 17 | 42, worst 0.108893 | identical, same breakdown |
| faces | 837 | 837 |
| seam crossings | 0 | 0 |
| **CIRCLE** | 300 | **410** |
| trimmed quadrics | 13, over 377 facets | 14, over **433** facets |
| split surfaces | 1 | **none** |
| area | 107318.432226 | **107318.432226** |
| volume | 224620.232151 | **224618.150378** |

433 - 377 = 56, exactly the band's facets, absorbed into one face over the whole
region. The split is gone, the census does not move, the face count does not
move, and **110 more exact circles** are written. The fear that yielding would
cost rim exactness is not merely unfounded - the trimmed patch writes *more*
exact curves than the band did.

**What blocks adoption is the volume**, and specifically its shape: the area is
identical to the last digit while the volume moves by 2.0818 mm3 (9.3e-6
relative). Geometrically identical faces should move neither; a genuinely
changed boundary should move both. Identical area with moved volume points at
something that changes the enclosed region without changing the surface
measure - a trim, an orientation, or a face contributing with a different
sense - and that is worth understanding before anything is adopted.

**And lid10 cannot adjudicate it.** It has no derived volume; taking
OpenCASCADE's figure as truth is the *kernel agreement is not proof* trap.

**So the next step is a fixture**, small enough to reason about, that reproduces
the split and carries a **hand-derived** volume: a declared cylinder or cone
with a boolean-attached remainder. Then the two representations are measured
against the model's own arithmetic, and the invariant becomes regression-tested
rather than observed once on two specimens.

Compare them on the reference parts by derived volume, by the kinds of curve
written at the rims, and by whether the false edge disappears - not by face
count, which both change.

**What is genuinely open, and it is two different things:**

1. **60 facets on lid10** where the losing claim names a *different* surface.
   That is a real contest settled by call order, and it is the ownership
   arbiter's actual target - two orders of magnitude smaller than the headline.
2. **A representation question, not an ownership one.** For the 452, both
   recognisers want the same surface but write different *faces*: a band writes
   walls with rims, caps and a seam; a quadric writes a trimmed patch. Which one
   the file gets is decided by which pass runs first. That deserves deciding on
   purpose, and no ownership arbiter - and no class hierarchy - addresses it.

**The lesson for the metric, which is the general one.** A count of disagreement
is not a count of damage, and a big number is not a finding. "452 contested"
looked like a defect, survived a precision check (0 misheld), survived a
provenance check (0 separated), and only dissolved when the claims were asked
what surface they were *for*. Every intermediate step read like progress.

### R0, attempted and reverted - 2026-09-22

The band pass was made to measure whether the ring it found is the whole of what
its surface covers there, and to give the region up rather than truncate it.
**It was landed, measured, and taken out again.** The code is parked on
`claude/step-region-r0-optimistic-parked`. What it bought, what it cost and
which of this document's own instructions it skipped are all worth keeping,
because the next attempt is the same rung.

#### What it bought, measured on the same build

| | base | attempt |
| --- | --- | --- |
| lid10 split surfaces | 1 (cone 50) | none |
| lid10 facets covered analytically | 913 (536 band + 377 patch) | 913 (480 + 433) |
| lid10 `CIRCLE` | 300 | 410 |
| lid10 census 7/16/13/21/17 (`fault-census.py`, this tree) | 0/0/0/3/162 | identical, same worst 0.108893 |
| lid10 faces, OCCT | 845, 1 solid, 1 shell | identical |
| `step-split-surface` volume | 15646.585807, -1.0489 against the model | **15647.634695, -1.9e-11** |
| `step-bored-cylinder` approximation volume | 5298.405620 (derived 5298.405619) | unchanged |
| `step-bored-cylinder` cylinder faces | 8 | 4 |

The trimmed patch reproduces the proof of concept's +110 circles exactly, and
`step-split-surface` - the fixture built for this question because it carries a
**derived** volume - became exact. Those results stand; they are not why it was
reverted.

#### Why it was reverted: the rung was not the class it claimed

`doc/step-rungs.md` puts R0 in **D0**, and D0 means *fixed by declarations
alone: no fit, no tolerance, no sample*. The implementation decided membership with
`min(bandOf(facet), max_off)`: `bandOf` is the **tessellation band** and
`max_off` is a tolerance. A D4 quantity was the yardstick of a D0 rung, which
the survey forbids in the line directly under its own class table.

**And this document had already said what the right test was.** Two sections
up, under *region first, representation second*:

> Determine the region from the declaration and provenance - the facets whose
> maker owns this surface and which lie on it, connected.

Neither the declaration nor provenance decided anything in the attempt;
provenance was asked only for its veto. The instruction was written, and the
implementation used a distance test instead.

#### What the class error cost, and it is not academic

Run in the exact tier on lid10, the rule gave two regions up. The
trimmed-quadric writer then refused them under a rule of its own -

```text
3 trimmed quadric regions left faceted - the surface is exact but some of it is
bounded off itself, and half a surface is worse than none
```

- and 56 facets that had been an exact conical face came out as planes:

| lid10, `--enable=step-analytic-surfaces` alone | before | after |
| --- | --- | --- |
| `Cone` faces, OCCT | 9 | **8** |
| `Plane` faces | 1574 | 1630 |
| `CIRCLE` | 413 | 411 |
| `LINE` | 3466 | **3633** |

Two rules, each right on its own, composing into a loss - and the loss landed on
the *exact* tier, which is the one the whole rung ordering exists to protect.
**Nothing in the suite saw it**: lid10 is not a fixture, the flagship checks run
with both flags, and no fixture asserts the exact tier's analytic coverage on a
model where this fires. That gap is worth a fixture before the next attempt.

#### The two things the next attempt needs

1. **A region with no tolerance in it.** `export_step.cc` already derives it, per
   original and per surface: `covered` (every corner on the surface) and
   `covered_cut`, whose comment is the definition to use - *"a cut facet has lost
   a corner to the boolean and still belongs to the surface the other two are
   on, which is the whole point of asking about origin rather than about fit"*.
   That is D0 and identical in both tiers. What is missing is a **positive**
   `Provenance` query; today it answers only `excludes`.
2. **A closed form may be exchanged, never surrendered.** This is the ladder's
   governing philosophy and the deeper of the two errors. A band on a declared
   cone and a trimmed patch on the same declared cone are both S0, both D0, so
   moving between them costs no rank and that part of the idea was sound. What
   the attempt did was give the band up *in the hope* that the patch would be
   written; when the writer refused it, the region fell all the way to D4
   planes. **Nothing may fall down the ladder.** So the band must *propose* its
   ring, the patch *propose* the whole region, and the whole-region claim win
   **only among claims that will actually be written** - this document's
   Propose / Arbitrate / Assemble, which is the part that does not exist.

#### The procedural lesson, and it is this document's own

The migration plan below is five steps, and it says why: *"a wholesale rewrite
validated against it is how a subtle regression ships"*. The attempt went
straight to **step 4 - change policy** - while step 3, flipping the source of
truth from `consumed` to the ledger, is not done and Propose is not pure. A
subtle regression shipped, on the tier nobody was measuring. The plan was right
and skipping it cost a landing.

Two smaller notes from the same work, both kept:

- `OnQuadric` - the trimmed-quadric pass's per-facet admission test, lifted into
  one class so two passes cannot disagree about *"on this surface"* - was
  landed separately, verified behaviour-neutral, and stays. It is the right
  shape for the question it answers; it is simply not a D0 question.
- **The exporter is not byte-deterministic.** The same binary exports the same
  model twice with different entity numbering, most likely a `std::map` keyed on
  a pointer. A behaviour-neutral change has to be verified by content - the
  report text, the entity census, the derived volumes - never by diffing files.

### What this means for step 4

Step 4 is not "arbitrate by precision". It is: give each claim its evidence,
rank the evidence, and let the arbiter decide - with the tie count above as the
before-measurement. A change that leaves 452 ties at 452 has done nothing,
whatever else it reports.

## The claim trinity was malformed, and every number read off it inherited that

**Found 2026-09-22 by doubting the census's own output.** `Claim { facet,
surface, precision, by, evidence }` is only worth what its fields are worth, and
two of them were being filled wrongly at the sites that matter most - the
*honoured* ones, which are the only claims anything downstream acts on.

**A band's cap named no surface at all.** `result.ledger.add({cap_low, nullptr,
DeclaredExact, "a band's cap", ..., true})` - no `surface`, and no
`surface_index`, so it took the `std::size_t(-1)` default that
`Claim::surface_index` documents as *"where the claimant cannot name one"*.
Nothing reading the ledger can tell that from a claim on a **fitted** surface,
whose index is simply past the declarations. The declaration census duly
reported step-sphere's two polar caps as *"written on a surface the
approximation fitted"* - **at the exact tier, where nothing can be fitted at
all**, a number contradicted by the flag it was exported under. The caps are
part of the zone's face, so the claim now says so.

**And the honoured claims hardcoded their precision.** The *contested* claim in
`recogniseQuadricPatches` correctly writes `surface_index < declared_count ?
DeclaredExact : Fitted`; the honoured one, twenty lines later, wrote
`DeclaredExact` unconditionally. The grid and Bezier claims wrote
`DeclaredInterpolant` unconditionally the same way. **So a fit that *won* was
recorded as a declaration**, and `ClaimLedger::misheld` - whose entire job is to
find a facet held by a claimant less precise than one that also covered it -
reads that label. It could never count a winning fit. That is why the number has
been 0 on every part ever measured, and the design's own warning about the tie
table applies to it word for word: *the zero reads as "no damage" and means "my
key cannot see the damage"*.

Both are fixed, an `EXPORT-ERROR` now fires on any honoured claim that cannot
name its surface (measured firing twice on step-sphere before the fix, silent
after), and the suite did not move: 58 of 58.

**What it did not change, and this is the part to keep.** lid10's 452 contested
facets are still 452 ties of equal precision. They were not an artefact of the
hardcoding - no fit holds anything on lid10, so every one of those claims was
legitimately `DeclaredExact`. **A tie means the rung system has not
distinguished the two claimants**, and no amount of correct labelling will: a
band's wall and a quadric face are the same precision on the same declared
surface because they are not a precision question at all. They are the
*representation* question this document already isolates, and the only thing
that resolves a tie is deciding it on purpose rather than by call order.

## The declaration census against this design - 2026-09-22

A census was built to answer *is everything that could be D0 actually D0*. It
works, it produced a work list of nine declarations, and **most of it is this
document's own list, rebuilt by hand on the wrong foundation.** Recording that
is worth more than the feature.

| what the census does | what this document already specifies |
| --- | --- |
| reports what class wrote each facet - declared surface, declared plane, fit, tessellation | *What becomes cheap*: **"Why is this facet a plane? - read the ledger."** |
| a new `Result::evidence` vector, recording which declarations a pass consulted | the claim's own `evidence` field - *"the measurement that justified it, so a report can say why and not only who"* |
| splits an unexplained declaration by whether its mesh is held under another name | consequence 1, **order is policy**: *"whoever runs first takes a facet and it becomes invisible to everyone after"* |
| cost: a struct field, two vectors, a per-facet class array, two report blocks | consequence 5: *"Every new answer costs plumbing... the reason this document exists rather than a sixth feature."* |

**And the number it could not produce is the one purity gives away for free.**
The census cannot say how many fitted or tessellated facets had a declaration
*available*, because `owned` names an original's surfaces rather than a facet's.
That was going to need a positive `Provenance` query. Under a **pure Propose**
it needs nothing: every recogniser proposes over the whole mesh regardless of
what has been taken, so a claim naming a declared surface *is* the evidence that
a declaration was available there. The question stops needing an instrument.

**Where the ledger is actually incomplete, measured.** `recogniseQuadricPatches`
already proposes over held facets and records the loss - which is why *"56
facets this quadric also covers are held by something that claimed them first"*
exists at all. But it records a claim only when the facet is **held by someone
else**; a facet it claims and then drops to its own span rule or `worst > band`
leaves no entry. And the band pass, which runs first and takes 536 facets on
lid10, records only what it honours. So the catalogue is missing exactly the
claims that would answer the census's hardest question.

**So the next step is not another instrument.** It is to finish **step 1** of
the plan below - *every pass records its claims into the ledger while `consumed`
still decides everything* - and to extend it from "what a pass lost" to "every
claim a pass would make, honoured or not". That is additive, behaviour-neutral
and testable by the suite not moving a number, and it turns the census from a
reconstruction of `consumed` into a read of the catalogue.

The census stays meanwhile: it is measurement, it found nine declarations to
explain, and its limits are now written down beside it.

## Determinacy: the ground this design stands on

**Recorded 2026-09-23**, because weeks were lost viewing determinism locally, at
the problem in hand, rather than globally. Everything above - the covering
order, provenance before measurement, a pure Propose - is a consequence of what
follows, and a change that cannot be justified against it is not justified.

**1. Provenance first: a D0 with *correct* provenance is the anchor.** A region
belongs to the surface its *creating function* declared, and ends where that
function's surfaces end. A region so defined strands nothing, because every
facet traces to a maker whose declarations cover it. A D0 held by the wrong
claimant is still D0 by class, and it is exactly what splits a surface into
pieces that do not track back to a maker. Three facts, each paid for:

- `consumed` does not mean consumed by the most precise claimant;
- consumed even by a D0 does not mean the *right* D0;
- consumed by a wrong claimant can leave facets behind that nothing is able to
  allocate afterwards.

**2. An entity is as precise as the independent closed forms that fix it.** For
every entity (point, curve, surface region, facet claim, and every combination)
rank lexicographically by:

1. the class of the **weakest** constraint the determining set needs - one D4
   in it makes it D4, however many D0s stand round it (*a rung is as exact as
   its evidence*);
2. the **freedom left**: a point needs three independent surfaces, or a curve
   and a surface; a curve two surfaces; a region its surface and a closed
   boundary of determined curves. Freedom left over is filled from the mesh,
   which is how a lower rung gets in;
3. **independent corroboration** beyond what is needed - each further closed
   form that agrees is evidence against a wrong classification.

Independence is algebraic - the rank of the constraints' gradients at the
entity - not a count of names: a surface declared twice, two coaxial cylinders,
a plane containing an axis, fix no more than one. `kCornerRungs`' "three
surfaces meeting in a point beats two meeting in a curve" is one instance of
this, not the rule.

**3. Globally, never locally.** What is maximised is the determinacy of the
*whole cover*, every facet included. A decision is judged by what it leaves on
every facet, and a stranded facet is a cost the arbiter must see **before** it
chooses: a region solved well by a nine-knot Bezier that leaves two facets out of
reach is worse than it looks, and no census or later pass recovers those two.
Which is why Propose must be pure and cover the whole mesh regardless of what is
held - a claim that is never proposed can never be weighed.

**4. Wherever a less precise rung holds ground, the question is *why*.** A wrong
classification? A missing declaration? Ground consumed by a misfitting claimant?
Underdetermined? A more precise rung may not build on a less precise one unless
that has been thoroughly validated. Since step 1 the ledger answers the first
three per facet; see below.

## Step 1, finished - 2026-09-23

**Every pass now records every claim it would make, once, at the point its fate
is decided**, and `consumed` still decides everything. Two fields carry it:

- `ClaimFate` replaces the `honoured` bool: *honoured*, *withdrawn* (a band given
  back), *held* (something already had it), *declined* (the claimant's own rule
  refused it, and `why` names the rule), *refused* (proposed as a region, and the
  writer would not write it).
- `EvidenceClass` - the class of what decided membership, the weakest in the
  determining set; `Unstated` first, so a forgotten site reads as unstated and
  is an `EXPORT-ERROR` on a held facet rather than a silent D0.

What each pass records that it did not before:

| pass | newly recorded |
| --- | --- |
| band | a walk confined to a declared surface and then refused **for want of a ring shape** - named from the probe at the same 1e-7 its own match uses, recorded once per (facet, surface) and only where no band claim exists |
| Bezier | held facets are now examined, not skipped; patches it could not sheet; patches the writer could not bound |
| grid | its 4x-median refusal; every patch the writer does not write, with why (seam, awaiting the approximation flag) |
| quadric | its three-facet floors, its span rule, `worst > band`, a seam-split piece too small, dead patches, and the exact tier's "half a surface" refusal |

**Evidence classes, stated honestly.** A band's wall is D0 (every corner on the
declared surface to 1e-7, rims matched to 1e-7). A band's cap is D4 (absorbed
because its gap is under one ring step). A declared sweep or Bezier is D1. **A
quadric face is D4 in both tiers**: `OnQuadric::covers` holds corners to
`min(bandOf(f), max_off)` and the interior to 1.25x the facet's sagitta, so the
tessellation decides even at `max_off = 1e-7`. lid10 writes 377 facets on a
declared exact surface by D4 evidence, the bayonet 265 - the mismatch step 5
will refuse, counted now.

**Verified behaviour-neutral by content**: on 104 exports (lid10, bayonet, f02,
f04 and all 48 fixtures, both tiers) every report line other than the ledger's
own and the wall-clock time is identical, and the entity census is identical on
all 104 files. Suite 58 of 58. No `EXPORT-ERROR` on any of them, before or
after - and note that `tests/stepexportsanitytest.py` does not check for one,
so on the fixtures the shadow check is loud to nobody.

**The census reads the catalogue now.** The number it was missing - fitted or
tessellated facets that had a declaration available - is a read of the ledger:

```text
lid10 approx   759 below: 306 declined, 0 refused, 0 held, 0 withdrawn, 453 no declared claim
               124 band: far rim neither at one height nor on one plane
                76 quadric: not a simple sheet   64 band: a chord runs between the rims
                20 quadric: span rule   18 sweep: 4x-median   4 quadric: worst > band
lid10 exact   1521 below: 23 declined, 761 refused (758 sweep awaiting the flag,
               3 half a surface), 737 no declared claim
bayonet approx  18 below, all 18 the sweep's 4x-median rule
```

The "no declared claim" branch is split by provenance, and fires where it
should: `step-approximate-report` 2048 of 2048 from a maker declaring nothing,
`step-extrude-refusals` 2112, `step-revolve-axis-point` 32; lid10 zero.

**What the catalogue showed on its first read, and each is lid10 until a
fixture isolates it:**

- **The band declines *are* purity's defect, counted.** 188 facets on lid10 are
  a walk the band pass confined to a declared surface and then refused for its
  shape - *"a claim truncated to fit a form"*, which until today was in nobody's
  claim.
- **The approximation tier surrenders a closed form the exact tier writes.**
  Joined facet by facet across the two tiers' ledgers: those same 188 facets of
  declared cylinder 12 are a quadric face at the exact tier and planes at the
  approximation tier, where `max_off = inf` widens the claim to fringe facets,
  the region's worst corner then exceeds `bandOf(region)`, and the whole region
  is declined. A D4 widening and a D4 all-or-nothing test, costing D0 ground.
  This is the handover's *"the approximation loses cylinder 12"*, now with its
  rule named. Open item 30.
- **Order is policy, seen for the first time.** "provenance separates the
  claimants on 6" (was 0): facets the sweep's maker owns, refused by the sweep's
  own 4x-median statistic, then taken by a quadric face.
- **Provenance's ownership half is a measurement.** 240 honoured band walls on
  lid10 sit on a surface `owned` says their maker does not own, or has nothing
  to say about. `owned` is not carried from the generator - `mergeSurfaces`
  keeps one flat list that forgets who declared what - it is re-derived in
  `export_step.cc` from corner positions (`runsThrough`, 1e-7 absolute), a 60
  degree facing test and `least = 3`, while the band matches at 1e-7 *relative*
  to the radius. Neither is identity. Open item 31, and it sits under R0: a
  region "whose maker owns this surface" is only D0 if *owns* is.

**The ledger is data now**: `OPENSCAD_STEP_LEDGER=<file>` writes every claim as
a tab-separated row - facet, origin, holder, claimant, surface, kind, precision,
evidence, fate, whether provenance says the maker owns it, why. Facet indices
are the same in both tiers, so the 188 above is one join of two files.

## Migrating without a big bang

The suite is 58 tests and it has a known history of blind spots - the flagship
check could not fail its round trip for four days. A wholesale rewrite validated
against it is how a subtle regression ships. So:

1. **The ledger exists alongside.** Every pass records its claims into it while
   `consumed` still decides everything. Additive, behaviour unchanged, suite
   bit-identical. *(Done 2026-09-23 - see "Step 1, finished", below.)*
2. **The shadow check.** *(Done 2026-09-21.)* Derive the assignment from the
   ledger and assert it equals the one `consumed` produced, with an
   `EXPORT-ERROR` when it does not. This is the safety net that makes step 3
   safe, and it is this project's own "make should-never-happen loud" applied to
   a refactor.

   It paid for itself the first time it ran, by finding two things the ledger
   had modelled wrongly and no export would have shown:

   - **A dropped band gives its facets back.** `consumed[f] = 0`, and something
     else takes them - so the first claimant is not the holder. lid10 disagreed
     on 162 facets of 1687, the bayonet on 42 of 1651.
   - **A pass's own refusal is a withdrawal.** The grid claim's 4x-median rule
     drops facets it had already covered. Six more on each part.

   With both modelled the ledger reproduces the exporter exactly:

   | part | facets checked | agree | contested |
   | --- | --- | --- | --- |
   | lid10 | 1671 | all | **372** |
   | bayonet | 1651 | all | **496** |
   | f02 | 703 | all | 32 |
   | f04 | 1374 | all | 64 |

   The contested column is the prize: 372 facets on lid10 are wanted by more
   than one recogniser, and every one of those contests is settled today by
   which function runs first, with no record kept. Suite 54 of 54, including all
   45 fixtures, so the agreement holds on every model the suite covers.
3. **Flip the source of truth** from `consumed` to the ledger. The suite must
   not move a single number. *(Done 2026-09-23, `d64877cad`: `Arbiter::held()`
   is read from the ledger's honoured claims, every stage reads it, `consumed`
   is a cache checked against the ledger at the end of every run.)*
4. **Change policy**, one tier at a time, measuring each: this is where the 56
   contested facets get decided by precision instead of by call order, and where
   each change is a separate landing with its own numbers.
5. **Structure**: recognisers become objects with declared inputs and outputs,
   and the dependency graph is derived from those declarations and asserted
   against the real call order - which is what would have caught
   `crossing_edges` as a declaration mismatch instead of an open shell. *(The
   pipeline and its checked stage table landed 2026-09-23; see "The recognition
   module, as built", below. The recognisers themselves are still functions in
   `AnalyticFeatures.cc`.)*

Steps 1-3 change no behaviour and are individually verifiable. Step 4 is where
the wins are, and by then every one of them is measurable in isolation.

### The recognition module, as built - 2026-09-23

`src/geometry/recognition/`, STEP-agnostic. Every piece landed behaviour-neutral,
each proven by `scripts/step-dev.sh compare` reporting **0 differences over 104
exports** - the four reference parts and all 48 fixtures, both tiers, compared
by report, entity census, and the ledger and surfaces tables sorted.

| unit | what it is | reads | writes |
| --- | --- | --- | --- |
| `FacetGraph` | the mesh and its edge -> loops adjacency, built once (four passes each built their own) | mesh | - |
| `Provenance` | facet -> original; surface -> the originals that declared it, carried (`Surface::owners`) | - | - |
| `Ledger` | `Claim { facet, surface, precision, by, why, fate, surface_index, evidence }` | - | - |
| `propose*` (in `AnalyticFeatures.cc`) | **pure**: every facet each surface covers, with what its region rules read. `proposeBezierPatches`, `proposeGrids`, `proposeQuadrics` | graph, surfaces, provenance | proposals |
| `form*` (in `AnalyticFeatures.cc`) | the region rules over what the policy grants from a proposal | proposals, `held()` | regions, loser claims |
| `Arbiter` | the **only** place a facet becomes held (`settle`), and `held()` read from the ledger. A `Claimant` states once what precision and evidence class its claims carry | ledger | grants |
| `Writability` | the writer's half of "can this region be written" - sections and crossing curves - implemented by `StepKernel`, asked by the quadric stage | standing regions | yes/no each |
| `Pipeline` | six stages over one `Context`, `band -> fit -> propose -> Bezier -> grid -> quadric -> leftover`, from a **stage table declaring what each reads and writes**, checked every run | all | `Recognition` |
| `Reports` | `dumpLedger`, `reportArbitration`, `reportDeclarationCensus` - every line recognition prints about itself, as a query over the ledger | ledger, result | report lines |

Two assertions guard the structure, both mutation-checked: a stage table whose
order contradicts its declarations (swapping grid and quadric fires it - the
quadric stage's writability walks the sweep faces), and a `consumed` cache that
parts from the ledger. A unit test (`[purity]`) guards that no proposal moves
with what is held or with the order proposers run in.

**Continuing in this shape is not negotiable.** Purity - proposals that read no
claim, one arbiter that grants, a ledger that is the truth, stages that declare
what they read - is what makes "why is this facet a plane?" a lookup, what lets
policy change in one table instead of across four passes, and what makes the
covering order (D0 first, nothing falls down the ladder) enforceable at all. A
change to recognition that reaches round it - a pass reading `consumed`, a grant
outside the arbiter, a stage the table does not declare, a report computed
inside the writer - is refused, whatever it buys. Refactoring that serves this
shape is endorsed and wanted; the constraint is only that each step is proven,
by the oracle when it claims to be neutral and by a fixture when it does not.

**How to add a recogniser, now.** (1) A `propose*` that reads only the graph,
the surfaces and provenance - if it wants anything a pass holds, it is not a
proposal. (2) A `form*` over its proposals and `Arbiter::held()`. (3) A
`Claimant` naming its precision and the class of what decides it - honestly: a
declared surface in the sentence does not make it D0. (4) A row in `kStages`
saying what it reads and writes; the order check does the rest. (5) The oracle:
`measure` before and after, `compare` - and if it is meant to change behaviour,
a fixture that asserts the change first.

**Step 4's first landing, 2026-09-24: the covering order inside a claim.** A
quadric proposal states per facet what decided its membership - D0 by identity
(`Provenance::owns`) and exact position (`recognition/Exact.h`, machine
precision), D4 by the tessellation band - and the arbiter is told before any
grant (`Arbiter::stateMembership`). The claimant's region rules are statistics
over facets, so they decide only D4 members; a D4 addition that makes a region
unformable is declined and the D0 members are formed alone. D0 ground is never
the stake of a D4 rule. Open item 30 closed with it, and `step-region-pocket`'s
derived volume was reached without the band yielding anything. The next landing
is the same order *between* claimants: the band's ring and a whole-region claim,
both D0, where the arbiter must choose the one that will be written (R0).

**R0 is region first, and the region is sequence-agnostic - recorded
2026-09-24, from the author.** *"We need to make sure to not split on a
misattributed boundary, because this inevitably leaves facets behind that can
no longer be attributed. This is the core of the ledger being sequence
agnostic."* A boundary between two faces of one declared surface that the model
never drew - a band's ring here, a patch there - is whoever-ran-first written
into the file, and the facets on the wrong side of it are exactly the ones no
later pass can attribute. So R0 is not "the arbiter chooses between a ring and a
whole region" after the band pass has carved the ring; the band pass must not
carve at all:

1. **The region is a pure function of provenance and exact membership**: per
   declared surface, the connected components of facets whose maker owns it
   (identity plus presence) and whose every corner is on it at machine
   precision. The quadric proposal already computes exactly this, per facet
   (`QuadricProposal::decided == D0`), reading no claim - so it is the same
   whatever order anything runs in.
2. **The representation is chosen per region, second**: a band where the region
   *is* a ring between two rims, a trimmed patch otherwise. The ring walk
   becomes a test of a given region's shape, never the thing that decides which
   facets belong. Its seed loop, which skips facets an earlier seed's ring took,
   is sequence dependence of exactly the kind this rules out.
3. **The ledger's "declared surface is split between different kinds of
   recogniser" becomes an assertion** once this lands: a split of one connected
   D0 region is a misattributed boundary, and should be an `EXPORT-ERROR`.

Measured state before it lands, 2026-09-24: since the D0 membership landing,
the D0 remainders beside a band are written as quadric patches rather than
planes - step-region-pocket's wall (34 band + 2 patch, exact volume), lid10's
cone 48, the bayonet's cylinder 6 and cone 71. None of their facets is stranded
any more, and every one of those boundaries is misattributed.

**What is not done, named so it is not mistaken for done:**

- **Step 4, the policy.** The arbiter's grant is still today's call order - pass
  order, then surface order - held in the `form*` functions as
  `held() || taken`. Moving that decision into the arbiter's table, keyed on
  provenance first and then determinacy, is the step every earlier one exists
  for, and the design's shuffled-order test becomes meaningful with it.
- **The band pass grants inside itself.** Its walls and caps are claimed
  honoured in `recogniseSurfacesOfRevolution`'s own `Result`, which the arbiter
  then adopts. It reads no other pass, so it is pure *between* recognisers, and
  splitting it into propose and form changes nothing until the arbiter has a
  policy to apply to bands - which is exactly R0 (a whole-region claim beating a
  ring, exchanged never surrendered). So it lands with step 4, fixture first,
  not as a neutral move.
- *Done since:* every claim - honoured or lost - is recorded through one
  `recordClaim` and a `Claimant` (`fefe50911`); each recogniser is its own file
  (`5c87fdfe9`: `Bands.cc`, `Bezier.cc`, `Grids.cc`, `Quadrics.cc`, `Fits.cc`,
  `Regions.cc`, shared helpers in `Internal.h`; `AnalyticFeatures.cc` is the
  696 lines the writer calls).
- Report strings still say `STEP export:`.

### The structural work is sanctioned - do not tiptoe around it

**Recorded 2026-09-23, because a session hesitated over it.** Turning the
recognisers into classes, building a graph-shaped pipeline whose dependencies
are declared rather than discovered by breakage, making Propose pure, replacing
the 4000-line function - **all of that is wanted, not merely tolerated.** It is
step 5 above, and the five consequences at the top of this document are what the
current shape costs. A change is not too big if it is what the design asks for.

What is refused is a **wholesale rewrite validated only against the suite**, for
the reason this section already gives: that is how a subtle regression ships.
The reverted R0 is the proof - it passed 56 of 56 while turning a declared cone
on lid10 into planes, because no fixture asserted the exact tier's analytic
coverage. So refactor in the plan's steps, land each one on its own numbers, and
keep the shadow check honest while `consumed` still decides.

Two further rules the same episode earned:

- **A step that claims to change no behaviour must be shown to change none**, by
  content - the export's report, the entity census, the derived volumes - and
  never by diffing files, because the exporter is not byte-deterministic.
- **Measure both tiers.** `--enable=step-analytic-surfaces` alone is a separate
  export and the suite is weakest there.

## What this does not change

The corner arbiter stays as it is - it is already the shape this document is
arguing for, and `kCornerTiers` / `kCornerRungs` are the precedent. The
declaration channel stays as it is. This is about *which surface owns a facet*,
which is the one contest still decided by call order.
