# STEP export: validating against a second kernel

`doc/step-export-status.md` closes with the one thing every measurement in it
shares: **OpenCASCADE is the only kernel that has ever read these files.** Every
volume, every canonical-surface census, every "reads back as one solid" was OCCT
answering. That is a real result and it is not the result that matters most,
because the failure that started this work was seen in **SOLIDWORKS**.

This document is the plan for getting a second opinion, and the reasoning behind
its shape. `scripts/step-interop-kit.py` builds the files it calls for.

## Why a control beside every coupon

A coupon that fails to import proves nothing on its own. SOLIDWORKS might
object to the units, the tolerance, the model, or this exporter's *faceted*
output — none of which would be news, and all of which would look identical to
"the analytic path is broken".

So the kit writes each model twice: once through the analytic path, once with
that path off. The faceted export is the control. Four outcomes, and only one of
them is a finding:

| analytic | faceted | reading |
| --- | --- | --- |
| imports | imports | the coupon passes |
| **fails** | **imports** | **a finding.** The defect is in the analytic entity this coupon exercises. |
| fails | fails | not an analytic problem. Something more basic — units, tolerance, the exporter's STEP framing — is wrong for this target. Fix that first; the coupon says nothing until you do. |
| imports | fails | odd, and worth recording. Usually a faceted export large enough to trip a different limit. |

Without the control, row 3 is indistinguishable from row 2, and row 3 is the
likelier one on a first run against an unfamiliar importer.

## What the coupons are for

The kit is ordered so that a failure isolates. Each coupon adds exactly one
thing to the one before it.

| Coupon | Exercises | Why it could fail where OCCT did not |
| --- | --- | --- |
| c01 cylinder | `CYLINDRICAL_SURFACE`, `CIRCLE` | Baseline. If this fails, nothing below is interpretable — stop and fix it. |
| c02 partial cylinder | 4 trimmed cylinders | A quadric trimmed short of its seam. |
| c03 cone | `CONICAL_SURFACE` | Half-angle sign and apex placement; a cone is degenerate at its apex. |
| c04 sphere | whole `SPHERICAL_SURFACE` | Both poles are parametric singularities. |
| c05 torus | whole `TOROIDAL_SURFACE` | Two closed seams, no rim. |
| c06 partial torus | 4 partial tori | Rim circles of latitude plus one seam along the tube. |
| **c07 fillet quadrics** | 12 cylinders, 8 sphere octants, 24 circles | **The octants carry three edges, not four** — the fourth side is the pole, where the patch is degenerate. Foreign importers routinely reject a 3-edge face, or silently "repair" it into something else. |
| c08 fillet oblique | as c07, nothing axis aligned | `AXIS2_PLACEMENT_3D` precision when no direction is a unit axis. |
| **c09 rational B-spline** | 24 `RATIONAL_B_SPLINE_SURFACE` | **Highest risk.** A rational surface is an ISO 10303-21 *complex instance*, and its sub-entity records must appear in a prescribed order. Finding F7 was exactly this class of defect, and OCCT is more forgiving of it than most commercial importers. |
| c10 B-spline text | 32 `B_SPLINE_SURFACE_WITH_KNOTS` | Non-rational, uniform knots — the easy spline case, and the control for c09. |
| c11 swept grid | one large B-spline face | General (non-uniform) knot vectors and a large control net. |
| c12 approximated | 4 swept-grid faces | The approximation pass; needs `step-approximate-surfaces` too. |
| r01 lid10 | real part | The model whose committed export was finding F1. |
| r02 bayonet | real part, 1693 faces | Scale, and a thread that stays faceted by design. |

The two starred rows are where to look first. c09 and c07 are the two places
where this exporter writes something an importer is *entitled* to be strict
about, and c10 exists precisely so that a c09 failure can be attributed to
rationality rather than to splines in general.

## Generating the kit

The binary must come from a staging directory that can run — a freshly linked
`build/pythonscad.exe` cannot resolve its own DLLs. See CLAUDE.md, *Running the
built binary*.

```bash
python3 scripts/step-interop-kit.py --binary build/staging/pythonscad.exe --outdir build/interop-kit
```

This writes 28 STEP files plus `results.csv`. The CSV already carries what
pythonscad thinks it wrote — face count, shell count, and a census by surface
type — and leaves the `cad_*` columns blank for the target system's answers.
Filling those in is the experiment.

## The procedure, per file

Do the analytic file and its faceted control back to back, so the comparison is
against the same session and the same settings.

**1. Import with diagnostics on.** In SOLIDWORKS, *File > Open*, select the
`.stp`, then *Options*: enable **Import Diagnostics** and choose *Solid/Surface
bodies* (not "graphics body" — a graphics body imports anything and tells you
nothing). Record every message the dialog produces, verbatim, in
`cad_import_errors`. "Zero errors" is itself a result worth writing down.

**2. Body type.** In the FeatureManager tree, does the part contain one **solid
body**, or one or more **surface bodies**? Record in `cad_body_type`. This is
the single most decision-relevant number in the whole exercise: a surface body
means the importer read the faces but could not sew them into a solid, which is
the interop failure that matters to a user. A solid body means it worked.

**3. Face count.** *Tools > Evaluate > Check*, or select all faces. Compare with
the `faces` column the kit already filled in. A count that matches proves no face
was dropped; a lower count can mean faces were merged (benign) or lost (not).

**4. Check Entity.** *Tools > Evaluate > Check*, with **Invalid faces**,
**Invalid edges**, and **Short edges** all ticked. Record the counts in
`cad_check_entity`. This is where a silently-repaired degenerate patch shows up.

**5. Mass properties.** *Tools > Evaluate > Mass Properties*. Record volume and
surface area. Compare three ways:

- against the **faceted control** of the same coupon — these should differ by
  only the chord error, and the analytic one should be the *larger* for a convex
  body, since facets cut corners;
- against the **exact** value where the coupon has one. `step-fillet` is a
  filleted box, which is the Minkowski sum of the box shrunk by 2r with a sphere
  of radius r, so its exact volume is computable and `doc/step-export-status.md`
  quotes it;
- against **OCCT's** answer, which `tests/steproundtrip.py` already produces.
  A disagreement between two kernels on the same file is a much sharper finding
  than either number alone.

**6. Spot-check face identity.** Click a face that should be a cylinder. The
status bar names the face type. Better, if FeatureWorks is available: *Insert >
FeatureWorks > Recognize Features*. If SOLIDWORKS recognises the cylinder as a
cylindrical face, the analytic export achieved what it exists for — a user gets
an editable, dimensionable feature rather than a mesh. **This is the actual
point of the feature**, and it is the one thing the OCCT round trip cannot
answer on the user's behalf.

## Pass criteria

A coupon passes when all of:

- one solid body, not surface bodies;
- zero import-diagnostic errors;
- face count equal to the kit's `faces` column;
- Check Entity reports no invalid faces and no invalid edges;
- volume within 0.5% of the faceted control's, and on the correct side of it;
- the surfaces the coupon exists to exercise are reported as that type.

Anything else is a finding, and the faceted control decides whether it is *this
exporter's* finding.

## What a result would license

The open decision in `doc/step-export-status.md` is whether the analytic path
stops being experimental — it is still behind `step-analytic-surfaces`, off by
default, and that document is explicit that nothing measured so far settles it
and that the round trip is what it is waiting for.

- **All coupons pass in SOLIDWORKS and Fusion:** the strongest available case
  for turning the feature on by default.
- **c09 fails, others pass:** do not turn it on. Fix the complex-instance
  writing first — it is a format defect, and a narrow one.
- **c07 fails, others pass:** the 3-edge octant needs a four-edge form with a
  degenerate fourth edge, which is a real change to the emitter and worth its own
  measurement.
- **Real parts fail while coupons pass:** scale or accumulation, not entities.
  Bisect by exporting the part with subsets of the recogniser enabled.

## Second target: Fusion

Fusion 360 has its own reader and its own opinions, and it is a second data
point for roughly no extra work: the same files, *Insert > Insert Derive* or
simply opening the `.stp`. The equivalents of the steps above are the browser's
Bodies node (solid vs surface), *Inspect > Section Analysis* for sanity, and
*Utilities > Compute All* to force a rebuild that surfaces bad geometry.

Record Fusion's answers in a second copy of the CSV rather than the same one;
the two kernels disagreeing is a result, and it is lost if the columns are
shared.

## What this cannot settle

The plan measures whether these files are *read correctly*. It does not measure
whether they are *good CAD* — whether the faces are laid out the way a
mechanical engineer would want them, whether the parameterisation survives a
fillet or a draft applied downstream, or whether a thread that stays faceted is
acceptable in a part someone intends to machine. Those are judgements, not
measurements, and they want a user rather than a script.

## The declared sweep is not sewn by SOLIDWORKS

Run 2026-09-01, SOLIDWORKS 2026 SP0.0, against the kit built from the current
exporter. Sixteen of eighteen coupons pass; the two findings are both real
parts, and they are the failure this whole document exists for - the analytic
export comes in as a **surface body** while its own faceted control comes in as
a solid.

```text
r01-lid10-analytic    SURFACE  1306 faces  vol 0.0000
r01-lid10-faceted     solid    2441 faces  vol 223482.2984
r02-bayonet-analytic  SURFACE   576 faces  vol 0.0000
r02-bayonet-faceted   solid    1685 faces  vol 234864.2425
```

OpenCASCADE reads both as one valid closed solid, so this is SOLIDWORKS
specific and no amount of `validatestep.py` or round-trip checking would have
found it.

### Which feature, isolated

Four exports of the same part, each differing by one thing:

| export | body |
| --- | --- |
| analytic only | **solid**, 1969 faces, vol 227209.5503 |
| approximation, trimmed quadrics only (no declared sweep) | **solid**, 1828 faces, vol 226215.5900 |
| approximation, declared sweep only (quadrics suppressed) | **SURFACE**, vol 0 |
| approximation, both | **SURFACE**, vol 0 |

So it is the **declared sweep**, and the trimmed quadrics are cleared. That
matters for attribution: the quadric and cone work is the newest thing here and
the obvious suspect, and it is not the cause. `declare_grid` on this part dates
from item 5.

It is also not swept grids as such. `c12-approximated` writes four of them and
imports as a solid. The difference is scale: the lid's sweep is a single face
whose boundary is 312 runs of up to 4 mesh edges, better than a thousand edges
on one face, and that is what does not sew.

### Why it was not seen until now

The kit exported both real parts with `step-analytic-surfaces` alone. In that
mode neither part's thread is declared at all - lid10 comes out at 1985 faces -
so the coupon that was meant to be the most realistic test was exercising a path
no user of the feature would take. Both are now in `APPROX`, which is what
surfaced this.

Four coupons were added for the newer entities, and all four pass:

| coupon | body | faces | volume | against |
| --- | --- | --- | --- | --- |
| c13-oblique-trim (`ELLIPSE`) | solid | 3 | 4824.8003 | 4824.92783 derived |
| c14-declared-cone | solid | 4 | 3962.5955 | 3962.5955337 exact |
| c15-bored-cylinder | solid | 4 | 5298.5717 | 5298.405619 derived |
| c16-bored-cone | solid | 25 | 5370.1929 | 5382.203842 derived |

`ELLIPSE` was the one to watch, being the only entity kind this exporter had
never shown a commercial reader, and it reads cleanly with no pcurve. Worth
noting too that SOLIDWORKS reports c15 as **four** faces where OpenCASCADE
reports ten: it sewed the seam-split halves back into whole periodic surfaces,
which is the best available outcome for a face that had to be cut so as not to
wrap.

### Correction: it is not "declared sweeps do not sew"

