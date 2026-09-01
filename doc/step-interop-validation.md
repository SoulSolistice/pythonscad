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
