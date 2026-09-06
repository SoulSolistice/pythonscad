# Corner exactness: handover

Written 2026-09-06, at the end of a long session. `doc/step-corner-exactness.md`
is the reasoning and the baseline; this is the state of the work and what to do
next.

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
    step-band-family          8.89e-02         planes done, cyl and sweep not
    step-cut-cone             3.16e-02         two triple points, correctly left
    step-exact-trim           3.42e-02         not looked at
    py-step-declare-grid      6.35e-16         reads done and is not - see the disc
    py-step-declare-grid-strip 6.35e-16        the same face, the same reason

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

## The disc, and why the Python pair is still red

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
