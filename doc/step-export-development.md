# STEP export: development

What is known about this exporter and holds today. Every claim here was measured
and has survived the measurement that could have refuted it; where a claim was
overturned it appears in *Refuted claims* rather than being deleted, because
several of the dead ends look attractive on a second reading.

Open items, roadmaps and anything still being chased live in
`doc/step-export-wip.md`. Build and test mechanics — how to get a binary that
runs, how to run ctest, what to install — live in `CLAUDE.md`. This document
assumes both.

---

## 1. Orientation

### Where the code is

| file | role |
| --- | --- |
| `src/io/export_step.cc` | entry point: mesh to `StepKernel`, then the ISO-10303-21 header |
| `src/io/StepKernel.cc/.h` | the entity model, and turning recognised surfaces into entities |
| `src/geometry/AnalyticFeatures.cc/.h` | the recogniser: which facets were modelled as a surface |
| `src/geometry/recognition/` | the recognition module: `FacetGraph`, `Provenance`, `Ledger`, `Arbiter`, `Pipeline` (the stages and their checked table), `Reports`. See `doc/step-recognition-design.md`, *The recognition module, as built* |
| `src/io/import_step.cc` | the reader, useful as a round-trip oracle |
| `src/geometry/Surface.h/.cc` | the declarable surface types and their membership tests |
| `src/core/primitives.cc` | what `cube()`, `cylinder()`, `sphere()` declare |
| `src/geometry/linear_extrude.cc` | `declareExtrudedCylinders`, `declareExtrudedPatches`, `declareExtrudedPlanes` |
| `src/geometry/rotate_extrude.cc` | `declareSurfacesOfRevolution` |
| `src/core/DeclareSurfaceNode.cc` | the SCAD `declare_*` builtins |
| `src/python/pyfunctions.cc` | the Python `declare_*` functions and object methods |

`AnalyticFeatures` knows nothing about STEP and is not reached through the
exporter. It answers a question any format with analytic surfaces has to ask —
*which runs of facets were modelled as a surface, and will every face sharing
their edges accept the substitution* — and returns plain data: bands, their
resolved rims, and the rule that rejected each band left faceted. A FreeCAD or
IGES writer could call it without taking the STEP entity model with it.

Two properties of that split are load-bearing:

- **The report is data, not `printf`.** The recogniser returns the lines and the
  caller prints them. A rejected surface is otherwise invisible — a wall that
  was never recognised looks exactly like a wall that was never there — and both
  of the defects that motivated the split were found within one run of making
  the exporter print the rule that rejected each band. Ship the diagnostic with
  the feature.
- **`Mesh` is the loops, not a `PolySet`.** The recogniser wants cleaned,
  canonicalised loops with their hole flags and normals. Handing it a geometry
  object would drag the merge-and-reparent pass in behind it, and that pass is
  exporter business.

The mesh arrives already merged: `export_step.cc` calls `mergeTriangles()`
(`src/geometry/GeometryEvaluator.cc`), which fuses coplanar triangles into
polygons and reports, per merged face, an outward normal and a parent index for
holes. Most of the exporter's difficulty is in consuming that output correctly.

### The boundary machinery is in one namespace on purpose

All of it sits in one anonymous namespace at the top of `src/io/StepKernel.cc`,
because the three passes that ask about a boundary have to get the same answer:

| what | where |
| --- | --- |
| the implicit form of a quadric, and Newton onto two at once | `quadricImplicit`, `projectOntoBoth` |
| the arc where two declared surfaces cross, and how far it misses | `intersectionArc`, `intersectionArcError` |
| the ellipse a plane cuts from a cylinder or cone, and when it closes | `planeSectionEllipse`, `sectionTiltFloor` |
| a patch's boundary cycles, and the sections found on them | `rawBoundaryCycles`, `findSections`, `boundaryCycles` |
| which sections both faces agree to write | `decide_sections`, a `std::function` inside `build_tri_body`, **called twice** |

`buildPatch` in `src/geometry/AnalyticFeatures.cc` records `run.loop`, the face
across each boundary run, which it computes anyway to split the runs.

### The three gates, and they fail independently

Every analytic face has to pass all three:

| gate | question | where it lives |
| --- | --- | --- |
| geometry | do these facets fit the surface exactly? | the fit and its residual |
| intent | did the model mean this surface, or is it a prism? | the declared records |
| topology | will every face using these edges agree to the substitution? | the rim rules |

**The third is where the work is.** The bayonet lid has eleven exact cylinder
fits in it, 26% of its faces, every one declared — and it produced no analytic
surface at all. All eleven failed the rim rules. Any estimate of a new surface
type that counts only how hard the surface is will be wrong by that margin.

### The two tiers

| flag | what it may assert | what it does to the mesh |
| --- | --- | --- |
| `step-analytic-surfaces` | only surfaces the model declared, fitting exactly, whose whole boundary lies on the surface | nothing: the mesher's output byte for byte |
| `step-approximate-surfaces` | also fits a surface to a smooth region nothing declared; runs the corner placement | moves junction corners onto declared surfaces |

The approximation tier needs the analytic flag as well. Both are still
experimental and off by default (`src/Feature.cc`). A fit is accepted only while
it stays inside the band the model's own tessellation leaves open, and the worst
error is reported on every export.

The tier a measurement belongs to matters and is easy to lose. The exact tier of
`step-bored-cylinder`, `step-bored-cone` and `step-cut-cone` keeps its plane
sections and measures further from the derived volume than the approximation
tier of the same models does, because the crossing curve is only available once
the corner placement has put the junction vertices on both surfaces.

---

## 2. The declaration channel

### The rule: declare first, recognise second

> **A generator that knows what surface it is making declares it. Recognition is
> for geometry whose maker could not say.**

Recognition is a heuristic conditioned on what survives. A cone is fitted between
two rims, so it needs both rims still there; a band needs its neighbours; a ring
needs its vertices on rings. Every one of those is a statement about the mesh
*after* the booleans have run, and a boolean is what removes them. A declaration
is a statement about intent, made before any of that, and carried through
transforms, booleans and hulls unchanged.

The experiment that settles it — two stock primitives, the same cut, the same
class of geometry:

```openscad
difference() {
  cylinder(r1 = 10, r2 = 0, h = 20);      // declares nothing
  translate([0, 0, -1.5]) rotate([12, 0, 0]) cube([60, 60, 6], center = true);
}
```

| cut so that the base rim is gone | written |
| --- | --- |
| `cylinder(r1 = 10, r2 = 0)` — declares nothing | **Plane 66**, the cone is lost |
| `cylinder(r1 = 10, r2 = 4)` — declares two cylinders | **Cone 1**, Plane 53 |

Uncut, both write their cone and the difference is invisible.

### And the corollary: a declaration that is not taken is a bug report

> **A declaration takes precedence over fitting and approximation through the
> whole consumer chain. Where one is not taken and should have been, the reason
> has to be found rather than worked around.**

This is not a nicety; it is how the worst class of defect here gets found.
Giving `CylinderNode` a `ConeSurface` changed **not one face**, and the change
was reverted on that measurement. The measurement was right and the conclusion
was wrong, because the question it should have provoked — *why was a declared
surface not used* — was not asked. Asking it found a missing `fabs`: a cone
whose radius falls with height has a negative slope, the threshold went
negative, and no declared cone of that orientation was ever claimed.

| the base rim cut away by a boolean | cone `r1=10, r2=0` | frustum `r1=10, r2=4` |
| --- | --- | --- |
| before | Plane 66 | Cone 1, Plane 53 |
| declaring the cone, alone | Plane 66 | Cone 1, Plane 53 |
| fixing the sign, alone | Plane 66 | Cone 1, Plane 53 |
| **both** | **Cone 2, Plane 2** | **Cone 3, Plane 3** |

A measurement that stops at "this change did nothing" cannot see a pair like
that.

### The channel is `PolySet::surfaces`, and it is not a sidecar

`PolySetBuilder::addSurface()` is the public API any 3D generator can call.
Transforms move the records, `hull()` keeps them on both backends, `minkowski()`
deliberately drops them, and `CGALNefGeometry` carries them between the one
conversion in and the one conversion out.

Two properties make a loose scheme safe:

- **A record is only ever a hint.** The exporter re-checks every declaration
  against the mesh and against the topology before acting on it. A *global* list
  of "surfaces mentioned anywhere in this model" would work nearly as well — it
  would only offer more candidates for the fit to reject.
- **World coordinates must ride along.** Records are stored in world coordinates
  and moved by the transforms above them. A sidecar written at render time would
  have to capture the transform stack in effect where the declaration was made,
  which is exactly what living inside the geometry gives for nothing.

The limit is worth stating: the fit gate catches a wrong declaration only when it
does not match the mesh. Someone can assert a cylinder that happens to fit a
prism elsewhere in the part. Bounded, but not zero.

**Every operation that can preserve a surface has to carry the record.** The test
is not "does the geometry survive" but "can the record still be wrong" — because
a record is a hint the exporter re-checks, carrying one through an operation that
may have destroyed the surface is safe, while dropping one is not. `minkowski()`
is the deliberate exception: it changes each radius it touches, so a surviving
declaration would be wrong in the one way the fit gate cannot always catch.

### Intent can be assembled from more than one primitive

A frustum has no declaration of its own: `hull()` of two coaxial cylinders — the
standard chamfer — declares the two cylinders and never the cone between them.
Accepting a cone when **both of its rims match a declared cylinder** is the same
statement of intent, made by two primitives instead of one. The question to ask
before adding a declaration to a node is not "does something declare this
surface" but "is there a combination of declarations which can only mean this
surface".

### What declares today

| producer | declares |
| --- | --- |
| `cube()` | six `PlaneSurface` |
| `cylinder()` | `CylinderSurface`; both cap planes; for `r1 != r2`, two rims |
| `sphere()` | `SphereSurface` |
| `rotate_extrude`, vertical segment | `CylinderSurface` |
| `rotate_extrude`, sloped segment | the two rims |
| `rotate_extrude`, arc | `SphereSurface` or `TorusSurface` |
| `rotate_extrude`, profile edge at constant height | the flat annulus `PlaneSurface` |
| `rotate_extrude`, partial sweep | the two end faces |
| `linear_extrude`, arc in the profile | `CylinderSurface` |
| `linear_extrude(scale=)` uniform | the two rims (a cone is fitted between them) |
| `linear_extrude` | both caps, always; a plane per straight profile edge the sweep keeps parallel |
| `FilletNode` | `BezierPatchSurface` |
| `declare_cylinder` / `_sphere` / `_cone` / `_torus` / `_grid` | SCAD and Python |
| `declare_sweep` | **Python only** |

Nothing declares under `polyhedron()`, `import()`, `surface()` or `projection()`,
correctly: there is no intent to read.

Measured, each probe declaring exactly what it uses:

```text
cube                     6 declared,  6 faces on them
linear_extrude(square)   6            6
linear_extrude(circle)   2            2   (the wall is the cylinder)
linear_extrude(twist)    2            2   (caps only, walls refused)
linear_extrude(offset)   6            6
text                    17           17
rotate_extrude           2            2
rotate_extrude(120 deg)  4            4   (two annuli, two end faces)
lid10                   41           27
```

### What each `linear_extrude` parameter does with a declaration

Every parameter is on the node and reachable where the declarations are made.

| parameter | effect |
| --- | --- |
| `height`, `v` | the sweep vector. An oblique `v` refuses the *cylinder* — an oblique cylinder is a real surface but not a `CYLINDRICAL_SURFACE` — and costs the caps and straight walls nothing |
| `center` | folded into the base height; nothing to refuse |
| `convexity` | a rendering hint, no geometry |
| `scale` uniform | cylinder becomes a cone; a straight wall stays planar, because `A'B' = s*AB` is parallel to `AB` |
| `scale` uneven | refuses the curved claim, and refuses a wall unless the edge runs along x or y — exactly the edges an uneven scale leaves parallel |
| `$fn` | never read. The declarations come from the arc and Bezier records and from the profile's straight edges, so they do not move with the tessellation, which is the point of them |
| `slices` | **never read.** It only says how finely a twist is tessellated |
| `twist` | caps still declared, everything else refused. See `doc/step-export-wip.md` |

A chord of a recorded arc or Bezier is **not** a straight edge, whatever the
outline says: it is the tessellation's chord, its plane moves with `$fn`, and
declaring it would put a tessellation artefact on the one channel whose point is
not moving.

### How each surface answers membership

Two projection defects in `GridSurface` cost a week between them, so the survey
that follows is worth keeping: where is a declaration answered by arithmetic, and
where by a search?

**Closed form, nothing to go wrong.** `PlaneSurface`, `CylinderSurface`,
`ConeSurface`, `SphereSurface`, `TorusSurface` — each `pointMember` is an
algebraic test with no iteration in it — and `SweepSurface`, whose membership is
an inversion: take the point's angle about the axis, round to the turn whose
station height is nearest, subtract that height, ask whether the remainder lies
on the profile. Exact, `O(profile)`, no tolerance beyond the one the caller
states about its own mesh. None of these needs a tessellation band and none can
land in the wrong basin.

**Answered by a projection, two left.**

- `GridSurface`, which `declare_grid` builds. It interpolates the caller's
  stations — a cubic along the sweep, a polyline across the profile — and every
  membership test afterwards is a Gauss-Newton projection onto that interpolant.
  Both known defects are fixed and it is sound as far as it is measured, but the
  tessellation band, the local minima and the profile corners are properties of
  the *interpolation* and none of them is a property of the model.
- `BezierPatchSurface`, which every fillet is. Same arithmetic, now guarded the
  same way. What always saved it was the search around the step rather than the
  step: twenty-five restarts on a 5x5 grid keeping the best, so a wandering
  descent costs a start rather than the answer. `GridSurface` had a single
  start, which is exactly why the identical bug was fatal there and invisible
  here.

**Approximate by construction, and correctly gated.** `fitCylinder`, `fitCone`,
`fitRevolved`, `gridFromRegion`, `quadricOfPatch`. These *are* fits: refused
without `step-approximate-surfaces`, and each reports the band it spent.

The rule the survey suggests: **a declaration should answer membership by
arithmetic, and where it cannot, it should say which tier it is in.** Six of the
seven surfaces do it by arithmetic, the seventh is a guarded search over a wide
base, and the fitting tier is a fit that admits it.

The one measurement worth carrying out of all of it: **an unguarded Gauss-Newton
step is not a rounding question.** It moved a point 11.9 mm on a 2 mm feature,
silently, in a routine whose name promises the opposite. Of 44959 projections on
`step-band-family`, 1038 came out *worse* than the coarse sample they started
from. Every step now has to improve the distance, by backtracking up to eight
halvings and giving up rather than taking one that does not.

The second: **do not start a coarse sample on a parameter boundary.** The sample
took `vspans()*2` points across `v`, putting one exactly on every span boundary.
A profile is a polyline, so every boundary is a corner where the derivative does
not exist and the finite difference straddles it; the descent stopped on the
spot and half of every sweep was written as facets. It now takes four points
inside each span, at its quarter points, and never a boundary.

### `declare_sweep` against `declare_grid`

`declare_grid` hands over a grid of points and a flag saying the profile loops.
It earns its place where there is genuinely no closed form — a general
`polyhedron()` — and it is the weakest tier of the channel.

`declare_sweep` hands over the *shape*: a closed, constant profile carried along
a helix. `SweepSurface` derives from `GridSurface` so the recogniser, the emitter
and `splineForm` keep working unchanged — the inherited net is a *rendering* of
the surface for those consumers, sampled from the closed form rather than handed
in — while `pointMember`, `project`, `onSurface` and `evaluate` are overridden
with the closed form. A profile that changes shape along the path is a different
declaration and is refused rather than approximated.

Measured on the band-family ridge, declared this way instead of as a grid: 1826
facets claimed whole and 192 cut, over three of the profile's four spans, the
fourth being the one buried in the wall. That is what the grid path reaches only
with both projection fixes in place, and this reaches it without a projection.

### The limits of the rule, named

Four producers genuinely cannot say what surface they made, and the fitting path
exists for them:

- **`hull()`** — the output belongs to no operand; no face of either operand lies
  on the collar's chamfer, and fitting catches exactly that. Provenance survives
  a hull but collapses to one id, so a gate keyed on it degrades rather than
  going blind.
- **`minkowski()`** — drops its records deliberately, as above.
- **`polyhedron()`** — has no intent to declare, which is why `declare_grid` and
  `declare_sweep` exist as user-facing channels.
- **an imported mesh** — an STL is a mesh and nothing more.

These are also the cases where corners can never be made exact. That is the
boundary of the corner work, and it should be stated rather than discovered.

**And a geometric argument cannot stand in for the missing declaration.** Asked
2026-09-23 whether a Minkowski decomposition, the half-space (H-)representation
or an Ehrhart polynomial could trace an undeclared polyhedron back to the
cylinder it approximates: no. The H-representation of a polyhedron is its chord
planes - the same inequalities as a prism whose corners sit on a circle; a circle
is never a Minkowski summand of a polytope; an Ehrhart polynomial exists only for
lattice (or rational) polytopes and counts points, never surfaces. What geometry
*can* state, with no threshold, is that the points satisfy a declared closed form
(x^2 + y^2 = r^2 at float equality) - corroboration, D0-grade evidence that the
facets *lie on* a surface. It is not intent: treating it as ownership is inferring
intent by measurement, the thing provenance exists to replace. Where intent is
real but undeclared, the fix is at the origin - the model declares it.

**Worked example, and fixed that way.** lid10's `bayonetChannel` is a
`polyhedron()` whose inner and outer walls are point lists at `rInner` and
`rOuter` - the same two cylinders its entry slots declare, because the callers
pass the same parameters. The channel declared nothing, so the measured owner
credited it with the slots' cylinder and the carried owner said, correctly, that
it declared none. Both specimens now wrap the channel in
`declare_cylinder(r = rOuter)` and `declare_cylinder(r = rInner)`.

