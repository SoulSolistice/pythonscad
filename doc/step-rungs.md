# What determines each entity, and therefore which rung it is

**Written 2026-09-22.** Four rungs were inserted by hand in one session. One
fixed 60 edges, one fired zero times, one was a statistic standing in for a
computation, and one made a census three times worse. Each was reasoned, none
was surveyed. This document is the survey: for every analytic entity the
exporter writes, *what fixes it*, and therefore where it belongs in the order.

**Every rule here is also a row of `doc/step-design-checklist.md`**, which every
change is held against before it lands.

**Read with it, since 2026-09-23:** `doc/step-recognition-design.md`,
*Determinacy: the ground this design stands on* - a D0 with **correct
provenance** is the anchor; an entity is as precise as the **independent** closed
forms that fix it (weakest class the determining set needs, then freedom left,
then corroboration); and determinacy is judged **globally**, over the whole
cover, stranded facets included. Every table below is an instance of that.

The rule the whole thing rests on, from
`doc/step-export-development.md`:

> A recogniser decides *which* facets it takes, and a writer decides *which
> curve* it writes, by what the **declaration** covers - never by what its own
> form can express.

## The qualification, and it is not "how accurate"

Rank by **what determines the answer**, not by how close the answer lands. An
entity is:

| class | meaning | may it be overruled? |
| --- | --- | --- |
| **D0 - determined** | fixed by declarations alone: closed form, no fit, no tolerance, no sample | never |
| **D1 - determined, interpolated** | fixed by the model's own stations, but reached through an interpolant that publishes a band (`declare_grid`, a Bezier patch) | only by D0 |
| **D2 - fitted to declarations** | a shape solved to agree with declared surfaces to a stated tolerance (a crossing arc at 1e-7) | by D0, D1 |
| **D3 - fitted to the mesh** | a shape fitted to tessellation vertices; correct only to the tessellation | by D0-D2 |
| **D4 - the tessellation itself** | a chord, a facet plane. States nothing about the model | by everything |

**The philosophy the ladder rests on, and everything below is a consequence of
it:** *a closed form is exact by definition and has to stay.* So the classes are
a **covering order**, not merely a tie-break. Every D0 representation is covered
first, then D1 takes what is left, then D2, then D3, and D4 takes only what
nothing above it could. A lower rank never drives a higher one, and nothing ever
falls *down* the ladder: an entity already written in closed form may be
exchanged for another closed form, never surrendered to a chord or a facet
plane. Days of debugging are in that sentence, and the one attempt that ignored
it is recorded under *Regions*, below.

Two consequences that have each cost a day:

- **A D4 may never be an input to a D0-D2.** A facet's plane is not a plane; a
  chord is not a constraint. The corner arbiter enforces this (`kCornerTiers`
  marks MeshPlane `used = false`); nothing else did - and on 2026-09-22 the
  cost of "nothing else" was measured. R0 was written with the tessellation
  band as its yardstick, landed green on 56 of 56, and took a declared cone on
  lid10 down to planes. **Everywhere but the corner arbiter this rule is an
  honour system, and the honour system has now failed once, expensively.**
- **"On the surface" is not "the right curve".** A section of a quadric by a
  chord plane lies *exactly* on the quadric and is still not a boundary of
  anything. Code 17 cannot see the difference, which is why code 17 went *up*
  when the wrong curves were removed.

## How to read a result, because the obvious metric is the wrong one

**Consume what is exact first, and protect it.** That is the whole concept, and
provenance, type and class are how it is expressed. A pass that takes whatever
it *can* take - without asking whose surface it is or whether the form it will
write is algebraically closed - is **greedy**, and greed is the thing the
classes exist to stop.

**So a rising fault census is not by itself a regression.** Code 17 counts edges
that do not lie on the face they bound. Greedy consumption *hides* such edges by
absorbing the facets into something that will tolerate them; every step towards
exactness exposes them, because an exact face is strict about its boundary where
a facet plane is not. As the foundation gains exactness the count goes **up**,
and that is the ladder working, not failing.

