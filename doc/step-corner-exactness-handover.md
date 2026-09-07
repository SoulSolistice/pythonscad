# Corner exactness: handover

Written 2026-09-06, at the end of a long session. `doc/step-corner-exactness.md`
is the reasoning and the baseline; this is the state of the work and what to do
next.

## Done

**2026-09-07: every fixture is exact on every exact surface.** Of 42, none puts a
corner of a plane, cylinder, cone, sphere or torus face off the surface that face
is written on. The three that still measure anything are B-spline faces, and each
is inside the tessellation band its own fit published - which is what a fitted
surface is entitled to and the most any mesh can say:

    step-band-family            7.44e-02   against a published band of 0.2077
    step-extrude-text-counter   4.27e-09   noise, as the baseline recorded
    step-extrude-text           4.24e-09   noise

The eight coupons the baseline listed are all resolved. What follows is how, and
the rest of this document is the record of getting there.

## The sweep's last stray: diagnosed, and four ways of fixing it that do not work

`step-band-family`'s B-spline reads 7.44e-02 (max 0.1015) inside a published band
of 0.2077. That allowance is the one thing in this document that moved the
goalpost: the band says how well the *surface* fits the mesh, not whether a
*corner* lies on the surface, and a corner can be projected onto a B-spline
exactly however coarse the fit is. Treat it as a marker for what follows rather
than as a principle.

**The diagnosis, and it is not about fitting.** Of the sweep face's 472 corners,
**381 lie exactly on the bore cylinder**, and the **129 that stray from the sweep
are all among them**, at r = 20.000000 to 1e-9. The other 252 lie on *both* to
1e-9, which is what a genuine crossing looks like. So every stray is a corner
where two ownerships are claimed and only the wall's is true.

**The fit is not at fault anywhere, and an earlier note here said it was.** Split
the same 472 corners by where they came from:

    the generator's own points        91 corners   none stray   max 1.42e-14
    made by the boolean, at r = 20   381 corners   129 stray    max 1.01e-01

A cubic interpolates its stations exactly, at both ends as much as in the middle,
and the measurement says so. The "a cubic has least support at the run-out"
reasoning - true of the lid, and quoted beside the 4x outlier test - does not
apply to this: it was the explanation offered here first and it is wrong.

**Why the strays cluster at u < 0.2 and u > 0.8** is the shape of the cut, not
the quality of the fit. Where the ridge is shallow - `f` tapering to zero - the
wall's cut runs *along the crest*, where the sweep and the cylinder are nearly
tangent, and a near-tangent cut lands off both surfaces. Through the middle the
ridge protrudes fully, the cut runs along its base where the two cross
transversally, and there it lands on the fit as well: those are the 252.
`step-declare-grid.py` has a constant profile, is never shallow, and strays only
at the one end its wall's bottom disc cuts - which is the same rule seen from the
other side.

**So there is no crease and nothing to continue tangentially.** The turn between
successive stations is 11.18 degrees at every station of step-band-family,
uniform end to end; the taper's kink is in the radial component only and the
helix's own turning swamps it. The surface is one smooth swept family throughout,
and trimming its ends away would discard a surface that is exactly right.

**The re-trim is viable topologically.** 374 edges are shared by the sweep and a
cylinder and **no edge in the file is used by three kinds of face**, so the whole
sweep/cylinder boundary is private to those two: re-trimming it disturbs no
faceted neighbour. That is the precondition item 3 of `step-corner-exactness.md`
assumes, and it holds.

**Four attempts, all measured, none usable:**

| attempt | result |
| --- | --- |
| move the shared corners onto both surfaces | 99 of 129 converge, but a median 0.054 and max 0.362 - relocating a corner, not refining it |
| 512 alternating iterations instead of 64 | more converge, at moves up to 0.4438; the B-spline stray does not shift at all |
| refuse a facet whose *every* corner an exact surface holds | no effect: a taper facet has only some |
| refuse a facet whose *any* corner it holds | the sweep shatters into 36 faces |
| refuse where the held corner is also the missed one | 35 faces, 673 in the file, stray only 7.44e-02 to 6.73e-02 |

The last three fail the same way, and it is the way the interior test failed
before it: **the refused facets are scattered through the region, so the claim
comes apart**. A region is not repaired by removing facets from the middle of it.