### Who declared a surface, and in which role - carried, not measured

**Since 2026-09-23.** `PolySet::original_ids` has always carried *which original
made a facet*. *Which surfaces an original declared* was not carried: every
boolean merges its operands' records into one flat list, and `export_step.cc`
re-derived ownership from positions. Now every `Surface` record carries it:

| field | meaning |
| --- | --- |
| `owners` | originals that declared it as a **face** - their facets lie on it |
| `witnesses` | originals that offered it as **evidence** - `cylinder(r1, r2)`'s two rim cylinders, a sloped `rotate_extrude` edge's endpoint radii, a tapered `linear_extrude`'s rims. No facet of those solids lies on them |
| `evidence_only` | set by a generator before any id exists; routes the ids at stamping |

Stamped where Manifold mints an original id - `createManifoldFromTriangularPolySet`,
`ManifoldGeometry::setColor` / `toOriginal`, `hull()` - and by
`ManifoldGeometry::addSurface` for a `declare_*` made over a subtree that is
already a Manifold (stamped with the subtree's originals). Copied by `clone()`,
so every transform carries it; every deduplication unites both lists with the
face winning.

**One geometric surface is one record.** `mergeSurfaces` and `containsSurface`
match by `describesSameSurface` (same axis *line* and radius for a cylinder; cones
likewise), not `sameAs`'s exact reference point. Item 17 had measured the rule
geometry-neutral and not landed it; it landed once ownership, the ledger and the
census - all per record - were seen counting one surface twice.

`export_step.cc` still decides with the **measured** `owned`, and reports the
carried ownership against it on every export. Why the switch is parked, and what
it needs, is open item 31.

---

## 3. What an analytic face's boundary is

A trimmed quadric's boundary edge is written as the most exact thing available,
in this order:

1. **A conic lying on both surfaces.** Two equal cylinders crossing meet in a
   pair of true ellipses; `ELLIPSE` is an entity, and it wins.
2. **The curve where two declared surfaces cross**, where that curve is not
   planar — two cylinders of unequal radius meet in a quartic ISO 10303 has no
   entity for. Found from the two *declarations* by Newton, fitted by a Bezier
   whose degree is raised until it is inside 1e-7 of both, written as a
   `SURFACE_CURVE` with a `PCURVE` on each surface.
3. **A plane section**, exact on the face's own surface and on no other. Written
   only where the face across it writes the same one.
4. **An arc**, where the edge runs round the axis at constant height.
5. **A chord**, exact at its two ends and nowhere between. In the exact tier a
   face bounded by one is refused.

### The midpoint test

A straight edge counts as exact when its **midpoint** is on the surface. A line
meets a quadric twice unless it lies in it, so a third point on the surface means
every point is.

That one line replaced an enumeration — "along the axis, or around it at constant
height" — which was a *cylinder's* answer. A cone's rulings run to its apex, so
every generator was counted a chord and every region bounded by one refused. The
midpoint test alone took four models from refusing fifty regions between them to
writing all of them.

### A plane section of a cylinder or a cone

`planeSectionEllipse` answers for both. The cone is worked in its plane of
symmetry — the one containing the axis and perpendicular to the cut — where the
cone shows as its two outermost generators and the cutting plane as a line. The
two crossings are the ends of the major axis; `b` is the half chord through the
centre at right angles, and the cone's own quadratic gives it in one square root
because that direction is perpendicular to the axis and to the major axis both:

```text
b^2 = h^2 / cos^2(alpha) - |C - apex|^2,   h = (C - apex) . axis
```

Checked against a second derivation before any code was written, and the second
way is the one a fixture can state: **`b` is the geometric mean of the cone's
radii at the two ends of the major axis.** On `step-cut-cone` both give 9.268985,
and the exporter writes 9.268985.

The tilt test is not optional: a plane cuts a cone in a closed section only while
it crosses every generator, `|n . axis| > sin(alpha)`. At the half angle it is a
parabola and past it a hyperbola, and neither closes.

**The plane comes from a face, not from the boundary.** Fitting a plane to three
consecutive boundary vertices needs two edges before it can see a plane at all,
and on `step-cut-cone` two of the four regions meet the cut along a *single*
edge. Planes are gathered per *surface* from the faces adjacent to any of its
regions: a plane that cuts a cone cuts all of it, so the region meeting the cut
along one edge is told what the plane is by the region that meets it along
thirteen. The fit remains, last, for the case where nothing planar is in the
picture — two cylinders crossing meet along an ellipse that is nobody's face.

**A declared plane is used in preference to a fitted one**, and it is not a
nicety: only a declared plane survives the corner placement. On
`step-band-family` 180 plane sections were agreed before the placement ran and
none after, every one of them on a fitted facet plane. The placement moves a
corner onto the surface the model *declared*, which is exactly off any facet
plane it happened to share.

```text
step-cut-cone            2 sections, 2 declared,  0 fitted
step-cylinder-cross      8 sections, 0 declared,  8 fitted
step-bored-cylinder     20 sections, 0 declared, 20 fitted
step-declare-grid-scad  32 sections, 0 declared, 32 fitted
```

### Finding a section: walk the cycle as a circle

`Patch::Run` splits a boundary wherever the neighbouring face changes, so one
ellipse arrives as thirty-three runs of a single edge each. `coplanarStretches()`
finds them by walking the boundary cycle **as a circle**: grow a plane from every
vertex, take the longest, consume its edges, repeat. A cycle has no first vertex
of its own, so a left-to-right scan reports a stretch straddling that arbitrary
seam as two shorter ones, and the neighbouring face — whose seam is elsewhere —
then disagrees about where the curve begins.

Three specific traps, each of which cost a build:

- **Seeking "where the boundary turns" to find a starting point does not work.**
  Every triple along an ellipse is non-collinear, so that lands in the middle of
  an arc. Seek where the *plane* changes.
- **Cutting the cycle at a wrapping stretch's own start is not enough.** It frees
  that stretch and pushes the next one across the new seam whenever the two meet
  end to end — which is exactly how two ellipse arcs meet at a Steinmetz pinch.
  The seam has to land on an edge no stretch covers.
- **The closing edge is a real edge.** Cycles built from `Patch::Run` by dropping
  each run's last vertex are closed, so a `for (i; i + 1 < n; i++)` walk quietly
  exempts the edge from the last vertex back to the first.

### An analytic curve is a joint decision, not a local one

The two faces meeting along a curve must write the **same** curve or the shell
opens, and the failure is silent in the exporter and loud only in
`validatestep.py` — *"12 edge(s) used by only one face"*.

So a section is written only where it turns up **twice** among the faces still
standing, keyed by the mesh vertices the stretch runs through, which both sides
arrive at identically because the search is seam-independent. Acceptance and
availability are circular — a face may be writable only because a section covers
its chords, and the section is only available while that face stands — so they
are settled together, by dropping whatever the last round refused and asking
again. Refusals only grow, so it terminates.

**Anything found by walking a boundary cycle must be found the same way from
either side**, so nothing in that search may depend on where the cycle starts:
not the scan order, not the tie-break, and not the tolerance. The flatness
tolerance here is scaled by the cycle's bounding box for exactly that reason.

The crossing curve gets this for free: both faces derive it from the same two
declarations, so they agree exactly without either knowing what the other did.

### The crossing curve, and why approximating it is honest

Two quadrics meet in a quartic in general and ISO 10303 has no entity for one;
OpenCASCADE's own export of such a solid writes a degree-7
`B_SPLINE_CURVE_WITH_KNOTS` of some thirty control points. Approximating is what
the format offers. Three things make it honest:

- it approximates the **true curve**, reached by Newton from the two implicit
  forms, not the mesh's polyline;
- it is held to a **stated tolerance**, the same 1e-7 the exact tier holds every
  other boundary to, by raising the Bezier's degree until the fit meets it rather
  than fixing one. A cubic leaves the surfaces by 3.4e-05 on `step-bored-cylinder`
  — three hundred times better than the chords and still not exact;
- both faces derive it from the same two declarations.

`step-bored-cylinder`'s approximation export, against a volume derived
independently as an elliptic integral:

```text
derived                       5298.405619
chords                        5301.57       short by 3.16
plane sections, on one        5298.921138   over by 0.5155
the curve where they cross    5298.405620   over by 1e-06
```

**An exact conic beats a fitted curve, and planarity is the test.** The first
version of this took all eight of `step-cylinder-cross`'s true `ELLIPSE`s and
wrote them as fitted B-splines: the same curve, less exactly, and no longer
recognisable as a conic. Fit the arc, take the smallest eigenvalue of its control
points' covariance, and if the arc is flat to 1e-9 leave it to the plane-section
pass. Flat means conic, and a conic has an entity.

It does not fire in the exact tier, and that is right rather than a limitation:
without the corner placement the junction vertices are up to 0.0189 off the
second surface, and no arc through them lies on both. The curve is available
exactly where the corners are already on it.

### The pcurves, and the check without which they are decoration

A curve on two surfaces should *say* it is on them, and STEP's word for that is
`SURFACE_CURVE` with a `PCURVE` on each. Each pcurve is a Bezier in that
surface's own `(u, v)`, fitted through the parameter images of the very points
the 3D curve was fitted through, so the two agree at the collocation parameters
by construction. **The parameter has to be unwrapped along the samples**: a
surface of revolution's `u` wraps, and a curve stepping over the seam must keep
counting.

Two ways to get this silently wrong, and both are why `check_surface_curves`
exists in `validatestep.py`:

- The 3D curve is written `.CURVE_3D.`, which makes it definitive. A reader takes
  it and ignores the pcurves, so a wrong pcurve changes nothing a face count, a
  shell check, a round trip or a volume would notice. It is invisible until some
  other kernel prefers the parameter space — the one place it cannot be debugged.
- The reference direction is chosen from **each face's own boundary**, so two
  faces on one cylinder do not share a parameter origin. A pcurve written against
  the wrong face's placement is off by a rotation and looks entirely reasonable.

Measured agreement on `step-bored-cylinder`: 6.6e-08, which is the curve's own
fit tolerance and not more.

---

## 4. Corner placement

A vertex the generator declared is on its surface to 1e-14. A vertex a boolean
made is not on anything: it sits on the chord planes of the facets that produced
it. When such a vertex becomes a corner of an analytic face, the file asserts it
is on a surface it is not on — and a strict reader refuses it while OpenCASCADE
widens the edge's tolerance until the assertion holds.

The placement runs **under `step-approximate-surfaces`**, after the pass that
settles which faces are analytic and before the pass that writes them.

### Where it may not run, measured

- **Not before `mergeTriangles`.** It keeps the faceted neighbours planar and
  destroys the merge: facets merge into large polygons because runs of them are
  coplanar, and a moved corner breaks exactly that. Twelve to sixty-two times the
  faces across five fixtures.
- **Not by projecting a corner onto one of its two surfaces.** It takes the
  corner off the other, and on lid10 the walls were always the worse of the two.

### The ladder is an arbiter now, and its precedence is two tables

**Landed 2026-09-19.** Every rung below used to *claim* a corner: whichever ran
first held it and the rest skipped it on `moves.count(v)`, so the ladder's order
was its precision ranking, fixed in control flow - and the rungs written first,
which are the least precise because they predate the exact ones, kept winning.
Measured on `step-exact-trim`, 32 corners were reachable two ways and went
entirely to whichever ran first; on lid10 the provenance rung held two corners
on the *curve* two surfaces share while the *point* all three of their faces
meet was one solve away.

A rung now only **proposes**. One arbiter, in `build_tri_body`, collects every
candidate for a corner - each rung's answer, the mesh position, and the solutions
of the constraints themselves taken in pairs and triples - and chooses. It
measures each candidate against every constraint the file asserts at that corner,
to the ladder's exactness floor of 1e-9, and takes the one that satisfies the
most precise set. Two tables at the top of `src/io/StepKernel.cc` are the whole
of the precedence, so reordering the ladder is editing a table:

**`kCornerTiers`** - what kinds of constraint a corner has, and how each counts.
Candidates are compared tier by tier from the most precise down, so one
constraint of a higher tier outweighs any number of a lower one.

| tier | kind | `hard` | `used` |
| --- | --- | --- | --- |
| 0 | declared exact: a quadric or closed-form sweep the model declared, and a plane it declared | no | yes |
| 1 | declared interpolant: a `declare_grid` sweep or a Bezier patch - declared, but interpolated through stations and publishing a band | no | yes |
| 2 | fitted face: what the approximation found. Less precise than a declaration, but the file asserts it | no | yes |
| 3 | provenance owner: a declared surface the corner's makers put there, with no face of it written at the corner. Only owners that *contributed* a facet are passed on, which filters provenance's known false owners | no | yes |
| 4 | mesh plane: the tessellation's chord | — | **no** |

**`kCornerRungs`** - which rung wins when two candidates are equally exact by
that measure: the file-faces solve, then provenance, then the mesh-plane triple,
then the mesh-plane conic, then the mesh position. Ties beyond that go to least
travel.

Three columns because precision, safety and relevance are different questions.
`rank` is precision; `hard` says a candidate that takes the corner off such a
constraint, where the corner was on it, is no candidate at all; `used` says
whether the kind enters the arbiter. Each setting of the last row was measured
and all but one was wrong:

- held `hard`, as the ladder did before the arbiter: corners stayed on a chord
  0.096 inside the declared bore on `step-declare-grid-scad`;
- ranked level with a declaration: two chord planes outvote the sweep, and lid10
  left 61 corners off faces the file asserts;
- ranked last but `used`: a far root on a bore's vertex line lies on *both* facet
  planes beside it, outscores the near root, and `step-declare-grid-scad` moved a
  corner 1.9 onto its neighbour's point;
- **unused**: none of that occurs. A chord says nothing about where a point of
  the model is, and a planar face the placement bends is fanned by the writer
  anyway.

The ranking between the middle tiers was measured too: provenance above the
fitted faces took 60 lid10 corners off faces the file writes, and the final
"no move may take a corner off an analytic face" check refused them.

**A far root is never a fallback for a near one.** Two planes and a quadric meet
in two points - the planes' shared line crosses the quadric twice - and both are
exact, so the score cannot separate them. Along that line the quadric's implicit
is exactly a quadratic in the line parameter, so three evaluations fix it and
both roots come out in closed form; both are offered as candidates and least
travel takes the near one. Within one score only the nearest candidate is
eligible: if it folds, that whole score is given up and the walk drops to the
next, rather than sliding sideways to the far root, which took a corner of f04
2.27 out when it did.

**What the arbiter does not decide is what may move.** A candidate that moves the
corner must leave the file unfolded - no edge at the corner turning round, no
facet *written as itself* folding or leaving its plane - and a facet an analytic
face absorbed is not asked, because it is not in the file. That replaced the
travel bound, which was a proxy for the same question and blind to direction:
lid10's crease corner has its nearest neighbour 0.2356 away *perpendicular* to a
1.338 move with 4.6 of room along it.

### Catalogue first, arbitrate second - and nothing decides a position anywhere else

The corner placement was rebuilt this way and it was the largest single
structural win this work has had: rungs stopped *claiming* and started
*proposing*, and an arbiter decides. What follows is that shape stated as
doctrine, because it generalises and because the places that still violate it
are enumerable.

**The rule.** A decision with more than one possible claimant is taken in two
phases. Phase one **catalogues** who claims what, and on what evidence. Phase
two **arbitrates**, by precision. Precedence lives in a table, not in the order
the passes happen to run in.

**The detector, and it is mechanical.** Any flag that makes a claimed thing
invisible to a later claimant is an arbiter with a one-line policy of "whoever
ran first" and no record of the contest:

```text
AnalyticFeatures.cc:1170   if (... consumed[f] || taken[f]) continue;   quadrics
AnalyticFeatures.cc:1806   if (... consumed[f] || taken[f]) continue;   declared grids
AnalyticFeatures.cc:2282   if (... consumed[f] || taken[f]) continue;   smooth regions
AnalyticFeatures.cc:2652   if (... consumed[seed] ...) continue;        fillets
StepKernel.cc:3181         // Every path checks `moves.count(v)` first, so first refusal
```

The last line is the corner placement's own comment, describing the defect the
arbiter was built to end. The four above it are **facet ownership**, which still
has it: whichever recognition pass runs first takes a facet, and a more precise
declaration arriving later cannot have it. That is the same defect, one
granularity up, and it is the next arbiter to build.

**What the catalogue buys beyond correctness.** Precedence becomes *data*:
reordering the corner ladder is "editing a table, not the control flow", so it
can be mutation-tested like any other assertion. And **losers become
reportable**. Today "why is this facet a plane?" costs an afternoon with an
instrumented build; with a catalogue it is one line - *claimed by the sweep and
by a chord plane; the sweep won at tier 1*. A large part of
`scripts/step-diagnostics/` exists to reconstruct, after the fact, information a
catalogue would simply have.

**Its limits, named rather than discovered.**

- *Arbitration has dependencies.* The corner arbiter needs to know which faces
  exist at a corner, which is recognition's output. So this is not one catalogue
  then one arbitration; it is staged, with feedback, and the first place that
  bites is the grid claim (open item 25: claim, place, revise the claim).
  Anything cyclic needs a termination argument before it is written.
- *Cataloguing is not free.* The corner arbiter enumerates every pair and triple
  of constraints per corner. That is affordable at corner scale and must be
  measured at facet scale rather than assumed.
- *No contention, no arbiter.* Where one claimant is possible by construction
  the machinery is overhead - though a catalogue with one entry costs little,
  and uniformity is usually worth more than the saving.

**The trap, paid for on 2026-09-21.** Phase one must catalogue claims *without
deciding positions*. A proof of concept for the grid claim did catalogue and
arbitrate in the right order, and reproduced the old defect anyway, because what
it arbitrated over was the coordinate the **mesh** happened to hold. Which is
the next section.

### The representation fits the region, never the region the representation

**The third doctrine, and the one that generalises furthest.** Stated as a rule:

> A recogniser decides *which* facets it takes by what the **declaration**
> covers, never by what its own form can express. Where the two differ, the
> region wins and the representation gives way.

**What it cost to learn.** lid10's cone 50 is one declared cone of one original
solid, and the exporter writes it as **two kinds of face** - 56 facets as a
band's wall and 6 as a quadric patch, touching along 2 mesh edges. The bayonet
has a cylinder and a cone the same way. Asked of provenance, every facet on both
sides comes from the *same* original, and that original declared the surface: so
nothing is ambiguous about where they belong.

The cause is not a contest and not a tolerance. A *band* is not something the
model declares - it is a way of writing a region that happens to be a ring
between two rims. The band pass chooses its facets by what a band can express,
so a region that is a ring **plus six facets a boolean left attached** is
truncated to the ring, and the remainder falls to whoever runs next. The model
gets a boundary it never asked for, inside a surface it declared once.

**Why it generalises.** Any recogniser that decides which facets by what it can
express splits surfaces this way, on geometry nobody has looked at yet. The
reference parts are *specimens*: they are where the defect happened to show, not
where it lives. A fix aimed at those three surfaces would be worthless.

**The order this implies:**

1. **Region** from the declaration and provenance - the facets whose maker owns
   this surface and which lie on it, connected.
2. **Representation** chosen to fit that region: a band with rims where the
   region really is a ring, a trimmed patch where it is not.
3. **Exact curves as a property of the boundary**, available to whichever
   representation wins. This is already true and was the reason to fear step 2:
   a circular run on a *quadric face* is written as a `CIRCLE` (`circular_runs`
   / `runCircle`), so a trimmed patch keeps exact rims. Rim exactness is not a
   band privilege.

**Attempted and reverted 2026-09-22**, and the full account is in
`doc/step-recognition-design.md`, *R0, attempted and reverted*. The short form,
because the failure is more useful than the feature would have been:

- **The rung is D0 and the implementation was not.** Membership was decided by
  `min(bandOf(facet), max_off)` - the tessellation band, a D4 quantity, as the
  yardstick of a rung `doc/step-rungs.md` classes D0. Step 1 above says *from
  the declaration and provenance*, and neither decided anything.
- **It cost the exact tier a conical face.** Two regions given up, the writer
  refused them under *"half a surface is worse than none"*, and 56 facets that
  were an exact cone came out as planes. Cone faces 9 to 8 on lid10, 167 more
  `LINE`s, two fewer `CIRCLE`s - on the tier the rung ordering exists to
  protect. Nothing in the suite saw it, because no fixture asserts the exact
  tier's analytic coverage on a model where it fires.
- **A region rule may not destroy a representation on its own.** Giving a band
  up is a bet that something else writes the region; when nothing does, the
  answer is planes. The whole-region claim has to win *among claims that can be
  written*, which needs the arbiter that is not built.
- **What was kept**: `OnQuadric`, one definition of "on this surface" shared by
  the passes that ask it, verified behaviour-neutral. And the knowledge that the
  exporter is not byte-deterministic, so a behaviour-neutral change is verified
  by content and never by diffing exports.

**And one expectation to unlearn, which the attempt did establish.**
`doc/step-rungs.md` had C0 and R0 as one defect from two ends. With regions
claimed whole, every split surface went and C0's 78 seam edges on lid10 did not
move at all. They share a description and not a cause: R0's facets were ones a
*band* could not hold, C0's are ones the general pass **refused**, each with a
stated reason. See open item 27.

### A point carries whether it has moved, and identity is never inferred from a coordinate

Three separate days have been lost to one mistake: **a coordinate used as an
identity.**

- A proof of concept refused two facets over a corner at
  `(76.7944 20.4008 1.0332)`. `at-vertex.py` finds no such point in the exported
  file: the placement had moved it, and the two facets were the ridge's own
  flank (open item 25).
- The flagship check's "cone corner 0.716 off its surface" on lid10 is
  **OpenCASCADE's**, not ours - in our text that vertex is used by the sweep and
  two planes and *no cone at all*, and OCCT had widened it to 1.08 (open item
  23).
- "The written face's loop acquires a vertex the recogniser never accepted"
  (open item 15).

*The mesh moves between recognition and writing* above records the symptom and
settles for a workaround - checksum `vertices` at both call sites. A checksum
says *that* something moved. It cannot say which point, or what it was before,
or who is entitled to an opinion about it.

So a point carries its status, and each value carries a rule:

| status | what it is | the rule |
| --- | --- | --- |
| **declared** | the generator emitted it (`isDeclaredPoint`) | exact on its surface; nothing may move it |
| **tessellation** | a boolean made it; it lies on facets | a station's sagitta off the smooth surface, legitimately - not an error |
| **placed** | the arbiter moved it, by a named rung, satisfying named constraints | its coordinate is the arbiter's answer and the only valid one |
| **provisional** | it will be placed and has not been | **no decision may be taken on it** |
| **reader-invented** | not ours at all - a kernel made it on read | an instrument must say which side of this line it read |

The fourth row is the 2026-09-21 defect stated so that it cannot recur, and the
fifth is why `reader-built.py` exists. Together they give the catalogue its
missing half: **phase one records claims against identities and evidence; phase
two assigns coordinates.** Without a status, phase one inevitably reaches for a
coordinate, because a coordinate is what is in front of it.

**Costs, honestly.** `vertices` is a plain `std::vector<Vector3d>`, so a status
lives in a parallel structure - invasive either way. And it must be derived or
invalidated when a point moves, never cached and left, or it becomes a second
thing that can be silently stale, which is the disease and not the cure.

**And the standing rule this makes explicit:** *identity is carried, never
inferred by proximity.* `diagnose-at-the-origin` says that of provenance; this
says it of coordinates. Matching a point to an entity by distance is the method
that produced all three failures above.

### The rungs themselves, as landed

| condition | action |
| --- | --- |
| a corner both of whose owners are declared, and which reach a transversal crossing | put it where the two cross |
| a corner whose two owners will not converge, exactly one of them exact | put it on the exact one; the fit only has to agree within the band it published |
| a corner a declared surface shares with two faces of the mesh | where the line those two faces cross meets the surface |
| anything else | leave it |

**The fit may only veto a corner it has a claim on.** Requiring the sweep to
agree after the move placed 92 of 351 corners and refused 262 — and 259 of those
were already off the sweep before any move, by up to 0.3962 against its band of
0.2077. They sit where the ridge's base meets the wall, which is the wall's
surface and not the sweep's: a surface with no claim on a corner was vetoing its
placement on the surface it is actually on.

**A corner may travel no further than its nearest neighbouring vertex.**
*Superseded 2026-09-19 by the fold test above; kept because the trap it was
written for is real and the replacement has to keep catching it.* The line
two faces cross along can meet the surface twice, and the far crossing is a
perfectly good solution to the wrong problem. Bounding by the face's extent let a
corner on `step-shared-arc` move 2.0 and took a cone that was exact to 1.41 out.
A corner correcting its own tessellation belongs nearer than the vertex next to
it.

Nor is the longest edge at the vertex a bound. On `step-partial-cylinder` the
mesh has 20-unit vertical edges, so that licensed a move of 1.9976 on a radius of
10 and the export broke. What bounds it is the local tessellation error: how far
the surface stands off the middles of the nearby chords that *do* lie on it. A
declared sweep states the same quantity outright as its band.

**A corner already on a declared quadric stays on it.** Provenance's single
owner is not a statement about the corner: it is the one surface the originals
meeting there own, and an original a boolean cut on every facet owns nothing,
so its surface drops out of the vote. On `band-fn024` the ridge leaves the bore
2 whole facets of the 3 ownership needs, its 48 rim corners - generated on the
bore to 1e-14 - get the outer wall as sole owner, and the conic placement slid
each 3.0 along the cap onto it. The travel bound did not stop it, because that
path bounds by the *farthest* vertex of the faces at the corner, 40 on a cap. So
the conic placement asks the declarations first, as the pass onto a face's own
quadric already did.

**No move may take a corner off the surface of an analytic face it bounds.**
The veto asks that of a face that keeps a `PLANE`; nothing asked it of a curved
face, whose corner the file equally asserts is on it. It is now checked over
every proposed move before the veto, each face judged against its own
`declaredBand`, and a corner already off is judged only on whether the move
takes it further. It is an `EXPORT-ERROR` and the move is held: measured
unreachable on every fixture and flagship coupon, veto or none, and reached by
exactly the two populations above the moment the rule before it is removed.

### Telling a cutting plane from a chord

Some planes at a corner are the tessellation's own chords of the very surface
being placed on, and a chord is not a constraint; taking all of them
over-determines the point. The declaration answers it without an anchor and
without a tuned constant: **a chord facet of a surface is spanned by two
directions tangent to it, so its plane's normal is parallel to the surface's own
normal there; a plane that cuts across — a cap — has a normal perpendicular to
it.** The question is only which of the two it is closer to, which is a midpoint.

```text
cutting planes   the two flat bottoms            0.000000
                 ridge end caps, and a 6-gon     0.000003
chords           step-declare-grid-scad          0.995 to 1.000
                 step-band-family                0.885 to 0.923
```

Five orders of magnitude apart with nothing between. The surface's normal is
taken numerically, as how much of a step along the plane's normal the surface
takes back, so this needs nothing added to the `Surface` API and works for any
declared kind.

The same question in its other form — **which planes count at a triple point** —
is the weakest thing in this exporter. A corner on a declared surface and two
faces of the mesh is placed where the line those faces cross meets the surface,
but some of the planes there are the tessellation's own chords of that very
surface, and taking all of them over-determines the point. Sixteen of
`step-exact-trim`'s corners have three planes of which one is a chord. The bound
is the tessellation's angular half-step, `cos(pi/6)`, nothing coarser than a
hexagon being a tessellation; measured, chords read 0.885 to 1.000 and faces
0.000 to 0.643, and **the closest pair is `step-band-family`'s 0.885 against
`step-cut-cone`'s 0.643.** A real margin, and a thin one. An earlier 0.5 form of
the same test read *exactly* 0.5000 on one of `step-cut-cone`'s real cut planes —
the threshold sitting on top of the data it had to separate. It is an inference
standing in for something the model knew, and a declared plane at that corner
would turn it into a lookup.

Two routes to the same answer were tried first and neither works.
**An `ArcCurve` does not survive a boolean** — `ManifoldGeometry` carries
`surfaces_` through and has no curves at all, and `StepKernel` says as much with
`(void)curves`. **Matching a declared surface's `refpt`** works only where the
anchor happens to be the rim: `primitives.cc` pushes one `CylinderSurface` for
`r1 == r2`, so a cylinder's far cap is anchored by nothing, and a sweep's end
caps never are. `step-declare-grid-topcap.py` is the regression guard for both
holes — `step-declare-grid.py` reflected, so the ridge arrives at the cap nothing
anchors. Every number in it is its twin's, a reflection being an isometry, and
any number that differs is the bug.

### Fanning a polygon the move would bend

The placement declines wherever moving a corner would take a face that keeps a
`PLANE` out of its own plane. A polygon can be fanned into triangles instead, and
a triangle is planar wherever its corners are.

Only planar faces need it: an analytic face is written on its own surface and
does not care whether its corners are coplanar. A polygon with a hole still
declines the move — a fan from one corner triangulates a simple loop and an inner
bound would have to be threaded into it — and so does a face that would turn over.

Two things about the fan are easy to get wrong and both were:

- **A fan triangle needs its own plane, not the polygon's.** Writing the
  polygon's normal under each triangle leaves every one of them asserting a plane
  its corners have just moved off. That made the whole triangulation look
  worthless — fourteen faces bought for three parts in a thousand — when it was
  one line.
- **A fan needs its apex chosen, not corner 0.** A fan triangulates a polygon
  only where every ear winds the way the polygon does. The polygons are convex
  before the move and not always after: on `step-declare-grid-scad` three corners
  of a bore facet end up nearly in a line along the ridge's trim, and the fan
  from corner 0 cut an ear of area **-4.0e-05**. The face written for it
  contradicts its own bound, `validatestep.py` rejects it as *"PLANE normal
  disagrees with the winding of its outer bound (dot=-1.000)"*, and the fixture
  fails looking like a face count. The apex is now chosen for the ear it leaves
  worst, the polygon is split only if some corner leaves none inverted, and the
  plane is written the way the bound winds, always.

### The mesh moves between recognition and writing

The placement moves vertices by up to 0.1536 on `step-band-family`, so a section
agreed on the first mesh need not exist on the second — 132 edges with one face,
44 ellipses and the 88 chords their neighbours still wrote. **`decide_sections`
is a function rather than a step, and it is asked twice**, the second answer
being the one written; the export says so where they differ.

The trap that costs a day here is a vertex checksum apart. Two calls to one
deterministic function returned different answers, and four rounds of reading the
code found nothing, because every argument named in the call was identical — it
was the mesh underneath them that had moved. Checksum `vertices` at both call
sites first.

### What the face count costs, and whose fault it is

Fanning is the price of an *unclaimed declaration*, not of the corner work. On
`step-band-family` the bent faces are bore facets, still planes because a smooth
region of 169 of them — area 898.9, worst dihedral 10.6 degrees, which is the
32-gon's own step — is left faceted on a cylinder the model declared. Claimed,
they would be analytic faces that do not mind where their corners are. A guard
skipping corners no analytic face uses was written and measured, and dropped
**zero** of them: the fans are not idle, the claim is short.

---

## 5. What is implemented, and what it is worth

### Entities: what ISO 10303-42 has, and what we write

**The policy, and why it is the right one.** We write the *basic* entities and
make them exactly right before reaching for the richer ones. A richer entity
states more intent, and it is also harder to debug: when a `SURFACE_OF_REVOLUTION`
is rejected the fault could be in the profile, the axis, the trim, or the
reader's handling of the entity itself, where a rejected `CONICAL_SURFACE` has
nowhere to hide. So each richer entity is taken on only when the basic ones
beneath it are measured clean - the conic family is complete because the
circle was right first, and the hyperbola was added only once a chord across a
cone was proven to be the fault.

**And SOLIDWORKS's strictness is what keeps that policy honest.** OpenCASCADE
heals quietly - it widened a vertex to 1.08 and invented a seam edge on lid10
without a word - so an exporter validated against it alone would have shipped
chords 0.1 off their surfaces, ellipses 1.6 off their own endpoints and faces
straddling their own seams, and called the result good. Every one of those was
found because SOLIDWORKS refused the file. It is a pain to satisfy and it is the
most valuable reader we have.

#### Curves

| entity | written | notes |
| --- | --- | --- |
| `LINE` | yes | the chord; still the most common curve, and the one that lies off a curved surface |
| `CIRCLE` | yes | a section perpendicular to an axis, and a band's rim |
| `ELLIPSE` | yes | a plane section of a cylinder or cone within the half angle |
| `HYPERBOLA` | yes, since 2026-09-17 | a plane steeper than a cone's half angle - almost every plane down a shallow draft cone |
| `PARABOLA` | yes, since 2026-09-17 | the knife edge between the two; written when the algebra lands on it |
| `B_SPLINE_CURVE_WITH_KNOTS` | yes, rational as a complex instance | the crossing of two declared surfaces, fitted to 1e-7 of both |
| `POLYLINE` | no | a chain of chords; nothing to gain over the edges themselves |
| `TRIMMED_CURVE` | no | we trim by vertices, which is what a B-rep edge is for |
| `COMPOSITE_CURVE` | no | ditto |
| `OFFSET_CURVE_3D` | no | no construct produces one |
| `SURFACE_CURVE` + `PCURVE` | yes | the 3D curve and where it runs across a surface; written on every sweep face |
| `INTERSECTION_CURVE` | **no** | the subtype that *states* an edge is where two surfaces cross. We write its supertype `SURFACE_CURVE` and leave the intent unsaid |
| `SEAM_CURVE` | **no** | the standard's own answer for the seam of a closed face. We re-seam into an unused span instead (open item 15); a face that uses its whole profile would need this |
| `DEGENERATE_PCURVE` | no | a cone's apex; not yet needed |

#### Surfaces

| entity | written | notes |
| --- | --- | --- |
| `PLANE` | yes | asserted on the plane its own corners are on since 2026-09-16, not the mesh's normal |
| `CYLINDRICAL_SURFACE` | yes | |
| `CONICAL_SURFACE` | yes | |
| `SPHERICAL_SURFACE` | yes | including a sphere closed on itself |
| `TOROIDAL_SURFACE` | yes | |
| `B_SPLINE_SURFACE_WITH_KNOTS` | yes, rational as a complex instance | the sweep, and a fillet's patch |
| `SURFACE_OF_LINEAR_EXTRUSION` | **no - measured gap** | `linear_extrude` of a scaled circle exports as **192 LINEs and 66 planes**, entirely faceted; STEP states it exactly as an `ELLIPSE` swept along a vector |
| `SURFACE_OF_REVOLUTION` | **no - measured gap** | `rotate_extrude` of a scaled circle exports as **32 `CONICAL_SURFACE`s**, one per profile segment; STEP states it exactly as one surface |
| `OFFSET_SURFACE` | no | no construct produces one directly |
| `RECTANGULAR_TRIMMED_SURFACE` | no | faces are trimmed by their bounds |
| `CURVE_BOUNDED_SURFACE` | no | ditto |
| `DEGENERATE_TOROIDAL_SURFACE` | no | a torus whose tube reaches its axis; `rotate_extrude` touching the axis, open in the roadmap |

#### Topology and representation

| entity | written | notes |
| --- | --- | --- |
| `VERTEX_POINT`, `EDGE_CURVE`, `ORIENTED_EDGE`, `EDGE_LOOP` | yes | |
| `FACE_BOUND`, `FACE_OUTER_BOUND`, `ADVANCED_FACE` | yes | `ADVANCED_FACE` requires its geometry to be complete - which is exactly the discipline we want |
| `CLOSED_SHELL`, `OPEN_SHELL`, `MANIFOLD_SOLID_BREP` | yes | |
| `SHELL_BASED_SURFACE_MODEL` | yes | for an open result |
| `BREP_WITH_VOIDS` | no | a solid with an internal cavity; would be needed the day a part has one |
| `POLY_LOOP`, `VERTEX_LOOP`, `FACETED_BREP` | no | the faceted path writes planar `ADVANCED_FACE`s instead |
| the CSG branch (`CSG_SOLID`, primitives) | no | a B-rep exporter by design |
| AP242 tessellated | no | ditto |

The two surface gaps are the ones worth closing, and they are the richer
entities the policy above is for: both describe exactly the shapes
`linear_extrude` and `rotate_extrude` make from a curved profile, and both carry
a profile curve that is one of the conics we now write correctly. They come
after the basic entities are clean on the shapes that already use them.

### Constructs, measured one export of a ten-line model each

| construct | written |
| --- | --- |
| `linear_extrude` of a circle | `CYLINDRICAL_SURFACE` + 2 planes |
| `linear_extrude(scale=)`, uniform taper | `CONICAL_SURFACE` + 2 planes |
| `linear_extrude` of an `offset(r=)` profile | one cylinder per rounded corner |
| `rotate_extrude` of a circle | **one** `TOROIDAL_SURFACE` for the whole solid |
| `linear_extrude` of a scaled circle (an elliptical cylinder) | **192 `LINE`s and 66 planes** - faceted; wants `SURFACE_OF_LINEAR_EXTRUSION` |
| `rotate_extrude` of a scaled circle | **32 `CONICAL_SURFACE`s**, one per profile segment; wants `SURFACE_OF_REVOLUTION` |
| `rotate_extrude` of a rectangle | 2 cylinders + 2 planes |
| `rotate_extrude` of a sloped polygon | cone + 2 cylinders + 2 planes |
| a revolved free-form curve, hollow | 80 exact cones + 2 planes, from 5120 facets |
| `sphere()` | one `SPHERICAL_SURFACE` closed at its poles |
| a filleted cube | 6 planes, 12 edge cylinders, 8 corner sphere octants |
| two equal cylinders crossing | 8 cylinder faces bounded by `ELLIPSE` arcs, no plane at all |

None of the extrude rows comes from `LinearExtrudeNode` or `RotateExtrudeNode`,
which declare nothing. They come from `geometry/linear_extrude.cc` and
`geometry/rotate_extrude.cc`. Worth knowing before looking for the code in the
wrong file.

### The sphere, closed at its poles

An OpenSCAD sphere has no pole vertex: its outermost ring sits half a ring step
short of the axis, so the mesh closes either end with a flat disc. The exporter
used to write those discs faithfully — a `SPHERICAL_SURFACE` beside two planar
caps, 0.0035% short of the sphere the model declared. Faithful to the mesh, wrong
about the model.

A run of bands reaching the last ring at either end now absorbs both discs and is
written as the complete quadric, bounded by its seam meridian alone: used once in
either direction, with the poles — centre ± r along the axis — where the two
usages meet. **There is no rim at all**, so the face has two edges, fewer than
any other face of revolution.

The gate is angular and structural rather than a tolerance on size: both end rims
have to be the whole bound of one flat face across the axis, and the gap each
spans has to be no more than one ring step of the run's own bands, since the
tessellation leaves exactly half a step and a boolean cut lands anywhere. A
sphere cut further in keeps its flat, and so does every other kind of cap. The
one cut this cannot see is one exactly at the last ring, which removes no facet
and leaves a mesh identical to the uncut sphere's.

A fitted sphere qualifies on the same evidence, because the closure asks what the
mesh is rather than what the model meant.

### Volumes derived from a model and confirmed by a kernel that never saw the arithmetic

```text
step-cylinder-cross   16 r^3/3              5333.33333    5333.333262  exact tier
step-cut-cone         a z-slice integral    1661.646512   1661.646504  approximation
step-bored-cylinder   an elliptic integral  5298.405619   5298.405620  approximation
step-bored-cone       likewise              5382.203842   5382.205081  approximation
step-sphere           (4/3) pi r^3           523.5987756   523.598776  exact tier
```

### Known quality gaps, by design or measured

**A boolean on the CGAL backend splits the walls it did not touch.** Exactly the
four fixtures whose top-level object is a Nef polyhedron recognise differently on
the two backends; every fixture that stays a `PolySet` is identical on both, down
to the entity count.

| fixture | Manifold | CGAL | what is lost |
| --- | --- | --- | --- |
| step-bore | 2 | 1 | the outer wall; the bore survives |
| step-nested-rings | 5 whole | 17 partial | every ring, cut into arcs |
| step-partial-cylinder | 4 | 0 | all four arcs |
| step-shared-arc | 2 | 0 | both walls |

Same declarations — both runs report the same count available, which is what the
sanity test asserts — and the same number of faces in the faceted export. A Nef
polyhedron records the boolean's seams, so the wall of a ring the operand never
reached still gets cut where the operand's plane passed through it, and a wall in
several pieces is a wall whose rims no longer bound a single face. **Export
analytic STEP on the default Manifold backend.**

**`minkowski()` drops every record**, deliberately, on both backends.

**A non-uniform scale drops the records it cannot express.** A cylinder under
`scale([2,1,1])` is an ellipse and no `Surface` subclass describes one;
`Surface::transform` returns false and the record is discarded rather than kept
wrong.

**A facet belongs to a band only if it lies *wholly* on the surface.** Where
anything flat is cut into a faceted wall, the boundary facet is a trapezoid with
two corners on the circle and two inside it. On a radius-10 wall at `$fn=32`
those corners sit five thousandths inside. They have to stay planar; taking them
would pull the face's boundary out onto the true circle and open the shell by far
more than the modelling tolerance. **The trim curve is not in the mesh** — that
is the hard ceiling on what recognition alone can reach, and it is what the
crossing curve and the corner placement exist to get round.

**Corners on a surface are not a facet on it.** A bore tessellated as one quad
per angular step running the whole length has all four corners on the two mouth
curves — and a mouth curve is a section of the cone — so all four are on the cone
to 1e-7 while the *middle* of the facet is 6.0 away from a cone whose
tessellation concedes 0.048. A lean test cannot reject it either: at theta = 180
the bore's wall runs so nearly parallel to the cone it pierces that the two
normals agree to 5.5 degrees. `recogniseQuadricPatches` samples the centroid and
the edge midpoints, against twice the band.

### What a model author can do about it

The exporter's three gates are all things a model author influences, and on the
bayonet the model decides far more than the exporter does. Cheapest first, and
all of them are about the topology gate:

- **Prefer primitives and booleans to `polyhedron()`.** `cylinder()`, `hull()`,
  `difference()`, `union()` and transforms all carry provenance; `polyhedron()`
  carries none and never can.
- **Interrupt a wall with planes, not with features.** A plane through the axis
  or perpendicular to it leaves an exact arc and an exact straight edge.
- **Let a wall end against one face, not against many.** This is the rule that
  costs the most. A rim bordering a single planar face, or a single chamfer that
  is itself a surface of revolution, collapses; a rim bordering one facet per
  segment does not. Run a chamfer or a fillet all the way round rather than
  stopping it against something small, and keep ribs, ramps and threads clear of
  the rim where two walls meet.
- **Chamfer by hulling with a coaxial copy.** `hull()` of two coaxial cylinders
  is recognised as a cone because both rims match a declared cylinder, and the
  rim between chamfer and wall is written once and shared.
- **A feature that must be non-analytic is cheaper if it is bounded by planes.**
  A thread or a ramp will stay faceted whatever happens; where it *ends* decides
  whether it also spoils its neighbours.

**A helical thread can never be analytic, and it takes the bore with it.** A
helix is not a surface of revolution, so no band recogniser will describe the
ridge — and a single-start thread running the length of a bore slices the bore
itself into a helical ribbon, which is not a band either. On the bayonet, 991 of
999 unrecognised faces are the hose thread, spread at about 72 faces per 5 mm
over z = 0..75, which is exactly `pitch x turns`. The largest exportability lever
in that model is a switch it already has: `_hoseThread = false`.

---

## 6. What the CAD kernels do

Three kernels have read these files, they disagree, and each disagreement is
informative once its mechanism is known.

### OpenCASCADE

The round trip (`tests/steproundtrip.py`) and every strict measurement here.

- **`BRepCheck_Analyzer` says "valid" and stops.** OCCT sews by widening the
  tolerance of an edge until it covers the gap between that edge and the faces it
  bounds, so a shape whose edges do not lie on their surfaces still comes back as
  one closed solid. It has simply been granted the slack.
- **What that tolerance *is*.** Per the OCCT STEP user guide: *"The resulting
  tolerance of `TopoDS_Edge` is a maximal deviation of its 3D curve and its
  pcurve(s)."* So the number `worst_tolerance()` reports is precisely our
  pcurve-versus-3D disagreement — the defect itself, not a kernel being generous.
- **The cap does not bind.** `read.maxprecision.mode = Preferred` may be exceeded
  *"currently, only for deviation of a 3D curve and pcurves of an edge, and
  vertices of such edge"* — exactly our case. Forcing the cap to 1e-7 changes
  nothing.
- **`DE_ShapeFixParameters` defaults `MaxTolerance3d` to 1.0**, which is the
  licence it exercises. The tolerance OCCT grants is therefore **not** a
  correctness measure: it is *largest*, 0.261, on the lid variant SOLIDWORKS
  reads happily.
- **OCCT's verdict depends on its settings** — `read.precision`,
  `read.maxprecision` in both modes, `read.surfacecurve` in all three,
  `read.stdsameparameter`, and the `FromSTEP` shape-processing sequence.
- **It repairs silently.** It had been splitting faces on read, so matching its
  face count says the exporter agrees with the repair, not that either is right.
- **It writes a closed sphere as a `VERTEX_LOOP` with no edges at all**, and
  reads ours back as one `Sphere` with two degenerate edges. Three kernels, three
  spellings of the same solid.

### SOLIDWORKS

Driven by `scripts/step-interop-solidworks.ps1`; needs SOLIDWORKS running.

- **It is strict rather than wrong on the analytic tier.** It reads our pcurves,
  holds them against our 3D curves, and refuses what does not agree. Where the
  two kernels differ, the strict one has usually been pointing at something real
  — the face split and the pcurve-purity finding both came out of following its
  objections.
- **Read `-FaultDetail` before forming a theory.** It names entity, kind and
  location. The count alone hid that `c11` has *two unrelated* faults, one on a
  sweep and one on a planar cap, and a whole fix was built for the wrong one.
- **`IFace2::Check` counts are not Import Diagnostics faulty faces.** 82 and 87
  hits where the dialog shows 1. On the band family the dialog counts *higher*
  than the API, 32 against 3 and 5 against 2. (An earlier note here sorted the
  codes into "informational" 13/21/30 and "structural" 7/17. Drop that: 21 is
  `swFaceSelfIntersecting`, and the split was never measured.)

  **Measured again on 2026-09-16, and it is worse than a count mismatch.** The
  dialog showed one faulty face on `f02-band-fn032-analytic` - "General geometry
  problem", on the bore wall - for a file the API calls valid. All three body
  checks agree with each other and disagree with the dialog:

  | file | `Check` | `Check2` | `Check3` | `IFace2::Check` | dialog |
  | --- | --- | --- | --- | --- | --- |
  | f02 today (three builds of it) | valid=1 | 0 | 0 | 0 faces | **1 face** |
  | f02 control 2026-09-08 | valid=0 | 2 | 2 | 2 faces, codes 17 | — |
  | lid10 today | valid=0 | 1 | 1 | **7 faces**, codes 7/17/21 | **2 faces** |

  So neither list contains the other: the dialog sees a fault the API does not on
  one file, and the API sees five the dialog does not on another. `Check` and
  `Check2` are marked obsolete and superseded by `Check3` in the API help, and
  the run above is what says they are also *redundant* - they agree with it
  everywhere - so the disagreement is between the whole check family and the
  dialog, not between its members. "General geometry problem" is not a string in
  `swFaultEntityErrorCode_e` either, which is the other sign that the dialog runs
  its own analysis.

  **There is no read-only API for the dialog's list.** `IPartDoc::ImportDiagnosis`
  is its engine but repairs rather than reports, returning only `>= 0` or `-1`;
  `IBody2::Diagnose` answers for gaps alone (`IDiagnoseResult` has
  `GetGapsCount` and the co-edges at each gap, and nothing else); and no
  interface in the assembly exposes a faulty-face list beyond `FaultEntity`.

### The census: what we write that a reader objects to, and which objection it becomes

`scripts/step-diagnostics/fault-census.py file.stp` is the consolidated form of
everything learned between 2026-09-14 and 2026-09-17. One table, keyed by the
SOLIDWORKS fault code each property turns into, measured on the file we wrote
rather than on what a reader made of it.

| code | property | what it means, and what it cost |
| --- | --- | --- |
| **7** | a vertex off the curve its own edge is written as | a curve substituted for a run of chords must pass through that run's own endpoints. lid10 once wrote 14 ELLIPSEs missing theirs by up to **1.6196**, on a grazing section of a 1.15° draft cone where the major axis runs as the reciprocal of the tilt and turns a sagitta into millimetres. |
| **16** | a vertex off the surface of a face that uses it | the corner placement left it there. On its own it is small; what makes it matter is the next row. |
| **13** | a short edge at a strayed vertex | *not* a fault in the file - an edge 0.219 long is a good edge. It becomes one because a reader widens a strayed vertex until it covers the stray, and then the edge's two ends are inside one tolerance. lid10 gained `swEdgeVerticesTouch` the day the plane veto stopped holding corners and left **one** behind, on an edge 0.219 long at a vertex 0.105 off its cone. The ratio is what to watch, not either number. |
| **21** | a loop that visits a vertex more than twice | a pinch, which is what self-intersecting means for a face. The seam of a face on a closed surface legitimately appears twice and is excluded. |
| **17** | an edge lying off the surface of a face it bounds | a chord where the surface curves. Every B-spline edge we write is exactly on both its surfaces; every straight line on a curved one is not, by up to **0.1089**. The conic a plane cuts is exact and writable - **hyperbola and parabola included**, and on a shallow draft cone it is almost always a hyperbola. |
| — | a face loop crossing its surface's seam | the pcurve has nowhere to go but the long way round, sweeping the whole profile instead of stepping across. OpenCASCADE repairs it by inventing a seam edge and widening a vertex to **1.08**, and everything touching that vertex is faulty afterwards. |

Two things the table is careful about, because both cost a day when they were
not:

- **Codes 13 and 21 are second-order.** They are what a reader *produces* out of
  a stray plus a short edge, or out of a pinch; fixing them directly is fixing a
  symptom. The census reports the precursor, not the code.
- **Our own count and a reader's need not agree, in either direction.** lid10's
  census says one strayed vertex and two short edges at risk, and SOLIDWORKS
  reports one code 13; it says three pinched loops and SOLIDWORKS reports two
  code 21. Neither is the other's ground truth. What the census is for is that it
  measures *our* file, which is the part we can fix.

For calibration: SOLIDWORKS ships a vertex **0.1015** off its own edge's curve in
the file it saves after repairing `f02` by hand - and that number is our own
`line-on-bspline` worst for the same file, read back through its writer. So code
7 is not a claim about exactness at the tier we hold ourselves to. It is a claim
about what its tolerances could not absorb.

### How to interrogate a reader, and why the fault list is the last thing to ask

Settled 2026-09-17, after a day in which three fixes measured clean on our side
and moved nothing on the CAD system's, and one afternoon of round trips answered
what the fault lists could not.

**1. Import raw, always.** Four settings in Tools > Options > Import change what
is measured rather than how it is reported, and three of them are on by default:

| setting | what it does to the measurement |
| --- | --- |
| knit / *Try forming solids* | forms a solid out of surfaces, so a shell with a hole in it still reads `solid` and the face count says nothing |
| *Automatically run Import Diagnostics* | repairs on import, so the faults counted afterwards are the ones the repair could not fix, not the ones the file has |
| *Create analytic faces* | re-reads B-splines as quadrics of its own, so a round trip compares the reader's idea of the surface against ours rather than ours against itself |
| custom tolerance | moves the threshold everything else is judged against |

`step-interop-solidworks.ps1 -Raw` turns all of them off and puts them back
afterwards; every run is still labelled with what was set, because a result
recorded without them cannot be compared with another one. Measured cost of not
doing this: the 2026-09-08 control's volume reads 19555.2696 with the defaults
and 19554.1241 raw, so analytic conversion had been quietly altering geometry in
every interop run this project has made.

**2. Round-trip everything.** `-RoundTrip` saves the reader's own model back out
through the reader's own writer. That file is the only artefact that says what
the reader *made* of ours rather than what it *says* about it, and it outranks
every fault list because it can be measured rather than read.

**3. To isolate a repair, round-trip it twice.** `-RoundTrip -Heal` writes
`<coupon>_SW.STEP` before the repair and `<coupon>_SW_healed.STEP` after it. The
pair differs by the repair alone - same writer, same settings, same session - and
`scripts/step-diagnostics/reader-diff.py` compares them by geometry, because the
ids are renumbered on every save and a text diff is noise.

That is what finally explained SOLIDWORKS's "General geometry problem" on `f04`,
a fault no API check reports at all: **every face matched on surface and corners,
the census was identical, and 215 B-spline curves had been re-fitted by a median
of 4.281e-07 and at most 9.583e-05 with both endpoints unmoved.** Nothing
structural was wrong; a curve was a ten-thousandth of a millimetre off the
surface it bounds. Ours were off by 0.1089, which is what open item 22 is about.

**4. A failed repair makes the diagnosis worse, not better.** On the bayonet on
2026-09-17, `ImportDiagnosis` took a 64-face solid to 2 faces and no volume at
all - `healfaces=-62 healvol=-235362.4410`. A repair that fails redistributes the
damage, and faults counted after one are faults of the repair. So the counts in
a `-Heal` run are taken **before** it, and the heal is reported as a delta beside
them rather than folded in.

**5. Only then read the fault list**, and read it knowing that `IFace2::Check`
and the Import Diagnostics dialog are different analyses and neither contains the
other (see *Fault codes* above).

- **So measure the repair, not the report** - `step-interop-solidworks.ps1
  -Heal`. It runs `ImportDiagnosis` and records what changed: face count, volume
  and area before and after, with the document closed unsaved either way. A body
  SOLIDWORKS alters is a body SOLIDWORKS thinks is wrong, whatever either list
  says, and unlike a count it can be compared between runs. Measured:

  | file | Δfaces | Δvolume | Δarea |
  | --- | --- | --- | --- |
  | f02 control 2026-09-08 | 0 | **−40.9364** | **+7.1597** |
  | f02 today | 0 | 0 | 0 |
  | lid10 today | 0 | 0 | 0 |

  The one file it alters is the known-bad control. It declines to touch either
  current file - including the one whose dialog lists a faulty face, and the one
  whose API lists seven. This is the same lesson as `c06`, where a clean fault
  report sat beside a solid 14% wrong and only the volume caught it: what a
  kernel *does* is a measurement, what it *lists* is a report.
- **Fault codes.** `swFaultEntityErrorCode_e`, all 36 of them. The number and
  name of each are reflected out of the installed
  `api/redist/SolidWorks.Interop.swconst.dll`, and the description is the API
  help's own sentence (SOLIDWORKS 2026 API help, *swFaultEntityErrorCode_e
  Enumeration*); the two sources agree member for member, checked 2026-09-18.
  The sentence is what matters - it says which *two* entities the code is a
  relation between - so it is quoted exactly rather than paraphrased.

  "Seen" is whether any file of ours has produced the code in a SOLIDWORKS run;
  "census" is the `fault-census.py` row that watches for its cause in our own
  file, where there is one.

  | code | name | SOLIDWORKS's description | relates | seen | census / notes |
  | --- | --- | --- | --- | --- | --- |
  | 1 | `swBodyCorrupt` | Data structure is corrupt | body | — | — |
  | 2 | `swBodyInvalidIdentifiers` | Body has invalid or duplicate identifiers | body | — | — |
  | 3 | `swBodyInsideOut` | Body is inside out | body | — | *`c06` read inside out yet reported clean - volume caught it, no code did* |
  | 4 | `swBodyRegionsInconsistent` | Regions of body are inconsistent | body | — | — |
  | 5 | `swEdgeNonPeriodicCurve` | Open or non-periodic curve on ring edge | edge | — | a closed edge whose curve is not closed |
  | 6 | `swEdgeNonPeriodicNomGeom` | Open or non-periodic nominal geometry on ring edge | edge | — | — |
  | 7 | `swEdgeVertexNotLie` | Vertex does not lie on edge's curve | vertex / edge curve | **yes** | code 7 row - exact; also produced by a reader merging a strayed vertex (see 16) |
  | 8 | `swEdgeVertexNotLieNomGeom` | Vertex does not lie on nominal geometry | vertex / edge curve | — | — |
  | 9 | `swEdgeWrongDir` | Curve in wrong direction for edge | edge / its curve | — | why a conic's *placement* turns rather than its vertices |
  | 10 | `swEdgeWrongDirNomGeom` | Nominal geometry in wrong direction | edge / its curve | — | — |
  | 11 | `swEdgeSpcurveOutOfTol` | Spcurve not within edge's tolerance | pcurve / 3D curve | — | why a sweep chord's pcurve is rebuilt and checked with its curve |
  | 12 | `swEdgeSpcurveOutOfTolNomGeom` | Spcurve not within edge's tolerance of nominal geometry | pcurve / 3D curve | — | — |
  | 13 | `swEdgeVerticesTouch` | Vertices of edge touch | edge / its two vertices | **yes** | code 13 row - second-order: a short edge at a strayed vertex |
  | 14 | `swEdgeBadFaceOrder` | On a non-manifold edge, the face order around the edge does not match the order of the faces' surfaces around the edge | edge / its faces | — | non-manifold only |
  | 15 | `swEdgeBadWire` | Two wireframe edges intersect at a position other than a vertex | wire | — | — |
  | 16 | `swFaceBadVertex` | Vertex does not lie on face's surface | vertex / face surface | never | code 16 row (analytic surfaces only); what every pre-2026-09-16 instrument measured |
  | 17 | `swFaceBadEdge` | Edge does not lie on face's surface | edge / face surface | **yes** | code 17 row - a chord where the surface curves |
  | 18 | `swFaceBadEdgeOrder` | Edge order around the face does not match the order of the edges' curves around the face | face / its loop | — | — |
  | 19 | `swFaceNoAccomVertex` | A surface singularity has no accompanying vertex | face / surface pole | — | a cone's apex or a sphere's pole inside a face with no vertex there |
  | 20 | `swFaceBadLoops` | Loops inconsistent | face / its loops | — | — |
  | 21 | `swFaceSelfIntersecting` | Face is self intersecting | face | **yes** | code 21 row - second-order: a pinched loop |
  | 22 | `swFaceBadWireframe` | A wireframe edge intersects a face at a position other than a vertex | wire / face | — | — |
  | 23 | `swFaceCheckerFailure` | Checker failure during face/face check | face / face | — | the checker gave up, not a verdict |
  | 24 | `swFaceFaceInconsistency` | Inconsistent faces | face / face | yes, faceted control | lid10's *faceted* control reads codes=24 |
  | 25 | `swGeomStateSelfIntersect` | Geometry is self intersecting | geometry | — | — |
  | 26 | `swGeomDegenerate` | Geometry degenerated | geometry | — | — |
  | 27 | `swRegionBadShells` | Inconsistent shells | region / shells | — | — |
  | 28 | `swShellBadTopologyGeometry` | A topological entity belonging to a shell is not geometrically within the shell | shell | — | — |
  | 29 | `swShellIntersect` | Shell shell inconsistency | shell / shell | — | — |
  | 30 | `swTopolNotG1Continuous` | Topology's geometry is not G1 continuous | entity | **yes** | accompanies 13 wherever a reader has merged vertices; no row of its own |
  | 31 | `swTopolSizeBoxViolation` | Some or all of entity is outside size box | entity | — | — |
  | 32 | `swTopolStateCheckFail` | Checker failure | entity | — | the checker gave up, not a verdict |
  | 33 | `swTopolStateNoGeometry` | Missing geometry | entity | — | — |
  | 34 | `swEntityStateInvalid` | State of entity is invalid | entity | — | — |
  | 35 | `swTopolMissingGeometry` | Missing geometry | entity | — | — |
  | 36 | `swEdgeTouchEdge` | Edges touch | edge / edge | — | — |

  Two things the table makes plain. First, the codes we have actually produced
  are **7, 13, 17, 21, 30** and, on a faceted control, 24 - five of 36, and all
  but 17 are either a relation the census measures directly (7) or a reader's
  response to one (13, 21, 30). Second, the relation every instrument measured
  before this session was **16**, which has never fired on a file of ours.

  "Nominal geometry" (8, 10, 12) is SOLIDWORKS's own term for the exact geometry
  behind a tolerant edge or vertex; we have never seen those codes and do not
  write tolerant entities.

  Read as sentences rather than as names, the codes split cleanly by *which pair*
  each one holds against the other, and that is the whole of their diagnostic
  value:

  - **7 and 8** are a vertex against its own edge's curve. Not against a surface,
    not against a face — against the curve that edge is written as. So a code 7
    is the exporter having substituted a curve for a run of chords that does not
    pass through the run's own endpoints, and nothing else.
  - **11 and 12** are the 2D pcurve against the 3D curve.
  - **16 and 17** are a vertex, and an edge, against the *face's surface*.
  - **13** and **30** are properties of one entity.

  This matters because our instruments did not all measure the same relation. The
  corner-stray family (`step-occt-strict.py`, `step-corner-stray.py`) measures a
  corner against the surface its face is written on — which is code **16**, and
  code 16 has never fired on any file of ours. The faults we actually get are 7
  and 17, and neither of them is what those scripts report. A green corner-stray
  run therefore says nothing about a SOLIDWORKS fault; that is not a limitation
  of the instrument but of reading it as an answer to a question it was never
  asked. `scripts/step-diagnostics/face-bad-edge.py` is the one that measures all
  of them, and it is the one to reach for first.

  **The invariant this implies, and it is general.** *A curve substituted for a
  run of chords must pass through that run's own endpoints.* It binds the ends
  only: the vertices between them are the chords' own, they are a sagitta off the
  surface because that is what a tessellation is, and the curve replaces them
  rather than passing through them. Two things follow, and both are in the code:
  the corner placement has to put those endpoints on the surfaces the file
  asserts - which means **planar faces are surfaces of the placement**, since a
  plane section's endpoints are corners between a plane and a quadric - and the
  exporter audits every finished `EDGE_CURVE` against its own two vertices before
  writing. See open item 20 for what it cost to learn and what it did not fix.

  SOLIDWORKS has a **separate** code for a bad vertex, a bad edge *order*, bad
  loops, a pcurve out of tolerance and a wire that does not close. So **17 is
  what is left when an edge is individually well formed, correctly ordered,
  inside a closed wire, with a pcurve that agrees — and is still not acceptable
  *to the face***. It is a relation between an edge and a face, not a property
  of either, and that narrows a diagnosis a long way before any measurement.

  13 `swEdgeVerticesTouch` is an edge whose two vertices coincide, which is how
  a periodic face's seam is written — though a 13 is *not* by itself evidence
  that a seam of ours caused it; see *Refuted claims*. 21 is
  `swFaceSelfIntersecting`, which earlier notes here called "informational" and
  is nothing of the kind.