So the 42 to 198 that C5's removal cost on lid10 is **not** the reason C5 is
still in place, and reading it that way is the trap this section is about. The
reason is an exactness one: C5's replacement is C6, a plain chord, which lies
*further* off the face than the wrong conic does. The census measured that; it
did not decide it. Fix C5's inputs and the same removal will read as a rise
too, and will be right.

**And a census that does not move is not a statement that nothing moved.** The
R0 attempt of 2026-09-22 is the worked example: the census was *identical* -
0/0/0/3/162, same worst 0.108893, same breakdown - while the exact tier lost a
conical face and 56 facets to planes. The census could not see it.

**What to watch instead**: how much of the model is written in closed form, per
declared surface, and whether any closed form was **surrendered** rather than
exchanged. Analytic face counts by kind, exact curve counts, and the derived
volume where the model has one. Those are the measures the ladder is about.

**And coverage is not the criterion either.** A rung that consumes *less* is not
thereby worse: refusing what it cannot state exactly is the rung doing its job,
and a smaller exact claim beats a larger inexact one every time. What makes a
smaller claim a defect is not its size but its reason - a form that could not
express the region, an order that got there first, a threshold that happened to
bite. Ask why the rung declined, not how much it declined.

**The one thing a rung may never do is manufacture a boundary.** A curve is
written *because* a boundary exists; a boundary does not exist because a curve
was written. So no curve rung may take a part of a surface and split it: C5 -
the conic a *facet* plane cuts from the quadric beside it - does exactly that
78 times on lid10, writing a seam across one declared surface against itself.
That is a curve rung reaching into the surface ladder, and it is why C0 is
where the survey says to start.

## The inventory

**Each geometric primitive carries its own deterministic ladder, and that is
why this inventory is split the way it is.** What determines a circle is not
what determines a surface, and neither is what determines a region. So curves
have C0-C6, surfaces S0-S3, regions R0-R2, corners their own two tables, and
each is resolved on its own ladder, exact first. The ladders do not borrow one
another's evidence, and a decision on one may not demote an entity on another.

The R0 attempt of 2026-09-22 broke both halves of that in one change: a
**region** question was decided by a *surface-membership distance* - the
tessellation band - and the answer was then allowed to take a **surface** entity
from S0 down to S3. Two ladders crossed, and the exact one lost.

### Curves, by what determines them

| rung | curve | determined by | class | today |
| --- | --- | --- | --- | --- |
| C0 | **no edge at all** | both sides are the *same* declared surface - there is no boundary | D0 | **not implemented**: 78 such edges on lid10, 18 on the bayonet, 31 on f04, written as a section of a surface by its own chord plane. The R0 attempt of 2026-09-22 did not move any of those counts, which is the finding: they are not where a band truncated a region but where the *general* pass refused one - a claim whose boundary strays too far, a facet reaching too far round the axis, a sweep facet off its fit. Open item 27 |
| C1 | `CIRCLE` on a quadric's rim | the surface's own axis and radius, where both ends share an axial level | D0 | **landed 2026-09-21**, 60 edges on lid10 |
| C2 | `CIRCLE` / `ELLIPSE` / `HYPERBOLA` / `PARABOLA` | a **declared** plane cutting a declared quadric | D0 | implemented - but measured 0 of 78 on lid10 use a declared plane |
| C3 | crossing of two declared surfaces (`B_SPLINE_CURVE`) | both declarations, solved to 1e-7 of each | D2 | implemented (`intersectionArc`), reached only when both sides are *written* as analytic faces |
| C4 | a column of a declared sweep | the surface's own knots and control net | D0 | parked (`claude/step-grid-claim-parked`) |
| C5 | conic by a **facet** plane | a tessellation chord plane | D4 | implemented and firing 78/18/31 times. The wrong curve, kept only because the chord is further off the face |
| C6 | `LINE` | nothing; the tessellation | D4 | the fallback |

The gap that matters is **C0**, and it is not a curve at all: the commonest
"conic" in this exporter is the seam of one surface against itself. R0 was
expected to remove most of it and, measured, removed none of it - see *What the
survey says to do*, below, and open item 27.

### Surfaces, by what determines them