**The contiguous trim was tried on that reasoning and is also wrong.** Keeping
the longest run of stations whose facets the fit passes through collapsed the
claim to a *single facet*: the per-facet stray is scattered through the whole
region, while it is only the *boundary corner* error that is concentrated at the
ends. The two are different measurements and only the second has the signature.
Reverted.

**What is left is the intersection curve, and the diagnosis now says exactly what
it must do.** The 129 strays are the corners the mesh placed *off* the curve
where the sweep and the cylinder cross; the 252 are the ones it happened to place
on it. Trim both faces to that curve and every shared corner is exact on both,
losing no surface - there is nothing wrong with the surface. The blocker is
computing it where the two are **near-tangent**, which is precisely where the
strays are: alternating projection reaches 99 of 129, at a median move of 0.054
and a max of 0.362.

## The one sentence

A corner of an analytic face should lie on the surface that face is written on;
where a boolean made it, it belongs on the curve where its two makers cross.
Four of the eight affected fixtures now do; two more read as though they do and
have had a flat face shattered instead, which is the next thing to fix.

## Where the tree is

Branch **`claude/step-apex-fan`**, and it is *not* green: **five tests fail**,
two of them the usual `ipython-smoke` / `repl-smoke` that cannot pass from the
build tree (run them against `build/staging/pythonscad.com` directly - see
CLAUDE.md). The other three are the immediate task below.

    2689  export-step-sanitytest_step-declare-grid-scad
    2712  export-step-py-sanitytest_step-declare-grid-strip
    2713  export-step-py-sanitytest_step-declare-grid

They fail because the corner placement now fans a bent polygon into triangles,
which changes their face counts. **This is expected and the fix is not to
regenerate them.** See "the immediate task".

**Superseded on 2026-09-06.** Only one of the three was a face count to derive.
`step-declare-grid-scad` is done - `Plane=8` becomes `Plane=22`, derived in the
fixture from the two tessellations that meet at the junction - and it needed a
code fix first, because its export was not merely different but *invalid*. The
two Python fixtures are still red and **must not be brought green by taking the
number the exporter now prints**; see "the disc, and why the Python pair is
still red" below.

One trap for anyone measuring on Linux: `steproundtrip.py` needs
`TopTools_IndexedMapOfShape`, which `cadquery-ocp` 8.0 no longer exposes, so it
**skips silently** and the whole suite goes green with the round trip never run.
`pip install cadquery-ocp==7.8.1.1.post1` restores it. Two of the three failures
above are invisible without it.

Other branches, all measured and all parked with their reasons in the commit
messages - none is a candidate to land as it stands:

| branch | what it holds |
| --- | --- |
| `claude/step-corner-ownership` | corner placement before `mergeTriangles`; destroys the merge, 12-62x the faces |
| `claude/step-sweep-boundary-snap` | projecting a sweep's corners onto the sweep; takes them off the other surface |
| `claude/step-sweep-cone-guard` | refusing a cone that meets a sweep only in chords; empirical, no fixture |
| `claude/step-sweep-vertex-exactness` | refusing a sweep corner past a quarter of its band; the quarter is chosen |
| `claude/step-corner-split` | the first triangulation, superseded by what landed |

## What landed, in order

1. `feat(export): ask the model who made a facet before measuring where it lies`
   - provenance gates the quadric claim; 490/142/86 tests spared, no output change
2. `fix(export): take the interior allowance from the surface, not from the neighbours`
   - `bandOf` measured the mesh's flatness, which corner placement invalidates
3. `feat(export): put a junction corner on the curve its two owners cross along`
   - placement after recognition, where an analytic face does not mind
4. `fix(export): gate the corner placement, and stop it turning a face over`
5. `feat(export): place a corner a declared surface shares with one plane of the mesh`
6. `feat(export): fan out a planar polygon the corner placement would bend`

## The measure, and where each fixture stands

`scripts/step-occt-strict.py` reports **corner-off p95** per file - how far a
face's own corners are from the surface it is on. **Done is 0 to within 1e-9.**

    fixture                     p95            state
    step-bored-cone           8.01e-14         done
    step-bored-cylinder       7.99e-14         done
    step-declare-grid-scad    3.79e-12         done, fixture derived and updated
    step-band-family          7.44e-02         done - bspline only, inside its band
    step-cut-cone             4.45e-13         done, its two triple points placed
    step-exact-trim           3.55e-15         done, 32 triple points placed
    py-step-declare-grid      1.86e-13         done, and the flat bottom kept
    py-step-declare-grid-strip 1.86e-13        done, and the flat bottom kept