- **It splits every periodic face at its seam** on re-export: two cylinders into
  four, four tori into eight, a closed sphere into two. It accepts a sphere
  closed on itself but does not write one itself.
- **Its reported volume is not always the volume of the body it keeps.** On `c11`
  it reports 14401.26 and then exports a body OpenCASCADE measures at 16663.29 —
  our own to 0.03%. Mass properties are integrated over the faces, and it has
  declared one of them faulty. Only the round trip separates that from a body
  that really is destroyed.
- **It is loose on ellipse-bounded quadrics.** On `c17` it lands 5.6e-6 from the
  Steinmetz value where OpenCASCADE lands 1.3e-8. Not the file: the other kernel
  reads the same file essentially exactly, and both agree on the faceted control.
- **Its verdict depends on Tools > Options > Import**, which is why every run is
  labelled with its `-ImportSettings`. A result recorded without its settings
  cannot be compared with another one — and since 2026-09-09 the driver **reads
  them out of the application** rather than trusting the label, so a run is
  self-describing: `knit`, `analytical-conversion`, `run-diagnostics`,
  `check-and-repair` and the rest land in an `import_options` column. That
  retrospectively rescued the run recorded as
  `as-configured-2026-09-08-unverified`: the setting is `knit=do-not-knit`, the
  same as every earlier run, so they are comparable after all.