| rung | surface | determined by | class | today |
| --- | --- | --- | --- | --- |
| S0 | a declared quadric or plane | the model | D0 | implemented |
| S1 | a declared sweep or Bezier patch | the model's stations, interpolated | D1 | implemented |
| S2 | a fitted quadric | the approximation pass, within the tessellation band | D3 | implemented, behind `step-approximate-surfaces` |
| S3 | planar facets | the tessellation | D4 | the fallback |

### Regions - which facets a surface gets

| rung | rule | class | today |
| --- | --- | --- | --- |
| R0 | the whole connected region the surface covers | D0 | **not implemented.** Attempted 2026-09-22 and reverted; the attempt is `claude/step-region-r0-optimistic-parked` and what it measured is below |
| R1 | provenance: the facets whose maker owns this surface | D0 | available (`Provenance`), used only as a *gate*, and its ownership half is **measured** (`owned`: corners on the surface, a 60 degree band, `least = 3`). Since 2026-09-23 ownership is also **carried** - `Surface::owners` (face declarers) and `witnesses` (evidence declarers), stamped where Manifold mints the ids - and reported against the measured one; since 2026-09-24 `owned` *is* identity plus presence - *declared as a face* (carried) **and** *present* (a whole facet of the maker on it by `runsThrough`, not wholly on a declared plane) - with no band and no count. Still used as a gate only; the positive query R0 needs is next. Open item 31 |
| R2 | whatever a recogniser's own form can express | D4 | what the band pass does today |

#### What the attempt got wrong, and it is a class error

**R0 is D0**, and this table says what D0 means: *fixed by declarations alone -
no fit, no tolerance, no sample*. The implementation asked whether a facet was
within `min(bandOf(facet), max_off)` of the surface. `bandOf` is the
**tessellation band**, a D4 quantity, and `max_off` is a tolerance. So the rung
the survey calls D0 was decided by the tessellation, which is exactly what the
line above the inventory forbids:

> A D4 may never be an input to a D0-D2.

The code contradicted the document it claimed to implement, and nothing in the
suite could see that, because the suite checks results and not classes.

**The symptom, measured.** Run in the exact tier on lid10 it gave two regions
up; the trimmed-quadric writer then refused them under its own rule - *"3
trimmed quadric regions left faceted - the surface is exact but some of it is
bounded off itself, and half a surface is worse than none"* - and 56 facets that
had been an exact conical face came out as planes:

| lid10, `--enable=step-analytic-surfaces` alone | before | after |
| --- | --- | --- |
| `Cone` faces, OCCT | 9 | **8** |
| `Plane` faces | 1574 | 1630 |
| `CIRCLE` | 413 | 411 |
| `LINE` | 3466 | **3633** |

Two rules each correct alone, composing into a loss. The exact tier got less
exact, which is the one thing the rungs exist to prevent.

**What R0 actually needs**, and it is two things, not one:

1. **A region determined without a tolerance.** Provenance already has the
   answer and `export_step.cc` already derives it: per original and per surface,
   `covered` (every corner on the surface) and `covered_cut` - *"a cut facet has
   lost a corner to the boolean and still belongs to the surface the other two
   are on, which is the whole point of asking about origin rather than about
   fit"*. That is D0 and the same in both tiers. What is missing is a positive
   `Provenance` query; today it only answers `excludes`.
2. **A closed form may be exchanged, never surrendered.** A band on a declared
   cone and a trimmed patch on the same declared cone are both S0, both D0 -
   moving between them costs no rank, and that part of the idea was sound. What
   the attempt actually did was give the band up *in the hope* that the patch
   would be written, and when the writer refused it the region fell all the way
   to D4 planes. Nothing may fall down the ladder. So the band must *propose*
   its ring and the patch *propose* the whole region, and the whole-region claim
   win **only among claims that will actually be written** - propose, arbitrate,
   assemble, from `doc/step-recognition-design.md`, which is the part that is
   not built.

**And the approximation tier is not a way round it.** Confining the rule to
`--enable=step-approximate-surfaces`, where it was measured to cost nothing and
buy 110 exact circles, was tried and rejected in the same session: it would
relabel R0 as an approximation-tier rule, and R0 is D0. A D0 rung does not
become sound by being switched off where it fails.

### Corners

