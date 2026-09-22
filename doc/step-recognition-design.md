# Recognition: a design for transparency

**Written 2026-09-21**, after a session in which answering "who claimed this
facet?" needed a signature change, a new vector, a flag, a diverted claim site
and a fifteen-line report - *per pass*, with two passes still to go. The answer
it produced is worth having (lid10: **56 facets a quadric covers are held by a
band's wall**, never visible before), and the cost of getting it is the problem
this document exists to remove.

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

### R0, as landed - 2026-09-22

The band pass now measures whether the ring it found is the whole of what its
surface covers there, and gives the region up rather than truncating it.

**How it asks, and this is the part worth copying.** It asks with `OnQuadric`,
which is `recogniseQuadricPatches`'s own per-facet admission test lifted into a
shared class - lean, corners inside `max_off` and inside the mesh's own
concession, interior inside the sagitta the facet's span implies - with
provenance asked before any of it. One definition of *on this surface*, shared
by the pass that gives a region up and the pass that takes it. Two definitions
would let a band keep a partial claim because its own test disagreed with the
test that then claimed the rest, which is the defect wearing a different hat.
The extraction was landed and verified separately: byte-for-byte the same
reports and the same volumes on lid10 and `step-split-surface`, suite 56 of 56.

**And it runs after the whole seed loop, which is the only reason it works.**
The first version asked at the claim and regressed three fixtures. A sphere is
written as a *stack* of bands - one per ring of its tessellation - so at the
moment a ring is claimed its neighbours are siblings that do not exist yet;
every ring therefore looked like a truncated region, every band was given up,
one drop cascaded into the next through "the band sharing this rim was dropped",
and `step-sphere` exported as 482 planes with 448 corners off their own
surfaces. Run once every band has claimed, a ring's neighbours are *held* and
the walk stops at them, which is what the chain means. This is the same lesson
as open item 25's, reached from the other end: **claim, then revise.** The
revision runs to a fixed point, because giving one band up frees its facets and
a neighbour on the same surface should go with it rather than be left holding
half a region; dropping is monotone, so it terminates.

**What it cost and bought**, same build before and after:

| | base | R0 |
| --- | --- | --- |
| lid10 split surfaces | 1 (cone 50) | **none** |
| lid10 regions given up whole | - | surfaces 9 (4 of 76), 50 (56 of 62), 52 (36 of 61) |
| lid10 facets covered analytically | 536 band + 377 patch = 913 | 480 + 433 = **913** |
| lid10 `CIRCLE` | 300 | **410** |
| lid10 census 7 / 16 / 13 / 21 / 17 | 0 / 0 / 0 / 3 / 162 | **identical, same worst 0.108893, same breakdown** |
| lid10 faces, OCCT | 845, 1 solid, 1 shell | **845, 1 solid, 1 shell** |
| `step-split-surface` volume | 15646.585807, **-1.0489 against the model** | **15647.634695, -1.9e-11** |
| `step-bored-cylinder` approximation volume | 5298.405620 (derived 5298.405619) | **5298.405620** |
| `step-bored-cylinder` cylinder faces | 8 | **4** |

The facet count is the line to read first: **the same facets are written
analytically, in fewer faces, with more exact curves.** Nothing was given up to
a fallback. `step-bored-cylinder` is the clearest case - the same exact volume,
in six faces instead of ten, because the wall arrives at the trimmed-quadric
pass in one piece instead of in the pieces a band's ring left between its two
arc-bounded strips.

**What it did not do, and this was the expectation to lose.** `doc/step-rungs.md`
said C0 and R0 were one defect from two ends. They are not. The 78 edges on
lid10 written as a section of a surface by its own chord plane are **still 78**,
18 on the bayonet and 31 on f04 unmoved. Those facets are not ones a band left
behind; they are ones the *general* pass refused, each with a stated reason - a
claim whose boundary strays 0.1094 where the tessellation allows 0.0067, 20
facets reaching further round the axis than four times the typical span, 24
sweep facets off their fit. C0 is a refusal problem, and open item 27 is where
it goes.

**Unadjudicated.** lid10's own volume moved from 224683.668014 to 224711.283834
by OpenCASCADE's reckoning, 1.2e-4 relative. lid10 has no derived volume, so
nothing here says which is right; the direction is the one more analytic
coverage predicts, since a chord plane cuts inside the surface it replaces, and
the fixture that *does* have a derivation became exact. That is as far as the
evidence goes.

### What this means for step 4

Step 4 is not "arbitrate by precision". It is: give each claim its evidence,
rank the evidence, and let the arbiter decide - with the tie count above as the
before-measurement. A change that leaves 452 ties at 452 has done nothing,
whatever else it reports.

## Migrating without a big bang

The suite is 54 tests and it has a known history of blind spots - the flagship
check could not fail its round trip for four days. A wholesale rewrite validated
against it is how a subtle regression ships. So:

1. **The ledger exists alongside.** Every pass records its claims into it while
   `consumed` still decides everything. Additive, behaviour unchanged, suite
   bit-identical. *(Started: `Result::consumed_by` records who took each facet,
   and the grid and quadric passes record what they covered and lost.)*
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
   not move a single number.
4. **Change policy**, one tier at a time, measuring each: this is where the 56
   contested facets get decided by precision instead of by call order, and where
   each change is a separate landing with its own numbers.
5. **Structure**: recognisers become objects with declared inputs and outputs,
   and the dependency graph is derived from those declarations and asserted
   against the real call order - which is what would have caught
   `crossing_edges` as a declaration mismatch instead of an open shell.

Steps 1-3 change no behaviour and are individually verifiable. Step 4 is where
the wins are, and by then every one of them is measurable in isolation.

## What this does not change

The corner arbiter stays as it is - it is already the shape this document is
arguing for, and `kCornerTiers` / `kCornerRungs` are the precedent. The
declaration channel stays as it is. This is about *which surface owns a facet*,
which is the one contest still decided by call order.