- **It is intermittently unstable under automation, and the cause is not known.**
  Three crashes in one day, all `mfc140u.dll` `0xC0000005`; two followed killing
  the driver mid-`LoadFile4`, which is why the script's NOTES say never to do
  that. Two other explanations were floated and both were refuted: "these files
  hang it" (the file that stalled then imported cleanly twice in a row) and "the
  driver edit did it" (pristine and modified builds give identical results).
  Whole 48-file suites have run without one.
- **`c06` is its standing outlier.** It measures `c06-partial-torus` 14.16% low
  where the model's own Pappus arithmetic, OpenCASCADE and Fusion agree to 1e-9,
  and it reports zero faults doing it. Its own re-export measures the same 14%
  short, so the geometry is genuinely destroyed on import. It splits our four
  quarter-tori into eight faces covering **87% more surface than exists in the
  part** while enclosing 14% less volume. It is the trim. The fillet sense is
  ruled out by arithmetic: flipping all four corners of an `offset(r=2)` would
  change the volume by 746 and the observed deficit is 1579.

### Fusion 360

`scripts/fusion/step-interop-fusion.py`, run inside Fusion — it has no
out-of-process automation. Reach it through *Utilities → ADD-INS → Scripts and
Add-Ins → Scripts*, add the file with the plus button, and run it.