Already an arbiter over candidates, with its precedence in two tables
(`kCornerTiers`, `kCornerRungs`). It is the model for the rest of this document
and is not restated here.

## What the survey says to do, in order

1. ~~**C0 and R0 are the same defect** and come first.~~ **Measured
   2026-09-22, and they are not the same defect.** The R0 attempt removed every
   split surface and bought 110 more exact circles on lid10, and left C0's 78
   seam edges exactly where they were. They share a description - facets of a
   surface that an analytic face does not cover - and not a cause: R0's were
   facets a *band* left behind because its form could not hold them, C0's are
   facets the *general pass refused*, each with a reason it states.

   **But do not read that as more than it is.** What was measured is that the
   *attempted* R0 did not move C0, and that attempt took its region from the
   tessellation band. The rule that leaves C0's facets planar at the exact tier
   is `boundary_lies_on_surface` - all of a surface or none, because a boundary
   that steps on and off a surface makes a kernel take a tight tolerance from
   the majority and break on every outlier - and a region taken from
   **provenance** would reach the natural boundary instead of a ragged one, so
   it may move C0 where the attempt did not. That is **untested**, and it is the
   measurement that settles which of the two to do first. The order below still
   holds; the reason given for it was half wrong and its replacement is not yet
   measured.
2. **C2 before C5.** Once regions are whole, a section plane is either declared
   or a genuine flat face; the facet case disappears rather than being refused.
3. **C3 where two declarations genuinely meet** - already correct, and it will
   be reached more often once regions are whole.
4. **Then, and only then, retire C5.** Refusing it before its inputs are fixed
   took code 17 from 42 to 198 on lid10, because C6 is worse.

## What completing the system means, and how to tell when it is done

The point of this document is not to fix the reference parts. lid10 and the
bayonet are **specimens**: they are where the ugly cases were found, not the
thing being optimised, and `doc/step-export-development.md`, *Scope*, has said
so since it was written. A finding on one counts when it has become a synthetic
fixture that isolates it. **If the ladder is complete and correct, the reference
parts fall out of it** - there is nothing about them to hunt.

Completing it has three axes, and they fail independently.

