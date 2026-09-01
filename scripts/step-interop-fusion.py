"""Import every STEP file of an interop kit into Autodesk Fusion and record what
it made of them.

A third kernel, and the reason for wanting one is in
doc/step-interop-validation.md: two real parts in this repository import into
SOLIDWORKS as *surface* bodies where their own faceted controls import as
solids, and everything that might explain it has been measured and ruled out -
the tessellation, the size of the model, the boundary of a single face, and the
slack OpenCASCADE has to accept to sew it. OpenCASCADE reads both as one valid
closed solid. With two kernels disagreeing and no mechanism, a third opinion
says which of them is the outlier.

Fusion has no out-of-process automation of the kind the SOLIDWORKS driver uses,
so this runs *inside* Fusion:

    Utilities -> ADD-INS -> Scripts and Add-Ins -> Scripts -> the green +
    -> pick this file -> Run

It asks for the kit folder, imports every .stp in it, and writes
`fusion-results.csv` beside them. Each import opens a document and closes it
again without saving.

Recorded per file, chosen to line up with the SOLIDWORKS driver's columns so the
two can be read side by side:

    body      solid / surface / none - the decision-relevant one. A surface body
              means the faces were read and could not be sewn, which is the
              failure a user actually meets.
    bodies    how many came back; more than one is its own kind of finding.
    faces     against the count the exporter wrote.
    volume    zero for a surface body, since there is no solid to measure.

Written but not yet run against a real Fusion - the API calls follow the
documented interface, and if it fails it should be read as "this script is
wrong" before "the export is wrong".
"""

import csv
import os
import traceback

import adsk.core
import adsk.fusion


def body_kind(bodies):
    """solid, surface, or mixed - what Fusion made of one imported file."""
    if bodies.count == 0:
        return "none"
    solids = sum(1 for b in bodies if b.isSolid)
    if solids == bodies.count:
        return "solid"
    if solids == 0:
        return "surface"
    return "mixed"


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface

        dlg = ui.createFolderDialog()
        dlg.title = "Pick the interop kit folder (the one holding the .stp files)"
        if dlg.showDialog() != adsk.core.DialogResults.DialogOK:
            return
        kit = dlg.folder

        files = sorted(f for f in os.listdir(kit) if f.lower().endswith(".stp"))
        if not files:
            ui.messageBox("No .stp files in %s" % kit)
            return

        rows = []
        for name in files:
            path = os.path.join(kit, name)
            doc = None
            try:
                opts = app.importManager.createSTEPImportOptions(path)
                doc = app.importManager.importToNewDocument(opts)
                design = adsk.fusion.Design.cast(app.activeProduct)
                bodies = design.rootComponent.bRepBodies
                kind = body_kind(bodies)
                faces = sum(b.faces.count for b in bodies)
                # A surface body has no volume, and asking for one raises rather
                # than returning zero, so it is only asked of a solid.
                volume = 0.0
                if kind == "solid":
                    volume = sum(b.volume for b in bodies)
                rows.append((name, kind, bodies.count, faces, volume, ""))
            except Exception as exc:  # one bad file must not end the run
                rows.append((name, "ERROR", 0, 0, 0.0, str(exc)[:200]))
            finally:
                if doc is not None:
                    try:
                        doc.close(False)
                    except Exception:
                        pass

        out = os.path.join(kit, "fusion-results.csv")
        with open(out, "w", newline="", encoding="utf-8") as fh:
            w = csv.writer(fh)
            w.writerow(["file", "body", "bodies", "faces", "volume", "error"])
            w.writerows(rows)

        # The comparison the kit is for: an analytic export against its own
        # faceted control. A coupon that fails alone proves nothing; one that
        # fails where its control succeeds is the finding.
        by_stem = {}
        for name, kind, _n, _f, _v, _e in rows:
            stem = name[:-4]
            for suffix in ("-analytic", "-faceted"):
                if stem.endswith(suffix):
                    by_stem.setdefault(stem[: -len(suffix)], {})[suffix[1:]] = kind
        findings = [c for c, k in sorted(by_stem.items())
                    if k.get("analytic") not in (None, "solid") and k.get("faceted") == "solid"]

        ui.messageBox(
            "%d files.\n\n%s\n\nWritten to %s%s"
            % (len(rows),
               "\n".join("%-34s %s" % (n, k) for n, k, *_ in rows),
               out,
               "\n\nFINDINGS (analytic failed where its control imported):\n  " +
               "\n  ".join(findings) if findings else "\n\nNo findings."))

    except Exception:
        if ui:
            ui.messageBox("Failed:\n%s" % traceback.format_exc())
