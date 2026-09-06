# Corner exactness: the status quo, before the rework

A corner belongs to whatever made it, and it should lie on that thing exactly.
Where two declared surfaces made it, it belongs on the curve where they cross.
Nothing in this exporter does that yet, and this document records what the tree
measures *before* the change so the blast radius can be judged afterwards rather
than argued about.

Written 2026-09-05, against `claude/step-apex-fan` at the commit that added
`corner_stray()` to `scripts/step-occt-strict.py`.

## What is wrong, in one measurement

A vertex the generator declared is on its surface to 1e-14. A vertex a boolean
made is not on anything: it sits on the chord planes of the facets that produced
it. When such a vertex becomes a corner of an analytic face, the file asserts it
is on a surface it is not on. OpenCASCADE widens the edge's tolerance until the
assertion holds and reports a valid solid - `DE_ShapeFixParameters` sets
`MaxTolerance3d` to 1.0, so it may spend a millimetre doing it. A stricter
reader refuses and rebuilds the face from what it can trust.

On the reference lid, that is the difference between two kernels agreeing about
the part's volume to 0.04% and disagreeing by 14%.

## The measure, and the definition of done

`scripts/step-occt-strict.py` reports **corner-off p95**: how far a face's own
corners are from the surface it is on, at the 95th percentile, over every
non-planar face. The p95 rather than the max, because one corner in five hundred
does not stop a kernel.

**Done is: every fixture below reads 0 to within 1e-9.**

The tolerance OpenCASCADE grants is *not* the measure and must not be used as
one. It is largest - 0.261 - on a lid variant SOLIDWORKS reads happily.

## The blast radius, measured

41 fixtures export today. Ten have a corner off a curved surface, and two of
those are at 4e-09, which is noise. **Eight coupons carry the defect:**

| fixture | faces | corner-off p95 | OCCT tolerance |
| --- | --- | --- | --- |
| `step-declare-grid-scad` | 18 | 9.63e-02 | 9.63e-02 |
| `step-band-family` | 187 | 8.86e-02 | 1.17e-01 |
| `step-bored-cylinder` | 10 | 4.62e-02 | 4.82e-02 |
| `step-bored-cone` | 10 | 4.44e-02 | 5.08e-02 |
| `step-exact-trim` | 36 | 3.42e-02 | 4.81e-02 |
| `step-cut-cone` | 4 | 3.16e-02 | 4.30e-02 |
| `py-step-declare-grid` | 11 | 2.50e-02 | 6.09e-02 |
| `py-step-declare-grid-strip` | 15 | 2.48e-02 | 6.57e-02 |

Every one is a coupon where a boolean cuts a declared surface, which is exactly
the case the rework is for. The other 33 are already exact and must stay so -
they are the regression net.

## The specimen, set aside

`examples/step_test/lid10.scad`, with its parameter set
(`-p lid10.json -P "New set 1"` - without it you get the default component, not
the lid). Mesh volume 223482.30. It is not a fixture and is deliberately not
worked against until the coupons are done; it is the part that judges the blast
radius at the end.

Where it stands now:

| lid10 variant | thread written | corner-off p95, thread / cone / cylinder | OCCT vol | SOLIDWORKS | apart |
| --- | --- | --- | --- | --- | --- |
| exact tier, thread faceted | none | - | 226945.52 | 227028.38 | **0.04%** |
| approximate, as committed | 9240 mm2 | 1.3e-02 / 3.6e-02 / 3.5e-02 | 225066.76 | 256515.31 | **14.0%** |

The walls are three times worse than the thread. That is the finding the whole
lid10 investigation ended on, and it is why the rework is about junctions rather
than about the sweep.

## What is parked, and why

Four branches, each a partial answer that measures well and does not land:

| branch | what it does | why it is not landed |
| --- | --- | --- |
| `claude/step-corner-ownership` | places a corner where its two declared owners cross - the real fix | fragments the claim: 13 trimmed quadrics become 155, single triangles come out as conical faces, 5 fixtures fail |
| `claude/step-sweep-boundary-snap` | projects a sweep's corners onto the sweep | takes them off the *other* surface they are on; needs the veto, and the veto refuses everything |
| `claude/step-sweep-cone-guard` | refuses a cone that meets a sweep only in chords | restores SOLIDWORKS knitting, but the cone-only restriction is empirical and has no fixture |
| `claude/step-sweep-vertex-exactness` | refuses a sweep corner past a quarter of its band | the quarter is chosen, not derived |