Beware the p95 on a small face: with nineteen corners it *is* the maximum, which
is why `step-cut-cone` reads 3.16e-02 for two corners out of nineteen. Look at
`straykind`-style per-kind numbers before believing a single figure.

The coupon kit is the independent check: 9 of 46 files still stray, and the band
family scales as the sagitta must - 0.102 at `$fn=24` to 0.005 at `$fn=96`.

## The fan, as landed, was wrong twice

Both defects were measured before either was touched, and both are fixed:

**It fanned from corner 0.** A fan triangulates a polygon only where every ear
it cuts winds the way the polygon does; an ear wound the other way lies outside
it, and the face written for it contradicts its own bound. The move is what
makes this bite - before it these polygons are convex and any corner would do.
After it, three corners of a bore facet sit nearly in a line along the ridge's
trim, and the fan from corner 0 cut an ear of area **-4.0e-05**: inverted, and
only just. `validatestep.py` caught it as *"PLANE normal disagrees with the
winding of its outer bound (dot=-1.000)"*, which is what
`export-step-sanitytest_step-declare-grid-scad` was actually failing on - not a
face count at all. On that fixture 2 to 3 corners of each polygon leave no ear
inverted, at a worst ear of 0.19 to 0.72, so the apex is now chosen for the ear
it leaves worst and the polygon is split only if some corner leaves none of them
inverted.

**It turned the plane to agree with the polygon.** That is what produced the
opposed PLANE above. The triangle's plane is now written the way its bound
winds, always; choosing the apex is what makes the two agree.

## The disc: fixed by aiming at the declaration

**Resolved 2026-09-06.** The section below records how it was found; this is what
it turned out to be and how it was fixed. The Python pair is green again with
`ROUNDTRIP-APPROX: Plane=4` **unchanged** - their numbers never needed deriving,
the placement needed correcting.

The corner was owned by a declared cylinder and a declared *sweep*, and those are
not worth the same: the cylinder states where its surface is, the sweep was
interpolated through the model's stations and publishes a tessellation band
saying how well. Aiming at the curve where the two cross spends the exact one to
satisfy the fitted one. For that corner:

    on the declared plane z = 0          exactly
    off the declared cylinder r = 19.2   0.0103    = 19.2(1 - cos(pi/96))
    off the fitted sweep                 0.0378    against its declared band of 0.1290

It is placed instead where its exact owner crosses the plane, and the fit only
has to agree within the band it declared. On the two fixtures that is a move of
at most 0.0847, the flat bottom stays one face, and nothing is fanned.

Two things make it safe:

- **A declaration only ever confirms a plane the mesh already carries.** It never
  invents one. `refpt` is not reliably a rim - `declare_cylinder` takes the
  caller's centre and a *fitted* cylinder's `refpt` is wherever the fit landed -
  so the test runs from the mesh's plane outwards, never from the declaration in.
- **Measured over the whole fixture set before it was written**: 1533 merged
  planes are bent by a move and **exactly 2 are vouched for** - the flat bottoms
  of `step-declare-grid.py` and `-strip`, matching a declaration's own plane to
  `0.00e+00`. The other 1531 are tessellation chords, extent 0.51 to 10.81, and
  keep being abandoned as before. No false positives.

Mutation checked: make the vouching test match nothing and exactly those two
fixtures fail again.

### How the plane is told from a chord, and why no rim declaration was needed

The first version of this asked whether a plane matched the one a declared
surface's `refpt` is anchored at. That works only where the anchor happens to be
the rim, and it left two holes: `primitives.cc` pushes one `CylinderSurface` for
`r1 == r2`, so a cylinder's *far* cap is anchored by nothing, and a sweep's end
caps never are.

Setting out to close those by declaring the rim found that neither obvious route
works. **An `ArcCurve` does not survive a boolean** - `ManifoldGeometry` carries
`surfaces_` through and has no curves at all, so a declared rim would be dropped
by the very union that creates the junction. **A new `PlaneSurface` would churn
every fixture**: the surface census in the report counts declared surfaces, so
two more per cylinder moves the `EXPECT: N analytic surfaces available` line
everywhere.