The section above named the declared sweep as the culprit, which was right about
lid10 and wrong as a general statement. A second real part settles it: the
user's own `base_coupler`, the same design family and the same feature, with its
thread declared, imports into SOLIDWORKS as **a solid** - 2838 faces, volume
229566.5077. So a declared sweep can sew, at the scale of a real part.

What was tried on lid10 and did not help, each a separate export differing in
one thing:

| tried | result |
| --- | --- |
| cutting the sweep along its length, 8 / 16 / 32 / 64 stations per face | SURFACE at every limit |
| coarser tessellation - 761 faces instead of 1326 | SURFACE |
| finer tessellation - band 0.1032 and 0.0459 | would not load, then crashed the session |

So neither the boundary size of a single face nor the size of the model is the
mechanism, and the two are worth ruling out because both were plausible.

The remaining hypothesis is the **tessellation band**: a fitted face is bounded
by the mesh's own polyline, and those chords sag off the surface they bound by
up to the band - 0.2527 on lid10, against a sewing tolerance orders of magnitude
tighter. It fits the evidence that exists (`c12-approximated` sews at 0.0107,
`base_coupler` at 0.0459, lid10 fails at 0.2527) and it fits the user's
observation that a good file opens instantly while a bad one takes minutes:
SOLIDWORKS is not rejecting these files, it is *healing* them, and failing.

It is not proven. The test that would prove it - lid10 at a band of 0.0459 -
crashed SOLIDWORKS twice, and the files it choked on are valid closed solids in
OpenCASCADE. Something about lid10 in particular is pathological, and that is a
separate question from the band.

If the band is the mechanism, the consequence is architectural rather than a
bug: **a fitted surface adjacent to faceted neighbours can only be sewn by a
kernel whose tolerance exceeds the model's own tessellation band**, because the
shared edge has to be straight for the planar neighbour and cannot then lie on
the curved face. That would make the approximation pass safe only below a
resolution-dependent threshold, which is a thing to state and measure rather
than to fix.

### Every configuration of the second part sews

The user's own model was then tried in three configurations, and all three
import as solids:

| | body | faces | volume |
| --- | --- | --- | --- |
| lid, thread declared | solid | 2838 | 229566.5077 |
| base, thread declared | solid | 1980 | 237372.0586 |
| base, undeclared | solid | 4101 | 235108.6801 |

Worth stating plainly because the natural reading of "it works when I drop the
declaration" is that the declaration is at fault, and on this part it is not:
declared and undeclared both sew, at half the face count declared. What changed
between the two observations was the part and its settings, not the feature.

So the failure is confined to `examples/step_test/lid10.scad`, which is this
project's own specimen rather than anything a user has. That is a much narrower
statement than the one this document started with, and it is the reason the
band hypothesis above is still only a hypothesis: the one part that exhibits the
failure is also the one that crashes the tool that would measure it.

### What the kit should grow

Every small coupon passes and both real parts failed, which is the shape of a
gap in the kit rather than a coincidence. Small coupons establish that an entity
is *encodable*; they say nothing about whether a kernel will sew a shell of a
thousand of them.

The useful form is not one large coupon but a **parameterised family**: the same
feature exported at a sweep of tessellation bands, so the kit reports the
threshold at which a kernel stops sewing rather than a pass or a fail. That
turns "SOLIDWORKS dislikes our sweeps" - which this document now knows to be
false - into a number the exporter can be designed against.

A second gap the same run exposed: `c11-swept-grid` was not in `APPROX` either,
so the coupon named for the swept grid had never exported one. It is now.

## The band family, and what it refuted

`tests/data/scad/step-export/step-band-family.scad` is one declared sweep on a
bored wall, with `FN` as its only knob, exported by the kit as coupons `f01`
through `f05` via `-D`. Every other coupon asks "does this import"; this one
asks "up to what tessellation does this importer sew", which is the question no
single-part test could answer.

It also gave the investigation its first free instrument. **OpenCASCADE had been
telling us something all along and was not being asked.** `BRepCheck_Analyzer`
says "valid" and stops, because OCCT sews by *widening the tolerance* of an edge
until it covers the gap between that edge and the faces it bounds. A shape whose
edges do not lie on their surfaces still comes back as one closed solid; it has
simply been granted the slack. `worst_tolerance()` reports that slack, and it
tracks the tessellation band as the theory predicted:

| FN | band | slack OCCT accepted |
| --- | --- | --- |
| 20 | 0.4214 | 0.283185 |
| 24 | 0.3207 | 0.196314 |
| 32 | 0.2077 | 0.117000 |
| 48 | 0.1181 | 0.056772 |
| 64 | 0.0807 | 0.034967 |
| 96 | 0.0485 | 0.018006 |

And on the first look the slack correlated perfectly with the SOLIDWORKS result:
everything it read as a solid needed 0.048 or less, and lid10, which it read as
loose surfaces, needed 0.264.

**The family refutes it.** Every member imports as a solid, including `fn020` at
a slack of 0.283 - *more* than the 0.264 that lid10 fails at, on a model of 102
faces. A tolerance threshold between the two would have to be narrower than the
gap between those two numbers, which is not a threshold.

So slack is not the mechanism either, and that is now three candidate
explanations measured and discarded: the boundary size of a single face, the
size of the model, and the tolerance the kernel has to accept. The correlation
was real and the causation was not, which is the sort of mistake a family of
coupons exists to catch and a single coupon cannot.

### What is left

Both failing parts - `lid10` and `bayonet_container_v1-2` - are real models
carrying a declared sweep, and both live in this repository. A third real model
carrying the same feature, the user's own, imports as a solid in every
configuration tried. So the question is no longer "what does this feature do to
an importer" but "what do these two files have that the other three do not", and
the answer is not tessellation, not scale, not slack, and not the sweep itself.

Worth noting what the tolerance measurement is still good for, having failed as
a predictor: it is the only cheap, deterministic number here that describes how
far an approximated face's boundary sits from the surface it bounds, it needs no
CAD licence, and it does not crash. `TOLERANCE:` and `TOLERANCE-APPROX:` assert
it. It just does not, on this evidence, decide whether SOLIDWORKS will sew.

## Diffing the parts that fail against the ones that do not

With three mechanisms ruled out, the remaining question was what the two failing
files have that the passing ones do not. The obvious candidates are the things
an importer is fussy about and OpenCASCADE quietly tolerates - geometry below
its own resolution, faces with holes, shells that do not close. None of them
distinguishes:

| | faces | min area | area<1e-4 | min edge | edge<1e-2 | multi-bound | edges not used twice |
| --- | --- | --- | --- | --- | --- | --- | --- |
| lid10 **(fails)** | 1327 | 0.00021 | 0 | 0.0011 | 40 | 7 | 0 |
| bayonet **(fails)** | 595 | 0.0098 | 0 | 0.0315 | 0 | 8 | 0 |
| coupler (passes) | 2840 | 3.9e-07 | **46** | 0.00026 | **396** | 9 | 0 |
| c12 (passes) | 6 | 40 | 0 | 0.435 | 0 | 0 | 0 |

The file that imports cleanly is the *worst* of them by every measure of small
geometry: forty-six faces under 1e-4 where the failing ones have none, and
three hundred and ninety-six short edges where lid10 has forty. Every shell is
closed. Nor is it the seam: all three sweeps come out as strips over one
B_SPLINE_SURFACE_WITH_KNOTS, so none of them is the two-faces-across-a-cut case.

So the diff finds nothing, which is worth recording as plainly as a finding
would be. Four mechanisms have now been measured and discarded, and the failure
is confined to two files whose only shared property is that they live in this
repository.

## A third kernel

`scripts/step-interop-fusion.py` does for Autodesk Fusion what
`step-interop-solidworks.ps1` does for SOLIDWORKS, with the same columns so the
two read side by side. Fusion has no out-of-process automation, so it runs
inside it:

```text
Utilities -> ADD-INS -> Scripts and Add-Ins -> Scripts -> + -> pick the file -> Run
```

It asks for a kit folder, imports every `.stp`, and writes `fusion-results.csv`
beside them.

The point of a third opinion is that two kernels currently disagree and no
mechanism explains it. OpenCASCADE reads both failing files as one valid closed
solid; SOLIDWORKS reads their faces and declines to sew them. A third kernel
says which is the outlier, and that changes what to do next: if Fusion agrees
with OpenCASCADE the exporter is probably fine and SOLIDWORKS is being strict
about something specific; if it agrees with SOLIDWORKS then the files really do
carry a defect that OpenCASCADE is repairing on the way in, and it is worth
finding.

A focused kit for that question is what `build/interop-fusion` holds: the two
parts that fail, the one that passes, and a faceted control for each.

## What Fusion said

Fusion imports all six files as one valid solid body each - the two parts
SOLIDWORKS refuses included.

| file | Fusion | SOLIDWORKS | faces (OCCT / Fusion / SW) | volume cm3 |
| --- | --- | --- | --- | --- |
| r01-lid10-analytic | solid | **SURFACE** | 1327 / 1327 / 1306 | 226.7486 |
| r01-lid10-faceted | solid | solid | 2457 / 2457 / 2441 | 223.4823 |
| r02-bayonet-analytic | solid | **SURFACE** | 586 / 582 / 576 | 236.9718 |
| r02-bayonet-faceted | solid | solid | 1685 / 1685 / 1685 | 234.8642 |
| r03-coupler-analytic | solid | not run | 2840 / 2844 / - | 227.3047 |
| r03-coupler-faceted | solid | not run | 7108 / 7108 / - | 227.1335 |

Two independent kernels read the files as closed solids and one does not, so
SOLIDWORKS is the outlier rather than the exporter. That answers the question
the third opinion was asked to settle, and it does not mean nothing is wrong:
an importer being stricter than two others is still an interoperability defect
if the strictness is about something real.

On the faceted files Fusion's volumes agree with SOLIDWORKS to every digit
reported - 223.4823 against 223482.2984 mm3, 234.8642 against 234864.2425 - so
the two are measuring the same solid and the disagreement is confined to the
analytic path. The face counts differ slightly and harmlessly: SOLIDWORKS is
low by 21 and 16 on lid10's two variants and by 9 and 0 on the bayonet's, which
is coplanar-face merging on import, not dropped faces. It happens on the
faceted file it accepts as readily as on the analytic one it refuses.

### The first metric that separates them

Every measure in the diff above pointed the wrong way. One does not: how far
the recovered analytic surfaces sit from the mesh they were recovered from,
read as the volume the analytic export gains over the faceted one. A cylinder
fitted through facet vertices bulges outside the chords, so the difference is
always positive and scales with the sagitta.

| | analytic | faceted | delta |
| --- | --- | --- | --- |
| lid10 **(fails)** | 226.7486 | 223.4823 | **+1.462%** |
| bayonet **(fails)** | 236.9718 | 234.8642 | **+0.897%** |
| coupler (passes) | 227.3047 | 227.1335 | +0.075% |

The part that imports cleanly deviates twelve to twenty times less than the two
that do not. That is the same quantity as the fit-band fraction measured
earlier - lid10 at 68% of its band, the coupler at 39% on `_resolution: 240` -
now correlating with the import outcome as well.

Three points is a correlation, not a mechanism, and the earlier test of the
kernel's absolute slack found the opposite (fn020 sews at 0.283 where lid10
fails at 0.264). What the two have in common is that a relative measure
separates the parts and an absolute one does not, which is worth taking
seriously rather than reconciling by hand.

The experiment that decides it already exists and has not been run: the
parameterised band family sweeps `$fn` from 24 to 96 over one shape, so its
deviation varies by an order of magnitude with everything else held fixed. It
postdates the SOLIDWORKS session. If the family crosses from solid to SURFACE
somewhere in that sweep, the deviation is the mechanism and the threshold is
measurable; if every member imports, it is not, and the two parts share
something still unnamed.