`claude/step-corner-ownership` is the one to build on. The other three are
records of what was tried.

## Where the exporter recognises instead of being told

This comes before the corner work, and not only for tidiness: a corner can be
placed exactly on a surface only if that surface has an exact position, and a
*fitted* surface does not - it was fitted to the mesh, so the mesh is where it
already is.

Under `step-approximate-surfaces` the exporter fits a surface to each smooth
region nothing declared, and then declares it on the model's behalf:

> the approximation contributes a declaration, not a face, so nothing downstream
> has to trust it

That is a sound design and it is not the problem. The problem is how much of the
output leans on it. Of the 41 fixtures, **ten rely on a fitted surface** and
**nine declare nothing at all**:

| fixture | declared | fitted | fitted as |
| --- | --- | --- | --- |
| `step-declare-grid-scad` | 3 | 3 | 1 cylinder, 2 ring |
| `step-bored-cone` | 4 | 3 | 3 ring |
| `step-bored-cylinder` | 2 | 1 | 1 ring |
| `step-cut-cone` | 1 | 1 | 1 ring |
| `py-step-declare-grid` | 2 | 1 | 1 cylinder |
| `py-step-declare-grid-strip` | 2 | 1 | 1 cylinder |
| `step-approximate-report` | **0** | 4 | 4 grid |
| `step-extrude-refusals` | **0** | 1 | 1 cylinder |
| `py-step-approximate-cylinder` | **0** | 1 | 1 cylinder |
| `py-step-approximate-turned` | **0** | 2 | 1 cone, 1 ring |

Six of the eight fixtures with inexact corners are on this list, which looked
like a cause and is not.

### Measured: the fit is not why the corners are inexact

Exporting each of the eight with the fitting pass disabled - so that only
genuinely declared surfaces can produce a face - leaves the strays **identical to
the last digit**:

| fixture | corner-off p95, with fitting | with fitting disabled |
| --- | --- | --- |
| `step-declare-grid-scad` | 9.6305e-02 | 9.6305e-02 |
| `step-band-family` | 8.9616e-02 | 8.9616e-02 |
| `step-bored-cone` | 4.7337e-02 | 4.7337e-02 |
| `step-bored-cylinder` | 4.6222e-02 | 4.6222e-02 |
| `step-exact-trim` | 3.4204e-02 | 3.4204e-02 |
| `step-cut-cone` | 3.1598e-02 | 3.1598e-02 |

**All eight are the junction case.** Not one inexact corner in the fixture set is
on a fitted surface. The overlap was a coincidence of the same coupons being
boolean-cut, which is both why they have a region to fit and why they have
junctions to get wrong.

So declaring more surfaces will not make a single corner exact. That work is
worth doing for coverage - it is why `step-approximate-report` writes 6 faces
instead of 2050 - but it is not this. The corner fix is
`claude/step-corner-ownership` and nothing else on the list substitutes for it.

### And the fitting earns its place in only half the cases

The same experiment says something about the fitting pass itself. On every
fixture that declares anything, it invents hundreds of ring cylinders and
produces **no face at all**:

| fixture | faces | with fitting disabled |
| --- | --- | --- |
| `step-declare-grid-scad` | 18 | 18 |
| `step-bored-cone` | 10 | 10 |
| `step-bored-cylinder` | 10 | 10 |
| `step-cut-cone` | 4 | 4 |
| `py-step-declare-grid` | 11 | 11 |
| `step-approximate-report` | **6** | 2050 |
| `py-step-approximate-turned` | **6** | 532 |
| `py-step-approximate-cylinder` | **3** | 66 |
| `py-step-declare-grid-strip` | **15** | 70 |
| `step-extrude-refusals` | **2121** | 2152 |

It is doing all of its work where nothing is declared, which is exactly what its
comment says it is for. Where a declaration exists it is dead weight - not
wrong, but hundreds of surfaces built and discarded. Declaring the extrudes
would move the bottom four rows into the top group and make that concrete.