Neither is needed, because the declaration already answers the question. A chord
facet of a surface is spanned by two directions tangent to it, so its plane's
normal is **parallel** to the surface's own normal there; a plane that cuts
across - a cap - has a normal **perpendicular** to it. So the test is only
whether the plane's normal is closer to parallel or to perpendicular, which is a
midpoint rather than a tuned constant. Measured over the fixtures, with the
surface normal taken numerically so no Surface API is needed:

    cutting planes   the two flat bottoms          0.000000
                     ridge end caps, and a 6-gon   0.000003
    chords           step-declare-grid-scad        0.995 to 1.000
                     step-band-family              0.885 to 0.923

Five orders of magnitude apart with nothing between them. This asks the
declaration, needs no anchor, and closes both holes at once - the far cap and
the sweep's end caps are now vouched for as well.

`step-declare-grid-topcap.py` is the regression guard: `step-declare-grid.py`
reflected, with the wall translated so its record is anchored at the far end, so
the ridge arrives at the cap nothing anchors. Every number in it is its twin's,
a reflection being an isometry, and any number that differs is the bug. Proven
against a file known to be wrong by restoring the anchor-matching test, under
which it comes out `Plane=96` while its twin stays at `Plane=4`.

## How the disc was found, and why the pair was red

`step-declare-grid.py` and `-strip` fan exactly one face, and it is the wrong
one: a **95-corner polygon which is the part's flat bottom**. Its middle lies
**18.97** from the nearest declared surface, against 0.094 for the bore facets
of `step-declare-grid-scad`. It is not a tessellation chord of anything; it is a
plane of the model, and a corner of it is being moved 0.042 off a plane it
genuinely lies on - the trap `doc/step-corner-exactness.md` already records as
*"do not project a corner onto one of its two surfaces, it takes it off the
other"*, arrived at from the other direction.

What that costs, and why the fixture must not be updated to `Plane=96`:

    corners off the surface they are on   plane p95   6.3e-16     "done"
    the part's flat bottom                            93 faces
                                                      3 not level
                                                      worst tilted 21 degrees

A flat disc has become 93 triangles, one of them 21 degrees off horizontal, and
the measure governing this whole exercise reports perfection. That is the
handover's own first trap - a census improving while the geometry gets worse -
and taking `Plane=96` into the fixture would ratify it permanently.

### Measured: the placement mixes a declaration with a fit

Investigated 2026-09-06, on the principle that the placement should favour a
declaration, or something computed from one, over anything read off the mesh.
Doing that turns out to name the defect exactly, and it needs no threshold.

**A plane is never declared.** `Surface.h` has sphere, torus, Bezier, cylinder,
cone and grid, and no plane; `curves` held no `ArcCurve` on either fixture. So
the exporter cannot ask whether a plane is a face of the model - the channel to
say so does not exist.

**But the cap plane is derived from a declaration anyway.** `primitives.cc`
anchors a cylinder's record *at its rim* - "the rims. A frustum says what it is
... by declaring the circle at each one" - so `refpt` with `normdir` is that
rim's plane. Dumped from the two fixtures:

    step-declare-grid.py    cylinder refpt (0, 0, 0)   axis (0, 0, 1)
    step-declare-grid-scad  cylinder refpt (0, 0, 0)   axis (0, 0, 1)
                            cylinder refpt (0, 0, -1)  axis (0, 0, 1)

`z = 0` is exactly the plane of the 95-corner disc being fanned. The four faces
fanned on `-scad` are bore *facet* planes, radial normals at the inradius
19.90369, and they match no declaration at all. The distinction the section above
reaches for by measuring middles and spans is simply **stated by the model**.

**And the three declarations agree with each other.** Walking the declared rim
circle `r = 19.2, z = 0` against the declared sweep finds them crossing at
`a = 30.0008 degrees`, off the sweep by **3.8e-15**. The declarations are
mutually consistent to machine precision; it is the placement that is not.

**What the placement actually targets.** It puts the corner where its two owners
cross - but one of those owners is a *fitted* B-spline which states its own
accuracy as a tessellation band of 0.1290, and the other is an exact declared
cylinder. For the disc corner:

    on the declared plane z = 0                      exactly
    off the declared cylinder r = 19.2               0.0103   = 19.2(1-cos(pi/96))
    off the fitted sweep                             0.0378   against its band of 0.1290