**Breadth - every entity the exporter writes has a ladder.** Curves, surfaces,
regions and corners have one here. The one that visibly does not is the
**boundary run**: which stretch of a loop becomes a single curve is decided by
case analysis (`RimRef`'s four kinds, `Patch::Run`), not by a classed ladder,
and the cases were added one at a time. Worth surveying on the same terms as the
rest - what *determines* a run, and therefore which rung it is.

**Depth - every rung names its class and the class of its evidence.** A rung is
as exact as what decides it. **Since 2026-09-23 every claim carries its
`EvidenceClass`** (D0-D4, the weakest class in what decided it), stated once per
recogniser in a `Claimant` and recorded through one rule, and every export
reports what decided each written facet. Measured: a band's wall is D0, a band's
polar cap D4, a declared sweep or Bezier patch D1, and **a trimmed quadric face
D4 in both tiers** - `OnQuadric::covers` holds corners to `min(bandOf(f),
max_off)` and the interior to 1.25x the facet's sagitta, so the tessellation
decides even at `max_off = 1e-7`. lid10 writes 377 facets on a declared exact
surface by D4 evidence, the bayonet 265. Counted, not yet refused: refusing the
mismatch is enforcement, and it waits on the policy (migration step 4).

**Feedstock - every primitive declares what it makes.** A ladder can only reach
D0 where a declaration exists, and where none does it falls to D4 with nothing
saying so. But *no declaration today* is not *undeclarable*, and the three cases
are different things that a single list would blur:

| | | |
| --- | --- | --- |
| **read** | the table in `doc/step-export-development.md`, *What declares today* | working |
| **there to be read, and not read yet** | the 2D channel. `Outline2d` already half-carries it - arcs survive, which is why `offset(r = 2)` corners come out as cylinders - and finishing it is what makes **one derivation** serve both extruders. `linear_extrude(twist=)` becomes one branch instead of a refusal; the ellipse item dissolves, because an extruded ellipse is a `SURFACE_OF_LINEAR_EXTRUSION` over a curve the exporter already writes; a revolved segment that meets its own axis stops being a silent `continue`. See *One derivation, not five guards* | the gap |
| **no intent to read** | `polyhedron()`, `import()`, `surface()`, `projection()`, an imported mesh. Correctly undeclarable - `declare_grid` and `declare_sweep` exist as the user-facing channel for exactly these, and they are also where a corner can never be made exact | a boundary, not a gap |

`minkowski()` and `hull()` sit between the last two: minkowski drops its records
deliberately, hull declares its operands but not the blend, and for both the
question is what the operands' declarations *imply* about the result - which is
not the same as carrying a record through an operation that invalidated it.

**The third row matters to the census below**: reporting D4 for an imported STL
is not a defect found, it is the boundary being where it belongs. A census that
cannot tell "fell to the tessellation" from "nothing could have been declared
here" grades the ladder wrongly on exactly the models it is meant to open up.

**And the instrument that would make all three measurable does not exist.** The
export cannot say what class its own output is, so *"how much of this model is
determined, and where does it fall to the tessellation?"* has no answer.

**Ask it from the declarations, not from the entities.** The stronger question
is not "what class is this face" but **"is everything that could be D0 actually
D0?"** - and that is answerable today, because the declarations are already in
hand. `doc/step-export-development.md` states the doctrine already, under *a
declaration that is not taken is a bug report*:

> A declaration takes precedence over fitting and approximation through the
> whole consumer chain. Where one is not taken and should have been, the reason
> has to be found rather than worked around.

**Built 2026-09-22**, and it reports four fates, because a count that stops at
two reports the channel working correctly as a list of defects:

| fate | meaning |
| --- | --- |
| **written** | a face stands on it |
| **evidence** | something read it to identify a surface and no face stands on it. `hull()` of two coaxial cylinders declares the two *cylinders* and nothing declares the cone between them, so the band pass matches **both rims**; those two have done their job and will never be a face |
| **no mesh left** | nothing matched it because the booleans took the geometry away. `step-bored-cylinder` declares the bore's two end discs and `difference()` consumes them - nothing to write and nothing to explain. Decided by **provenance**, which is identity: is any facet of the mesh from a maker that owns this surface |
| **unexplained** | mesh on it, no face on it, nothing read it. *That* one is the bug report |

**And the headline number was mostly not a defect.** The standing figure was
*"lid10 declares 41 planes and writes 27 planar faces on a declared plane"* -
fourteen unaccounted for. Measured properly: of those 41 planes, **10 are
written, 31 have no mesh left on them, and none is unexplained.** The plane
channel is clean. What is left is small, named and curved:

```text
lid10   exact tier  25 surfaces: 13 written, 1 evidence, 7 no mesh, 4 unexplained
                    cylinder 43, cone 54, cylinder 56, sweep 65
        both flags  3 unexplained - cylinder 12, cylinder 43, cylinder 56
bayonet exact tier  23 surfaces: 11 written, 0 evidence, 7 no mesh, 5 unexplained
                    cylinder 24, cylinder 60, cylinder 67, cone 78, sweep 87
```

**Measured again 2026-09-23, after two root-cause fixes** - the specimens declare
their bayonet channel's walls, and one geometric surface is one record (two
records of one infinite cylinder had been counted as two surfaces, one of them
"unexplained"):

```text
lid10   exact tier  22 surfaces: 13 written, 1 evidence, 6 no mesh, 2 unexplained
                    cone 52, sweep 62
        both flags  1 unexplained - cylinder 12 (open item 30: the approximation
                    tier surrenders its quadric face)
bayonet exact tier  20 surfaces: 11 written, 0 evidence, 7 no mesh, 2 unexplained
                    cone 75, sweep 84
        both flags  0 unexplained
```

Indices moved with the record list; the cone and the sweep are the same
declarations as before.

Nine declarations across the two specimens, each a *reason to find*. Note the
tier split on lid10: the approximation writes cone 54 and sweep 65 and loses
cylinder 12, so the two tiers fail on different declarations.

**A fifth category was tried and refuted.** lid10's original 67 owns cylinder 39
*and* cylinder 43 over the same 120 facets, and original 40 owns cylinder 9 and
cylinder 56 the same way, which reads exactly like one surface declared twice
with the twin unwritable. `Surface::sameAs` says no on every pair on every
model: they are a ring's two walls, distinct surfaces with equal facet counts.
The bucket fired zero times and was removed rather than landed, and the pairs
stay bug reports.