Fusion agrees with OpenCASCADE everywhere it has been asked, including on `c06`
to six figures and on files SOLIDWORKS once refused. It does not copy a file it
re-exports: it re-decomposes it, and its rewrite of lid10 describes the same
solid to 0.087% of volume and 0.02% of area while writing every seam, trim and
placement its own way.

### Two facts about kernel disagreement that generalise

**A face a kernel reports no fault on can still be measured 0.17% wrong**, and a
body can be faultless and 14% wrong. `faults=0` is necessary and not sufficient.

**A kernel can prefer a wrong file.** Restricting each face's surface to its own
profile span measured *exactly* on a coupon and **8.2% wrong** against a screw
sweep's derived volume — 520.216 against 480.940 from `turns*2pi*A*(R+dc)` — and
it was the variant SOLIDWORKS graded best. Without that derivation it would have
shipped.

### Half a fix is worse than none

The sharpest kernel finding in this project, and it generalises past its own
occasion. A corner-snapping "ladder" that moved what it could and claimed the
rest anyway regressed the bayonet from **1 faulty face to 83** in SOLIDWORKS,
while every local instrument said it had improved:

| measure | uniformly loose | the mixture |
| --- | --- | --- |
| boundary vertices off their own surface | 334 / 958 | **46 / 958** |
| pcurve endpoints off the surface | 475 / 3400 | **188 / 3400** |
| `BRepCheck_Analyzer` at a forced 1e-6 | 4 bad faces | 4 bad faces |
| entity counts, topology | — | identical |
| **SOLIDWORKS faulty faces** | **1** | **83** |

Order the variants by the *count* of bad pcurve endpoints and SOLIDWORKS makes
no sense. Order them by **purity** and it is exact:

| | pcurve endpoints off | worst | SOLIDWORKS |
| --- | --- | --- | --- |
| uniformly loose | 475 | 0.218 | **1** |
| a mixture | 188 | 0.218, unchanged | **83** |
| uniformly tight | 8 | 0.021 | **0** |

**A kernel that infers a face's tolerance from its boundary grants a uniformly
loose boundary one loose tolerance and finds everything consistent. Given a
mostly-exact boundary with outliers it takes a tight tolerance from the majority,
and every outlier then breaks each face that touches it.**

The same shape appears one level down, inside the ladder's own rungs: holding
only the vertices already proven on a surface is worse than holding all of them
or none, because a boundary that steps in and out of a surface makes the
recogniser split at every step.

The principle that survives is **refuse what cannot be placed exactly** — move
every claimed vertex or make no claim.

---

## 7. Testing

### The four layers

| layer | asks | run by |
| --- | --- | --- |
| `tests/validatestep.py` | is the file well formed by this project's lights? | every fixture |
| the exporter's own report | did the recogniser recognise what it should have? | `EXPECT:` |
| `tests/steproundtrip.py` | does an independent kernel read it as the intended solid? | `ROUNDTRIP:`, `VOLUME:`, … |
| the interop kit | does a *commercial* kernel agree? | by hand |

`validatestep.py` has one check per historical defect and has caught real bugs,
but it is a proxy: it knows what this exporter has got wrong before, not what a
kernel requires. `validateSTEP()` runs fourteen: `check_real_literals`,
`check_references`, `check_units_and_context`, `check_directions`,
`check_topology`, `check_shared_vertices`, `check_face_normals`,
`check_hole_nesting`, `check_bound_enclosure`, `check_cylindrical_faces`,
`check_surface_curves`, `check_bspline_faces`, `check_shells` and
`check_shell_volumes`. `check_closed_sphere` is a face-shape rule reached from
`check_cylindrical_faces` rather than a check of its own.

The round trip is OpenCASCADE and is an **optional** dependency: it skips
silently when absent, so a green suite does not mean it ran. See `CLAUDE.md` for
the version that has to be installed.

### The defects the checks exist for

Every check in `validatestep.py` is one historical defect. The classes are worth
knowing, because each one produced a file that a CAD system rejected or silently
misinterpreted while looking fine from the inside:

- **Formatting.** Coordinates rounded to six significant digits by the default
  `ostream` precision; `DIRECTION('',(0,0,1))` and `1e-07` written as REALs where
  ISO 10303-21 requires a decimal point and an upper-case exponent; a comma
  decimal separator on a German locale, because `snprintf` follows `LC_NUMERIC`
  and `openscad.cc` calls `setlocale(LC_ALL, "")`.
- **Topology.** Every face carrying its own copy of each vertex, so neighbouring
  faces were never stitched; face normals inverted or zero, from taking the plane
  as the cross product of the first two edges, which points inward at a reflex
  corner and collapses when the first three vertices are collinear; zero-area
  faces and zero-length edges; disconnected bodies in one `CLOSED_SHELL`.
- **Missing framing.** No units, no modelling tolerance, mandatory arguments
  absent — importers then fall back on their own defaults.
- **Suffix resolution.** `-o part.stp` rejected while `export(part, "part.stp")`
  silently wrote STL, because resolution keyed on the format identifier rather
  than the suffix.
- **The membrane.** `mergeTriangles()` decides which face a hole belongs to by
  keeping the *last* enclosing loop rather than the innermost, so where a plane
  holds concentric loops the hole is recorded against the wrong face and a bore
  is sealed over. **It is invisible to topological checks** — the hole edges are
  still used by exactly two faces, once in each direction, so the shell is
  watertight and every edge-pairing test passes. Only a nesting check finds it.
  The first diagnosis was also wrong: `pointInPolygon()` *does* fail outright on
  concentric circular loops, and fixing that was real and was not what produced
  the membrane. The distinguishing evidence was that the fix printed no warning
  on the affected model. The exporter no longer trusts `faceParents` for holes.