So it is 0.0103 out on something exact and 0.0378 out on something that never
claimed better than 0.1290 - and the move sacrifices the exact plane to chase
the fitted surface. That is the wrong way round, and it is not a rare corner:
across the three fixtures, corners already sitting *exactly* on their exact
owner and inside the fitted owner's own band are 281 of 514, 98 of 194 and 95 of
286 - a third to a half of every move made.

**What follows.** The target should be built from the exact declarations and the
fit used only as a tolerance:

1. Collect what the corner is on that is *exact* - its declared quadric owners,
   and any plane a declaration anchors that it lies in.
2. Place it on the intersection of those. For the disc corner that is the rim
   circle `r = 19.2, z = 0`, reached by a radial move of 0.0103 which keeps
   `z = 0`, so the disc stays flat and nothing is fanned.
3. Use the fitted owner only to check the result is within its declared band -
   0.0378 against 0.1290 here, so there is nothing left to correct.

This also dissolves the `step-band-family` convergence problem below: projecting
onto exact declarations does not depend on two surfaces crossing transversally,
which is what 418 of its 704 corners fail at.

A PoC that only *skips* the corners in (3) was measured and is not sufficient on
its own - the disc corner is 0.0103 off its cylinder, so it is not skipped, and
the disc is fanned exactly as before. The move has to be re-aimed, not gated.

### What decides it, if the declarations are not used

A corner may be taken off a plane only when that plane is a **tessellation chord
of a surface the corner is being placed on**, and not when it is a face of the
model. The two cases are far apart on every measure taken:

| | bore facet, `-scad` | flat bottom, `.py` |
| --- | --- | --- |
| face's middle, off the nearest surface | 0.094 | **18.97** |
| the surface's own sagitta there | 0.0963 | - |
| the face's span about that axis | 11.25 deg, the tessellation's own | **360 deg** |

The span is the decisive one and it is not a new idea: it is the same test
`fix(export): take the interior allowance from the surface` already uses to
refuse the 134-degree bore quad, judged against the claim's own median span so
that nothing assumes a resolution. A tessellation facet of a cylinder does not
span the whole turn.

**Do not reach for a threshold on the middle instead.** It looks decisive at
0.094 against 18.97 and it is not: for an *ideal* chord facet the corners lie on
the surface and the middle is a whole sagitta off it, so the comparison inverts.
The 0.094 here passes only because these corners are themselves 0.0963 off, and
a rule resting on that is the fourth chosen threshold in a document that records
three.

The work is to ask the recogniser the question it already answers - is this
planar face a member of that surface - rather than to re-derive it in
`build_tri_body`.

## step-band-family: the cylinder half, and what the other half costs

Its 704 junction corners are the two-owner kind, the bore cylinder and the
declared sweep. 286 reach the curve where the two cross; the other 418 do not,
because the two meet at a median 15.85 degrees there against 42.70 where the
projection converges, and alternating projection wants a transversal crossing.

**Where they were left is not neutral.** Every one of the 235 corners that stray
sits on a bore facet's chord plane, exactly 20(1 - cos(pi/32)) = 0.0963 inside
the cylinder it is written on. Placed on the exact owner alone - the fit only
having to agree within the band it published - the cylinder goes exact:

    cylinder   8.89e-02  ->  1.64e-09
    plane      exact, unchanged
    bspline    7.18e-02  ->  7.44e-02   unchanged in substance
    faces      193       ->  361

**The fit may only veto a corner it has a claim on**, and getting that wrong is
what made the first attempt place 92 corners instead of 351. Requiring the sweep
to agree after the move refused 262, and **259 of those were already off the
sweep before any move**, by up to 0.3962 against its band of 0.2077 - they sit
where the ridge's base meets the wall, which is the wall's surface and not the
sweep's. A surface with no claim on a corner was vetoing its placement on the
surface it is actually on.

### What the +87% faces is, and why it is not the placement's fault

The bent faces are bore facets, fanned because a moved corner takes them out of
plane. They are bent because they are still *planes*: the report says

    1 smooth region left faceted, 169 facets in all, area 898.9,
      band 0.0847 (typical 0.0526), worst dihedral 10.6 degrees