### The sweep, and what it predicts

`scripts/step-interop-kit.py --only '^f0'` builds the family on its own -
`--only` exists so one question can be put in front of an importer without
asking a tester to open forty files. Its deviations, measured with
OpenCASCADE:

| coupon | faces | analytic | faceted | delta |
| --- | --- | --- | --- | --- |
| f01-band-fn024 | 134 | 19886.67 | 19282.26 | **+3.135%** |
| f02-band-fn032 | 181 | 19730.67 | 19379.77 | +1.811% |
| f03-band-fn048 | 269 | 19600.61 | 19451.12 | +0.769% |
| f04-band-fn064 | 360 | 19563.16 | 19476.17 | +0.447% |
| f05-band-fn096 | 532 | 19534.63 | 19494.13 | +0.208% |

The sweep spans a factor of fifteen and brackets both failing parts - lid10 at
1.462% falls between f02 and f03, the bayonet at 0.897% between f03 and f02 -
while the coupler that imports cleanly sits below the whole range. Every member
is the same shape with the same topology, differing only in how finely it was
sampled, and all ten export with the validator clean.

That makes the prediction falsifiable in the useful direction. If the deviation
is the mechanism, SOLIDWORKS should refuse f01 and f02, accept f04 and f05, and
change its mind somewhere near 1%. If it accepts all five, the deviation is a
coincidence of three data points and the two parts share something still
unnamed - which is worth knowing just as much, because it would rule out the
last measurable candidate and point at the topology rather than the geometry.

The kit is at `build/interop-band`, ten files, and `results.csv` beside them has
the columns to fill in.

## What the sweep actually found

SOLIDWORKS' answer on the family, faulty faces from the import diagnostic:

| coupon | deviation | faulty faces (analytic) | faulty faces (faceted) |
| --- | --- | --- | --- |
| f01-band-fn024 | +3.135% | **0** | 0 |
| f02-band-fn032 | +1.811% | **32** | 0 |
| f03-band-fn048 | +0.769% | 1 | 0 |
| f04-band-fn064 | +0.447% | 5 | 0 |
| f05-band-fn096 | +0.208% | 1 | 0 |

**The deviation hypothesis is refuted.** The member with the largest deviation
by a factor of one and a half imports with nothing flagged at all, and the
count does not fall as the sampling gets finer. It is not monotonic in
anything. The volume delta correlated on three real parts and does not survive
a controlled sweep, which is what the sweep was for.

One thing does hold across every row: only the analytic exports are ever
flagged. The faceted controls come back clean at every density.

(f01's two rows report identical figures - 67 short edges, the same minimum
radius of curvature, and a curvature radius at all, where every other faceted
member reports all faces planar. That is the analytic body measured twice. It
does not change the conclusion, since the analytic figure is the one carrying
it, but f01-faceted is unmeasured rather than measured clean.)

### The defect the sweep did find

Sampling each edge along its length and projecting onto both faces it bounds:

| | edges | off by >1e-4 | worst |
| --- | --- | --- | --- |
| line on B-spline | 258 | **254** | 0.196312 |
| line on cylinder | 388 | **366** | 0.171103 |
| line on plane | 496 | 0 | 0.000000 |
| circle on cylinder | 2 | 0 | 0.000000 |
| circle on plane | 2 | 0 | 0.000000 |

(f01-band-fn024-analytic; f02 is the same picture at 464 of 472 and 702 of 724.
The faceted control is 3366 line-on-plane incidences, all exactly zero.)

**Every edge bounding a recovered curved face is still a straight line.** The
exporter replaces a run of facets with a cylinder and leaves the mesh's
polyline boundary in place, so the edge is the chord and the surface is the
arc, and the gap between them is the sagitta. Only the two rim circles are
right, because those are emitted as exact circles on purpose.

This is not a subtle defect and it is present in every member of the family, at
essentially every edge of every curved face. That is precisely why it does not
correlate with the faulty-face count: it is not a graded fault that worsens with
deviation, it is a systematic one that is everywhere. SOLIDWORKS' count is then
a measure of where its healing happens to give up, which is downstream of the
disease rather than the disease.

It also explains the rest of the record without any new assumption. The faceted
files always sew because a line on a plane is exact. OpenCASCADE and Fusion
accept the analytic ones by widening tolerance to swallow the sagitta, which is
exactly what `worst_tolerance` has been reporting all along and what an absolute
threshold could never separate, because nothing is within tolerance to begin
with. And a part with more curved area has more of these edges to bridge, which
is the difference between a coupon and lid10.

The fix is well defined and correct on its own merits, whatever SOLIDWORKS then
does: an edge bounding an analytic face has to be a curve lying *on* that face,
shared with the neighbour across it. On a cylinder the vertical segments are
already right - a ruling is on the surface - and it is the circumferential and
oblique ones that need to become arcs or curves on the surface. This is where
pcurves stop being unnecessary; the two earlier occasions when they were shown
not to be needed were both faces whose boundaries were exact already.

## Reading back what SOLIDWORKS made of it

Two corrections to the sweep table first. f01's faceted control, re-measured,
is clean and all planar as expected. And f02-analytic, reopened, came back with
**12** faulty faces where the first import reported 32 - the same file, the same
importer, a different answer. The faulty-face count is therefore not a
measurement of the file at all; it records where a heuristic healer happened to
give up on that run. It should not be used as a metric again, and the
non-monotonic 0/32/1/5/1 row needs no further explaining.

Exporting the healed bodies back out to STEP was expected to supply an answer
key - a kernel that gets edges right, showing what ours should have been. It
does not, and what it does instead is more useful.

| | our f01 | SOLIDWORKS' f01 |
| --- | --- | --- |
| ADVANCED_FACE | 134 | 144 |
| CYLINDRICAL_SURFACE | 3 | 10 |
| B_SPLINE_SURFACE_WITH_KNOTS | 1 | 4 |
| LINE | 571 | 138 |
| B_SPLINE_CURVE_WITH_KNOTS | 0 | 319 |
| PCURVE / SURFACE_CURVE | 0 | 0 |

It replaced most of the straight edges with B-spline curves - and they still do
not lie on the faces they bound:

| f01 round trip | edges | off >1e-4 | worst |
| --- | --- | --- | --- |
| bspl on plane | 242 | 192 | 0.151957 |
| bspl on cylinder | 250 | 243 | 0.143222 |
| bspl on B-spline | 146 | 58 | 0.113821 |
| **line on cylinder** | **22** | **0** | **0.000000** |
| line on plane | 254 | 0 | 0.000000 |

Against our 0.196 and 0.171, so the error survives the round trip nearly intact.
SOLIDWORKS kept the vertices where they were, re-described the edges through
them as splines, and stored the mismatch as tolerance - tolerant topology,
which is the same thing OpenCASCADE does by widening its own, just less
willingly and, as the 32-then-12 result shows, not reproducibly. Its own
re-export even contains planar faces whose boundary misses the plane by 0.022.

Two things fall out of that. It is not that SOLIDWORKS demands exact edges: it
writes inexact ones itself, and it writes no pcurves either, which retires that
question for the third time. And no external kernel is going to hand us the
correct file, because every one of them copes by tolerancing rather than by
fixing. That makes the defect ours to fix on its own merits rather than to
negotiate with an importer.

The one exact row is the confirmation worth keeping: **22 line-on-cylinder
edges, zero deviation, in both files.** Our export has 388 line-on-cylinder
incidences of which exactly 22 are clean - the vertical rulings, which lie on a
cylinder by construction. The same 22 survive the round trip untouched. That is
the shape of the fix stated precisely: the rulings are already right, and it is
the circumferential and oblique boundary segments that have to become arcs, or
curves on the surface, instead of chords.

## Is the mesh already wrong before the exporter sees it?

Worth asking, because if the vertices handed to the exporter were not on the
surfaces the model declares, no amount of care about edges would help. They are,
and the way to see it is to turn the approximation off. With
`step-analytic-surfaces` alone, every edge of every file measured is exactly on
the face it bounds:

| exact pass only | edges | off >1e-4 | worst |
| --- | --- | --- | --- |
| c01-cylinder | 3 | 0 | 0.000000 |
| step-bored-cylinder | 164 | 0 | 0.000000 |
| **lid10** | **2410** | **0** | **0.000000** |

lid10, all 2410 edges, nothing off by more than 1e-4. The mesh is not the
problem: a tessellated cylinder's vertices lie on the true cylinder, and the
exact pass writes surfaces that pass through them.

The same model with `step-approximate-surfaces` added:

| lid10, approximate | edges | off >1e-4 | worst |
| --- | --- | --- | --- |
| line on B-spline | 479 | 477 | **0.264218** |
| line on cylinder | 210 | 150 | 0.100000 |
| line on plane | 2593 | 0 | 0.000000 |

0.264218 is the number OpenCASCADE reported as its worst tolerance on this file
when the kernel's slack was first measured, and it was read then as the slack
OCCT had to accept. It is the same quantity seen from the other side: the
furthest any edge lies from a face it bounds. The two measurements agreeing to
six figures is what closes the argument.

So the defect belongs to the approximate pass, not to the mesh and not to the
exact pass. That is consistent with the one thing SOLIDWORKS was reliable
about, that only analytic exports were ever flagged, and with the observation
that the failure went away when the model was exported without the declaration, since
`declare_grid` is a fit and needs the approximation flag to be written at all.

It also sets the boundary of what the arc promotion can do. A boundary segment
at constant height on a surface that *contains* its endpoints becomes an exact
arc, and 48 of f01's edges and 192 of f05's do. Where the surface is a fit, the
endpoints are off it by up to the tessellation band and no curve through them
lies on it; those edges are the residual, and closing them means moving
vertices, which moves every planar facet that shares them.

## The exact tier, in SOLIDWORKS

Both reference parts, exported with `step-analytic-surfaces` alone:

| | faulty faces | check entity |
| --- | --- | --- |
| lid10-exact | **0** | no invalid edges/faces |
| bayonet-exact | **0** | no invalid edges/faces |

Solid bodies, nothing flagged, on the two parts that came in as SURFACE bodies
with the approximation on. That closes the thread the SOLIDWORKS investigation
opened: the exporter's exact pass is accepted by all three kernels, and the
disagreement was always about the approximate one.

The round trip says the same thing from the other direction, and more sharply.
Asked to re-export the healed body, SOLIDWORKS wrote back:

| | our lines | its B-splines | edges off a face |
| --- | --- | --- | --- |
| approximate file | 571 | **319** | 0.152 worst |
| exact file | 4700 | **0** | **0.000000** |

Given a file whose edges lie on their faces, it changes nothing: no curve is
re-described, no tolerance is opened, and every edge comes back on its face.
Given one whose edges do not, it replaces most of them with splines and stores
the mismatch as tolerance. The healing was never a preference of the importer's;
it was a repair, and there is nothing to repair here.

One caveat on the pair. In the exact tier the two reference parts come out with
the same face census over the same 4360-facet mesh, 85 bytes apart - the
filename in the header. They are near duplicates, and the interop kit has been
treating them as two independent samples. Worth resolving before either is
quoted as corroborating the other.

## The approximate tier: it is the trim, not the tolerance

Knitting is not the explanation. Both reference parts import as surface bodies
with **Try forming solid(s)** and with **Do not knit** alike, 82 faulty faces
either way, one open surface, and *Create analytic faces* changes nothing.
SOLIDWORKS' separate heal command fails on them too. An earlier reading of this
file that guessed the knit setting was the cause was wrong, and the automated
run's SURFACE verdict stands.

What the re-export shows is worse than a refusal to sew:

