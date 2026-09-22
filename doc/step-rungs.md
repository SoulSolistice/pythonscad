# What determines each entity, and therefore which rung it is

**Written 2026-09-22.** Four rungs were inserted by hand in one session. One
fixed 60 edges, one fired zero times, one was a statistic standing in for a
computation, and one made a census three times worse. Each was reasoned, none
was surveyed. This document is the survey: for every analytic entity the
exporter writes, *what fixes it*, and therefore where it belongs in the order.

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

Two consequences that have each cost a day:

- **A D4 may never be an input to a D0-D2.** A facet's plane is not a plane; a
  chord is not a constraint. The corner arbiter enforces this (`kCornerTiers`
  marks MeshPlane `used = false`); nothing else did.
- **"On the surface" is not "the right curve".** A section of a quadric by a
  chord plane lies *exactly* on the quadric and is still not a boundary of
  anything. Code 17 cannot see the difference, which is why code 17 went *up*
  when the wrong curves were removed.

## The inventory

### Curves, by what determines them

| rung | curve | determined by | class | today |
| --- | --- | --- | --- | --- |
| C0 | **no edge at all** | both sides are the *same* declared surface - there is no boundary | D0 | **not implemented**: 78 such edges on lid10 are written as a section of a surface by its own chord plane. Open item 26 |
| C1 | `CIRCLE` on a quadric's rim | the surface's own axis and radius, where both ends share an axial level | D0 | **landed 2026-09-21**, 60 edges on lid10 |
| C2 | `CIRCLE` / `ELLIPSE` / `HYPERBOLA` / `PARABOLA` | a **declared** plane cutting a declared quadric | D0 | implemented - but measured 0 of 78 on lid10 use a declared plane |
| C3 | crossing of two declared surfaces (`B_SPLINE_CURVE`) | both declarations, solved to 1e-7 of each | D2 | implemented (`intersectionArc`), reached only when both sides are *written* as analytic faces |
| C4 | a column of a declared sweep | the surface's own knots and control net | D0 | parked (`claude/step-grid-claim-parked`) |
| C5 | conic by a **facet** plane | a tessellation chord plane | D4 | implemented and firing 78/18/31 times. The wrong curve, kept only because the chord is further off the face |
| C6 | `LINE` | nothing; the tessellation | D4 | the fallback |

The gap that matters is **C0**, and it is not a curve at all: the commonest
"conic" in this exporter is the seam of one surface against itself.

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
| R0 | the whole connected region the declaration covers | D0 | **not implemented.** One surface per reference part is split across two recognisers; the region is truncated to whatever form claimed it first |
| R1 | provenance: the facets whose maker owns this surface | D0 | available (`Provenance`), used only as a *gate* |
| R2 | whatever a recogniser's own form can express | D4 | what happens today |

### Corners

Already an arbiter over candidates, with its precedence in two tables
(`kCornerTiers`, `kCornerRungs`). It is the model for the rest of this document
and is not restated here.

## What the survey says to do, in order

1. **C0 and R0 are the same defect** and come first. A region of a declared
   surface that is not claimed whole produces facets; those facets are then
   mistaken for planes by every downstream pass, which invents curves for a
   seam that should not exist. Everything else on this page is downstream of it.
2. **C2 before C5.** Once regions are whole, a section plane is either declared
   or a genuine flat face; the facet case disappears rather than being refused.
3. **C3 where two declarations genuinely meet** - already correct, and it will
   be reached more often once regions are whole.
4. **Then, and only then, retire C5.** Refusing it before its inputs are fixed
   took code 17 from 42 to 198 on lid10, because C6 is worse.

## How to add a rung without guessing

Three things, each of which was skipped by at least one rung this session:

- **Name its class.** If it is D3 or D4, it may not overrule anything and may
  not feed anything higher.
- **Say what it is measured against**, and make it something the model states.
  A statistic over the tessellation's own spread is the yardstick made of the
  error it grades.
- **Prove it fires, and prove the suite fails without it.** One rung inserted
  this session fired zero times on every reference part; the rules already
  forbid landing a guard nothing exercises, and a survey would have predicted it
  (both sides were the same surface, so there was no crossing to write).