- **The dropped loop.** A merged loop whose winding disagreed with its face
  normal was taken for a hole, and if nothing coplanar enclosed it, dropped.
  **Dropping a face is never the right answer**: its edges are then used once
  instead of twice and the shell is open along all of them — 94 such edges over
  61 faces in one annulus of a committed export. And nothing enclosing it is
  precisely the evidence that it is *not* a hole. It is now kept as an outer
  bound, reversed so its winding agrees with the mesh normal.

Two lessons from the last of those, both general:

- **A rejected thing is invisible**, and this time it was a face rather than a
  band. The diagnostic existed and was printed. Nobody validates the stdout of an
  export of a real model, so the only witness was a 2.9 MB artifact committed to
  `examples/step_test/`.
- **A real model finds what fixtures cannot.** Every fixture is a small synthetic
  part and all of them validate. Running `tests/validatestep.py` over an export
  of an actual user model is one command and is not part of any test.

### What the sanity test runs per fixture

`tests/stepexportsanitytest.py` exports each fixture five times:

1. **faceted** (default) — validated and round-tripped;
2. **faceted under a comma-radix locale**, compared against (1) after
   normalisation, so a number formatted through `LC_NUMERIC` cannot slip back in;
3. **approximation** (`step-analytic-surfaces` + `step-approximate-surfaces`);
4. **analytic** (`step-analytic-surfaces` alone);
5. **analytic on `--backend=CGAL`**, comparing *how many declarations reached the
   exporter* against (4).

That last one asserts the channel, not what was written: the two backends
genuinely do not write the same surfaces, which is measured under *Known quality
gaps*. Asserting what was written was the first form of this test and was wrong.

`report_contradictions()` checks the exporter's report against itself on every
fixture and both analytic runs — totals against their breakdown, a population
against the same population two lines later, a bound against what it bounds, a
maximum over an empty set. It exists because four well-formed numbers in that
report were false about the set they described and no test failed.

### Derived, not captured

The rule that matters most, and the one that is easy to lose:

> A fixture asserts numbers worked out from the model's own dimensions. A number
> that can only be obtained by running the exporter is a last resort, and the
> fixture has to say that is what it is.

A census captured from the tool under test locks its behaviour in, which is worth
having, but it cannot say the behaviour was ever *right*. Regenerate it after a
regression and the test agrees with the regression. `pi*r^2*h` is not an opinion
about this exporter, and a kernel measuring the exported solid has no access to
the arithmetic that predicted it — the two agreeing is the only thing in the
suite that says the solid is the *intended* one rather than merely a
self-consistent one.

Writing the derivation beside the number is part of it. Worked examples:

- **A filleted cube comes apart into the pieces the fillet is made of** — the
  Minkowski sum of `cube(a-2r)` with a ball: `512 + 384 + 24*pi + (4/3)*pi`.
- **`step-rounded-profile` falls out of Pappus** as `2*pi*(1612 + 52*pi)`, once
  the profile is sliced by height rather than by shape.
- **A screw sweep's volume is `turns * 2pi * A * (R + dc)`**, which is what
  adjudicated an 8.2% error a kernel preferred.
- **The Steinmetz solid is `16*r^3/3`**, with no pi in it — which is why
  `step-cylinder-cross` could be committed *before* the code that makes it
  right, with its volume deliberately unasserted.

**A sphere *is* `(4/3) pi r^3`, and this project once wrote down the opposite as
a derivation.** The claim was that OpenSCAD's tessellation has no pole vertex, so
the export legitimately keeps two flat caps. The reasoning was written out in
full, argued for in prose, agreed with by OpenCASCADE to six figures, and was
faithful to the mesh and **wrong about the model**: `sphere()` declares a whole
sphere and carries no latitude bound. Every check was downstream of the same
mistaken premise. Deriving from the *model* means from what the model declared,
not from what its mesh happens to be.

Where a figure is genuinely underivable, **omit `VOLUME:` and say why**. A partly
recognised wall is neither the polygon's volume nor the circle's but a mixture; a
twisted extrude's mesh has no closed form. And **a plausible reason is worse than
none**: a fixture line here claimed sixteen ellipses survived "because the tilt
test excludes the rest", and the tilt test admits twenty-eight of the thirty-two.
State the number and say it is not explained, which is what
`step-cylinder-cross` does for its lobe halving.

### The directives

All are comment lines, matched anywhere in the fixture. The regexes are anchored
with `(?<![-\w])` so `APPROX:` does not match inside `ROUNDTRIP-APPROX:` — they
did once, and every approximation fixture failed at the same moment.

| directive | asserts | derived? |
| --- | --- | --- |
| `EXPECT:` / `EXPECT-NOT:` | a substring of the exporter's own report | the *claim* is (32 facets on a 32-gon band) |
| `APPROX:` / `APPROX-NOT:` | the same, for the approximation run | as above |
| `ROUNDTRIP:` | the kernel's surface census, **exhaustively** | counts, mostly |
| `ROUNDTRIP-APPROX:` | the same, for the approximation export | mostly |
| `VOLUME:` / `VOLUME-APPROX:` | the kernel's measured volume | **yes, always** |
| `TOLERANCE:` / `TOLERANCE-APPROX:` | the worst slack OpenCASCADE had to grant | no — an instrument reading |
| `RADII:` | every face of a kind has this radius | yes — the model says `r=10` |
| `EDGES:` / `EDGES-APPROX:` | the kernel's edge census | mostly |
| `CANONICAL:` | what the kernel thinks each B-spline really is | no — it is an audit |

**`ROUNDTRIP:` is exhaustive.** It accounts for every surface kind the kernel
reads, not only the ones a fixture names. Checking a subset let an unlisted kind
through in silence: a stray `TOROIDAL_SURFACE` from a recogniser that overreached
would be noticed by nothing, because no fixture would think to write `Torus=0`.
The cost is having to state the planes, which is no bad thing — the plane count
is the number that moves when a substitution goes wrong. It is also why the
refusal fixtures carry one: on `step-extrude-refusals` the assertion is not
`Plane=2152`, which is the mesher's business, but that no `Cylinder`, `Cone`,
`Sphere`, `Torus` or `BSplineSurface` appears beside it.

**`EDGES-APPROX:` earns its place because a face census cannot see a boundary.**
A cylinder trimmed by chords and the same cylinder trimmed by the conic it is cut
on read as one `Cylinder` either way, and only the edges say which.

**`VOLUME:`'s tolerance is optional and absolute**, defaulting to 1e-6 relative,
which is what a kernel agrees to when it can read the geometry straight. A
fixture asking for more has to justify it in prose and say which of two things it
is: a stated property of the exporter, or slack. `step-oblique-trim` is the one
legitimate case — it writes its elliptical trim as a plain 3D curve with no
pcurve, so the reader re-derives the parameterisation and lands about 2e-6 away,
and **the same displacement is measurable on OpenCASCADE's own export of the same
solid**, 5026.548244 with its pcurves against 5026.558839 with them stripped.

`VOLUME:` is the only line in this suite that has ever noticed a chorded trim;
every census figure was identical before and after. Any fixture whose model has a
closed-form volume should state it.

### Mutation-check every new assertion

A green run proves nothing about a directive that is not being read. **An
expectation nothing has been shown to refuse is a deleted check.** Perturb it and
confirm the failure: drop a `Plane=2` from a census, move a volume by 0.2%, set a
face count back to what it was before a fix.

Two harnesses make this permanent for the validator's own acceptances —
`tests/bspline-check-mutations.py` and `tests/closed-sphere-check-mutations.py`,
the latter with seven near misses — because teaching the validator a new face
shape is *adding an acceptance*.

The mutation check can itself be green for the wrong reason: the first run of one
reported every mutation as uncaught, and the fault was in the check, because
`0% tests passed` contains the string `tests passed`.

### Behaviour-neutral is proven by the oracle, never argued

**Since 2026-09-23.** A change that claims to change no behaviour - a refactor, a
new report, a record that is carried but not yet read - is proven by
`scripts/step-dev.sh measure <name>` before and after and `compare <a> <b>`
(`scripts/step-diagnostics/measure-compare.py`). It compares, per export, over
the four reference parts and every fixture in both tiers (104 exports): the
report less wall-clock lines, the STEP entity census, and the claim ledger and
surface tables row by row after sorting. Zero differences is the bar; the suite
is far weaker.

It was checked on itself before it was trusted: two runs of one binary compare to
0; a copy with one ledger field and one report word changed shows exactly those
two. Its first version, a shell `diff`, compared nothing on MSYS2 (no `diff`
installed) and said nothing - and its first Python version explained a
difference by set membership, so a line printed twice showed as an empty REPORT.
Both are fixed; both are the lesson of this file in miniature.

### Two shapes of circular validation

**Never relax a validator rule because your own change broke it.** Every
relaxation must name evidence the *file* has to show, never a claim the exporter
makes about itself. The first draft of the three-edge rule triggered on the file
merely *containing* an `ELLIPSE` — the exporter's own claim, so the check and the
thing checked agreed by construction. What replaced it names two arcs found
sharing a vertex, or the apex computed from the surface's own placement and found
among the face's vertices. Mutation-checked: nudging one `CONICAL_SURFACE`'s half
angle by 0.01 rad, touching no vertex, must make the rule fire and nothing else.

One rule *strengthened* at the same time and it is the one that pays: every
bounding `ELLIPSE` is walked at eight parameters and each point asked of its
surface, which catches a centre in the wrong place or a major axis aimed the
wrong way — either of which leaves both radii right.

**Rewriting a fixture assertion your own change broke is the other one**, and it
looks more innocent. Three fixtures asserted "no analytic surfaces were declared"
and planes began to be declared. The narrowing was defensible — the fixtures are
about a wrong *curved* claim — but "defensible" is not the test:

1. reintroduce the defect the fixture exists to catch and confirm the **rewritten**
   line fails (letting `declareExtrudedCylinders` through under a twist);
2. look at what the rewrite stopped constraining and pin it again wherever the
   number is derivable (`step-concave` states 8 because its profile is a hexagon
   and an extrusion has two caps; `step-revolve-axis-point` states 1 because one
   of its three profile edges lies at constant height);
