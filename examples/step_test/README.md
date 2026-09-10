# STEP export test artifacts

Exports of a real user model, kept as measurement input for work on analytic
STEP export. They are not part of any test: `tests/data/scad/step-export/` holds
the fixtures the sanity suite runs. These two exist because every fixture is a
small synthetic part, and the numbers that decide what is worth building next -
how much of a part can be analytic at all, and which gate rejects the rest - can
only be measured on something real.

| file | what it is |
| --- | --- |
| `bayonet_container_v1-2.scad`, `.json` | the model and its parameter set |
| `bayonet_container_v1-2.stp` | **faceted** export of the base, `$fn = 60`, dated 2026-08-10 — 1685 faces over 1693 loops. Still fails `validatestep.py`'s hole-nesting check, which is consistent with its date; a fresh export of the same part is clean at the same face count, so regenerating it would not move the probe figures |
| `lid10.scad`, `.json` | the same model, lid part |
| `lid10.stp` | **analytic** export of the lid, dated 2026-09-01 — 1326 faces, validates clean (42801 entities, 1 shell) |

## What each one is for

`bayonet_container_v1-2.stp` is the input for `scripts/step-analytic-probe.py`,
which replays the recogniser over an exported mesh:

```bash
scripts/step-analytic-probe.py surfaces examples/step_test/bayonet_container_v1-2.stp
scripts/step-analytic-probe.py bands     examples/step_test/bayonet_container_v1-2.stp
```

Every figure in *What a model author can do about it* in
`doc/step-export-development.md` is one run of that script over this file: 1693
loops (1685 faces plus 8 holes), 664 facets - 39.4% of the outer loops - on one
of 14 surfaces of revolution, 26 bands fitted exactly, 25 surviving the rim
rules.

It has to stay a **faceted** export. The probe replays the recogniser, so running
it over an analytic export measures the answer rather than the question.

`lid10.stp` is an analytic export, so the probe does not apply to it. It is a
readable sample of what the analytic path produces on a real part.

## Neither is a known-good reference

Check either one rather than assuming:

```bash
cd tests && python3 -c "from validatestep import validateSTEP; validateSTEP('../examples/step_test/lid10.stp')"
```

- `bayonet_container_v1-2.stp` **fails** the hole-nesting check. It predates the
  fix for the membrane that check exists to catch, which is consistent with its
  date, and it does not affect its use as probe input - the probe reads loops,
  not validity.
- `lid10.stp` **passes** as committed. It used to be the witness for the dropped
  loop - 94 edges used by one face, described under *The defects the checks exist
  for* in `doc/step-export-development.md` - and it was re-exported after that
  fix, so it is now the after rather than the before.

**Both are stale in the other sense.** Nothing regenerates them and no test reads
them, so every improvement to the exporter silently invalidates them; twice they
have been a whole feature behind, and each time the part looked in a CAD system
exactly as though the feature did not work. `lid10.stp` predates the sphere
closure, the projection fixes and the plane declarations. Regenerate with the
part's own parameter set and both flags - without `-p`/`-P` you get the default
component, not the lid:

```bash
build/staging/pythonscad.com examples/step_test/lid10.scad \
    -p examples/step_test/lid10.json -P "New set 1" -o examples/step_test/lid10.stp \
    --enable=step-analytic-surfaces --enable=step-approximate-surfaces
```

A quick way to tell whether one is current is to count its faces against a fresh
export with `grep -c ADVANCED_FACE`.

## They are excluded from the example test suite

`tests/CMakeLists.txt` globs `examples/**/*.scad` recursively into
`dump-examples`, `render-*`, `preview-*` and `throwntogether-*`. These two models
are measurement input rather than examples, and neither has - or wants - a
regression baseline, so both are named in the `REMOVE_ITEM` list beside
`Basics/roof.scad`. Without that they contribute 16 failures to a full `ctest`,
every one of them "missing expected output".