| | shell | worst tolerance | B-spline faces |
| --- | --- | --- | --- |
| our r02 | closed solid | 0.264 | 2 |
| SOLIDWORKS' re-export of it | **does not close** | **4.882928** | 1 |

Nearly five millimetres, and a B-spline face lost outright. On screen the thread
is not slightly off, it is shredded into a field of broken quads. Nothing about
tolerance produces that. It is the face being trimmed along the wrong path.

**The cause is that we write no PCURVE.** Without parameter-space curves an
importer has to reconstruct each face's trim by projecting its 3D edges back
onto the surface. On a plane or a cylinder that is trivial - which is exactly
why the exact tier, which contains no B-spline face at all, imports perfectly
into all three kernels. On a long thin fitted B-spline, 154 by 4 stations with
three hundred boundary segments, it is ill conditioned: the trim wanders, the
face deforms, and the shell stops closing. It also explains why *Create analytic
faces* is irrelevant, since that is about how surfaces are classified and not
how faces are trimmed, and why healing fails - there is no small gap to close,
the boundary is in the wrong place.

The test needed no exporter change. OpenCASCADE read our file and wrote it
straight back out: same surfaces, same 0.264 of edge sag, verified as the same
closed solid at the same volume, differing only in carrying 3400 PCURVEs and
1692 SURFACE_CURVEs. SOLIDWORKS reads that one with the thread intact and **one**
faulty face, against 82.

So there are two separate defects here and they had been running together:

1. **No parameter-space trim.** The one that mangles the thread, and the one to
   fix first.
2. **The fit degenerates at the run-out.** What is left after the pcurves go in -
   a single bad face at the end of the thread, which is the overshoot this
   project has measured before: a B-spline fitted through this thread oversteps
   0.378 at the run-out where the band is 0.109.

It is also the case, finally, where pcurves earn their place. Three earlier
occasions concluded they were unnecessary, and all three were faces whose bounds
were exact and trivially projectable. A trimmed fitted sweep is neither.

## What it actually is: two regions in one face

The pcurves did not fix it. Written correctly - `.CURVE_3D.` after
`.PCURVE_S.` turned out not to be a value ISO 10303 has - SOLIDWORKS still
reports 82 faulty faces and still imports a surface body, and the thread is
still shredded.

The earlier conclusion was over-attributed, and the confound is worth naming:
OpenCASCADE's rewrite is not our file plus pcurves. It is OpenCASCADE's
*repaired* shape written out, and the repair includes splitting our faces.

Bisecting to the smallest thing that carries a declared sweep -
`step-declare-grid-strip.py`, nine faces - makes the repair visible. What we
write, and what OpenCASCADE makes of it:

| our ADVANCED_FACE | bounds | edges per bound | OpenCASCADE reads |
| --- | --- | --- | --- |
| B-spline | 2 | 314, 322 | **2 faces**, one wire each, areas 289.65 and 286.03 |
| cylinder | 2 | 138, 225 | 2 faces |
| cylinder | 3 | 158, 138, 40 | 3 faces |
| cylinder | 2 | 42, 22 | 2 faces |
| cylinder | 2 | 22, 40 | 2 faces |

Nine faces in, fifteen out, every one of them with a single wire. The second
bound of the sweep is not a hole in the first: it is a second region of the same
surface, of the same size, somewhere else on it.

**So the exporter packs disjoint regions into one ADVANCED_FACE and calls all
but the largest an inner bound.** A face's inner bounds have to lie inside its
outer one; two separate patches of surface are not a face with a hole. The
heuristic that chose the outer bound - largest area in parameter space - assumed
every cycle belonged to one region, and where the booleans leave two, it labels
the second a hole.

OpenCASCADE splits them and says nothing, which is why its census has been
reporting more faces than the file contains all along, and why nothing in a
round trip ever flagged it. SOLIDWORKS does not split. It is handed one face
with two disjoint outer loops and builds what it can, which is the shredded
thread on screen.

That also explains the shape of every earlier result. The exact tier has no such
face - a band is a ring and a trimmed quadric that survives the proof test is
one region - so it imports everywhere. The approximate tier's sweep and its
trimmed quadrics are exactly the faces the booleans cut into pieces.

The fix is to split a claimed region into connected components before writing
anything, one face per component, and to decide outer against inner only within
a component. The pcurves stay: they are correct, and they will matter once the
faces they bound are valid.

### It was, and the fix holds

Landed, and it is the result of the whole investigation:
**SOLIDWORKS goes from 82 faulty faces to one, with no gaps.** The remaining one
is at the thread's run-out and is the overshoot this project has measured
before - a B-spline fitted through the thread oversteps 0.378 where the band is
0.109 - so it is the second of the two defects named above rather than a
remainder of the first.

Two things to carry, both about attribution rather than about the fix:

- **The pcurve work was landed on a diagnosis that turned out to be
  over-attributed.** It is correct and it was not the defect; the section above
  says so and is worth re-reading before anything else is attributed to a missing
  pcurve.
- **Everything else in this document predates the split.** Every body type, face
  count and volume recorded above was measured against an exporter that packed
  disjoint regions into one face. They are still evidence about the *questions*
  they were asked, and none of them is a measurement of the exporter as it now
  stands.

## The round trip, as a step of the kit

*Reading back what SOLIDWORKS made of it* did this by hand once and it was the
most informative hour of the whole investigation. It is now part of the kit:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\step-interop-solidworks.ps1 `
  -ImportSettings do-not-knit -RoundTrip
```

```bash
python3 scripts/step-interop-sw-roundtrip.py --kitdir build/interop-kit
```

`-RoundTrip` saves each imported coupon straight back out as
`<coupon>_SW.STEP`; the Python script reads both halves of the pair with
OpenCASCADE and prints what changed, in the same surface census the rest of the
suite is written in. It writes `sw-roundtrip.csv` beside the kit.

**Why this and not the body type the driver already records.** A body type is a
verdict, and this is evidence. "SOLIDWORKS made a solid" does not say whether it
kept the surface the coupon is about - and this document has already had to
record it making a solid by *healing*: reading our faces, discarding what it did
not like, and sewing the rest. The re-export says exactly what survived. It is
the argument of `check_bound_enclosure()` one level further out, and of
`doc/step-export-testing.md`'s opening: a kernel agreeing with us proves nothing
on its own, because the agreement can be what its repair pass produced.

Three things it can see that nothing upstream of it can - substitution, where a
B-spline comes back as a fan of planes and the analytic path bought nothing;
degradation, where a cylinder comes back as a spline, so the surface survived as
geometry but not as intent and feature recognition has nothing to find; and
drift, where the volume moves further than the coupon's own band allows.

### What it said on the first pair it was given

The only re-export on disk when this was written, `r02-bayonet-strictcorners`:

```text
r02-bayonet-strictcorners          unbounded
    Cone                       6 -> 8
    Cylinder                  29 -> 22
    Plane                    886 -> 884
    shells                     1 -> 3
    extent                 bounded -> unbounded, it contains an infinite surface
    volume        237897.112943 -> unmeasurable, the shape is unbounded
```

Seven of our twenty-nine cylinders did not survive, our one shell came back as
three, and the file contains a surface with no bounds at all - OpenCASCADE gives
that the box +/-1e100, which is why its volume integrates to 2.16e168. The first
draft of the script duly reported a drift of 9e167 per cent, which is how the
guard came to be there: a volume is only usable if it fits inside its own
bounding box, and a bounding box is only usable if it is finite.

That result is from the strict-corners variant and is not a verdict on anything
currently proposed - it is on disk from an older run and is here because it is
what the tool was tested against. What it demonstrates is the point of the tool:
every one of those six lines is invisible to a body type and a face count.

## c06: a coupon that passes every criterion and is wrong

Run 2026-09-02, against the kit built from the exporter with the face split and
the ladder in. **All 48 files import as solids**, both reference parts included,
which has not happened before - lid10 and the bayonet were the two failures this
document was opened for.

And that is the finding, not the good news. `c06-partial-torus-analytic` imports
as a solid, with the right face count, with zero import errors, and SOLIDWORKS
measures its volume **14.16% low**. Every criterion under *Pass criteria* above
says it passed.

### Four measurements of the same file

| source | volume | note |
| --- | --- | --- |
| Pappus, from the model's own dimensions | 11154.9335655 | no access to the exporter |
| OpenCASCADE | 11154.933573 | |
| Fusion 360 | 11154.934 | area 3640.244 also exact |
| **SOLIDWORKS** | **9575.7969** | sole outlier |

The first row is what makes this different from every earlier disagreement in
this document. `2*pi*(1612 + 52*pi)` is arithmetic on the model's dimensions and
cannot be influenced by the exporter, so this is not two kernels disagreeing
with the blame unassigned. **The file is provably right and SOLIDWORKS is
provably wrong on it.** That is the whole argument for deriving a fixture's
numbers rather than capturing them, made concrete.

Fusion's surface area, 3640.244 mm², is the per-face sum of our own export to
the digit: 1633.628 of cylinder, 980.177 of plane, 1026.439 of torus.

### It is the trim, and the seam is why

SOLIDWORKS' own re-export of the file, read back with OpenCASCADE:

| | ours | SOLIDWORKS' re-export |
| --- | --- | --- |
| CYLINDRICAL_SURFACE | 2 faces, area 1633.628 | 4 faces, area **1633.628** |
| PLANE | 2 faces, area 980.177 | 2 faces, area **980.177** |
| TOROIDAL_SURFACE | 4 faces, area **1026.439** | 8 faces, area **1916.538** |

The surfaces themselves are identical in both files - `TOROIDAL_SURFACE` at
major radii 16 and 10 with a minor radius of 2, which is what the model's arc
centres say. Our four faces are each a quarter of a tube revolved fully, whose
area is `pi^2*R*r`, so the four sum to `pi^2*(32+32+20+20)` = 1026.44, matching
the measurement to six figures.

SOLIDWORKS splits every periodic face at its seam - two cylinders into four,
four tori into eight. On the cylinders that reproduces our geometry exactly. On
the tori it covers **87% more surface than exists in the part** and encloses 14%
less volume, so the surplus is folded back on itself. It is the trim.

The fillet sense is not the explanation and can be ruled out by arithmetic
rather than by eye. `offset(r = 2)` is an outward offset, so all four corners are
convex; flipping a corner changes the first moment by the difference between the
quarter disc and its complement in the corner square:

| flipped | delta first moment | delta volume |
| --- | --- | --- |
| the two inner corners | 43.0 | 270 |
| all four corners | 118.7 | 746 |
| **observed deficit** | **251.3** | **1579** |

### SOLIDWORKS names the entity class itself

With the fault diagnostics automated - `IBody2::Check3`, `IFace2::Check` and
`IEdge::Check`, all read-only - the analytic coupons report what the dialog
shows. `c11-swept-grid`, another of the four disagreements:

```text
c11-swept-grid-analytic   faults=1 faultyfaces=3 faultyedges=2 gaps=0 codes=13/30
c11-swept-grid-faceted    faults=0 faultyfaces=0 faultyedges=0 gaps=0
```

The control is clean and the analytic file is not, which is the whole method of
this kit working as intended. And the codes are `swFaultEntityErrorCode_e`:

- **13, `swEdgeVerticesTouch`** - an edge whose two vertices coincide. That is a
  *closed* edge, which is exactly how a seam on a periodic face is written, and
  exactly how this exporter bounds a face closed round the axis: one seam, used
  once in either direction.
- **30, `swTopolNotG1Continuous`** - a tangent discontinuity where smoothness was
  expected, which is what a fitted sweep's boundary is where it meets a planar
  neighbour.

So the volume evidence and SOLIDWORKS' own diagnostic point at the same thing
from opposite directions, and it is the seam representation rather than the
surfaces, the fillet sense or the tolerance.