3. say plainly where it is not derivable (`step-extrude-refusals`' third body is
   an ellipse whose arc record was dropped, so its plane count is the mesher's).

### Kernel agreement is not proof

**A fix is not validated by showing that OpenCASCADE now agrees with the file.**
This is the same closing of the circle as putting a kernel's measurement into a
fixture, and it is easier to fall into because it does not look like capturing
anything. It happened here: the evidence offered for a face-splitting fix was
that the reference parts went from *586 faces written, 595 read* to *595 written,
595 read*, and that no `ROUNDTRIP-APPROX` line moved. Both true, neither evidence
— OCCT had been splitting those faces on read, so 595 was OCCT's own repair, and
the `ROUNDTRIP-APPROX` lines had been captured from that repair. A wrong fix that
split the faces along different lines would have produced both observations.

What replaced it is an assertion about the *file*: `check_bound_enclosure()` maps
each multi-bound face's loops into the surface's own parameters and requires
every inner bound to have a vertex inside the outer one. A hole does; a second
region of the same surface does not. It is proven by being run against a file
known to carry the defect, and silent on the same part after.

The order, as a rule:

1. Derive the property from the model, and assert it on the file.
2. Prove the assertion catches a file that is known to be wrong.
3. *Then* confirm with a kernel, preferably one that was the source of no number
   in step 1.

### The ladder a change climbs

Most of what has gone wrong here was a step taken out of order — a fix landed
before it was measured, a measurement trusted because a kernel agreed with it.

1. **PoC.** Make the change badly and cheaply, on a branch, and measure it.
   Landing it is not the goal; finding out whether the mechanism does what it is
   supposed to is. That is the cheapest place for an idea to die.
2. **Fixture.** A model small enough to reason about, every expectation derived
   from the model, every directive mutation-checked.
3. **Provenance.** Run the fixture set and read the provenance report against
   what the models are. It answers *which solid is this* where every other check
   answers *what is this near*.
4. **OpenCASCADE.** The round trip, and `scripts/step-diagnostics/` for what a
   round trip does not say. Necessary, not sufficient: OCCT repairs silently.
5. **Final.** A kernel that was the source of no number in steps 1 to 4 —
   SOLIDWORKS or Fusion, through `scripts/step-interop-*`. This is the only step
   that can tell you the export is right rather than self-consistent.

Skipping 3 or 5 is what makes a wrong fix look finished.

### Five whys, with the branches named before measuring

The method that has repaid the most. Write down each candidate link *before*
measuring it, and check each against something that is not the thing under test.
On `c11`, four of five links were refuted — including one this repository had had
in its own documentation for a week — and the refutations were worth more than
the answer.

Three habits that paid and one that cost:

- **Read `-FaultDetail` before forming a theory.**
- **Report what was recognised beside any fault count.** `faults=0` over a sweep
  that is half faceted is a much weaker result than it looks, and this project
  reported one as though it were not.
- **A control that removes one variable beats any amount of reasoning.** The
  standalone ridge — the same sweep with no wall to cut it — located a sweep
  defect in one run by claiming all four spans where the fused one claimed two.
  The untapered band-family control (`f = 1`) settled the taper question the same
  way.
- **The cost:** the short edges on `c11` were measured early, the answer "none
  below 1e-3" was taken, and the *distribution* was not looked at. It was the
  distribution that mattered.

### What a fixture looks like, and where it lives

The house style is a long comment arguing for the model, then the assertions. The
argument is the point: it says which defect the fixture exists to catch and why
the numbers are what they are, so the next person can check the reasoning rather
than regenerate the numbers.

Fixtures live in `tests/data/scad/step-export/*.scad` (33) and
`tests/data/pythonscad-step-export/*.py` (12), and are picked up by a glob —
**adding one needs a `cmake -B build` before ctest can see it**, which costs a
full rebuild.

Keep both front ends in step. Anything reachable from SCAD and from Python wants
a fixture each, because the two bindings can drift: `declare_cone` takes four
doubles and so could not use the macro that generates its three siblings.

### What "derived, not captured" costs, as a worked example

`step-declare-grid-scad`'s `Plane=8` becoming `Plane=22` is four pieces of bore
facet fanned into eighteen triangles. The fixture states which four (where the
ridge crosses the bore), why each has 6 or 7 corners (a facet spans 11.25
degrees, the ridge's 33 stations are 16.875 apart, and each span carries the one
diagonal its quad is split along, so a trim line takes a vertex entering and
leaving plus one per ridge edge crossing inside), and why the other four planes
do not move (their corners are already on the cylinder at r = 20 exactly).

The number took a minute. The paragraph took the afternoon, and it is the
paragraph that would have caught the flat-bottom defect below.

### Committed artifacts go stale

`examples/step_test/*.stp` are generated files kept in the tree so a reader can
open one without building anything. Nothing regenerates them and no test reads
them, so **every improvement to the exporter silently invalidates them** — twice
now they have been a whole feature behind, and each time the part looked in a CAD
system exactly as though the feature did not work. A quick way to tell whether
one is current is `grep -c ADVANCED_FACE` against a fresh export.

### Scope

The suite covers what the exporter can reach, and `examples/step_test/lid10.scad`
is a **specimen rather than a target**: it is where the adversarial cases were
found, not the thing being optimised. A finding on it is worth having only once
it is expressed as a synthetic fixture that isolates it — which is why
`step-t-junction`, `step-oblique-trim` and `step-declare-cone` exist.

---

## 8. The interop kit

`scripts/step-interop-kit.py` builds 24 coupons as 48 STEP files plus
`results.csv`, which already carries what pythonscad thinks it wrote — face
count, shell count and a census by surface type — leaving the `cad_*` columns for
the target system's answers.

```bash
python3 scripts/step-interop-kit.py --binary <abs path to pythonscad.com> --outdir build/interop-kit
```

The binary path must be absolute and must be the staged one: every export runs
with `cwd=ROOT`, and Windows resolves a relative executable against the parent's
directory instead.

### Why a control beside every coupon

Each model is written twice, once through the analytic path and once with it off.
The faceted export is the control. Four outcomes and only one is a finding:

| analytic | faceted | reading |
| --- | --- | --- |
| imports | imports | the coupon passes |
| **fails** | **imports** | **a finding.** The defect is in the analytic entity this coupon exercises. |
| fails | fails | not an analytic problem — units, tolerance, framing. Fix that first. |
| imports | fails | odd, and worth recording. Usually a faceted export large enough to trip a different limit. |

Without the control, row 3 is indistinguishable from row 2, and row 3 is the
likelier one against an unfamiliar importer.

### The coupons, and what each isolates

| coupon | source | exercises |
| --- | --- | --- |
| c01 cylinder | `step-declare.scad` | `CYLINDRICAL_SURFACE`, `CIRCLE`. Baseline; if this fails nothing below is interpretable |
| c02 partial cylinder | `step-partial-cylinder.scad` | a quadric trimmed short of its seam |
| c03 cone | `step-cone-primitive.scad` | half-angle sign and apex placement |
| c04 sphere | `step-sphere.scad` | one `SPHERICAL_SURFACE` closed on itself: **two** edges, no plane |
| c05 torus | `step-torus.scad` | two closed seams, no rim |
| c06 partial torus | `step-rounded-profile.scad` | rim circles of latitude plus one seam along the tube |
| c07 fillet quadrics | `step-fillet.py` | 12 cylinders, 8 sphere octants; the octants carry **three** edges |
| c08 fillet oblique | `step-fillet-oblique.py` | as c07 with nothing axis aligned — placement precision |
| c09 rational B-spline | `step-fillet-refusals.py` | 24 `RATIONAL_B_SPLINE_SURFACE` complex instances, whose sub-entity records must appear in a prescribed order |
| c10 B-spline text | `step-extrude-text.scad` | 32 non-rational splines, uniform knots — the control for c09 |
| c11 swept grid | `step-declare-grid.py` | a declared sweep as one B-spline face; non-uniform knots, large net |
| c12 approximated | `step-approximate-report.scad` | the approximation pass, fitted sweeps standing alone |
| c13 oblique trim | `step-oblique-trim.scad` | `ELLIPSE` bounding a cylinder, written with **no pcurve** |
| c14 declared cone | `step-declare-cone.scad` | a cone and a cylinder sharing one `CIRCLE`, the cone from a declaration |
| c15 bored cylinder | `step-bored-cylinder.scad` | a quadric with holes; 80 `SURFACE_CURVE` / 160 `PCURVE` |
| c16 bored cone | `step-bored-cone.scad` | as c15 on a taper, where a pcurve's parameterisation is easiest to get wrong |
| c17 cylinder cross | `step-cylinder-cross.scad` | a closed shell with no planar face at all, `ELLIPSE` *arcs*, faces that pinch to a point; the Steinmetz volume is known |
| f01–f05 band family | `step-band-family.scad` at `-D FN=` 24/32/48/64/96 | one declared sweep on a bored wall at a sweep of tessellations |
| r01 lid10 | `examples/step_test/lid10.scad` | a real part |
| r02 bayonet | `examples/step_test/bayonet_container_v1-2.scad` | a real part, 1693 faces, a thread faceted by design |

c09 and c07 are where this exporter writes something an importer is *entitled* to
be strict about; c10 exists so a c09 failure can be attributed to rationality
rather than to splines in general.

The band family is the only coupon that asks a question a single part cannot:
*up to what tessellation does this importer sew*. Read its members together,
never alone.

### The kit checks its own claim

`EXPECT_FACES` holds the analytic face count for the four coupons where that
number follows from the model rather than from a run, and the kit stops if one
moves:

| coupon | faces | why |
| --- | --- | --- |
| c04 sphere | 1 | closed on itself, bounded by its seam alone; no plane anywhere |
| c05 torus | 1 | closed in both directions, bounded by its own two seams |
| c07 fillet quadrics | 26 | 6 planes, 12 edge cylinders, 8 corner octants |
| c17 cylinder cross | 8 | each cylinder keeps two 180-degree lobes, each lobe written as two faces; no cap survives |

A coupon absent from that table simply reports its count as before. A count
captured from a run would lock in whatever the exporter last did.

### The procedure, per file

Do the analytic file and its faceted control back to back, in the same session
with the same settings.

1. **Import with diagnostics on.** SOLIDWORKS: *File > Open*, then *Options* →
   **Import Diagnostics**, *Solid/Surface bodies* (not "graphics body", which
   imports anything and tells you nothing). Record every message verbatim; "zero
   errors" is a result worth writing down.
2. **Body type.** One solid body, or one or more surface bodies? A surface body
   means the importer read the faces and could not sew them.
3. **Face count**, against the kit's own `faces` column.
4. **Check Entity** with invalid faces, invalid edges and short edges ticked.
5. **Mass properties**, compared three ways: against the faceted control of the
   same coupon (which should differ by the chord error, the analytic one larger
   for a convex body); against the **exact** value where the coupon has one; and
   against OpenCASCADE's answer.
6. **Spot-check face identity.** Click a face that should be a cylinder; better,
   run FeatureWorks. If the cylinder comes back as a cylindrical face, the
   analytic export achieved what it exists for — an editable, dimensionable
   feature rather than a mesh. **That is the actual point of the feature**, and
   it is the one thing the OCCT round trip cannot answer.

### Pass criteria

One solid body; zero import-diagnostic errors; face count equal to the kit's
column; no invalid faces or edges; volume within 0.5% of the faceted control's
and on the correct side of it; and the surfaces the coupon exists to exercise
reported as that type.

**And every one of those can be satisfied by a wrong file.** `c06` has passed all
of them every time while being 14% wrong. The row that decides is the derived
one.

### The round trip is evidence; a body type is a verdict

`-RoundTrip` saves each import straight back out as `<coupon>_SW.STEP`;
`scripts/step-interop-sw-roundtrip.py` reads both halves with OpenCASCADE and
prints what changed in the same surface census the rest of the suite uses.

Three things it sees that nothing upstream of it can: **substitution**, where a
B-spline comes back as a fan of planes and the analytic path bought nothing;
**degradation**, where a cylinder comes back as a spline, so the surface survived
as geometry but not as intent and feature recognition has nothing to find; and
**drift**, where the volume moves further than the coupon's own band allows.

It is also the only instrument that separates *"the geometry was destroyed"* from
*"the number is wrong over an intact body"*, which need opposite responses.

Two guards it needed: a volume is only usable if it fits inside its own bounding
box, and a bounding box is only usable if it is finite. A re-export containing
one unbounded surface integrated to 2.16e168 and the first draft duly reported a
drift of 9e167 per cent.

### Instruments that need no licence

- `scripts/step-occt-strict.py [--kitdir <dir>]` — how far a face's own corners
  are from the surface it is written on, at the 95th percentile, over every
  non-planar face. **This is the measure**, not the tolerance OCCT grants.
- `scripts/step-corner-stray.py <file.step> …` — the same, broken down by kind of
  surface. That breakdown is what found the last corner bug: on
  `step-declare-grid-scad` the whole of a 0.096 sat on the planes while the
  cylinders and the sweep were already exact, and one number per file could not
  have said so.
- `scripts/step-analytic-probe.py` — replays the recogniser over an existing
  faceted export: the geometric ceiling, every band the rules fit, and the rule
  that rejected each one they drop.
- `scripts/step-interop-crosscheck.py` — puts three volumes beside each other
  (derived, OpenCASCADE, the CAD system's) and flags a disagreement.
- `scripts/step-interop-faultmap.py` — per-entity fault dump.
- `scripts/step-diagnostics/edge-on-surface.py`,
  `scripts/step-diagnostics/face-boundary-deviation.py`.
- `scripts/step-diagnostics/face-bad-edge.py <file.stp>` — per face, every
  quantity SOLIDWORKS has a *separate* fault code for: an edge off the face's
  own surface, a pcurve off its 3D curve, a vertex off its edge, the shortest
  edge, and the wire's closure gap. Printed with the face's kind, area and
  centre in `-FaultDetail`'s own units, so the two lists join. This is what
  narrowed code 17 to a relation between an edge and a face.
- `scripts/step-diagnostics/vertex-on-curve.py <file.stp> …` — every vertex
  against the curve its own edge is written as, which is SOLIDWORKS fault code 7
  and the relation none of the corner-stray scripts measure. Read straight from
  the text with no kernel in the path, which is the point: read back through
  OpenCASCADE, lid10 showed strays the file does not contain *and* nothing about
  the fourteen it did. `export-step-flagship-coupons` asserts it over all six
  coupons.
- **Locating a fault, once the census has counted it.** Each of these answers
  one question about one place in a file, and together they are how every
  finding of 2026-09-16 to 18 was pinned to an entity:
  - `at-vertex.py <file> x y z [tol]` — what we wrote at a point: the vertex,
    its edges, and the faces using each.
  - `near-edge.py <file> x y z [radius]` — our edges whose midpoint is near a
    point. SOLIDWORKS reports a faulty edge by its centre; this is how that
    centre is matched back to an entity of ours (to 0.0001 on lid10).
  - `face-corners.py <file> <face>` — a face's loops, edge by edge, with each
    edge's curve kind and length.
  - `vertex-owners.py <file> [x y z]` — every vertex against the surface of every
    face using it (code 16), analytic surfaces only.
  - `point-vs-faces.py <file> x y z` (OCP) — one point against every face that
    uses it, *B-spline surfaces included*, which is the census's blind spot.
  - `pinched-loops.py <file>` — the faces behind the census's code-21 count,
    listed; legal seams of closed faces excluded exactly as the census does.
  - `sweep-chords.py <file>` (OCP) — line edges on a sweep face, split by whether
    their *ends* are on both surfaces (a crossing curve can replace them) or not
    (the corner is at fault, not the edge).
  - `reader-built.py <file>` (OCP) — edges the reader holds that we never wrote,
    which is how OpenCASCADE's seam edge and 1.08 vertex were shown to be its own
    construction.
- `scripts/step-diagnostics/fault-census.py <file.stp> …` — every property this
  exporter is known to get wrong, keyed by the fault code it becomes. The first
  thing to run on a new export and the first thing to compare across a change.
- `scripts/step-diagnostics/reader-diff.py <a.stp> <b.stp>` — what changed
  between two files whose entity ids do not correspond: faces matched by surface
  and corners, curves by their control points and placements, with the
  displacement of every replaced curve. For `ours` against `theirs_SW.STEP`, and
  for `theirs_SW.STEP` against `theirs_SW_healed.STEP`, which isolates a repair.
- `scripts/step-diagnostics/seam-straddle.py <file.stp> [face]` — the `v` each of
  a face's own pcurves runs between, and whether its loop crosses the seam of a
  surface closed in `v`. A crossing has nowhere to go but the long way round, and
  is what makes a reader rebuild the face; with no face argument it reports every
  closed-surface face in the file. lid10 has exactly one such face and it crosses
  twice, `step-band-family` has none — and those are also the coupon SOLIDWORKS
  still faults and the one it reads clean.
- `scripts/step-diagnostics/edge-lengths.py <file.stp>` — the edge-length
  distribution straight out of the STEP text, closed edges counted separately.
  No kernel in the path, so no silent repair: what it reports is what was
  written.
- `scripts/step-diagnostics/band-family-controls.scad` — `step-band-family.scad`
  with `WALL`, `TAPER`, `PITCH` and `RUNOUT` added, identical to it at the
  defaults. `PITCH` moves a coupon across the tessellation-alignment line with
  `$fn` held fixed; `RUNOUT` turns the taper from a switch into a series.
- `scripts/step-diagnostics/band-family-volumes.py` — the only derivable volume
  in that family: the untapered standalone ridge by Pappus, and the polyhedron
  the model emits by the divergence theorem over its own face list. Not the same
  solid, and the difference is the model's rather than the exporter's.

**Beware a p95 over few corners.** With nineteen it *is* the maximum, so one
outlier sets it. Look at the per-kind numbers before believing a single figure.

---

## 9. Traps, all paid for once

Each of these cost real time and each will recur.

### Measurement

- **A surface census is not a correctness measure.** It improved monotonically
  while the geometry got worse — lid10's faceted remainder 1216 to 968 — on an
  export whose solid was wrong by half. Only the hand-derived volume dissented.
- **A corner census is no more one than a surface census is.** The Python pair
  reads a plane p95 of 6.3e-16 with its flat bottom in 93 pieces, one tilted 21
  degrees.
- **The tolerance OpenCASCADE grants is not the measure either.** It is
  *largest*, 0.261, on the lid variant SOLIDWORKS reads happily.
- **A fault count read without a recognition count beside it can say the opposite
  of what it appears to.** Half of `step-band-family`'s ridge is inside the wall
  it is fused to, so about half of that sweep is structurally cut facets in every
  variant; `faults=0` over that is weak.

### Geometry and arithmetic

- **Never choose the same thing twice from two sets of coordinates.** Both ends
  of a seam were picked independently as the rim vertex of smallest angle;
  `atan2` has its branch cut at pi, an even-sided polygon has a vertex exactly
  there, and the sign of a coordinate that is zero to fifteen digits decided
  which side it fell on. Derive the second from the first. The same trap, found
  again in `FilletNode` and shipping: a rail computed two ways came out one ulp
  apart, `PolySetBuilder`'s vertex lookup is exact, and a filleted cube exported
  with **48 quadrilateral holes**. The crack was in the `PolySet`, so it was
  never a STEP problem — every export of a filleted body carried it.
- **Grow a region by the surface, not by adjacency.** A band grown by crossing
  edges into any quad walks straight through a rib welded to the wall and out the
  far side. Fit the surface from the seed's neighbourhood first, then admit only
  facets that lie on it.
- **A merge that keeps one of its inputs has to read them all first.** Writing
  the new base before computing the height from the old one silently turned a
  19.9 mm zone into a 0.38 mm one — and only when the run happened to be seeded
  from its top end. Take a copy of both ends, then assign.
- **A walk that always goes the same way only works on a monotone stack.** A
  sphere's bands stack along the axis so stepping by each one's *top* rim is
  sound; a torus's profile turns around at its widest and narrowest points, the
  walk turned round, came back to its seed, and every torus fell out as the stack
  of 32 exact cones it also is. Leave a band by the rim you did not enter it by.
- **Do not let an approximation displace an exact entity.** Anything that fits a
  curve must ask first whether the curve has a name.

### Declarations and frames

- **A hint is recorded in the profile's own frame; the outlines may not be.**
  `Polygon2d::outlines()` returns *transformed* vertices when a 3D transform is
  pending, while its `arcs` and `beziers` are untransformed. Comparing a
  transformed vertex against an untransformed centre finds no arc at all, and a
  rounded square then declared thirty planes for six faces. Use
  `untransformedOutlines()`.
- **A profile that has been through Clipper is snapped to its decimal grid.**
  `CLIPPER2_MAX_DECIMAL_PRECISION=8`, so a vertex that should be exactly on a
  recorded arc is within about 1e-8 of it and no closer. A test asking "are these
  two points on this circle" can afford to be loose — a straight edge's ends are
  nowhere near one — and at 1e-9 it matched nothing.
- **A declared plane must only ever confirm a plane the mesh already carries**,
  never invent one. `refpt` is not reliably a rim, so the test runs from the
  mesh's plane outwards, never from the declaration in.

### Process

- **Measure only after a build that succeeded.** A compound "build; measure;
  compare" that does not stop on a failed build compares the *old* binary with
  itself and reports 0 differences. Capture the build's exit status and gate on
  it.
- **`scripts/step-dev.ps1 build` shows only the last three lines.** A compile
  error is off the end of that window; run `scripts/msys2-build.sh` with the log
  kept and grep it for `error:` when a build fails.
- **Git Bash has no `ctest`, MSYS2 has no `diff`.** Run the suite through
  `step-dev.ps1 test`; compare through `step-dev.sh compare`, which is Python.
- **Paths from PowerShell into `step-dev.sh` must use forward slashes** - a
  backslash path is split by bash and the export writes nowhere. From PowerShell,
  pass the `.ps1` itself with backslashes, and file arguments with slashes.
- **A heredoc through the Bash tool can turn `\n` in C++ source into a real
  newline.** Edit C++ string literals with the Edit tool, or write the edit
  script to a file first.
- **The clang-format hook reformats and fails the first commit.** Stage again
  and commit again; never `--no-verify`.
- **A mutation that is not in the source cannot be detected.** Every new
  assertion this session - the stage order check, the `consumed`-cache check -
  was proven by planting the fault, building, and seeing it fire, then
  restoring.

- **The mesh moves between recognition and writing.** Any decision taken on
  vertex *positions* has to be taken twice, or it describes a mesh that no longer
  exists. Checksum `vertices` at both call sites before reading the code.
- **A boundary cycle's closing edge is easy to lose.**
- **A count is not a derivation.**
- **A validator that finds nothing may have looked at nothing.** Every
  structural check in `validatestep.py` sits under `if entities:`, so a STEP
  file with an empty `DATA` section accumulates no problems and prints
  `validation ok (0 entities, 0 faces, 0 shell(s))`. The same shape of error
  runs through this whole harness: an absent OCCT turns the round trip into a
  pass, a failed projection is recorded as zero deviation, and an import error
  is a skip note rather than a result. Ask what a check *rejects* before quoting
  what it accepts.
- **An invariant checked over a whole file is not checked per body.**
  `check_topology` flattens the faces of every shell into one list and counts
  edge uses in one map, so "twice, in opposite directions" is satisfied by an
  edge used once in each of two shells. Demonstrated by splitting a validated
  cube's single `CLOSED_SHELL` into two three-face halves: neither is
  watertight and the file validates.
- **A tolerance must not depend on where the part sits.** The corner allowance
  scaled by the largest distance from the *world origin*, so translating an
  unchanged solid relaxed its own acceptance. Anything scaled by position rather
  than by size has this bug; use the bounding-box diagonal.
- **Half of a rational curve is its weights.** A boundary curve with the right
  control points and the wrong weights is a different curve - 0.5 where 0.70710678
  belongs turns a circular arc into a parabola - and it passed a count check, a
  positivity check and a control-point-match check. When comparing them, compare
  up to a common positive factor, because scaling every weight together leaves
  the curve unchanged.
- **`m^T m == scale^2 I` does not exclude a reflection.** Every orthogonal matrix
  satisfies it whatever its determinant. A declaration that reconstructs a
  direction with a cross product cannot survive one, and `SweepSurface` was
  accepting mirrors and writing a helix 20 mm from its own mesh.
- **Do not grep a build log you piped through `tail`.** A `FAILED` line off the
  end of the window is a green result from something that was not looking.
- **A column that reads zero is not a column that read something.** The interop
  kit's `band` — the number every argument about tessellation slack rests on —
  read `0.000000` for every file of every kit ever generated, because the kit
  parsed stderr and the exporter's report comes out on stdout. Twenty of the
  twenty-four coupons declare no sweep, so a zero looked like the right answer.
  Before leaning on a column, make it report something non-zero on a file you
  know should move it.
- **A caught exception looks exactly like a measured absence.** Calling
  `BRep_Tool::CurveOnSurface` with the C++ signature raises a `TypeError` in
  OCP; caught, it reads as "this edge has no pcurve", and a new diagnostic
  reported 100% pcurve-less files with a flawless pcurve error of zero. The
  other half of the same trap: OpenCASCADE *builds* a projected pcurve where the
  file stores none, so that column then equals the off-surface distance and
  looks like an independent confirmation of it.
- **Put a known-faulty coupon in every interop run.** A page of clean rows and a
  silent instrument are the same picture. `f02-band-fn032-analytic.stp` must
  read `faults=2 faultyfaces=2 codes=17`; without that row nothing else in the
  run means anything, and this project has twice reasoned from a set of clean
  verdicts before establishing that the session could produce a dirty one.
- **A check whose result is never read passes everything.** `step-flagship-check.py`
  called `not steproundtrip.roundtripSTEP(target)`, which returns `(ok, lines)`
  - a non-empty tuple, never false. From the day it landed no flagship coupon
  could fail the kernel round trip, a 25-shell file included, and every "round
  trip included" said of those coupons rested on the validator alone. Read
  properly, all six fail it, on corner slack. The mutation that exposes this
  kind is not a changed input but a known-bad file handed to the check.
- **A tolerance proportional to the radius hides the most on the largest part.**
  `validatestep.py` passes a trimmed quadric whose corners stray under 5% of its
  radius: 1.0 on the band family's bore, which is why a 3.0 desertion was caught
  there, and 4.05 on lid10's r = 81 chamfers, where 2.25 went through. The same
  defect on both parts, and one verdict each.
- **A run-out is a series, not a switch.** Asking "tapered or untapered" gave a
  clean two-way split that survived two sessions and dissolved the moment the
  run-out was walked continuously — clean at 0, faulty at 0.05 to 0.07, clean at
  0.08 to 0.10, faulty at 0.20 and 0.40. Any parameter compared at two values is
  a parameter that has not been measured.

---

## 10. Refuted claims, and why

Kept because each of these was believed on evidence, and each looks attractive
again on a second reading.

**"Declaring more surfaces will make the corners exact."** Exporting the eight
straying fixtures with the fitting pass disabled leaves every stray identical to
the last digit. Not one inexact corner in the fixture set is on a fitted surface.
The overlap was a coincidence of the same coupons being boolean-cut, which is
both why they have a region to fit and why they have junctions to get wrong.

**"A cubic has least support at the run-out, so the sweep's ends stray."** Split
`step-band-family`'s 472 sweep corners by provenance: the generator's own 91
points never stray, max 1.42e-14; the 381 the boolean made at r = 20 carry all
129 strays. A cubic interpolates its stations exactly at both ends as much as in
the middle. The clustering at `u < 0.2` and `u > 0.8` is the *shape of the cut* —
where the ridge is shallow the wall's cut runs along the crest, where the sweep
and the cylinder are nearly tangent, and a near-tangent cut lands off both.

**"A swept face is one face with creases inside it, so split it per profile
span."** A coupon isolating it goes from one fault and 0.13% volume error to no
fault and an exact volume when split. On the real part it changed nothing,
because the cause was elsewhere. The variant that restricted each face's surface
to its own span measured exactly on the coupon and was 8.2% wrong on a derivable
ridge, while being the variant SOLIDWORKS liked best.

**"The crossing curve is the targeted fix for the sweep's 0.0609 mm of slack."**
Built in full — the solver generalised, grid patches admitted to the `along` map,
the lookup hoisted into one helper. 567 of `c11`'s 771 boundary edges have a
declared sweep as one of their two surfaces and 507 have both ends on both.

| residual accepted | crossing curves written | granted slack | bad faces at 1e-6 |
| --- | --- | --- | --- |
| none — before | 0 | 0.060891 | 7/11 |
| 1e-9 * scale | 93 | 0.060891 | 7/11 |
| 1e-6 * scale | 210 | 0.060891 | 7/11 |

Two hundred and ten exact crossing curves moved the slack by **nothing**, because
a crossing curve fixes only the edges that *are* a crossing, and the rest of a
sweep's boundary is where it meets planar facets or where the boolean cut it —
one chorded edge being enough to fail its face. It also opened the shell, 420
edges used once, because `along` is built from `rawBoundaryCycles` while the grid
emitter walks `patch.runs`. Reverted whole.

One thing from it is worth keeping: `projectOntoBoth` exits on a *step* below
`1e-14 * scale`, which a quadric reaches because its implicit is exact and the
iteration is quadratically convergent, and which a sweep never can because
`GridSurface::project` is itself iterative and the composition inherits its
floor. Asking instead for a point that is *on* both surfaces to a stated residual
is the right question, and fixed all 507.

**"Code 13 is our seam representation."** Held in this repository from
2026-09-02. `c11` has zero edges whose vertices coincide and zero duplicate
vertex locations among 763, so there is no such edge in the file and the
conclusion does not follow for it. Codes 13 and 30 went away when the projection
fixes recognised the missing half of each sweep — not when anything about seams
changed.

**"SOLIDWORKS does not sew declared sweeps."** Right about lid10 and wrong as a
general statement. A third real part with the same feature and its thread
declared imports as a solid, 2838 faces, in every configuration tried. Declared
and undeclared both sew, at half the face count declared.

**"The tessellation band decides whether an importer sews."** The band family
refutes it: every member imports as a solid, including `fn020` at an OCCT slack
of 0.283 — *more* than the 0.264 at which lid10 once failed, on a model of 102
faces. Three candidate explanations were measured and discarded this way: the
boundary size of a single face, the size of the model, and the tolerance the
kernel has to accept.

**"Small or degenerate geometry is what a strict importer objects to."** The one
file that imports cleanly is the worst of three by every measure of small
geometry: forty-six faces under 1e-4 where the failing ones have none, and 396
short edges where lid10 has forty.

**"The band family's clean members are clean because their tessellations are
multiples of 24."** Half right, and the half that is right was dismissed too
early. The split is `3 | $fn`, and it is not numerology: `turns = 40/12 = 10/3`,
so `round($fn * turns)` gives the ridge an angular step of `1200/steps` against
the wall's `360/$fn`, and the two are equal exactly when three divides `$fn`.
Where they are not, the stations drift past the facet boundaries and the boolean
cuts slivers whose lengths the two steps **predict to about 5%** — at `$fn` 64,
station 1 sits 0.008803 deg from a boundary, predicted arc 0.003073, measured
0.003384, and so on for twelve of them. Changing `pitch` alone moves a coupon
across that line with `$fn` fixed, in both directions, and the slivers follow.

So the alignment story predicts the *slivers* exactly. What it does not predict
is the faults — see the next entry.

**"`faults=0` on the band family is bought by leaving facets out."** True as a
correlation within the tapered family and not causal. The untapered control —
`f = 1`, the same model with a constant profile — refuses nothing at `$fn` 64 and
96 and is clean, so there it is had for nothing.

**What the refusal rule fires on is settled and still stands.** At `$fn` 96, by
tenth of the sweep, refused 60 12 0 0 0 0 0 0 0 55 against claimed 149 96 97 97
97 97 98 96 96 149: every refusal is in the two ramps where
`f = max(0, min(1, t/0.2, (1-t)/0.2))` changes shape, and not one is in the
constant middle. A cubic through the stations cannot follow a piecewise-linear
taper through its corners.

**"…and the taper is therefore a fault source in SOLIDWORKS in its own right."**
Refuted 2026-09-09, and it was the last correlate standing. Made a series rather
than a switch — `RUNOUT` in `band-family-controls.scad`, `$fn` 32, everything
else held — the verdict **alternates**: clean at 0, faulty at 0.05, 0.06 and
0.07, clean at 0.08, 0.09 and 0.10, faulty at 0.20 and 0.40. Not a threshold and
not monotone. Both surprising cells were re-imported twice under different file
names and gave the same answer, so it is deterministic rather than noise.

Also note what the post-fix numbers do to the premise: after the projection
fixes every wall variant of the family claims **90.4–90.5%** of its sweep's
facets whole, flat across `$fn` and across the taper, so the 56.3% / 49.9% split
the original reading was built on no longer exists.

**"The bunched vertices on `c11`'s sweep are the exporter's."** The faceted
control carries the same slivers to six decimals — shortest 0.006474 against
0.006470 — and imports with no fault. They are the boolean's. What differs is
what they bound: harmless between two planar facets, and on the boundary of a
curved face already standing 0.0609 mm off where it should be, the place a kernel
runs out of room.

**"The corner placement has not reached the sliver's vertices."** It has. In the
faceted control every sliver has one endpoint 2.1e-04 to 6.9e-04 inside the
circle; in the analytic export both endpoints are on `r = 19.2` to 3.55e-15. The
declaration fixes where each vertex sits and not how many there are: the sliver
survives the move essentially unchanged, 0.021310 becoming 0.021267.

**"`SURFACE_OF_REVOLUTION` collapses zero faces."** Measured on the bayonet,
whose profiles are arcs and lines the band rules already handle, and not true in
general: a revolved arbitrary curve is exactly what the entity is for.

**"A frustum cannot hand over a shape, only rings."** `primitives.cc` declares a
`ConeSurface`.

**Three ways of finding a corner's second plane from raw triangles**, all wrong:
by provenance (a frustum's cap belongs to the same original as its wall, so the
cap is called part of the cone and the corner moves off the base); by distance at
the edge's scale (cut facets near the rim are taken for wall facets, so no second
plane is found and nothing moves); by distance at the sagitta's scale (wrong on a
thin face — the base sliver's own middle is within any threshold a wall facet
must pass). `mergeTriangles` answers it exactly a few lines later: a merged
planar face *is* a plane.