**And it has to be asked per facet as well, because per declaration it cannot
see the worst case.** *Written* means a face stands on the declaration, not that
the declaration holds everything it should - one facet of a hundred reads the
same as all hundred. Worse, consider a fitted surface that consumes a region
which was really a combination of exact primitives, and leaves a few facets
behind: the declarations that should have covered them had their ground taken,
so their recognisers never proposed, **no losing claim exists to count**, and
the stranded facets have no route back to anything. Once a facet is consumed
every later pass is blind to it. That is invisible to a per-declaration count
and to the ledger's contention numbers alike.

So the export also reports what class wrote each facet:

```text
lid10  exact   909 on a declared surface, 27 on a declared plane,
                 0 fitted, 1521 left to the tessellation
       approx  1671 on a declared surface, 27 on a declared plane,
                 0 fitted,  759 left to the tessellation
step-approximate-report
       exact      0 / 2 / 0 / 2048        approx  0 / 2 / 2048 / 0
```

The last line is the tier in one row - the same model entirely tessellated
without the flag and entirely on fitted surfaces with it - and it is what
demonstrates the fitted bucket fires, so lid10's zero there is a true zero.

**`consumed` is not `consumed by the most exact claimant`, and that is the
foundation under all of it.** It is also why the rung order has to be *followed*
rather than admired: D0 is covered first precisely so a D3 fit never gets the
chance to take exact ground and strand the remainder.

**Answered since 2026-09-23, by the ledger.** Once every recogniser records
every claim it would make with its fate, a claim naming a declared surface on a
fitted or tessellated facet *is* the evidence a declaration was available, and
its fate is the reason. The export now prints *why N facets are fitted or left
to the tessellation*: declined by the claimant's own rule (named), refused by
the writer, held first by something less precise, withdrawn, or claimed for no
declaration at all (split by whether the maker declares anything). The paragraph
below is how it stood before, kept because its reasoning still holds.

**One number was deliberately missing.** How many of the fitted and tessellated
facets had a declaration *available*, which is exact ground that did not come
out exact, needs a per-facet, per-surface membership test. `owned` cannot answer it: it
names an *original's* surfaces, not a facet's, so on any model that declares
anything it reports every facet of that original as owned by every surface it
owns, and the answer comes back "all of them". That was measured and the
refinement was taken back out rather than shipped. The test that would answer it
is `export_step.cc`'s `covered` / `covered_cut`, and exposing it as a positive
`Provenance` query is the same thing R0 needs.

The census grades the ladder rather than a part, changes no behaviour, and turns
the doctrine above into a work list.

## How to add a rung without guessing

Three things, each of which was skipped by at least one rung this session:

- **Name its class, and name the class of its inputs.** If it is D3 or D4, it
  may not overrule anything and may not feed anything higher - and if its
  *evidence* is D3 or D4 then it is itself D3 or D4, whatever the entity it
  decides about. R0 was called D0 because a declared surface was in the
  sentence; what decided it was `bandOf`, and that is what fixed its class.
  Since 2026-09-23 the ledger carries both - a claim's `ClaimPrecision` and its
  `EvidenceClass` - through the recogniser's `Claimant`
  (`src/geometry/recognition/Ledger.h`); state it honestly there. Making the
  mismatch loud is the next step.
- **Say what it is measured against**, and make it something the model states.
  A statistic over the tessellation's own spread is the yardstick made of the
  error it grades.
- **Put it in the recognition module's shape.** A pure proposal, a form over
  what the arbiter grants, a `Claimant`, a row in the stage table - see
  `doc/step-recognition-design.md`, *How to add a recogniser, now*. Prove a
  structural change neutral with `scripts/step-dev.sh measure` / `compare`.
- **Prove it fires, and prove the suite fails without it.** One rung inserted
  this session fired zero times on every reference part; the rules already
  forbid landing a guard nothing exercises, and a survey would have predicted it
  (both sides were the same surface, so there was no crossing to write).