### The two real parts, where a third opinion still matters

| part | OpenCASCADE | Fusion 360 | SOLIDWORKS |
| --- | --- | --- | --- |
| r01-lid10 | 226617.51 | ~225,700 (-0.4%) | **351652.94 (+55.2%)** |
| r02-bayonet | 238544.66 | not run | **363852.89 (+52.5%)** |

*Corrected below: those two SOLIDWORKS readings are a defect in what we wrote,
not in how it measures. See "Corrected: on the two real parts it is evidence of
ours".*

Both now import as solids where they used to come in as surface bodies, so the
face split did what it was for - and it turned a loud failure into a quiet one.
A surface body announces itself to the user; a solid whose volume is half again
too large does not, and nothing in *Pass criteria* would catch it.

Fusion and OpenCASCADE agree on lid10 to 0.4% on volume and the same 0.4% on
area, which is not the six-figure agreement of c06 and is worth stating as such:
lid10 carries two fitted B-spline faces of 9,522 mm² against a total of 107,344,
and evaluating those numerically is where a fraction of a percent lives. It is
the same answer; SOLIDWORKS' is not.

### What this changes about the kit

Three of the four disagreements were invisible to everything the kit measured
before this run, because a body type and a face count cannot see them.
`scripts/step-interop-crosscheck.py` now puts the three volumes beside each
other - derived, OpenCASCADE, and the CAD system's - and flags a disagreement
past a threshold. Over the whole kit it flags exactly four files, all analytic,
no faceted control among them, which is what says the disagreement is about the
analytic surfaces and not about units or settings.

Eight coupons carry a derived volume, and OpenCASCADE matches every one of them
to 0.000000% - the sole exception being `c13-oblique-trim` at -0.000256%, the
pcurve-less ellipse re-parameterisation this document already explains, whose
fixture allows 0.02.

### And it is not our file: SOLIDWORKS breaks Fusion's rewrite of it too

The test that reassigns the defect, and it took one round trip through a third
kernel. Our `r01-lid10-analytic.stp` was opened in Fusion 360, which reads it as
one solid, and exported straight back out as STEP. Read with OpenCASCADE:

| | solids / shells | volume | area | faces |
| --- | --- | --- | --- | --- |
| ours | 1 / 1 | 226617.511 | 107343.693 | BSpline 2, Cone 8, Cylinder 6, Plane 1311 |
| Fusion's rewrite | 1 / 1 | **226420.160** | **107321.917** | BSpline 5, Cone 6, Cylinder 5, Plane 1314 |

Fusion did not copy the file. It re-decomposed it - two of our cones and one of
our cylinders come back as B-splines, the plane count moves, and every seam,
trim and placement in it is Autodesk's rather than ours. And it describes the
same solid, to 0.087% of volume and 0.02% of area.

**SOLIDWORKS shreds that file too.**

That rules out a whole class of hypothesis in one measurement. Whatever
SOLIDWORKS objects to survived a complete decompose-and-rewrite by an
independent kernel, so it is not this exporter's entity choices, not its seam
representation, not its face packing and not its formatting - Fusion would have
written each of those its own way. Three of the four candidate causes earlier in
this document are about how *we* write the file, and none of them can be it.

What is left is the geometry: a thin-walled part carrying a helical thread as
about thirteen hundred small planar faces, at this feature size, next to fitted
surfaces. Which is where this document had already arrived by a different route -
*"Something about lid10 in particular is pathological"* - and why finer
tessellation crashed the session twice.

Two limits on the claim, both worth keeping. It is one part and one test. And
the geometry's lineage still runs back through our mesh, so what this shows is
that SOLIDWORKS fails on *this shape* however it is written, not that it would
fail on a shape Fusion authored from nothing.

The control that would sharpen it is cheap and has not been run: put Fusion's
rewrite of the **faceted** lid10 through the same loop. SOLIDWORKS reads our
faceted lid10 perfectly - a solid of 2441 faces whose volume matches
OpenCASCADE's to ten digits - so if Fusion's rewrite of that one also opens
cleanly, the difference is the analytic surfaces at this scale with the writer
eliminated on both sides.

## The first run with fault diagnostics

Run 2026-09-02, all 48 files, `-ImportSettings do-not-knit`, with
`IBody2::Check3`, `IFace2::Check`, `IEdge::Check` and `IBody2::Diagnose` read
per body. **Every file imports as a solid.** Seven bodies carry any fault:

| body | faults | faulty faces | faulty edges | code |
| --- | --- | --- | --- | --- |
| c11-swept-grid-analytic | 1 | 3 | 2 | 13 `swEdgeVerticesTouch`, 30 `swTopolNotG1Continuous` |
| c17-snapped-sweep-analytic | 1 | 1 | 0 | 17 `swFaceBadEdge` |
| f02-band-fn032-analytic | 3 | 3 | 0 | 17 |
| f04-band-fn064-analytic | 2 | 2 | 0 | 17 |
| **r01-lid10-analytic** | 3 | **81** | **86** | **7 `swEdgeVertexNotLie`** |
| **r02-bayonet-analytic** | 3 | **82** | **86** | **7** |
| r01-lid10-**faceted** | 6 | 0 | 0 | 24 `swFaceFaceInconsistency` |

Eighteen of the twenty-four analytic files are entirely clean, including every
quadric coupon, both fillets, the rational B-spline, the text splines and three
of the five band members. Gaps are zero everywhere.

**The two reference parts land on one signature.** About eighty-two faulty faces
and *exactly* eighty-six faulty edges each, every one `swEdgeVertexNotLie` - a
vertex that does not lie on the edge it bounds. That is the defect this document
identified with OpenCASCADE and described as every edge bounding a recovered
curved face still being a straight line, now named by SOLIDWORKS on specific
entities. The two parts agreeing on the edge count to the unit points at the
feature they share, which is the declared thread.

### Three things this run corrected

**The faceted control is not automatically clean.** lid10's carries six
`swFaceFaceInconsistency` faults at body level while importing as a solid whose
volume matches OpenCASCADE to ten digits. So the control still discriminates,
but on the *kind* of fault rather than on its presence: the control's is
body-level with no entity implicated, the analytic file's is eighty-six named
edges.

**Faults and correctness are close to orthogonal.** `c06-partial-torus-analytic`
reports zero faults, zero gaps, zero faulty faces and zero faulty edges - fully
walked, nothing skipped - while SOLIDWORKS holds a solid 14% away from the one
in the file. A body can be faultless and wrong, and lid10's control shows a body
can be faulty and measured exactly right. Neither the body type nor the fault
count would have caught c06; only the volume derived from the model's own
dimensions did.

**The reported volume is not the volume of the body it keeps.** For the two real
parts and for c11, what SOLIDWORKS reports on import disagrees with what
OpenCASCADE reads back from the body SOLIDWORKS itself saved:

| | ours | SOLIDWORKS reported | its own saved body |
| --- | --- | --- | --- |
| c06-partial-torus | 11154.93 | 9575.80 (-14.2%) | 9575.80 (-14.2%) - agrees |
| c11-swept-grid | 16626.90 | 14381.70 (-13.5%) | 16651.36 (+0.15%) |
| r01-lid10 | 226617.51 | 351652.94 (+55.2%) | 241006.86 (+6.3%) |
| r02-bayonet | 238544.66 | 363852.89 (+52.5%) | 234909.83 (-1.5%) |

Only c06 is self-consistent, which is what makes c06 a geometry defect and the
other three a mass-properties one. A +55% reading is not evidence that the solid
is half again too big.

#### Corrected: on the two real parts it is evidence of ours

That last sentence was wrong about r01 and r02, and the correction is worth more
than the original entry. It reads +55% because the thread's face is written with
corners that are not on it, and SOLIDWORKS rebuilds what it cannot trust. Export
the same lid with the sweep left faceted - `step-analytic-surfaces` alone, no
approximation flag - and the two kernels agree:

| r01-lid10, same model, same settings | OpenCASCADE | SOLIDWORKS | apart |
| --- | --- | --- | --- |
| thread faceted (exact tier) | 226945.52 | 227028.38 | **0.04%** |
| thread as one fitted face | 225066.76 | 256515.31 | **14.0%** |

Zero faulty faces on the first, and the volume agrees to a fraction of a
percent. Nothing about SOLIDWORKS' mass properties changed between those two
rows; what changed is whether the file asserts a corner that is on the surface
it claims. The reading was ours all along, and the "mass-properties quirk"
reading let it stand for weeks.

c06 is untouched by this: it is self-consistent, and three sources including the
model's own arithmetic still disagree with SOLIDWORKS about it.

What makes the difference measurable without a commercial kernel is in
`scripts/step-occt-strict.py` - the distance from a face's corners to its own
surface, at the 95th percentile. The tolerance OpenCASCADE grants does not do
it: it is *largest* on the variant SOLIDWORKS reads happily. See
`DE_ShapeFixParameters`, whose `MaxTolerance3d` defaults to 1.0, which is the
licence OpenCASCADE is exercising when it widens an edge and calls the result
valid.

### An open discrepancy with the recorded result

`doc/step-export-status.md` §23 records the face split as taking SOLIDWORKS
**"from 82 faulty faces to 1, and no gaps"**. Half of that reproduces and half
does not:

| | recorded | measured now |
| --- | --- | --- |
| body type | SURFACE | **solid** |
| gaps | - | **0** |
| faulty faces | 82 -> 1 | **81 / 82** |

Both parts landing on 81 and 82 rather than one of them landing there makes an
instrument artefact unlikely, and the direction is wrong for one: on the band
family the dialog counts *higher* than the API, 32 against 3 and 5 against 2, so
a dialog reading of 1 against an API reading of 81 would be the opposite of the
established relationship.

**Not yet corrected in §23, because the check that settles it has not been run:**
open `r01-lid10-analytic.stp` with Import Diagnostics enabled and read the
dialog's own faulty-face count, which is the instrument the original claim was
made with. Until then what is safe to say is the part that reproduces - both
reference parts now import as solids with no gaps, where they were surface
bodies - and that is the stronger claim anyway.

## Run 2026-09-08: the closed sphere, and c17

All 24 coupons, 48 files, after the sphere closure landed. `-ImportSettings` was
recorded as `as-configured-2026-09-08-unverified`: SOLIDWORKS' Tools > Options >
Import could not be read from the automation, so this run is **not** comparable
with the `do-not-knit` runs above on any absolute number. What it is comparable
on is each coupon against its own faceted control, which sees the same settings
in the same session, and against arithmetic, which sees no settings at all.

**Every file imports as a solid. Findings: 0.** As always that is the weakest
thing here - `c06` has passed it every time while being 14% wrong - so the row
that matters is the derived one.

### The nine coupons with a closed form

The only rows that can say an export is *right* rather than self-consistent.

| coupon | derived | OpenCASCADE | SOLIDWORKS | SW vs derived |
| --- | --- | --- | --- | --- |
| c01 cylinder | 6283.185307 | 6283.185307 | 6283.185300 | 1.1e-9 |
| c03 cone | 3183.480556 | 3183.480556 | 3183.480600 | 1.4e-8 |
| **c04 sphere** | **4188.790205** | **4188.790205** | **4188.790200** | **1.2e-9** |
| c05 torus | 1776.528792 | 1776.528792 | 1776.528800 | 4.5e-9 |
| c06 partial torus | 11154.933565 | 11154.933573 | 9575.796900 | **-14.16%** |
| c07 fillet quadrics | 975.587014 | 975.587014 | 975.587000 | 1.4e-8 |
| c13 oblique trim | 4824.927830 | 4824.915473 | 4824.800300 | -2.6e-5 |
| c14 declared cone | 3962.595534 | 3962.595534 | 3962.595500 | 8.6e-9 |
| **c17 cylinder cross** | **5333.333330** | 5333.333262 | 5333.363200 | **+5.6e-6** |

