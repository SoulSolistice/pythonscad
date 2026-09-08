# Corner exactness: handover

Started 2026-09-06 as a note on corner exactness. The work has since widened past
that - the boundary a face is trimmed to, and what the model is allowed to state
about its own surfaces, turned out to be the same problem seen from two ends - so
the file name is now narrower than the contents. It stays because four places in
the code cite it. `doc/step-corner-exactness.md` is the reasoning and the
baseline; `doc/step-export-testing.md` is the fixture doctrine and is not
optional reading.

## Read this first

**Branch `claude/step-corner-fixtures-iwpzh3`, green.** Verify before changing
anything, and again after:

    ctest --test-dir build -R step        # 44 tests, all passing 2026-09-08

Two things that make that number a lie if they are missing:

- `pip install cadquery-ocp==7.8.1.1.post1`. `steproundtrip.py` needs
  `TopTools_IndexedMapOfShape`, which `cadquery-ocp` 8.0 no longer exposes, so
  it **skips in silence** and the suite goes green with the round trip never run.
  Whole classes of failure are invisible without it.
- lid10 is the blast-radius specimen and is *not* in the suite. It needs
  `-p examples/step_test/lid10.json -P "New set 1"`; without those you get the
  default component and every number is incomparable.

### What an analytic face's boundary is now

This is the central thing that changed, and it is worth having before reading
anything else. A trimmed quadric's boundary edge is written as the most exact
thing available, in this order:

1. **A conic lying on both surfaces.** Two equal cylinders crossing meet in a
   pair of true ellipses; ELLIPSE is an entity, and it wins.
2. **The curve where two declared surfaces cross**, where that curve is not
   planar - two cylinders of unequal radius meet in a quartic ISO 10303 has no
   entity for. Found from the two *declarations* by Newton, fitted by a Bezier
   whose degree is raised until it is inside 1e-7 of both, and written as a
   SURFACE_CURVE with a PCURVE on each surface.
3. **A plane section**, exact on the face's own surface and on no other. Only
   written where the face across it writes the same one.
4. **An arc**, where the edge runs round the axis at constant height.
5. **A chord**, exact at its two ends and nowhere between. In the exact tier a
   face bounded by one is refused.

A straight edge counts as exact when its **midpoint** is on the surface, which is
the general form of "along the axis": a line meets a quadric twice unless it lies
in it, so a third point on the surface means every point is. That one line is
what lets a cone's rulings, which converge on its apex, stop being counted as
chords.

### Where the code is

All of the boundary machinery is in one anonymous namespace at the top of
`src/io/StepKernel.cc`, deliberately, because the three passes that ask about a
boundary have to get the same answer:

| what | where |
| --- | --- |
| the implicit form of a quadric, and Newton onto two at once | `quadricImplicit`, `projectOntoBoth` |
| the arc where two declared surfaces cross, and how far it misses | `intersectionArc`, `intersectionArcError` |
| the ellipse a plane cuts from a cylinder or cone, and when it closes | `planeSectionEllipse`, `sectionTiltFloor` |
| a patch's boundary cycles, and the sections found on them | `rawBoundaryCycles`, `findSections`, `boundaryCycles` |
| which sections both faces agree to write | `decide_sections`, a `std::function` inside `build_tri_body` **called twice** - see the two-meshes section |

Elsewhere: `buildPatch` in `src/geometry/AnalyticFeatures.cc` now records
`run.loop`, the face across each boundary run, which it computed anyway to split
the runs. The declarations are `declareExtrudedCylinders`, `declareExtrudedPatches`
and `declareExtrudedPlanes` in `src/geometry/linear_extrude.cc`, and
`declareSurfacesOfRevolution` in `src/geometry/rotate_extrude.cc`. On the test
side, `check_surface_curves` and `_face_reaches_cone_apex` in
`tests/validatestep.py` are new, and `EDGES-APPROX:` joined the directive set in
`tests/stepexportsanitytest.py`.

### The two rules that govern changes here

Both were violated once each in the session that wrote this, caught, and are
recorded because they are easy to violate again.

- **Derived, not captured.** An expectation must be worked out from the model,
  not read off a run. Where a figure genuinely cannot be derived, say so in the
  fixture and state it anyway - `step-cylinder-cross` does that for its lobe
  halving. A *plausible* reason is worse than none: a line here claimed sixteen
  ellipses survived "because the tilt test excludes the rest", and the tilt test
  admits twenty-eight of the thirty-two.
- **No circular validation.** Never relax a check because your own change broke
  it. Every relaxation must name evidence the *file* has to show, never a claim
  the exporter makes about itself - the first draft of the three-edge rule
  triggered on the file merely containing an ELLIPSE, so the check and the thing
  checked agreed by construction. Mutation-check every relaxation: reintroduce
  the defect it exists to catch and watch it fail.

## What landed, in order

The first six are 2026-09-06; everything from 7 on is 2026-09-07. There is one
more between 6 and 7 that is worth knowing about -
`feat(export): place a corner on its exact owner when the two will not converge`,
which is what the "how the disc was found" section below ends in.

1. `feat(export): ask the model who made a facet before measuring where it lies`
   - provenance gates the quadric claim; 490/142/86 tests spared, no output change
2. `fix(export): take the interior allowance from the surface, not from the neighbours`
   - `bandOf` measured the mesh's flatness, which corner placement invalidates
3. `feat(export): put a junction corner on the curve its two owners cross along`
   - placement after recognition, where an analytic face does not mind
4. `fix(export): gate the corner placement, and stop it turning a face over`
5. `feat(export): place a corner a declared surface shares with one plane of the mesh`
6. `feat(export): fan out a planar polygon the corner placement would bend`
7. `feat(export): finish band-family, and run the approximation tier on every fixture`
   - 32 of 43 fixtures had never exported the approximation tier at all