### What should declare and does not

Reading what the declaration-free fixtures actually model turns the gap list in
`doc/step-export.md` from a survey into a work list:

| construct | fixture | what it could declare |
| --- | --- | --- |
| `linear_extrude(twist=)` | `step-approximate-report`, `step-extrude-refusals` | the swept net - roadmap item 2 |
| `linear_extrude(scale=)`, non-uniform | `step-extrude-refusals` | a ruled surface between the two profiles |
| an extruded ellipse | `step-extrude-refusals` | an elliptical cylinder - roadmap item 4 |
| `linear_extrude(v=)`, oblique | `step-extrude-refusals` | a cylinder along `v` |
| `rotate_extrude`, sloped segment | - | the `ConeSurface` - see the table in `doc/step-export.md` |

`step-extrude-refusals` is named for exactly this: it is the fixture of things
the exporter declines to declare, and four of the five are declarable.

### What legitimately cannot declare, and must keep fitting

`py-step-approximate-cylinder` is a hand written `polyhedron()` over a computed
point list - no generator to speak for it, which is what an imported mesh looks
like from the exporter's side. That fixture, `hull()`, `minkowski()` and an
imported STL are the cases the fitting path exists for, and they must keep
working. They are also the cases where corners can never be made exact, which is
the boundary of this whole plan and should be stated rather than discovered.

One stale note found on the way: `py-step-approximate-turned.py` says a frustum
"cannot hand over a shape... it has to hand over *rings*". `primitives.cc`
declares a `ConeSurface` since this week, so that reasoning wants re-checking
against what the fixture now does.

## Measured: why the claim fragments, and what does not fix it

Placing corners on `claude/step-corner-ownership` shatters the claim. On
`step-bored-cone`, the smallest fixture that shows it:

| | trimmed quadrics | facets | facets per face |
| --- | --- | --- | --- |
| baseline | 6 | 52 | 8.7 |
| with corner placement | 32 | 102 | 3.2 |

The claim nearly doubles, which is the placement working - corners now sit
exactly on their surfaces, so junction facets that used to fail the membership
test pass it. Then it comes out in fragments: the cone's 31 facets split into 14
pieces of 3, 1, 1, 4, 3, 3, 1, 1, 1, 2, 1, 4, 3, 3, while the bore cylinder's 20
split cleanly into 17 and 3.

**The interior test is what fragments it**, and that is this week's own work.
Disabling it takes the cone claim from 31 facets in 14 pieces to 52 in 5, one of
them 44. Two explanations were tested and refused, and are recorded so they are
not guessed again:

- *A collapsed band.* `bandOf` skips any dihedral above the smoothing angle, so
  a facet tilted by a moved corner could lose every smooth neighbour and be
  allowed nothing. Measured: **zero of 42 rejections have a zero band**. They run
  0.003 to 0.026.
- *The provenance gate would spare them.* It does not. The gate spares 400
  facet-surface tests on this fixture and the fragmentation is identical,
  because provenance *agrees* those facets are on the cone and the interior test
  rejects them anyway.

What the numbers say is that the allowance is derived from the wrong thing. The
rejected facets have `corners-off 0.000000` and interiors of 0.013 to 0.057
against allowances of 0.007 to 0.052. A facet on a cone of radius 10 at $fn=32
has a sagitta of 0.048 by construction, so those interiors are *right* - it is
the allowance that is wrong. `bandOf` measures the mesh's local flatness, the
dihedral to a neighbour, and corner placement changes dihedrals: a facet whose
neighbours happen to end up nearly coplanar is allowed 0.0033 while sitting a
correct 0.048 off the cone.

So the interior allowance has to come from the surface the facet is claimed for -
the sagitta a facet of that angular span must have on it - rather than from what
its neighbours are doing. The trap in the obvious form of that is worth writing
down: the bore quad this test exists to reject spans 134 degrees of the cone, and
a sagitta bound at that span is 6.09, just over the 6.0 it is out by. A pure
sagitta bound readmits the defect. The span itself is the giveaway - no
tessellation facet of that surface spans 134 degrees - and that is where the
next attempt should start.

## Measured: placing corners costs the merge, and that is fatal