`c06` is unchanged and still the standing outlier: the model's arithmetic and
OpenCASCADE agree to 1e-9 and SOLIDWORKS dissents by 14%, with zero faults
reported. Nothing in this work touched it.

### c04: the sphere, closed at its poles

The coupon that this run existed for, and it is the cleanest row in the table.

- **One face in, one face out.** SOLIDWORKS reads the file as a single face and
  repairs nothing. Every other quadric coupon here is knitted on import - c15,
  c16 and c17 all arrive with fewer faces than the file has - so a coupon that
  comes back with exactly the topology it was written with is the exception.
- **Two derived quantities, not one.** Volume `(4/3)pi r^3` = 4188.7902 and area
  `4 pi r^2` = 1256.6371, both matched to the digit. Area was not asked for and
  is the better check of the two: a solid can hit a volume by luck of
  compensating errors, and hitting both leaves nowhere for a flattened pole to
  hide. The faceted control measures 4121.9898 and 1246.5840 - 1.6% and 0.8%
  low, which is the inscribed polyhedron and exactly what the caps used to cost.
- **The round trip keeps it a sphere.** `kept`, volume identical to ten figures.
  The census moves `Sphere 1 -> 2`, which is SOLIDWORKS re-seaming a closed
  periodic face into two on the way out rather than any loss: it accepts a
  sphere closed on itself but does not write one itself. OpenCASCADE does the
  same thing in reverse, writing a `VERTEX_LOOP` with no edges at all. Three
  kernels, three spellings of the same solid.

The worry going in was the opposite result. The face has **two** edges - one
seam meridian used once in either direction - where c07's octants have three,
and the note on c07 has said since the kit was written that "foreign importers
routinely reject or silently repair a 3-edge face". Two is further out than
three, and it was fine.

### c17: new coupon, and the one number SOLIDWORKS is loose on

Two equal cylinders trimmed to the curve where they cross - a closed shell with
no planar face anywhere, bounded by ELLIPSE arcs, with faces that pinch to a
point. It imports as a solid, round-trips `kept`, and its volume is the
Steinmetz `16 r^3/3`, so it can be measured rather than counted.

OpenCASCADE reads it 1.3e-8 from the exact value. SOLIDWORKS reads it 5.6e-6
away - four hundred times further, though still 0.03 in 5333. **It is not the
file**: the same file is read essentially exactly by the other kernel, and both
kernels agree on the faceted control. It is SOLIDWORKS integrating over
ellipse-arc-bounded quadrics, and it is worth having written down before some
future run reads 5333.3632 as a defect and goes looking for one on our side.

### What is unchanged and still open

- `c06-partial-torus`, above.
- `c11-swept-grid`, and it is **not** the same failure as c06 - see below.
- **`r01-lid10-analytic` round-trips to something that is not a solid at all**,
  and whose volume "exceeds its own bounding box" - every Cone, Cylinder and
  Plane gone from what SOLIDWORKS wrote back. `r02-bayonet-analytic` loses 7
  cylinders and gains 2.4% of volume. Both are pre-existing, both are on the
  reference parts rather than the coupons, and neither is touched by this work.
- The band family still degrades to splines on the way back out at every `$fn`.

### c11 is not c06: a wrong number over an intact body

Both coupons have been recorded the same way - "SOLIDWORKS disagrees with
OpenCASCADE by 13-14%" - and the round trip separates them. Read the last column
against the second:

| coupon | OCCT, our file | SOLIDWORKS reports | OCCT, SOLIDWORKS' rewrite |
| --- | --- | --- | --- |
| c06 partial torus | 11154.933573 | 9575.796900 | **9575.796869** |
| c11 swept grid | 16658.031755 | 14401.261100 | **16663.289647** |

`c06` writes back the solid it reported: OpenCASCADE measures SOLIDWORKS' own
export at 9575.7969, the same 14% short. The geometry is genuinely destroyed on
import, which is what the sections above concluded and it stands.

`c11` does not. SOLIDWORKS reports 14401.26 and then exports a body that
OpenCASCADE measures at 16663.29 - our own 16658.03 to 0.03%. **The body is
intact; the reported volume is wrong.** That is consistent with the face being
flagged: mass properties are integrated over the faces, and SOLIDWORKS has
declared one of them faulty. It matters because the two need opposite responses -
c06 is geometry to fix, c11 is a face shape to make palatable - and because a
crosscheck row alone cannot tell them apart. Only the round trip can.

### c11, chased to the end: five whys, four of them wrong

Written up in full because four of the five steps were refuted, and the
refutations cost more than the answer did. The section that follows this one is
what the first pass concluded; it is wrong, and it is left standing with this
correction above it because the way it was wrong is the instructive part.

**Observation.** `-FaultDetail` names the entities rather than counting them,
and c11 has two *different* faults, not one:

| entity | kind | code | where |
| --- | --- | --- | --- |
| face 1 | bspline | 30 | the sweep |
| face 3 | plane, area 1156 | 13 | z = 0, the base cap |
| edge 454 | curve | 13 | (17.0300, 8.7376, 0) |

The first pass saw only the bspline row and built a fix for it. The plane was
there all along.

**Why 1 - is the code 13 edge degenerate in our file?** No. c11-analytic has
**zero** edges whose two vertices coincide and **zero** locations carrying more
than one of its 763 vertices. The shortest edge in the file is 0.0065 mm.

That also refutes what this document says two sections down - that code 13 is
"exactly how this exporter bounds a face closed round the axis: one seam, used
once in either direction". There is no such edge in this file. The conclusion
drawn from it, "it is the seam representation rather than the surfaces", does
not follow for c11.

**Why 2 - does SOLIDWORKS merge sub-tolerance vertices and make one itself?**
No. The faulty edge is ours, #761, matched to SOLIDWORKS' reported centre to
four decimals, and it is **0.231 mm** long. A *shorter* edge on the same face,
0.200 mm, is not flagged. Length is not the discriminator.

**Why 3 - is it the new SURFACE_CURVE trim entity?** Partly: #761 bounds the
sweep and the base plane, and is written as a `SURFACE_CURVE` over a `LINE` with
a `PCURVE` on the B-spline. `validatestep.py` checks a pcurve against its 3D
curve only on cylinders and cones - "only the two this exporter writes a fitted
pcurve on" - so a pcurve on a B-spline or a plane is checked by nothing here.
That gap is real and worth closing whatever the cause turns out to be.

**Why 4 - is it that the curve carries one pcurve where it should carry two?**
No. 522 of the file's 581 `SURFACE_CURVE`s carry exactly one pcurve on a
B-spline, and 59 carry two. The faulty edge is one of the 522. One flagged out
of 522 identical in that respect is not a discriminator.

**Why 5 - does the shell hold together at a tolerance we choose?** No, and this
is the answer. `step-occt-strict.py` resets every subshape to 1e-6 and asks
again:

```text
file                          granted     bad faces    bad edges
c11-swept-grid-analytic.stp   0.060891    7/11         0/1544
c11-swept-grid-faceted.stp    0.000000    0/534        0/2710
```

The analytic file only closes because OpenCASCADE widened its tolerances by up
to **0.0609 mm** on the way in. Seven of its eleven faces do not hold without
that slack; the faceted control needs none. That is not a new defect - it is the
chorded boundary the band family was built to measure: a fitted face is bounded
by the mesh's own polyline, so it sags off its own surface by up to a station's
sagitta, and 0.0609 is that sagitta. SOLIDWORKS is sewing a shell whose faces
stand that far apart, and both fault codes are downstream of it.

So c11 belongs with the band family and not with the tangent-break story below.

**Where the slack is spent, seen by eye first.** Opening the imported part and
looking along the sweep's boundary shows the vertices bunching in places -
clusters of near-coincident points with a visible kink, against an otherwise
even spacing. They are real, and measuring them says where:

```text
                edges  shortest   median    <0.01mm  <0.05mm  strict 1e-6  SW faults
analytic          772  0.006470   1.2125          2       18   7/11 bad           1
faceted control  1355  0.006474   1.2580          2       19   0/534 bad          0
```

Every one of the twenty shortest edges sits at **r = 19.200 exactly** - the
cylinder's own radius - in mirror pairs about the ridge's mid-height, which is
where the helical ridge's boundary crosses the wall. The median edge is 1.21 mm,
so these are twenty to a hundred and ninety times shorter than their neighbours.

The faceted control carries the same slivers to six decimals and imports with no
fault at all, so the *sampling* is the boolean's. What separates the two files is
what the slivers bound: between two planar facets a 0.0065 mm edge is harmless,
and on the boundary of a curved face that already stands up to 0.0609 mm off
where it should be, it is where a kernel runs out of room.

**But the declaration does reach these vertices, and that is the sharper point.**
A flat 96-gon facet spans r from 19.1897 at its middle to 19.2 at its own
vertices, so a genuine mesh-to-mesh intersection point lands *inside* 19.2. In
the faceted control it does:

```text
                  sliver length   endpoint radii
faceted  control     0.006474     19.199789616 , 19.200000000
         analytic    0.006470     19.200000000 , 19.200000000
faceted  control     0.021310     19.199314583 , 19.200000000
         analytic    0.021267     19.200000000 , 19.200000000
```

Every sliver in the control has one endpoint on a cylinder tessellation vertex
and one 2.1e-04 to 6.9e-04 *inside* the circle. In the analytic export both are
on r = 19.2 to 3.55e-15. The corner placement has already moved them onto the
declared cylinder - this is the "corners moved where two declared owners cross"
line in the report doing exactly its job.

So the declaration is not missing. It corrects **where each vertex sits** and it
does not touch **how many there are**: the sliver survives the move almost
unchanged, 0.021310 becoming 0.021267. Positions come from the declarations;
the sampling still comes from two tessellations intersecting. That is precisely
what item 2 of "what a boundary can be" would settle, because a crossing curve
derived from both declarations replaces the whole polyline and makes the vertex
count irrelevant - and it is what a chord cannot do however exactly its two ends
are placed.

**Where the slivers come from, one level further down.** Two tessellations meet
at r = 19.2 and they do not align: the ridge has 60 stations every 9.1525
degrees, the cylinder 96 facets every 3.75, and 9.1525/3.75 = 2.4407, so station
positions drift against facet boundaries and occasionally land very close to
one. Multiply that angular gap by the radius and it predicts the sliver:

| ridge station | gap to nearest facet boundary | predicted arc | measured edge |
| --- | --- | --- | --- |
| 34 at 311.186 deg | 0.0636 deg | 0.021299 mm | 0.021267 at theta 311.2 |
| 25 at 228.814 deg | 0.0636 deg | 0.021299 mm | 0.021267 at theta 228.8 |
| 9 at 82.373 deg | 0.1271 deg | 0.042598 mm | 0.042535 at theta 82.4 |
| 50 at 97.627 deg | 0.1271 deg | 0.042598 mm | 0.042535 at theta 97.6 |

The chord is slightly shorter than the arc, which is the whole of the
disagreement. **It does not explain the two shortest**, 0.006470 at theta 172.5
and 7.5, which sit exactly on facet boundaries with no station near them - so
that is a second family and an open branch, not a closed one.

That points at two different fixes and they are not alternatives - the first is
upstream of this exporter, and the second is the band family's open question:

- fewer slivers out of the boolean, which is not this code's to give; or
- an analytic face whose boundary does not need a tenth of a millimetre of
  slack to close, which is what "up to what tessellation band does this importer
  sew" has been asking all along.

### The fix that was built for the wrong cause, and what it measured