10.6 degrees is the 32-gon's own angular step, so that region is the bore, and
the model declared the cylinder it lies on. Claimed, those 169 facets would be
analytic faces, which do not mind where their corners are, and the placement
would cost no faces at all. The face count is the price of an unclaimed
declaration, not of the corner work. A guard that skipped corners no analytic
face uses was written and measured: it dropped **zero** of them, so the fans are
not idle - it is the claim that is short.

### What is left

The sweep's own corners still stray at 7.44e-02. They are corners the sweep's
face uses that are not on the sweep - the same 259 that were never on it. That
is not a placement to be fixed but a claim that reaches past its surface, or the
boundary work in doc/step-corner-exactness.md item 3. **band-family is not
done**; half of it is, and the half that is left is a different question.

## Triple points, and the threshold that wants replacing

A corner where a declared surface meets **two** faces of the model was declined
outright - "belongs on neither conic and stays". That point is computable, and
declining it left 48 corners on `step-exact-trim`, 6 on `step-shared-arc` and 2
on `step-cut-cone` where the mesh put them. Placing them takes two of the last
three straying fixtures to exact:

    step-exact-trim   3.42e-02  ->  3.55e-15    32 of its 48 placed
    step-cut-cone     3.16e-02  ->  4.45e-13    both placed
    step-shared-arc   3.77e-15  ->  3.77e-15    unharmed

Two things had to be got right, and the first one bit hard.

**How far a corner may travel.** The line two faces cross along can meet the
surface *twice*, and the far crossing is a perfectly good solution to the wrong
problem. Bounding the move by the face's extent let a corner on
`step-shared-arc` move 2.0 and took a cone that was exact to **1.41 out** - a
fixture that had never strayed at all. The bound is the corner's nearest
neighbour: it is correcting its own tessellation, so it belongs nearer than the
vertex next to it. That also declines 16 of `step-exact-trim`'s 48, which cost
nothing - the file is exact either way, so they were not the straying ones.

**Which planes count.** Not every plane at the corner is a face; some are the
tessellation's own chords of the very surface being placed on, and a chord is
not a constraint. 16 of `step-exact-trim`'s corners have three planes of which
one is a chord, and taking all three over-determines the point.

That test is the weakest thing in this document. A chord's plane normal is
parallel to the surface's normal there and a face's is not, so the bound is the
tessellation's angular half-step - `cos(pi/6)`, nothing coarser than a hexagon
being a tessellation. Measured, chords read 0.885 to 1.000 and faces 0.000 to
0.643, and **the closest pair is `step-band-family`'s 0.885 against
`step-cut-cone`'s 0.643**. That is a real margin but a thin one, and it is an
inference standing in for something the model knew. The earlier 0.5 form of the
same test read *exactly* 0.5000 on one of `step-cut-cone`'s real cut planes - the
threshold sitting on top of the data it had to separate.

**This is what a declared plane would fix**, and it is a better reason for one
than any in the section below: it turns the classification from a threshold that
has to be defended by measurement into a lookup, and gives the fallback - still
needed for `hull()`, `minkowski()` and imported meshes, which declare nothing - a
ground truth to be validated against rather than argued about. Adding it needs
the placement to handle a corner with three or more candidates first, or the
extra declaration pushes those corners into the `many` bucket and nothing is
placed at all; that is now done, so the way is clear.

## What the declaration channel cannot say

Still true, and still worth knowing, but **no longer blocking the corner work** -
the test above needs none of it:

- **There is no plane declaration at all.** `Surface.h` has sphere, torus,
  Bezier, cylinder, cone and grid. A model cannot state that a face is flat.
- **A straight cylinder declares one rim, not two.** For `r1 == r2` only one
  `CylinderSurface` is pushed, at `z1`, and `addSurfaceUnique` would fold a
  second one into it anyway since coaxial cylinders of equal radius count as the
  same surface.
- **A grid declares no end planes**, and `curves` never reaches the exporter
  through a boolean at all: `ManifoldGeometry` has no curves member, so
  `ArcCurve` is dead outside `import_step.cc`. `StepKernel` says as much with
  `(void)curves;`.

What would buy something is the *reverse* of the corner work: a declared plane
would let the exporter write a cap as a declared face rather than recognise it,
and would give a corner on two planes a triple point to be placed at. Neither is
needed for exactness today.