8. `test(export): a fixture whose trim curve has a closed form`
   - `step-cylinder-cross`, the Steinmetz coupon, committed *before* the code and
     with its volume deliberately unasserted
9. `feat(export): trim a quadric to the ellipse it is cut by, not to its chords`
   - the plane section, and the joint decision that keeps the shell closed
10. `feat(export): a cone's plane section, the planar face across it, and rulings
    that converge` - and the midpoint test, which was the unplanned one and worth
    more than either
11. `feat(export): trim two quadrics to the curve where they actually cross`
    and `feat(export): say a crossing curve lies on both surfaces, with a pcurve on each`
12. `feat(export): declare the planes an extrude sweeps, and write faces on them`

### The numbers worth carrying

Each is a volume derived from the model, measured by a kernel that never saw the
arithmetic:

    step-cylinder-cross   16 r^3/3             5333.33333    5333.333262  exact tier
    step-cut-cone         a z-slice integral   1661.646512   1661.646504  approximation
    step-bored-cylinder   an elliptic integral 5298.405619   5298.405620  approximation
    step-bored-cone       likewise             5382.203842   5382.205081  approximation

The tier matters. The three approximation figures need the corner placement to
have run, which puts the junction vertices on both surfaces and is what makes the
crossing curve available at all; the exact tier of those three has no such
placement, keeps its plane sections, and measures further out. The bottom two are
the coupons this document once called "inherently an approximation".

### Parked experiments

All measured, all rejected, reasons in their commit messages. None is a candidate
to land as it stands:

| branch | what it holds |
| --- | --- |
| `claude/step-corner-ownership` | corner placement before `mergeTriangles`; destroys the merge, 12-62x the faces |
| `claude/step-sweep-boundary-snap` | projecting a sweep's corners onto the sweep; takes them off the other surface |
| `claude/step-sweep-cone-guard` | refusing a cone that meets a sweep only in chords; empirical, no fixture |
| `claude/step-sweep-vertex-exactness` | refusing a sweep corner past a quarter of its band; the quarter is chosen |
| `claude/step-corner-split` | the first triangulation, superseded by what landed |

---

**Everything below is the record of how it was arrived at**, oldest first within
each topic. It is kept because the measurements in it are expensive and several
of the dead ends look attractive on a second reading. Where a section has been
overtaken, it says so at its head.

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

Still worth knowing, but **no longer blocking the corner work** - the test above
needs none of it:

