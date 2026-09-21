# Recognition: a design for transparency

**Written 2026-09-21**, after a session in which answering "who claimed this
facet?" needed a signature change, a new vector, a flag, a diverted claim site
and a fifteen-line report - *per pass*, with two passes still to go. The answer
it produced is worth having (lid10: **56 facets a quadric covers are held by a
band's wall**, never visible before), and the cost of getting it is the problem
this document exists to remove.

This is a design, not a record of what is built. What is built is in
`doc/step-export-development.md`; what is open is in `doc/step-export-wip.md`.

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