## The immediate task

~~Update the three fixtures~~ - done for `step-declare-grid-scad`, and refused
for the other two with the measurement above. What it took, as a worked example
of what "derived, not captured" costs: `Plane=8` to `Plane=22` is four pieces of
bore facet fanned into eighteen triangles, and the fixture states which four
(where the ridge crosses the bore), why each has 6 or 7 corners (a facet spans
11.25 degrees, the ridge's stations are 16.875 apart and each span carries one
diagonal), and why the other four planes do not move (their corners are already
on the cylinder at r = 20 exactly). The number itself took a minute; the
paragraph took the afternoon, and it is the paragraph that would have caught the
disc.

**Next, and it is what unblocks the Python pair:** ask the recogniser whether a
planar face is a member of the surface a corner is being placed on, and refuse
the move where it is not. The deciding measurement is already taken - see the
table above.

## What to do after that

**`step-band-family` is the next real work.** Its planes are exact and its
cylinders and sweep are not, at 7e-02 to 9e-02, with 704 two-owner corners that
*are* being moved. So the corners that stray there are not the corners being
placed. Find out which they are before writing any code - that single
measurement decides everything after it.

Then `step-exact-trim`, which has not been looked at.

Then lid10, which is set aside deliberately and is the specimen for judging
blast radius, not a development target. It needs
`-p examples/step_test/lid10.json -P "New set 1"`; without it you get the
default component and every number is incomparable. That cost a whole
comparison in this session.

## Traps, all paid for once

- **A surface census is not a correctness measure.** It improved monotonically
  while the geometry got worse - lid10's faceted remainder 1216 to 968 - on an
  export whose solid was wrong by half. Only the hand-derived volume dissented.
- **The tolerance OpenCASCADE grants is not the measure either.** It is
  *largest*, 0.261, on the lid variant SOLIDWORKS reads happily.
  `DE_ShapeFixParameters` defaults `MaxTolerance3d` to 1.0, which is the licence
  it is exercising.
- **`IFace2::Check` counts are not Import Diagnostics faulty faces.** 82 and 87
  hits where the dialog shows 1; codes 13, 21 and 30 are informational, 7 and 17
  are structural.
- **Do not move a corner before `mergeTriangles`.** It keeps the neighbours
  planar and destroys the merge.
- **Do not project a corner onto one of its two surfaces.** It takes it off the
  other, and on lid10 the walls were always the worse of the two.
- **Do not ask raw triangles which plane they belong to.** Three thresholds, all
  wrong; `mergeTriangles` answers it exactly a few lines later.
- **A fan needs its apex chosen, not corner 0.** The polygons are convex before
  the move and not always after it, and one inverted ear - measured at -4.0e-05
  - writes a PLANE exactly opposite its own bound. The file is then invalid, and
  it fails looking like a face count.
- **A corner census is no more a correctness measure than a surface census.**
  The Python pair reads a plane p95 of 6.3e-16 with its flat bottom in 93 pieces,
  one tilted 21 degrees. Both traps in this list's first entry apply to the
  measure that replaced it.
- **`steproundtrip.py` does not import under `cadquery-ocp` 8.0** - the round
  trip then skips in silence and two of these three failures do not appear at
  all. Pin 7.8.1.1.post1.
- **A fan triangle needs its own plane, not the polygon's.** Getting this wrong
  made the whole triangulation look worthless - fourteen faces for three parts
  in a thousand - when it was one line and the fixture goes fully exact.
- **`quick.sh` pipes the build through `tail`**, so grepping its output for
  `FAILED` misses real failures. Use `berr.sh`, which keeps 30 lines.

## Tools

- `scripts/step-occt-strict.py --kitdir <dir>` - corner-off p95 per file
- `scripts/step-interop-kit.py --binary <abs path to pythonscad.com> --outdir <dir>`
  (the binary path must be absolute, and `.com` not `.exe`)
- `scripts/step-interop-solidworks.ps1 -KitDir <dir> -ImportSettings "..."` -
  needs SOLIDWORKS running; always label the run with its import settings
- the per-kind corner measurement used throughout this session is a short OCP
  script; it walks every face, takes each distinct vertex, and measures its
  distance to that face's own surface analytically for plane/cylinder/cone and
  by projection otherwise. Worth making permanent next to `step-occt-strict.py`.
