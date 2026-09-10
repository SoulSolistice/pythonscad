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

### The ladder, as landed

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

**A corner may travel no further than its nearest neighbouring vertex.** The line
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

### Entities written

`PLANE`, `CYLINDRICAL_SURFACE`, `CONICAL_SURFACE`, `SPHERICAL_SURFACE`,
`TOROIDAL_SURFACE`, `B_SPLINE_SURFACE_WITH_KNOTS`, `RATIONAL_B_SPLINE_SURFACE`
(as an ISO 10303-21 complex instance), `CIRCLE`, `ELLIPSE`, `LINE`,
`B_SPLINE_CURVE_WITH_KNOTS`, `SURFACE_CURVE` with `PCURVE`.

### Constructs, measured one export of a ten-line model each

| construct | written |
| --- | --- |
| `linear_extrude` of a circle | `CYLINDRICAL_SURFACE` + 2 planes |
| `linear_extrude(scale=)`, uniform taper | `CONICAL_SURFACE` + 2 planes |
| `linear_extrude` of an `offset(r=)` profile | one cylinder per rounded corner |
| `rotate_extrude` of a circle | **one** `TOROIDAL_SURFACE` for the whole solid |
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
- **Fault codes.** `swFaultEntityErrorCode_e`, reflected out of the installed
  `api/redist/SolidWorks.Interop.swconst.dll` rather than remembered — the
  neighbours are as informative as the codes themselves:

  ```text
   7 swEdgeVertexNotLie      11 swEdgeSpcurveOutOfTol   13 swEdgeVerticesTouch
  15 swEdgeBadWire           16 swFaceBadVertex         17 swFaceBadEdge
  18 swFaceBadEdgeOrder      20 swFaceBadLoops          21 swFaceSelfIntersecting
  24 swFaceFaceInconsistency 30 swTopolNotG1Continuous  35 swTopolMissingGeometry
  36 swEdgeTouchEdge
  ```

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

- **The mesh moves between recognition and writing.** Any decision taken on
  vertex *positions* has to be taken twice, or it describes a mesh that no longer
  exists. Checksum `vertices` at both call sites before reading the code.
- **A boundary cycle's closing edge is easy to lose.**
- **A count is not a derivation.**
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
