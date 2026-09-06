# Corner exactness: handover

Written 2026-09-06, at the end of a long session. `doc/step-corner-exactness.md`
is the reasoning and the baseline; this is the state of the work and what to do
next.

## The one sentence

A corner of an analytic face should lie on the surface that face is written on;
where a boolean made it, it belongs on the curve where its two makers cross.
Three of the eight affected fixtures now do, one is partly there, and three
fixtures need their numbers re-derived before anything else happens.

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
    step-declare-grid-scad    3.79e-12         done, fixture needs updating
    step-band-family          8.89e-02         planes done, cyl and sweep not
    step-cut-cone             3.16e-02         two triple points, correctly left
    step-exact-trim           3.42e-02         not looked at
    py-step-declare-grid      -                fixture needs updating
    py-step-declare-grid-strip -               fixture needs updating

Beware the p95 on a small face: with nineteen corners it *is* the maximum, which
is why `step-cut-cone` reads 3.16e-02 for two corners out of nineteen. Look at
`straykind`-style per-kind numbers before believing a single figure.

The coupon kit is the independent check: 9 of 46 files still stray, and the band
family scales as the sagitta must - 0.102 at `$fn=24` to 0.005 at `$fn=96`.

## The immediate task

Update the three fixtures, one at a time, each moved number carrying a reason.
`doc/step-export-testing.md` requires expectations be **derived, not captured**,
and this is the session where that matters most: regenerating them from the new
output would destroy the property that caught this week's worst bug, a solid
wrong by half while every surface count said it had improved.

For each: run the export, read what changed, and write down *why* the number is
what it is - "the polygon at the junction is fanned into N triangles because a
corner of it moves onto the conic" - not "the exporter now says N".

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