- ~~**There is no plane declaration at all.**~~ Done, twice over: `PlaneSurface`
  is in `Surface.h`; `cube()` and `cylinder()` declare their planes, and so now
  do `linear_extrude` (both caps always, plus a plane per straight wall) and
  `rotate_extrude` (the flat annuli, and a partial sweep's two end faces). The
  exporter writes a planar face on a declared plane in preference to fitting one.
  See "declaring the planes, and what each extrude parameter does with them".
  What is still missing is *provenance* coverage: on lid10 only **14 of 25**
  declared surfaces are mapped onto an original at all.
- **A straight cylinder declares one rim, not two.** For `r1 == r2` only one
  `CylinderSurface` is pushed, at `z1`, and `addSurfaceUnique` would fold a
  second one into it anyway since coaxial cylinders of equal radius count as the
  same surface.
- **A grid declares no end planes**, and `curves` never reaches the exporter
  through a boolean at all: `ManifoldGeometry` has no curves member, so
  `ArcCurve` is dead outside `import_step.cc`. `StepKernel` says as much with
  `(void)curves;`.

The first of those - a declared plane letting the exporter write a cap as a
declared face rather than recognise it - is what landed on 2026-09-07. The
second, giving a corner on two planes a triple point to be placed at, has not
been tried and is still available.

## The curve trim: a plane section written as the conic it is

The trim boundary of a quadric face was always the mesh's polyline, and the
exact tier refused any face whose boundary left its own surface - which is every
face cut by a boolean, because a chord of a curve is not on the curve. That
refusal was correct and it was also the last thing standing between this
exporter and an analytic body.

A boundary edge can now be honest about a curved surface three ways rather than
two: along the axis (a line), round it at constant height (an arc), or **across
it in a plane** (a conic). A plane section of a cylinder is an ellipse, exact,
and writable as one.

**The mesh does not hand the section over.** `Patch::Run` splits a boundary
wherever the neighbouring face changes, so one ellipse arrives as thirty-three
runs of a single edge each. `coplanarStretches()` finds them by walking the
boundary cycle instead, and it walks it **as a circle**: grow a plane from every
vertex, take the longest, consume its edges, repeat. Two earlier versions
scanned left to right from index 0 and both were wrong for the same reason - a
cycle has no first vertex of its own, so a stretch straddling that arbitrary
seam is reported as two shorter ones, and the neighbouring face, whose seam is
elsewhere, then disagrees about where the curve begins. Two faces that disagree
cannot share an edge.

Three specific traps, each of which cost a build:

- **Seeking "where the boundary turns" to find a starting point does not work.**
  Every triple along an ellipse is non-collinear, so that lands in the middle of
  an arc. Seek where the *plane* changes.
- **Cutting the cycle at a wrapping stretch's own start is not enough.** It
  frees that stretch and pushes the next one across the new seam, whenever the
  two meet end to end - which is exactly how two ellipse arcs meet at a
  Steinmetz pinch. The seam has to land on an edge **no stretch covers**.
- **The closing edge is a real edge.** Building a cycle from runs and then
  iterating `i + 1 < n` silently exempts the edge from the last vertex to the
  first from the boundary test.

### The joint decision, which is the whole of the difficulty

An ellipse one face writes and its neighbour does not is a hole in the shell.
`step-bored-cylinder`'s approximation tier proved it: twelve edges used by one
face, an invalid export, on a change that had left every other fixture alone.

So a section is written only where it turns up **twice** among the faces still
standing - `section_curves`, keyed by the set of mesh vertices the stretch runs
through, which both sides arrive at identically because the search is
seam-independent.

That makes the two decisions circular: a face may be writable only because a
section covers its chords, and the section is only available while that face
stands. They are settled together, by dropping whatever the last round refused
and asking again. Refusals only grow, so it terminates.

### The fixture, and why it is the one that could not be faked

`step-cylinder-cross` - two equal cylinders crossing at right angles, the
Steinmetz solid. It was committed *before* the code, stating what the export
should be and deliberately leaving the volume unasserted, because asserting the
5320.49 the exporter then produced would have pinned the defect in place.

Everything about it is derived from the model: the planes `z = y` and `z = -y`,
the ellipse semi-axes `r` and `r*sqrt(2)`, the surviving region
`|z| <= r*|sin theta|` and hence eight faces, and each face's three edges - two
elliptical arcs meeting at the pinch plus one ruling.

**Three edges, not four**, is the shape the validator did not know. Its floor of
four for a face of revolution is right for the reason it gives - a rim runs at
constant height and a ruling at constant angle, so in the surface's own
(angle, height) rectangle every side is axis aligned and two of them never meet.
A plane section is not axis aligned: it climbs as it goes round, so two of them
cross, and where they do the region closes to a point on its own. Same for "a
partial face needs two distinct end edges": one ruling is the whole of the ends
when the other end is a pinch. Both rules were widened to name that shape, and
both still require the pinch to be really there - two elliptical arcs sharing a
vertex.

And the figure none of the census lines can fake: the Steinmetz volume is
exactly `16*r^3/3 = 5333.33333`, no pi in it. OpenCASCADE measures the exported
solid at **5333.333262**, 1.3e-8 relative. With the chorded trim the same export
measured 5320.49 - 0.24 per cent low. **Nothing else in the fixture's census
moved when the trim was fixed except the surface names.** The volume is the only
line that knew the difference, which is the argument for `VOLUME:` in general.

## The cone's section, the planar neighbour, and the two meshes

The curve trim was cylinder-only and needed a quadric on both sides, so it fired
on exactly one model. Both limits are gone, and removing them turned up two
things that were wrong for longer than either.

### A plane section of a cone

`planeSectionEllipse` answers for both surfaces. The cone is worked in its plane
of symmetry - the one containing the axis and perpendicular to the cut - where
the cone shows as its two outermost generators and the cutting plane as a line.
The two crossings are the ends of the major axis, so the centre and `a` follow;
`b` is the half chord through the centre at right angles, and the cone's own
quadratic gives it in one square root because that direction is perpendicular to
the axis and to the major axis both, so every linear term drops out.

    b^2 = h^2 / cos^2(alpha) - |C - apex|^2,   h = (C - apex) . axis

Checked twice before any code was written, and the second way is worth keeping
because a fixture can state it: **b is the geometric mean of the cone's radii at
the two ends of the major axis.** On `step-cut-cone` both give 9.268985, and the
exporter writes 9.268985.

The tilt test is not optional: a plane cuts a cone in an ellipse only while it
crosses every generator, `|n . axis| > sin(alpha)`; at the half angle it is a
parabola and past it a hyperbola, and neither closes.

### The plane comes from a face, not from the boundary

The first version fitted a plane to three consecutive boundary vertices and grew
it along the cycle. That is the weaker way round and it has a hard floor: it
needs two edges before it can see a plane at all. On `step-cut-cone` two of the
four regions meet the cut along a **single edge**, so the fit could never see it,
and the whole surface was refused for those two edges.

Now the planes are gathered per *surface* from the faces adjacent to any of its
regions, and each boundary edge is tested against them. A plane that cuts a cone
cuts all of it, so the region that meets the cut along one edge is told what the
plane is by the region that meets it along thirteen. The fit remains, last, for
the case where nothing planar is in the picture at all - two cylinders crossing
meet along an ellipse that is nobody's face.

### A declared plane in preference to a fitted one, and why it is not a nicety

Where a declared `PlaneSurface` agrees with the neighbouring face, its exact
normal and point are used instead of the fit. The export now says which:

    step-cut-cone            2 sections, 2 declared,  0 fitted
    step-cylinder-cross      8 sections, 0 declared,  8 fitted
    step-bored-cylinder     20 sections, 0 declared, 20 fitted
    step-declare-grid-scad  32 sections, 0 declared, 32 fitted

The 20 and the 32 are *bore facet planes* - artefacts of `$fn`, not of the model.
The curve is exact against the mesh, but its input moves when the tessellation
does.

And the measurement that settles the argument: **only a declared plane survives
the corner placement.** On `step-band-family` 180 sections were agreed before the
placement ran and none after it, every one of them on a fitted facet plane. That
is not a coincidence and it is not a tolerance to widen: the placement moves a
corner onto the surface the model *declared*, which is exactly off the facet
plane it happened to share with a neighbour. Where the plane is itself declared
the corner stays in it, because that is what it was moved onto.

### One decision, taken twice, on two different meshes

That is also the shape of the bug it uncovered. The corner placement runs
*between* the pass that decides which faces are analytic and the pass that writes
them, and it moves vertices - by up to 0.1536 on `step-band-family`. A section
agreed on the first mesh need not exist on the second, and a curve one face
writes while its neighbour has stopped seeing it is a hole in the shell: 132
edges with one face, 44 ellipses and the 88 chords their neighbours still wrote.

`decide_sections` is therefore a function rather than a step, and it is asked
twice - once while the faces are being settled, once immediately before they are
written, the second answer being the one that is written. Where the two differ
the export says so, because in the exact tier it means a face was called exact
partly on a section that is now chords.

**The trap to avoid here is a vertex checksum apart.** Two calls to one
deterministic function returned different answers, and four rounds of reading the
code found nothing, because the inputs named in the call were all identical - it
was the mesh underneath them that had moved. Checksumming `vertices` at both
points took one build and ended it.

### What the midpoint test found, which was none of the above

Widening the sections turned up a straight-edge bug that had nothing to do with
them. `boundary_lies_on_surface` accepted an edge "along the axis, or around it
at constant height" - and *along the axis* is a cylinder's answer. A cone's
rulings run to its apex, so every generator was being counted a chord and every
region bounded by one refused.

The test that replaces it is one line and does not enumerate cases: **a straight
edge lies on a quadric when its midpoint does.** A line meets a quadric twice
unless it lies in it, so a third point on the surface means every point is. That
alone took four models from refusing 50 regions between them to writing all of
them.

### What it did to the specimen

lid10, exported with `-p examples/step_test/lid10.json -P "New set 1"`:

    trimmed quadrics written      11  ->  40
    regions refused for their boundary   30  ->   1
    ELLIPSE edges                  0  -> 253
    validatestep complaints        2  ->   0

The two complaints it used to carry were cylindrical faces bounded by one rim,
and they are gone because the export changed, not because a rule did - the apex
exception is conical only and never applied to them.

One thing it does *not* fix, and the check that says so: `steproundtrip.py` still
reports a PLANE face with a corner 2.2623e-06 off its own surface against an
allowance of 1.2595e-06. That is not this work. The **plain faceted export, with
the analytic pass switched off entirely, reports the identical figure** - lid10's
mesh has a facet its own vertices are not coplanar to, and the invariant's
`1e-8 * extent` is marginally too tight at this model's size. Worth settling on
its own; measuring the faceted export first is what keeps it from being blamed on
whatever changed last.

### The validator, and not letting it agree with the exporter

Three rules had to widen, and widening a validator to accept what the exporter
now writes is how a suite stops testing anything. Each is tied to evidence the
file has to *show*, not to a curve it merely contains:

- three edges on a face of revolution, where two elliptical arcs are found
  sharing a vertex (the pinch) or the cone's apex is found among the face's own
  vertices - the apex computed from the surface's placement, not read off the
  boundary;
- one rim instead of two, under the same apex evidence;
- an ELLIPSE bounding a CONICAL_SURFACE, which is simply true below the half
  angle.

The first draft of the first rule relaxed on `has_ellipse` - the presence of the
curve, which is the exporter's own claim - and that is the circularity. It is
fixed.

One rule *strengthened* at the same time, and it is the one that pays: every
bounding ELLIPSE is now walked at eight parameters and each point asked of the
surface. That catches a centre in the wrong place or a major axis aimed the wrong
way, either of which leaves both radii correct and the rim off the face.

Mutation checked, and the check is worth repeating whenever these rules move:
nudging one CONICAL_SURFACE's half angle by 0.01 rad in an accepted export - no
vertex touched, every loop still closed - moves the computed apex off the face's
corner, and the *only* complaint is the edge-count rule. The relaxation is
carrying its weight.

## Declaring the planes, and what each extrude parameter does with them

Asked which entities still rest on the mesh, the answer split in two: curved
geometry written as facets, and *flat* geometry written from a fit rather than
from a declaration. The second is the larger population and the easier fix.

Before this, **only `cube()` and `cylinder()` declared planes.** Every other flat
face - every extrude cap, every straight extrude wall, every revolved annulus -
was written on a plane fitted to its own corners. That plane is exact as a plane,
so nothing in the suite complained; what it is not is *stable*, and the cost was
measured this session: on `step-band-family` 180 plane sections were agreed
before the corner placement and none after, every one of them on a fitted facet
plane.

Three things landed:

- `linear_extrude` declares its two caps always, and a plane per straight profile
  edge where the sweep keeps them parallel;
- `rotate_extrude` declares the flat annulus under any profile edge at constant
  height, and the two end faces of a partial sweep;
- the exporter *uses* a declared plane when writing a planar face, in preference
  to fitting one - accepted only when every one of the face's own corners is
  already in it, so this replaces the coefficients and moves nothing.

Measured, and each probe declares exactly what it uses:

    cube                     6 declared,  6 faces on them
    linear_extrude(square)   6            6
    linear_extrude(circle)   2            2   (the wall is the cylinder)
    linear_extrude(twist)    2            2   (caps only, walls refused)
    linear_extrude(offset)   6            6
    text                    17           17
    rotate_extrude          2            2
    rotate_extrude(120 deg)  4            4   (two annuli, two end faces)
    lid10                   41           27

### Which parameter does what

Every parameter of a `linear_extrude` is on the node and reachable where the
declarations are made. What each one costs:

| parameter | effect on the declaration |
| --- | --- |
| `height`, `v` | the sweep vector. An oblique `v` refuses the *cylinder* - an oblique cylinder is a real surface but not a CYLINDRICAL_SURFACE - and costs the caps and straight walls nothing |
| `center` | folded into the base height; nothing to refuse |
| `convexity` | a rendering hint, no geometry |
| `scale` uniform | cylinder becomes a cone; a straight wall stays planar, because A'B' = s*AB is parallel to AB |
| `scale` uneven | refuses the curved claim, and refuses a wall unless the edge runs along x or y - which are exactly the edges an uneven scale leaves parallel |
| `$fn` | never read. The declarations come from the arc and Bezier records and from the profile's straight edges, so they do not move with the tessellation - which is the point of them |
| `slices` | **never read.** It only says how finely the twist is tessellated |
| `twist` | **the one real gap.** Caps still declared; everything else refused |

A twist is not undeclarable in principle, and this is worth stating because the
parameters do determine it completely. A straight profile edge under a
continuous twist sweeps a ruled helicoidal surface, which ISO 10303 has no
primitive for - but the `GridSurface` channel already in this codebase writes
exactly that case, as a B-spline through a grid of stations, and a twisted
extrusion can compute its stations exactly from `height`, `v`, `twist` and
`scale` without reference to `slices`. It would land in the approximation tier
for the same reason `declare_grid` does: OpenSCAD's mesh for a twisted extrude is
the `slices` polyline, so the declared smooth surface and the mesh differ by the
slice sagitta, and only the approximation tier is allowed to spend that.

### The fixtures that had to move, and whether that was circular

Three fixtures asserted `no analytic surfaces were declared`, which is the whole
surface list being empty. Planes now populate it, so all three failed, and
rewriting an assertion because your own change broke it is exactly the shape of
circular validation. It was checked rather than argued:

- the rewritten line, `0 analytic surfaces available (0 cylindrical, 0 spherical,
  0 toroidal, 0 Bezier)`, is narrower but still names the defect class these
  fixtures exist for - a wrong *curved* claim;
- **mutation checked**: letting `declareExtrudedCylinders` through under a twist,
  which is that defect exactly, makes the rewritten line fail;
- the exhaustive census - `ROUNDTRIP: Plane=2152` with no Cylinder, Cone, Sphere,
  Torus or BSplineSurface beside it - was never touched and is the real guard;
- and the part that *was* weakened, the plane channel, is pinned again wherever
  the number is derivable: `step-concave` states 8 because its profile is a
  hexagon and an extrusion has two caps, `step-revolve-axis-point` states 1
  because one of its three profile edges lies at constant height, the second is
  the axis and the third is the cone. `step-extrude-refusals` is left unpinned
  and says why: its third body is an ellipse whose arc record was dropped, so
  what it declares is one plane per tessellation chord and the count is the
  mesher's, not the model's.

## The crossing curve: a trim that lies on both surfaces

A plane section lies exactly on the surface it is a section of, and only on that
one. Where the boundary is the curve two *surfaces* cross along, a section is
still the wrong curve - it is exact on one side and wrong on the other by
whatever the two disagree by. On `step-bored-cylinder` that was measured at
0.00992: the arcs sit on the wall to 1e-15 and leave the bore by up to a
hundredth.

What replaces them is the curve itself. Two quadrics meet in a quartic in
general - two cylinders of unequal radius crossing is the everyday case - and ISO
10303 has no entity for one. OpenCASCADE's own export of such a solid writes a
degree-7 B_SPLINE_CURVE_WITH_KNOTS of some thirty control points, so
approximating is not a shortcut here: it is what the format offers. Three things
make it honest rather than a fudge:

- it approximates the **true curve**, found from the two *declarations* by
  Newton, not the mesh's polyline;
- it is held to a **stated tolerance** - the same 1e-7 the exact tier holds every
  other boundary to - by raising the Bezier's degree until the fit meets it,
  because how much of the curve one mesh edge spans is not something the exporter
  can know in advance. A cubic leaves the surfaces by 3.4e-05 on this coupon:
  three hundred times better than the chords it replaces, and still not exact;
- **both faces derive it from the same two declarations**, so they agree on it
  exactly without either having to know what the other did. That is the joint
  decision the plane sections needed a claim count for, obtained here for
  nothing - and it is the same argument for declarations that the section planes
  made, one step further along.

### What it is worth

`step-bored-cylinder`'s approximation export, whose volume is derived
independently as an elliptic integral:

    derived                       5298.405619
    chords                        5301.57      (short by 3.16)
    plane sections                5298.921138  (over by 0.5155)
    the crossing curve            5298.405620  (over by 1e-06)

Two parts in 1e13. The coupon that opens this document's roadmap as "the general
case at its smallest, and inherently an approximation" now exports as the exact
solid.

### An exact conic beats a fitted curve, and planarity is the test

Two *equal* cylinders crossing meet in a pair of true ellipses, and ELLIPSE is an
entity. Taking those edges as fitted B-splines would trade an exact curve for an
approximation of the same curve, which is what the first version did to
`step-cylinder-cross` - all eight of its ellipse arcs became B-splines.

The discriminator is planarity, and it is a property of the two surfaces rather
than of anything the exporter chose: fit the arc, take the smallest eigenvalue of
its control points' covariance, and if the arc is flat to 1e-9 leave it to the
plane-section pass. Flat means conic, and a conic has an entity of its own.

So the boundary of an analytic face is now decided in four tiers, most exact
first: a conic that lies on both surfaces; the fitted curve where they cross; a
plane section, exact on one; and a chord, exact at its ends only.

### The pcurves, and the check without which they are decoration

A curve on two surfaces should *say* it is on them, and STEP's word for that is
SURFACE_CURVE with a PCURVE on each. Each pcurve is a Bezier in that surface's
own (u, v), fitted through the parameter images of the very points the 3D curve
was fitted through, so the two agree at the collocation parameters by
construction. The parameter has to be unwrapped along the samples: a surface of
revolution's u wraps, and a curve stepping over the seam must keep counting.

Two things make this easy to get silently wrong, and both are why
`check_surface_curves` exists in validatestep.py:

- The 3D curve is written `.CURVE_3D.`, which makes it definitive. A reader takes
  it and ignores the pcurves, so a pcurve that is wrong changes nothing a face
  count, a shell check, a round trip or a volume would notice. It is invisible
  until some other kernel prefers the parameter space - which is the one place it
  cannot be debugged.
- The reference direction is chosen from **each face's own boundary**, so two
  faces on one cylinder do not share a parameter origin. A pcurve written against
  the wrong face's placement is off by a rotation and looks entirely reasonable.

So the check walks each 2D curve, puts every (u, v) through the basis surface's
own parametrisation, and asks whether the point that comes out is where the 3D
curve is at the same parameter. Measured agreement on `step-bored-cylinder`:
6.6e-08, which is the curve's own fit tolerance and not more. Mutation checked -
perturbing one pcurve control point by 1e-3 in u, touching nothing else, produces
exactly one complaint.

The sweep's own pcurves, which are Line2d on a B-spline surface, are not covered
by this: mapping through a B-spline surface's parametrisation is a bigger piece
than mapping through a quadric's, and it is worth doing next time that path
moves.

### Where it does not fire, and why that is right

The **exact tier** of `step-bored-cylinder` writes no crossing curves, because
its corner placement does not run, so its junction vertices are up to 0.0189 off
the second surface and no arc through them is on both. The curve is only
available where the corners are already on it - which is exactly the condition
the placement pass exists to create, and the two passes are worth reading
together.

## Measured 2026-09-07: what the remaining refusals were blocked on

**Superseded the same day by the section above** - all five of these now write
their quadrics, and the reason the measurement below did not predict it is worth
keeping: the experiment relaxed the two things it was asking about and nothing
moved, because the actual blocker was a third thing neither relaxation touched
(a cone's generators being counted as chords). A measurement that says "neither
of your two options" is not a measurement that says "nothing will help". Kept as
written, for that.

Asked which is the better follow-up - declaring more entities, or teaching a
planar face to carry a conic - and the measurement said **neither, except in one
place**. Five models then refused a quadric for its boundary:

    step-cut-cone            4 regions
    step-bored-cylinder     10
    step-bored-cone         10
    step-declare-grid-scad  26
    lid10                   30

The experiment is two one-line relaxations of what just landed, run together and
then thrown away: accept a plane section claimed by **one** face rather than two
(which is the planar neighbour, granted for free), and let a **cone**'s boundary
be covered by one as well. Both together, the refusals go:

    step-cut-cone            4 -> 0
    everything else          unchanged

So `step-cut-cone` is the whole of what the plane story buys, it does not buy it
without the cone work, and the two are one piece of work rather than two. A cone
cut by a plane tilted less than its half angle is an ellipse - 12 degrees here
against a half angle of `atan(10/20)` = 26.57 - so the curve is as writable as
the cylinder's, and the face on the other side is a cube's, which is where the
planar neighbour comes in.

**The other 76 regions are not plane sections at all**, which is why neither
relaxation moves them: a cylinder bored by a cylinder of a *different* radius
meets it in a quartic, and the declared grid meets the bore in something with no
name at all. They need a general intersection curve between two curved surfaces -
`SURFACE_CURVE` with a pcurve on each - and that is the next large piece.

Two more figures from lid10, both measured the same afternoon and neither
touched by the above:

- of its **1598** two-owner junction vertices only **463** reach the curve where
  the two owners cross;
- **28 edges written as an arc, 28 left straight**, so half the arc candidates
  are still chords.

## What "derived, not captured" actually costs

Kept as the worked example, because the cost is the point and it is easy to
underestimate. `step-declare-grid-scad`'s `Plane=8` becoming `Plane=22` is four
pieces of bore facet fanned into eighteen triangles, and the fixture states which
four (where the ridge crosses the bore), why each has 6 or 7 corners (a facet
spans 11.25 degrees, the ridge's stations are 16.875 apart and each span carries
one diagonal), and why the other four planes do not move (their corners are
already on the cylinder at r = 20 exactly).

The number itself took a minute. The paragraph took the afternoon, and it is the
paragraph that would have caught the disc.

## What to do next

Items 1 and 2 of the old list are done - the cone's plane section and the general
intersection curve both landed on 2026-09-07. What is left:

1. **`VOLUME:` deserves to be on more fixtures.** It is the only line in this
   suite that noticed the chorded trim; every census figure was identical before
   and after. Any fixture whose model has a closed-form volume should state it.
   `step-approximate-turned` gained one on 2026-09-08 and it is a fair example
   of the cost of not having it: the fixture asserted a census, a facet count
   and a surface report, and every one of them was satisfied by a ball whose
   poles were flat. Its volume is `pi*h*(r1^2+r1*r2+r2^2)/3 + (4/3)*pi*R^3` from
   the model's own literals - the two solids are disjoint, so they add.
2. ~~**`sphere()` exports with its poles flattened**~~ - **done, 2026-09-08.**
   `$fn=32; sphere(r=5)` came back as `Plane 2, Sphere 1`, two planar polar caps
   at z = +/-4.9759, measuring **523.580594** against `4/3 pi 125` =
   **523.5988**; a sphere less two caps of height 0.0241 is **523.5806**, which
   matched to six figures. Faithful to the mesh, wrong about the model. A run of
   bands reaching the last ring at either end now absorbs both caps and is
   written as the complete quadric, bounded by its seam meridian alone - used
   once in either direction, poles at centre +/- r along the axis, no rim at
   all. It reads back as one `Sphere` with two degenerate edges at 523.598776.
   `step-sphere`, `step-sphere-closed` and `step-approximate-turned` cover it.

   Two things the entry above got wrong, both worth keeping as warnings:

   - **"bounded by its two seams, which is the shape `check_cylindrical_faces`
     already describes for a periodic face" is not true.** A sphere is periodic
     in one direction only, so it has one seam used twice and no rim, which is
     *two* edges - below the floor of four the periodic branch sets and below
     the three the fillet octant is let through on. The validator rejected the
     shape, and it rejected OpenCASCADE's own export of a sphere too, which is
     the quickest way to see it: OCCT writes a `VERTEX_LOOP` with no edges at
     all. `check_closed_sphere()` is the rule that was needed, and
     `closed-sphere-check-mutations.py` holds it to seven near misses, because
     an acceptance never shown to refuse anything is a deleted check.
   - **The gate cannot be "the cap is small".** It is angular and relative to
     the run's own bands: the tessellation leaves exactly half a ring step, so a
     gap of a whole step or more means a ring is missing and something has cut
     the sphere. Both ends have to qualify or neither does - a sphere with a knob
     on one pole exports exactly as it did before, since the face that would
     close only the far end (one rim, one seam to a degenerate pole) is a shape
     the writer does not have. The one cut this cannot see is one exactly at the
     last ring, which removes no facet and leaves the uncut sphere's mesh.

   **The volume was the check that mattered**, as the entry said - a face census
   cannot tell a sphere from a sphere with its poles cut off, since both read as
   `Sphere 1` plus some planes. It is also the trap in miniature: this defect had
   been *written down as a correct derivation* in `step-export-testing.md` ("a
   sphere is not `(4/3)pi r^3`"), argued for in prose, and confirmed by
   OpenCASCADE to six figures. Every check was downstream of the same mistaken
   premise. That entry is now a warning rather than an example.
3. **c11's faults are the chorded boundary, and the sweep needs 0.0609 mm of
   slack to close.** Chased to the end on 2026-09-08 and written up as five
   whys in `step-interop-validation.md`; four of the five were refuted. The
   answer is that `step-occt-strict.py` fails 7 of 11 faces at 1e-6, because a
   fitted face is bounded by the mesh's own polyline and sags off its own
   surface by a station's sagitta. It belongs with the band family, not with
   the tangent-break story.

   **And it is a declared crossing written as a chord.** The sliver vertices are
   not points any generator emitted - they sit 0.2000 mm from the nearest ridge
   station - but they lie at `r = 19.200000000000`, on the declared cylinder to
   between 0 and 3.55e-15. So they are exactly where two *declared* surfaces
   cross: `cylinder(r=19.2)` and the `declare_grid()` sweep.

   That is item 2 of "what a boundary can be" at the top of this document - the
   curve where two declared surfaces cross, found from the two declarations by
   Newton and written as a SURFACE_CURVE with a PCURVE on each. What is actually
   written is item 5, a chord: a SURFACE_CURVE over a `LINE` carrying **one**
   pcurve, on the B-spline only. The tier the exact machinery refuses a face for.

   **It is not one gate, and the first write-up of this said it was.** The
   `dynamic_cast<const GridSurface *>` in `StepKernel.cc` is in the *corner
   placement*, not the crossing search. The crossing search is barred from
   sweeps architecturally: it runs inside `decide_sections`, over the patches
   `recogniseQuadricPatches` returned, and that recogniser only ever produces
   cylinders and cones. A sweep is recognised separately by
   `recogniseGridPatches` and written by a separate emitter. The two never meet.

   Scoped on 2026-09-08 by making the change and measuring that it did nothing.
   The solver itself generalises cleanly - `projectOntoBoth` and
   `intersectionArcError` need only a value and a gradient per surface, and
   `closestOnSurface` already handles a `GridSurface`, so the distance to the
   nearest point with the unit vector along it is the same linearisation. That
   part was written, built, and changed the export by not one entity, because
   nothing ever hands it a sweep.

   What it actually takes, in order:

   1. the value/gradient generalisation above, which is small and was proven
      inert on its own;
   2. grid patches admitted to the `along` map so a sweep-against-quadric edge
      is seen from both sides;
   3. **the grid emitter taught the edge ladder the quadric emitter already
      has** - crossing curve, then plane section, then arc, then chord. This is
      the real work: `quadric_faces` carries about a hundred and fifty lines of
      it and `grid_faces` carries none.

   Steps 2 and 3 are not separable. A crossing curve taken by the cylinder's
   face while the sweep's face still writes a chord gives the two faces
   different geometry for one edge, and the shell comes apart - so this lands
   whole or not at all.

   Worth doing anyway: it would put the boundary exactly on both faces, which is
   what the 0.0609 mm is paying for, and it would make the sliver lengths stop
   mattering, because a chord's length only matters while the chord is the
   geometry.

   Three things fell out of it worth acting on:

   - **`validatestep.py` checks a pcurve against its 3D curve only on cylinders
     and cones.** A pcurve on a B-spline or a plane is checked by nothing. c11
     writes 581 `SURFACE_CURVE`s, all of them on B-splines. That gap should be
     closed whatever the cause of the faults.
   - **The `-FaultDetail` TSV should be read before any theory is formed.** It
     names entity, kind and location; the count alone hid the fact that c11 has
     two unrelated faults, one on the sweep and one on a planar cap, and a whole
     fix was built for the wrong one of them.
   - **Import *time* is a diagnostic and is not recorded.** A file SOLIDWORKS is
     happy with opens in seconds and one it has to work at takes minutes, which
     is a signal available before any fault count. `step-interop-solidworks.ps1`
     times nothing; one Stopwatch around `LoadFile4` and a column would have it.

4. **c06 imports cleanly and comes in inside out.** Noticed by eye on
   2026-09-08: SOLIDWORKS reports no fault at all on `c06-partial-torus`, and
   its inner fillet is *concave* where the model has it convex. That sits
   beside the 14% volume shortfall this coupon has always had, and a fillet
   turned the wrong way would explain a deficit of roughly that shape. Worth
   taking together rather than separately, and worth taking once there is an
   import with no faulty faces to compare against.

5. ~~**A swept face is one face with creases inside it**~~ - **tried and
   refuted, 2026-09-08.** The reasoning was that a polyline profile creases a
   face in its interior where a B-rep wants an edge, and that is true: a coupon
   isolating it goes from one fault and a 0.13% volume error to no fault and an
   exact volume when split per span. On the real c11 it changed nothing - still
   codes 13/30 - because the cause is item 3, not this.

   Keep two results from it. Restricting each face's surface to its own span
   measured *exactly* on the coupon and is **8.2% wrong** on a derivable ridge,
   while being the variant SOLIDWORKS liked best: the sharpest instance yet of a
   kernel preferring a wrong file. And a face SOLIDWORKS reports no fault on can
   still be measured 0.17% wrong, so "faults=0" is necessary and not sufficient.

6. **The twist.** `linear_extrude`'s walls are the last thing its parameters
   determine that is not declared - see "declaring the planes, and what each
   extrude parameter does with them" for why `slices` and `$fn` are not the
   obstacle and `GridSurface` is the mechanism.

7. **What still rests on the mesh**, measured 2026-09-08 and not yet acted on.
   Curved geometry with no declaration at all: `minkowski()` declares nothing
   (a rounded cube exports as 142 planes); `hull()` declares its inputs but not
   the blend it creates, so a hull of two spheres arrives as 28 recognised cones;
   `import()`, `surface()` and `projection()` have nothing to declare. Each of
   those is analytically known from its operands - a minkowski with a sphere is
   planes, cylinders and spheres - so the question worth asking of each is what
   the operands' declarations imply about the result.

Then lid10, the blast-radius specimen rather than a development target. It needs
`-p examples/step_test/lid10.json -P "New set 1"`; without it you get the default
component and every number is incomparable. Its current state: 40 trimmed
quadrics written and 1 region refused, 27 planar faces on declared planes of 41
declared, and one pre-existing complaint that is **not** this work - a PLANE face
with a corner 2.2623e-06 off it, which the plain faceted export with the analytic
pass switched off reports identically.

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
- **Do not let an approximation displace an exact entity.** The first crossing
  curve took `step-cylinder-cross`'s eight true ELLIPSEs and wrote them as fitted
  B-splines - the same curve, less exactly, and no longer recognisable as a
  conic. Anything that fits a curve needs to ask first whether the curve has a
  name.
- **The mesh moves between recognition and writing.** The corner placement runs
  after the pass that decides which faces are analytic and before the pass that
  writes them, so any decision taken on vertex *positions* has to be taken twice
  or it describes a mesh that no longer exists. Two calls to one deterministic
  function returned different answers and four readings of the code found
  nothing, because every argument named in the call was identical. Checksumming
  `vertices` at both call sites found it in one build; do that first next time.
- **Do not relax a validator rule on the presence of a curve.** The first draft
  of the three-edge rule triggered on the file containing an ELLIPSE - which is
  the exporter's own claim, so the check and the thing being checked agreed by
  construction. Every relaxation has to name evidence the file must *show*: two
  arcs found sharing a vertex, or the apex computed from the surface's placement
  and found among the face's own vertices. Mutation-check it: nudging one
  CONICAL_SURFACE's half angle by 0.01 rad, touching no vertex, must make the
  rule fire.
- **A count is not a derivation, and a plausible reason is worse than none.** A
  fixture line here said sixteen ellipses survived "because the tilt test
  excludes the rest". The tilt test admits twenty-eight of the thirty-two, which
  one line of arithmetic said and the prose did not. State the number and say it
  is not explained; that is what `step-cylinder-cross` already does for its lobe
  halving.
- **An analytic curve on a trimmed face is a joint decision, not a local one.**
  The two faces meeting along it must write the *same* curve or the shell opens,
  and the failure is silent in the exporter and loud only in
  `validatestep.py` - "12 edge(s) used by only one face". Anything found by
  walking a boundary cycle must therefore be found the same way from either
  side, so nothing in that search may depend on where the cycle starts: not the
  scan order, not the tie-break, and not the tolerance (the flatness tolerance
  here is scaled by the cycle's bounding box for exactly that reason).
- **A boundary cycle's closing edge is easy to lose.** Cycles built from
  `Patch::Run` by dropping each run's last vertex are closed, so a
  `for (i; i + 1 < n; i++)` walk exempts the edge from the last vertex back to
  the first - which quietly excuses one chord per cycle from the boundary test.
- **A hint is recorded in the profile's own frame; the outlines may not be.**
  `Polygon2d::outlines()` returns *transformed* vertices when a 3D transform is
  pending, while its `arcs` and `beziers` are untransformed. Comparing a
  transformed vertex against an untransformed centre finds no arc at all - and
  the code that was excluding chords of arcs then excluded nothing, so a rounded
  square declared thirty planes for six faces. Use `untransformedOutlines()`.
- **A profile that has been through Clipper is snapped to its decimal grid.**
  `CLIPPER2_MAX_DECIMAL_PRECISION=8`, so a vertex that should be exactly on a
  recorded arc is within about 1e-8 of it and no closer. A test asking "are these
  two points on this circle" can afford to be loose - a straight edge's ends are
  nowhere near one, so the discriminator is enormous - and at 1e-9 it matched
  nothing.
- **Rewriting a fixture assertion your own change broke is the other circular
  validation**, and it looks more innocent than relaxing a validator rule. Three
  fixtures here asserted "no analytic surfaces were declared" and planes began to
  be declared. The narrowing was defensible - the fixtures are about a wrong
  *curved* claim - but "defensible" is not the test: reintroduce the defect the
  fixture exists to catch and confirm the *rewritten* line fails. Then look at
  what the rewrite stopped constraining and pin it again wherever the number is
  derivable, and say plainly where it is not.
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
