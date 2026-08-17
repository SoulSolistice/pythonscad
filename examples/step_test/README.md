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
| `bayonet_container_v1-2.stp` | **faceted** export of the base, `$fn = 60`, 2026-08-10 |
| `lid10.scad`, `.json` | the same model, lid part |
| `lid10.stp` | **analytic** export of the lid, 2026-08-13 |

## What each one is for

`bayonet_container_v1-2.stp` is the input for `scripts/step-analytic-probe.py`,
which replays the recogniser over an exported mesh:

```bash
scripts/step-analytic-probe.py surfaces examples/step_test/bayonet_container_v1-2.stp
scripts/step-analytic-probe.py bands     examples/step_test/bayonet_container_v1-2.stp
```

Every figure in *What is actually left in the bayonet* in `doc/step-export.md` is
one run of that script over this file: 1693 faces, 664 of them (39.4%) on one of
14 surfaces of revolution, 26 bands fitted, 25 surviving the rim rules.

It has to stay a **faceted** export. The probe replays the recogniser, so running
it over an analytic export measures the answer rather than the question.

`lid10.stp` is an analytic export, so the probe does not apply to it. It is kept
as the witness for a defect it exposed - see below.

## Both files fail `tests/validatestep.py`, on purpose

Neither is a known-good reference, and neither should be treated as one:

```bash
cd tests && python3 -c "from validatestep import validateSTEP; validateSTEP('../examples/step_test/lid10.stp')"
```

- `bayonet_container_v1-2.stp` fails the hole nesting check. It predates the fix
  for the membrane that check exists to catch, which is consistent with its date
  and does not affect its use as probe input - the probe reads loops, not
  validity.
- `lid10.stp` has 94 edges used by one face. That is the defect described under
  *The dropped loop* in `doc/step-export.md`: a loop whose winding disagreed with
  the mesh normal, enclosed by nothing, was dropped rather than kept, and dropping
  a face opens the shell along every edge of it. The exporter no longer does that,
  so **this file is the before, not the after**. Re-exporting it after the fix,
  and validating the result, is the cheapest confirmation that the fix works:

```bash
pythonscad examples/step_test/lid10.scad -o /tmp/lid10.stp \
    --enable=step-analytic-surfaces --trust-python
cd tests && python3 -c "from validatestep import validateSTEP; validateSTEP('/tmp/lid10.stp')"
```

Keep it until that has been run on a build that has the fix.