Worth keeping because two of its measurements are useful and one is a trap.

The first diagnosis was the profile's creases: a sweep's profile is a polyline -
c11's is a trapezoid turning 116.6, 63.4, 63.4, 116.6 degrees - so the four
spans meet at three tangent breaks *inside* one face, where a B-rep puts a
sharp edge between two faces. That reasoning is sound as far as it goes, and a
coupon built to isolate it confirms the mechanism. The same 2x2x10 tube, volume
exactly 40 by construction:

| written as | SOLIDWORKS faults | SOLIDWORKS volume |
| --- | --- | --- |
| one face, creases inside | 1, code 30 | 39.9479 |
| one face per span, one surface each | **0** | **40.0000** |
| one face per span, all on one surface | 0 | 39.9306 |

Two things there are worth carrying:

- **A face SOLIDWORKS does not complain about can still be measured wrongly.**
  The third row reports no fault and is 0.17% out. "faults=0" is necessary and
  nowhere near sufficient, which is the same lesson c06 taught with a 14% error
  and a clean report.
- **The volume error appears with the fault.** Row one is both faulty and wrong,
  which is how a flagged face and a wrong mass property come as a pair.

On the real c11 the fix did not work. Per-span faces still report codes 13/30 -
because, as Why 5 says, the cause was never the creases - and SOLIDWORKS' volume
got *worse*, 13478 against 14401.

And the variant that restricts each face's surface to its own span, which is the
row that measured exactly on the coupon, is **wrong on real geometry**. A
standalone ridge is a screw sweep, and in cylindrical coordinates the Jacobian
is r and does not depend on z, so the pitch drops out and Pappus applies
exactly: `turns * 2pi * A * (R + dc)` = **480.940136** for A = 2.56 and a
centroid radius of 19.9333.

| written as | OpenCASCADE | vs derived |
| --- | --- | --- |
| per-span faces, full surface | 480.939221 | 1.9e-6 |
| per-span faces, restricted surface | 520.216397 | **+8.2%** |

The restricted variant is the one SOLIDWORKS liked best on the real part -
16660.53 against a 14401.26 that everything else agreed was wrong - and it is
8.2% out on the only version of this shape whose volume can be derived. It is
the cleanest example this project has produced of the rule at the top of this
document: **a kernel preferring a file is not evidence the file is right.**
Without the derivable ridge it would have shipped.

All of it was reverted.

### What the first pass concluded, which Why 1 above refutes

Reported at import: 1 fault, 2 faulty faces, 1 faulty edge, codes 13 and 30 -
`swEdgeVerticesTouch` and `swTopolNotG1Continuous`. The sweep face is the one a
user sees highlighted, which is the whole helical ridge in one piece.

Two structural properties of that face would produce exactly those two codes:

- **Its seam columns coincide.** All 60 rows of the control net have their first
  and last control point identical - `(20.6, 0, -1.2)` for row 0 - while the
  surface is written `v_closed = .F.`. The profile is declared closed, so the
  tube is spelled as an open rectangle whose two v-boundaries are the same curve
  in space. An edge there has both vertices in one place: `swEdgeVerticesTouch`.
- **Its interior is creased.** Degree 1 across v with knot multiplicities
  `(2,1,1,1,2)` is a polyline, so the four profile spans meet at three tangent
  breaks *inside a single face*. Most kernels expect a face to be G1 in its
  interior and creases to be edges between faces: `swTopolNotG1Continuous`.

The match between the two properties and the two codes is strong but it is an
inference, not a proof - nothing here made SOLIDWORKS name the entity. What
would settle it is splitting the sweep into one face per profile span, each G1
inside and each bounded by real edges at the corners, and re-importing. That is
also the fix if the inference is right, and it removes the coincident seam for
free, because no face then spans more than one profile span.

Worth noting what this is *not*: the ridge protruding below the cylinder's base
and into its wall is the model. `step-declare-grid.py` sweeps a profile of
`dr` -1.0..+0.6 about `R = 20` from `z = -1.2` to `10.2`, over a cylinder of
`r = 19.2, h = 14`, and the union keeps the overhang. A part whose ridge starts
1.2 below the base and 0.2 inside the wall is the coupon behaving as written -
it is the case the fixture exists for, a sweep fused onto a wall rather than
standing alone.

### Every fault in the kit is on a file with a swept surface

Noticed by eye on the run of 2026-09-08 and then counted, because it decides
whether the crossing curve is worth finishing:

```text
analytic files with a B-spline surface : 11, of which faulty 5
analytic files without one             : 13, of which faulty 0
```

Not one quadric-only coupon reports a fault. That includes every coupon added or
reworked recently and every one that has ever been suspected: c06 the standing
14% outlier, c13's pcurve-less ellipse, c15 and c16 carrying 80 SURFACE_CURVEs
each, c17's closed shell of nothing but trimmed quadrics, c04's sphere closed on
itself. All clean.

The eleven that do carry one split further, and the split is informative:

| coupon | B-spline surfaces | faulty | what they are |
| --- | --- | --- | --- |
| c09 fillet refusals | 24 | no | rational patches, bounded by their own net |
| c10 bspline text | 32 | no | extruded glyphs, no other declared surface to meet |
| c12 approximated | 4 | no | fitted sweeps standing alone |
| f01, f03, f05 | 1 each | no | a declared sweep on a wall, tessellations aligned |
| **f02, f04** | 1 each | **yes** | the same, tessellations not aligned |
| **c11 swept grid** | 2 | **yes** | a declared sweep fused to a declared cylinder |
| **r01, r02** | 2 each | **yes** | real parts, sweeps meeting walls |

So it is not "has a B-spline" - c10 has thirty-two of them and is clean. It is a
declared sweep *meeting another declared surface*, which is exactly the
population the crossing curve exists for, and within that population it is the
pairs whose tessellations do not divide into one another.

That is correlation and it is worth saying so: a sweep meeting a wall is also the
most intricate geometry in the kit, and complexity alone would put it at the top
of any fault list. What lifts it above coincidence is that the mechanism was
found independently - the slivers at the crossing, the chorded boundary, the
0.0609 mm of slack - and that the band family's clean and faulty members split
along the alignment the same mechanism predicts.

It also sharpens what the crossing curve has to be judged on. It made 210 edges
exact and moved the strict-tolerance slack by nothing; whether it moves *these
five files* in SOLIDWORKS is a different question and the one that matters, and
it has not been asked yet, because the change as first written opens the shell -
420 edges used by one face - and was reverted before it could be.

### The band family across tessellation, and it is not the band

The run of 2026-09-08 has all five members, which is the first time the question
the family was built for has been asked of SOLIDWORKS end to end.

| coupon | faces | faults | OpenCASCADE | SOLIDWORKS | diff | round trip |
| --- | --- | --- | --- | --- | --- | --- |
| f01 fn 24 analytic | 149 | 0 | 19549.523161 | 19650.658400 | +0.52% | degraded-to-spline |
| f02 fn 32 analytic | 366 | **2, code 17** | 19575.511046 | 19582.583800 | +0.04% | degraded-to-spline |
| f03 fn 48 analytic | 313 | 0 | 19521.523850 | 19548.050300 | +0.14% | degraded-to-spline |
| f04 fn 64 analytic | 362 | **2, code 17** | 19545.492757 | 19501.081800 | -0.23% | degraded-to-spline |
| f05 fn 96 analytic | 662 | 0 | 19507.342752 | 19520.925500 | +0.07% | degraded-to-spline |

Every faceted control agrees with OpenCASCADE to 1e-7 or better at every
tessellation, so the calibration holds throughout and the rows above are about
the analytic surfaces alone.

**Two things, and neither is what the family was expecting.**

The faults are not monotonic in the band. fn 32 and fn 64 carry two faulty faces
each; fn 24, 48 and 96 carry none. A finer tessellation is not safer and a
coarser one is not worse - 96 is clean and 64 is not.

**And the clean ones are clean because less was attempted.** Opening f05 in
SOLIDWORKS shows it: the sweep is fault-free and part of it is still a sawtooth
of flat facets. Counting what each member refused puts it beyond doubt:

| fn | facets left faceted | faulty faces |
| --- | --- | --- |
| 24 | 11 | 0 |
| 32 | **0** | **2** |
| 48 | 40 | 0 |
| 64 | **0** | **2** |
| 96 | 127 | 0 |

Every member that refused some facets is fault-free, and both members that
refused none are faulty. The refusal is the rule that drops a facet whose corner
is further off the fit than four times the amount the claim is typically off -
at fn 96, four times 0.0024, catching 127 of them by up to 0.0180. Where that
rule fires it removes exactly the geometry SOLIDWORKS would have objected to;
where the stray distribution has no such outlier, at fn 32 and 64, the marginal
facets are claimed and the objection follows.

So `faults=0` is being *bought* here, with faces left faceted, and a fault count
compared across the family without that column beside it says the opposite of
what it appears to. It is the same lesson as c06 from the other end: there, a
clean report over wrong geometry; here, a clean report over geometry that was
never attempted.

An earlier version of this section read the split as tessellation alignment -
24, 48 and 96 being multiples of 24 and the other two not. That was numerology.
The refusal rule explains it mechanically and predicts it, and the alignment
story predicts nothing.

**The discriminator is the taper, and a control proves it.** Looking at f05 in
SOLIDWORKS the refusals seem to alternate - one strip written as a surface, the
next left faceted, then another written - which should be impossible on a helix
whose every turn is the same shape. It is impossible, and the premise is wrong:
the turns are *not* the same shape. `step-band-family.scad` tapers the ridge in
and out,

```text
f = max(0, min(1, t/0.2, (1 - t)/0.2))
```

so the profile grows over the first fifth of the sweep, holds, and shrinks over
the last fifth, with corners in `f` at t = 0.2 and 0.8. Asking the exporter
where its refusals fall settles it - at fn 96, by tenth of the sweep:

```text
refused:  60  12   0   0   0   0   0   0   0  55
claimed: 149  96  97  97  97  97  98  96  96 149
```

Every one of the 127 refusals is in the two ramps. Not one facet is refused
anywhere in the constant middle.

The control is the same model with `f = 1` and nothing else changed:

| $fn | tapered, facets refused | untapered, facets refused |
| --- | --- | --- |
| 32 | 0 | 0 |
| 64 | 0 | 0 |
| 96 | 127 | **0** |

A helical sweep of constant profile is claimed whole at every tessellation
tried. So the refusal rule is not firing on the helix, on the tessellation, or
on the wall it is fused to - it fires on the profile *changing shape*, where a
cubic through the stations cannot follow a piecewise linear taper through its
corners. That is worth knowing before reading anything else the band family
says, because it means four of that family's five rows are measuring a taper
rather than a band.

**Count what was recognised, not what was refused.** Opening the untapered
control shows only about half its sweep written as a surface and the rest a
sawtooth of facets - on a model whose report says nothing was left faceted. Both
statements are true, and the column used above was the wrong one. A facet leaves
the claim two ways: refused as an outlier, which is what "left faceted" counts,
and **cut across it** by the boolean, which is not counted there at all and is
much the larger of the two.

| model | claims whole | cut across it | left faceted | of the sweep written | faulty faces |
| --- | --- | --- | --- | --- | --- |
| tapered fn 32 | 367 | 285 | 0 | **56.3%** | 2, code 17 |
| tapered fn 64 | 727 | 571 | 0 | **56.0%** | 2, code 17 |
| tapered fn 96 | 1100 | 848 | 127 | 49.9% | 0 |
| untapered fn 32 | 321 | 323 | 0 | 49.8% | 2, code 13/30 |
| untapered fn 64 | 639 | 642 | 0 | 49.9% | 0 |
| untapered fn 96 | 960 | 963 | 0 | 49.9% | 0 |