With the allowance taken from the surface, corner placement does what it was
meant to. On `step-bored-cone` every corner lands on the surface it is written
on - cone p95 8.01e-14 against 4.44e-02, cylinder exactly 0 - the claim nearly
triples from 52 facets to 140, and the volume is 5384.26 against the derived
5382.204. The first fixture to reach what this document set as done.

And it is not usable, for a reason that was never measured until it was:

| fixture | faces before | faces after |
| --- | --- | --- |
| `step-bored-cone` | 10 | 32 |
| `step-bored-cylinder` | 10 | 126 |
| `step-declare-grid-scad` | 18 | 244 |
| `py-step-declare-grid` | 11 | 685 |
| `py-step-declare-grid-strip` | 15 | 689 |

Twelve to sixty-two times the faces. Exact corners bought with the whole of the
analytic consolidation, which is what the exporter is for.

The cause is the thing that was supposed to make it safe. Placing corners before
`mergeTriangles` does keep the faceted neighbours planar - triangles that stop
being coplanar simply do not merge, and planarity comes out at the baseline
2.26e-06 rather than the 0.0331 of a move made later. That was measured and is
true. What was not measured is that *not merging is the cost*. The mesh's facets
merge into large polygons because runs of them are coplanar; a moved corner
breaks exactly that, so every claim arrives pre-shattered and no amount of
fixing the claim afterwards puts it back.

So a corner cannot be moved in the mesh the recognisers read. Reverted; the
provenance gate and the surface-derived allowance stay, both being neutral on
output and better derived than what they replace.

### What this leaves

The requirement has not changed - a corner of an analytic face should be on the
surface that face is on, and a strict reader refuses it otherwise. What is now
ruled out is getting there by moving the mesh:

- **after `mergeTriangles`**: breaks the faceted neighbours, 0.0331 out of plane
  where they are quads, and on the reference lid 155 of the sweep's 163
  neighbours are quads.
- **before `mergeTriangles`**: keeps them planar and destroys the merge, at the
  cost above.

Which points where the edge work already pointed: the exact geometry belongs in
what is *written*, not in the mesh. A face's boundary should be the true curve
where its two surfaces cross - computable from two declarations - while the mesh
keeps the chords it has and the faceted neighbours keep meeting them. That is a
larger change than any tried here, and it is the only remaining direction that
does not trade the consolidation away.

## Where it stands after the placement

A junction corner is now put on the curve its two declared owners cross along,
after recognition - where the claims are already fixed and an analytic face does
not care whether its corners are coplanar. Two coupons reach exactness at no
cost at all, and the interop kit says so independently of the fixtures:

    9 of 46 kit files still put a face's corners off its own surface

    f01-band-fn024-analytic     1.022e-01
    f02-band-fn032-analytic     8.221e-02
    r02-bayonet-analytic        3.590e-02
    c11-swept-grid-analytic     2.457e-02
    f03-band-fn048-analytic     2.295e-02
    f04-band-fn064-analytic     2.055e-02
    r01-lid10-analytic          1.281e-02
    f05-band-fn096-analytic     4.955e-03
    c10-bspline-text-analytic   3.922e-09   (noise)

    c15-bored-cylinder-analytic and c16-bored-cone-analytic: exact

The band family is the theory measured: 0.102 at `$fn = 24` falling to 0.005 at
`$fn = 96`, twenty-fold for a four-fold resolution, which is the sagitta and
nothing else. It also says the remaining strays are not a defect to be found but
a tessellation to be placed - the same work, on corners the placement does not
yet reach.

### What the placement does not reach

Only corners whose **two** owners are declared surfaces. That leaves out the
common case: a corner where a declared quadric meets a *plane* the mesh carries
but nothing declared. `step-cut-cone` is entirely this - a declared cone cut by
a cube's face - and nothing moves there at all. So is most of `step-band-family`
and `step-declare-grid-scad`, where the placement runs and the p95 barely
shifts, 8.96e-02 to 8.89e-02 and 9.63e-02 to 9.60e-02.

A plane is an exact surface and the intersection of a quadric with one is a
conic. Widening the placement to treat a planar face as an implicit declaration
is the next step, and it is what makes the rest of this list worth doing.

#### Attempted, and how it fails

