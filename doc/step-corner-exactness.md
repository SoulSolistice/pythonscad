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

## The order of work

1. **Declare what is not declared.** The gap list is in `doc/step-export.md`,
   *What declares today, and what could*. A junction can only be placed exactly
   when both its owners are declared, so this comes first.
2. **Place the corner from the declaration**, not from the mesh, before
   `mergeTriangles` - which is what makes it safe, because triangles that stop
   being coplanar then simply do not merge instead of becoming broken quads.
3. **Then the fixtures**, one at a time, each moved number carrying a reason.

On (3): `doc/step-export-testing.md` requires expectations be derived and not
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