Half of this ridge is inside the wall it is fused to - the profile's outer span
sits past the bore radius - so the union cuts it there and about half the sweep's
facets are cut ones. That much is structural and not a defect. What matters is
that it is **half in every variant**, so "how much was recognised" is a column
that has to be read beside any fault count: `faults=0` over a sweep that is half
faceted is a much weaker result than it looks, and this document reported one.

Read that way the six rows say something the refusal column could not. **The two
faulty code-17 rows are the two that claimed more than half**, 56.3% and 56.0%;
every row at 49.9% is clean of code 17, including the tapered fn 96 which the
outlier refusal pulled down from 56% to 49.9%. The taper pushes the claim past
what this exporter can support and the refusal, where it fires, pulls it back.

The exception is untapered fn 32, which claims 49.8% and is faulty anyway - with
codes 13/30 and sixteen faulty edges rather than code 17. A different failure at
a different place, as its codes say, and unexplained.

**And SOLIDWORKS on the control, which corrects the paragraph above it.** Both
variants at three tessellations, analytic and faceted, in one session:

| model | facets refused | faulty faces | codes |
| --- | --- | --- | --- |
| tapered, fn 32 | 0 | 2 | 17 |
| tapered, fn 64 | 0 | 2 | 17 |
| tapered, fn 96 | 127 | 0 | - |
| untapered, fn 32 | 0 | 2 | **13/30**, 16 faulty edges |
| untapered, fn 64 | 0 | **0** | - |
| untapered, fn 96 | 0 | **0** | - |

Every faceted control in the set is clean, so the calibration holds throughout.

The untapered models at fn 64 and 96 refuse **nothing** and are **clean**. That
refutes the reading two paragraphs up, that `faults=0` was being bought by
leaving facets out: here it is had for nothing. Within the tapered family the
anti-correlation between refusals and faults was perfect and it was not causal -
the refusals and the faults are both symptoms of the taper, and removing the
taper removes the faults at fn 64 while leaving nothing refused.

What the taper costs is therefore sharper than "it makes the exporter refuse
facets". It is a fault source in SOLIDWORKS in its own right: code 17 at fn 32
and 64, gone at fn 64 the moment the profile is made constant.

fn 32 is the one that does not clear. It trades code 17 for **13/30 with sixteen
faulty edges** - c11's signature, and the only rise in edge faults anywhere in
this document. A sweep of constant profile at a coarse tessellation is its own
case and is not explained here.

The volume disagreement does not shrink with tessellation either: 0.52, 0.04,
0.14, -0.23, 0.07 per cent, no trend and a sign change in the middle. Refining
the mesh does not walk SOLIDWORKS towards OpenCASCADE.

So "up to what tessellation band does this importer sew" has an answer, and it
is that the band is not the variable. What the family actually measures is the
outlier refusal, and the honest reading of its five rows is that this exporter
keeps SOLIDWORKS happy by declining to write the facets it is least sure of.

## Run 2026-09-08b: after the two projection fixes

The same 48 files, same session settings, after `GridSurface::project` was made
to descend and to start inside a span rather than on a profile corner. Only the
rows that moved:

| coupon, analytic | faces before | faults | faces after | faults |
| --- | --- | --- | --- | --- |
| f01 band fn 24 | 149 | 0 | **17** | 0 |
| f02 band fn 32 | 366 | 2, code 17 | **9** | 2, code 17 |
| f03 band fn 48 | 313 | 0 | **49** | 0 |
| f04 band fn 64 | 362 | 2, code 17 | **39** | 2, code 17 |
| f05 band fn 96 | 662 | 0 | **153** | 0 |
| r01 lid10 | 1088 | 1, codes 7/13/21/30 | **797** | 1, codes **7/17/21** |
| r02 bayonet | 359 | 1, codes 7/13/21/30 | **68** | 1, codes **7/17/21** |

Two things, and the second is the one that matters.

**Recognition improves by a lot.** A coupon's face count as SOLIDWORKS reads it
is a fair measure of how much was written as surface rather than as facets, and
the band family falls by between four and forty times. The bayonet goes from 359
faces to 68. That is the upper flank of every sweep in the kit, which was being
left faceted because a projection stalled on a profile corner.

**Codes 13 and 30 are gone from both real parts.** `swEdgeVerticesTouch` and
`swTopolNotG1Continuous` are the two this document spent a day on - the pair
that prompted the crossing-curve work and the face-splitting attempt, neither of
which moved them. Recognising the missing half of each sweep did. What is left
on the real parts is 7/17/21, and 17 is now the only code anywhere in the kit
that a coupon carries alone.

The fault *count* is unchanged at six files, which is why it is the wrong number
to read on its own - the same six files, carrying different faults over a
markedly better solid.

## The ladder, and why half a fix is worse than none

The snap of §26 in `doc/step-export-status.md` regressed the bayonet from **1
faulty face to 83** in SOLIDWORKS. Finding out why took most of a session and
overturned four hypotheses, so the route matters as much as the answer.

### Everything measurable said the opposite

Three variants of the same part, differing only in coordinates - identical
topology, 5434 points, 1700 edges, 595 faces, 2126 lines, 53 circles, 479
pcurves in each:

| measure | split (**1 fault**) | + ladder (**83 faults**) |
| --- | --- | --- |
| entity counts, topology | - | **identical** |
| boundary vertices off their own surface | 334 / 958 | **46 / 958**, seven times better |
| planar faces off their own plane | 0 | 0 |
| vertices not on their edge curve | 0 | 0 |
| min face area, short edges | 9.79e-3, none | **identical** |
| `BRepCheck_Analyzer` at a forced 1e-6 | 4 bad faces | **4 bad faces** |
| pcurve endpoints off the surface | 475 / 3400 | **188 / 3400** |

And `strict-both`, which SOLIDWORKS calls **clean**, is the *worst* of the three
by several of them: it carries the one 6.7e-6 sliver, the one 0.019968 off-plane
face, and eight edges whose endpoints miss by 0.021.

Every OpenCASCADE instrument, at every tolerance, with every reader parameter the
STEP user guide names - `read.precision`, `read.maxprecision` in both modes,
`read.surfacecurve` in all three, `read.stdsameparameter`, and the `FromSTEP`
shape-processing sequence disabled - ranks the ladder file as **better**. Not one
of them orders the three the way SOLIDWORKS does.

### What the guide corrected

Two things in the OCCT STEP user guide are worth writing down, because this
document had one of them subtly wrong:

- **The tolerance is not sewing slack.** *"The resulting tolerance of TopoDS_Edge
  is a maximal deviation of its 3D curve and its pcurve(s)."* So the 0.264 that
  `worst_tolerance()` reports is precisely our pcurve-versus-3D disagreement,
  which is the defect itself rather than a kernel being generous. Earlier text
  here describing it as OCCT "widening tolerance until it covers the gap" is one
  level off, though the conclusion drawn from it was right.
- **Why the cap does not bind.** `read.maxprecision.mode = Preferred` may be
  exceeded *"currently, only for deviation of a 3D curve and pcurves of an edge,
  and vertices of such edge"* - exactly our case. Forcing the cap to 1e-7 changes
  nothing, and now there is a documented reason.

### The answer: it is the mixture

Asking SOLIDWORKS *which* entities it objects to settled it. All 414 fault codes
on both real parts are `7`, `swEdgeVertexNotLie` - a vertex that does not lie on
the edge it bounds - and the faulty faces are not the sweep but **79 or 80 small
planar facets along its border**, median area 24 mm², plus the one cylinder and
the one B-spline they touch. The 86 faulty edges are all `SPCURVE_TYPE`: pcurves.

Order the variants by the *count* of bad pcurve endpoints and SOLIDWORKS makes no
sense. Order them by **purity** and it is exact:

| | pcurve endpoints off | worst | SOLIDWORKS |
| --- | --- | --- | --- |
| split - uniformly loose | 475 | 0.218 | **1** |
| ladder - a mixture | 188 | 0.218, **unchanged** | **83** |
| strict-both - uniformly tight | 8 | 0.021 | **0** |

The ladder halved the count and left the worst case identical. It turned a
boundary that was uniformly ~0.2 off into one that is *mostly exact with 38
outliers*. A kernel that infers a face's tolerance from its boundary grants the
first case one loose tolerance and finds everything consistent; in the second it
takes a tight tolerance from the majority, and every outlier then breaks each
face that touches it.

**This is §26's own finding one level up.** Its table for the ladder's internal
rungs reads 70 / 160 / 9 faces for hold-none, hold-proven-only, hold-all, and
concludes *"an intermediate hold is worse than either extreme."* That describes
rung 2 against rung 3. It also describes the ladder against the exporter without
it, and that generalisation was not made at the time.

### Move all or refuse the claim, measured

The obvious repair is to stop half-moving: a vertex the snap cannot place
disqualifies the facets that use it, so a claim is either wholly moved or not
made. Implemented through `AnalyticFeatures::Mesh::unmoved` and measured:

| bayonet | SW faulty faces | SW volume | faces written |
| --- | --- | --- | --- |
| split | **1** | - | **595** |
| ladder | 83 | 363852 (**+52%**) | 595 |
| move-all-or-refuse | **9** | **238696, correct** | 1046 |
| strict-both | **0** | - | 948 |

It works: 81 faulty faces to 7 on the lid, 82 to 9 on the bayonet, `c17` to zero,
and the mass-properties failure disappears with them - SOLIDWORKS' volume goes
from +55% to +0.17%. The sweep's pcurve outliers fall from 38 to 6.

**And it is still dominated.** `strict-both` has fewer faults *and* fewer faces.
The reason is visible in the same audit: only the grid claim was gated, so the
sweep cleaned up while the cylinders stayed at 157 outliers, held by the ladder's
third rung and never refused. `strict-both` applies the same principle to both
paths.

So the three positions collapse into one principle - **refuse what cannot be
placed exactly** - and the ranking on the bayonet is unambiguous:

```text
split           1 fault,   595 faces    full coverage, near-clean
strict-both     0 faults,  948 faces    clean, at a coverage cost
move-all        9 faults, 1046 faces    dominated by both
ladder         83 faults,  595 faces    dominated by split
```

The pre-snap exporter already reached one faulty face with the most consolidated
output of any variant. The snap was built to close a 0.264 deviation that, after
the face split, SOLIDWORKS was no longer objecting to.

### Three lessons this leaves

**SOLIDWORKS partly redeems itself.** This document has spent a long time
treating it as the difficult one, and on the analytic tier it is strict rather
than wrong: it reads our pcurves, holds them against our 3D curves, and refuses
what does not agree. OpenCASCADE absorbs the same disagreement into edge tolerance
and says nothing. Where the two differ, the strict one has usually been pointing
at something real - the face split and this both came out of following its
objections. It remains the outlier on `c06`, where three sources including the
model's own arithmetic agree against it.

**Every kernel has quirks, and settings decide much of the behaviour.** OCCT's
verdict depends on `read.precision`, `read.maxprecision`, `read.surfacecurve` and
`read.stdsameparameter`; SOLIDWORKS' depends on Tools > Options > Import, which
is why every run here is labelled with `-ImportSettings`. A result recorded
without its settings cannot be compared with another one, and a kernel's default
is a policy rather than a truth.

**The diagnostic tooling earned its place.** Nothing in the previous regime could
have found this. The body type said *pass*; the validator said *valid*;
OpenCASCADE said *one closed solid* at every setting; 40 of 40 fixtures were
green. What found it was the per-entity fault dump, the round-trip comparator,
the three-way volume cross-check and the strict re-check at a tolerance we
choose - four instruments built in one session, three of which contradicted a
conclusion the fourth had suggested. That is the point of having more than one.