The conic itself is not the problem. Alternating projection between the declared
quadric and the plane converges exactly as it does between two declarations, and
on `step-cut-cone` it lands every corner, moving at most 0.0387. What defeated
three attempts is a smaller question: *which* plane.

At a cut corner the facets meeting there are the quadric's wall facets, the
cutting plane's, and sometimes a third - the sliver of the original base that a
tilted cut leaves behind. A corner on two planes is a triple point and must not
be moved onto either conic; a corner on one may be. So the second plane has to
be found, and every way of finding it from the raw triangles was wrong:

- **by provenance.** A frustum's cap belongs to the same original as its wall,
  so asking whether the maker owns the surface calls the cap part of the cone
  and loses the plane the corner is also on. It then moves off the base.
- **by distance, at the edge's scale.** Cut facets near the rim fall within an
  edge length of the quadric and are taken for wall facets, so no second plane
  is found at all and nothing moves.
- **by distance, at the sagitta's scale.** Better, and still wrong on a thin
  face: the base sliver's own middle is within any threshold that a wall facet
  must pass.

The mistake is common to all three - asking raw triangles which plane they
belong to, when `mergeTriangles` answers it exactly a few lines later. A merged
planar face *is* a plane, and the planes at a corner are then simply the
distinct ones among the faces using it. The placement for this case belongs in
`build_tri_body` beside the two-owner one, after the merge, where
`loop_normals` makes the question arithmetic rather than a guess. That is the
next attempt, and no fourth threshold should be tried before it.

### Splitting, implemented and parked

`claude/step-corner-split` fans out the planar polygons a moved corner would
otherwise bend, so the move can proceed instead of being declined. It works -
both exports come back closed and valid - and it is not landed, because on
today's fixtures it costs 5 and 16 faces to gain three parts in a thousand. The
corners it unblocks are not the corners that stray. It becomes worth landing
once the placement reaches the quadric-plane junctions above, and not before.

Two guards found while building it did land, being bugs either way: the
placement belongs under the approximation flag, since the exact tier asserts
nothing the mesh does not state; and a triangle, which stays planar wherever its
corners are, can still turn over when a corner crosses the line of the opposite
edge.

## The order of work

1. ~~Give the interior test an allowance that comes from the surface.~~ Done -
   `fix(export): take the interior allowance from the surface`.
2. ~~Place the corner where its two declared owners cross.~~ Tried and reverted:
   it works and costs the merge, see above.
3. **Write the boundary from the declarations rather than moving the mesh.**
   Where two declared surfaces meet, the edge between their faces is a curve
   both can be trimmed by, and it is computable from the two records. The mesh
   keeps its chords, the faceted neighbours keep meeting them, and only the
   analytic faces get the exact boundary. Nothing measured so far rules this
   out, and everything else is ruled out.
4. **Declare what is not declared**, from the work list above. This buys
   coverage rather than exactness - it moves the four fixtures that currently
   depend on fitting onto declared surfaces, and it is what makes the fitting
   pass dead weight rather than load-bearing. It does not have to precede (1).
5. **Then the fixtures**, one at a time, each moved number carrying a reason.

The placement has to happen before `mergeTriangles`, which is what makes it
safe: triangles that stop being coplanar then simply do not merge, instead of
becoming quads with a corner out of their plane. Measured, that is 2.26e-06 on
ten faces - the baseline - against 0.0331 for the same move made after
recognition.

On (5): `doc/step-export-testing.md` requires expectations be derived and not
captured. Regenerating eight fixtures' EXPECT lines from the new output would
destroy the property that caught this week's worst bug - a solid wrong by half
while every surface count said it had improved. Each number that moves needs a
sentence saying why, and that is the slow part of this plan rather than the
code.

## The full baseline

Every fixture as it exports today. Anything in this table that moves is a
regression until argued otherwise.