**A threshold on a face's middle, to tell a model plane from a tessellation
chord.** It looks decisive at 0.094 against 18.97 and it is not: for an *ideal*
chord facet the corners lie on the surface and the middle is a whole sagitta off
it, so the comparison inverts. The 0.094 passes only because those corners are
themselves 0.0963 off. The span decides instead — 11.25 degrees, the
tessellation's own, against a disc's 360.

**A pure sagitta bound for the interior allowance.** The bore quad the test exists
to reject spans 134 degrees of the cone, and a sagitta bound at that span is
6.09, just over the 6.0 it is out by. The *span* is the giveaway: no tessellation
facet of that surface spans 134 degrees.

**"A collapsed band explains the fragmenting claim."** Zero of 42 rejections have
a zero band; they run 0.003 to 0.026. **"The provenance gate would spare them."**
It does not — provenance *agrees* those facets are on the cone and the interior
test rejects them anyway.

**"A closed sphere is the periodic shape `check_cylindrical_faces` already
describes."** A sphere is periodic in one direction only, so it has one seam used
twice and no rim — *two* edges, below the floor of four the periodic branch sets
and below the three the fillet octant is let through on. The validator rejected
the shape, and rejected OpenCASCADE's own export of a sphere too.

**"The gate for closing a sphere can be that the cap is small."** It has to be
angular and relative to the run's own bands.

**Three explanations for half a declared sweep never being claimed**, each
refuted by a control before the real cause was found: the triangulation diagonal
(flipping it changes nothing); neighbouring turns (widening the pitch to a 24 mm
gap changes nothing); the flank's own shape (rotating the profile leaves the same
lower flank claimed whole as span 0 and not at all as span 3).

**Two contiguity repairs for a fragmenting claim**, both wrong the same way:
refusing a facet whose *any* corner an exact surface holds shatters the sweep
into 36 faces, and keeping the longest run of stations whose facets the fit
passes through collapsed the claim to a *single facet*. The per-facet stray is
scattered through the whole region while only the *boundary corner* error is
concentrated at the ends. Different measurements, and only the second has the
signature.

**"Knitting settings explain the surface bodies."** Both reference parts imported
as surface bodies with *Try forming solid(s)* and with *Do not knit* alike, 82
faulty faces either way, and *Create analytic faces* changed nothing.

**"The mesh handed to the exporter is already wrong."** With
`step-analytic-surfaces` alone, every edge of every file measured lies exactly on
the face it bounds — lid10, all 2410 edges, nothing off by more than 1e-4. The
defect belonged to the approximate pass.

**"Code 17 is a sliver bounding a face whose own boundary is loose."** Refuted
2026-09-09, and it is worth reading because it survived a great deal. The chain
was measured end to end: incommensurate tessellations cut slivers, the exporter
keeps them and places their ends exactly on the declared cylinder, and they bound
an analytic face whose chorded boundary stands up to 0.1 mm off it. A ratio —
shortest edge over worst edge-off-its-own-face — separated all seven files whose
verdicts were known, with two orders of magnitude and no overlap, faulty below 1
and clean above.

It then failed on three more files, and the sharpest counterexample leaves
nothing standing: `f02` and the same model with the taper switched off carry the
**same edge** — 0.000916 mm, both ends at `r = 20.000000000`, `dz` 0.000070,
`z` 10.226, on the same B-spline/cylinder pair — and one is faulty. `p125-fn024`
has the *worst* ratio in the whole set, 0.0049, and imports clean.

What survives is the population, and it is the only thing this chase settled:
`-FaultDetail` names the faulty faces on every faulty coupon as **the swept
B-spline and the bore cylinder**, two code-17 records apiece, `faultyedges=0`.
The cylinder is one SOLIDWORKS made itself — we write nine faces on the bore and
it reports a single faulty cylinder of 2098.71 mm². Every measurement above is
taken on the file we write, and that face does not exist until the import has
knitted it, which is where the thread now goes.

**"The sliver is the analytic export's."** The faceted control carries it to
five decimals — 0.000916 against 0.000938 at `$fn` 32, and twelve identical
edges at `$fn` 64 — and imports clean at every tessellation. It is the boolean's.

**"The faulty member of the run-out pair has more of its run-in left faceted."**
An eyeball hypothesis, found by looking before it was measured, and the closest
anything has come. Answering it with the exporter's *count* is answering the
wrong question — 12 facets in all four, but the same 12 span a run-in whose
length changes with the parameter, so **measure the area**. By area it is right
pairwise: 35.32 mm² of faceted run-in on the faulty `t007` against 33.07 on the
clean `t008`.

It still fails, twice. The faulty band 32.63–37.94 overlaps the clean band
0–33.07 by 0.44 mm², so `t005` is faulty below a `t008` that is clean; and both
ends invert it, a clean file at 20.60 and two faulty ones with **no** faceted
run-in at all. The same shape of answer comes from the analytic claim's area,
monotone inside the band with a clean gap at the flip and inverted outside it.
A monotone quantity cannot produce a verdict that alternates — which is the
general form of why nine of these entries failed.

Worth keeping as the discipline it teaches: a count and an area are different
measurements, and quoting one to dismiss a hypothesis about the other is how a
live idea gets buried.

**"The jagged sweep boundary is what SOLIDWORKS objects to."** The boundary
*is* jagged — visibly so, and the observation is worth having for its own sake
(see the crossing curve in `doc/step-export-wip.md`). It is not the trigger: the
turning angle between consecutive boundary chords is median 11.586 / max 94.395
on the faulty `t007` and median 11.588 / max 94.393 on the clean `t008`, with 14
edges over 30 degrees on each.

**"An import setting is the lever on code 17."** Refuted 2026-09-10, and by a
stronger result than "nothing flipped". *Try forming solid(s)* against *Do not
knit*, and *Create analytic faces* on against off, were changed together, and
SOLIDWORKS' reading of `f02` is **bit-identical** across the change: solid, 9
faces, volume 19555.2696, `faults=2 faultyfaces=2 faultyedges=0 codes=17`, and
`-FaultDetail` names the same two faces with the same areas to six decimals —
bspline 1027.844725 at `(0.0298, 0.0882, 17.3483)` and cylinder 2098.714282 at
`(0.1454, 0.0000, 20.0000)`. `t008` and the faceted control stayed clean.

Two things fall out of it. The knit setting has almost nothing to act on: our
files carry a `MANIFOLD_SOLID_BREP` over one `CLOSED_SHELL` with no `OPEN_SHELL`
and no `SHELL_BASED_SURFACE_MODEL`, so SOLIDWORKS reads the solid directly
rather than sewing surfaces — which also explains why solids arrived under *Do
not knit* all along. And every run in this document is comparable across the
change, because the coupon that anchors them is faulty either way.

**"The jagged sweep boundary is the analytic path's."** Seen rather than
computed: the faceted control shows the same staircase along the ridge's trim,
with the mesh's triangles visible. The jaggedness is the boolean's intersection
polyline, and the analytic export inherits it — the same conclusion the sliver
measurements reached from the other side, now visible in a CAD system.

**"The crossing curve on a sweep will move code 17."** Not refuted and not
shown — **untested**, and the distinction matters. Built 2026-09-10, it writes
498 of `f02`'s 640 candidate edges as the true crossing fitted to 9.96e-08 and
takes the faces whose boundary leaves their own surface from 9 of 12 to 7 of 12.
SOLIDWORKS reads the result as `faults=2 faultyfaces=2 codes=17`, exactly as it
read the all-chord version, and the run-out flip survives intact.

The reason that settles nothing is in this document already: *one chorded edge
is enough to fail its face*. 142 edges still fail to fit inside 1e-7 and stay
chords, so the face SOLIDWORKS objects to has never yet been offered a boundary
that lies on it. Reporting this as "the crossing curve does not fix code 17"
would be the same error as reporting a green suite whose round trip never ran.

**"Root cause C is corners whose two owners are coaxial cylinders that never
meet."** Written into the handover on 2026-09-10 when the relaxed veto put
`band-fn024`'s bore 3 off itself, and it named the right surfaces and the wrong
mechanism. At `$fn` 24 provenance names two owners for *none* of the 368
junction vertices; the coaxial pair is the `$fn` 32-and-up population, where the
two-owner placement correctly finds no crossing and moves nothing. What moved
the 48 was the conic placement, handed a single owner because the bore's
original has 2 whole facets of the 3 ownership needs. Refuted with it: that the
membership test rejects a subtracted cylinder (it claims the bore at 19, 7 and
85 whole facets at `$fn` 32, 48, 64) and that the record is dropped (it is
surface 3 in every export).

**"lid10's (-66.9057, -48.6098, 95.0) is a real junction of four surfaces."**
Recorded on 2026-09-10 as the point root cause B's three coincident
`VERTEX_POINT`s legitimately share. It is where C put them: corners generated
on two chamfer cones, at r = 82.0547 and 80.4547, each handed the r = 82.70
wall as sole owner and slid along z = 95 onto it, 0.65 and 2.25. B's fix -
key the written vertex by where the corner ended - is right on its own terms;
the example that motivated it was a desertion.