| fixture | faces | curved faces | corner-off p95 | OCCT tolerance |
| --- | --- | --- | --- | --- |
| `py-step-approximate-cylinder` | 3 | cyl 1 | 1.42e-14 | 1.00e-07 |
| `py-step-approximate-turned` | 6 | cone 1, sph 1 | 9.18e-15 | 1.00e-07 |
| `py-step-declare-cone` | 4 | cone 1, cyl 1 | 3.55e-15 | 1.00e-07 |
| `py-step-declare-grid-strip` | 15 | bspl 2, cyl 9 | 2.48e-02 | 6.57e-02 |
| `py-step-declare-grid` | 11 | bspl 2, cyl 5 | 2.50e-02 | 6.09e-02 |
| `py-step-declare-py` | 3 | cyl 1 | 5.33e-15 | 1.00e-07 |
| `py-step-fillet-oblique` | 26 | cyl 12, sph 8 | 2.63e-14 | 1.00e-07 |
| `py-step-fillet-refusals` | 38 | bspl 24, cyl 6 | 8.88e-16 | 1.00e-07 |
| `py-step-fillet` | 26 | cyl 12, sph 8 | 0.00e+00 | 1.00e-07 |
| `py-step-t-junction` | 6 | - | 0.00e+00 | 1.00e-07 |
| `step-approximate-report` | 6 | bspl 4 | 3.69e-15 | 1.00e-07 |
| `step-band-family` | 187 | bspl 1, cyl 9 | 8.86e-02 | 1.17e-01 |
| `step-bore` | 4 | cyl 2 | 1.78e-15 | 1.00e-07 |
| `step-bored-cone` | 10 | cone 6, cyl 2 | 4.44e-02 | 5.08e-02 |
| `step-bored-cylinder` | 10 | cyl 8 | 4.62e-02 | 4.82e-02 |
| `step-chamfered-cylinder` | 4 | cone 1, cyl 1 | 3.55e-15 | 1.00e-07 |
| `step-concave` | 8 | - | 0.00e+00 | 1.00e-07 |
| `step-cone-primitive` | 3 | cone 1 | 0.00e+00 | 1.00e-07 |
| `step-cube` | 6 | - | 0.00e+00 | 1.00e-07 |
| `step-cut-cone` | 4 | cone 2 | 3.16e-02 | 4.30e-02 |
| `step-declare-cone` | 4 | cone 1, cyl 1 | 3.55e-15 | 1.00e-07 |
| `step-declare-grid-scad` | 18 | bspl 1, cyl 9 | 9.63e-02 | 9.63e-02 |
| `step-declare` | 3 | cyl 1 | 9.43e-10 | 1.00e-07 |
| `step-disjoint` | 12 | - | 0.00e+00 | 1.00e-07 |
| `step-exact-trim` | 36 | cyl 2 | 3.42e-02 | 4.81e-02 |
| `step-extrude-circle` | 3 | cyl 1 | 5.33e-15 | 1.00e-07 |
| `step-extrude-refusals` | 2121 | cyl 1 | 1.78e-15 | 1.00e-07 |
| `step-extrude-text-counter` | 21 | bspl 19 | 4.27e-09 | 1.00e-07 |
| `step-extrude-text` | 36 | bspl 32 | 4.24e-09 | 1.00e-07 |
| `step-nested-rings` | 11 | cyl 5 | 1.42e-14 | 1.00e-07 |
| `step-oblique-trim` | 3 | cyl 1 | 3.55e-15 | 1.00e-07 |
| `step-partial-cylinder` | 26 | cyl 4 | 3.55e-15 | 1.00e-07 |
| `step-pie-slice` | 5 | cyl 1 | 0.00e+00 | 1.00e-07 |
| `step-revolve-axis-point` | 33 | - | 0.00e+00 | 1.00e-07 |
| `step-rotate-extrude` | 6 | cone 1, cyl 3 | 7.11e-15 | 1.00e-07 |
| `step-rounded-box` | 10 | cyl 4 | 9.74e-10 | 1.00e-07 |
| `step-rounded-profile` | 8 | cyl 2, tor 4 | 3.55e-14 | 1.00e-07 |
| `step-shared-arc` | 6 | cone 1, cyl 1 | 3.77e-15 | 1.00e-07 |
| `step-sphere` | 3 | sph 1 | 5.58e-16 | 1.00e-07 |
| `step-tapered-extrude` | 3 | cone 1 | 5.17e-15 | 1.00e-07 |
| `step-torus` | 1 | tor 1 | 0.00e+00 | 1.00e-07 |

total: 41 fixtures
with corners off a curved surface: 10
