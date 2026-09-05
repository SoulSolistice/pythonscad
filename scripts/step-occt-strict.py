#!/usr/bin/env python3
"""Check every export against a tolerance we choose, not the one OpenCASCADE granted.

`tests/steproundtrip.py` asks `BRepCheck_Analyzer` whether a shape is valid, and
the answer is almost always yes. `doc/step-interop-validation.md` explains why:
OCCT sews by widening the tolerance of an edge until it covers the gap between
that edge and the faces it bounds, so the shape is valid *with respect to the
slack it was granted*. On the bayonet that slack is 0.264 mm. A check against a
tolerance chosen to make the check pass cannot fail.

This asks the other question. `ShapeFix_ShapeTolerance` resets every subshape to
a tolerance we name, and `BRepCheck_Analyzer` is then asked again. What comes
back is how much of the shape only held together because of the slack.

Two things this does **not** do, both deliberate:

  It does not read with different reader settings. The wiki's Interface_Static
  parameters were tried - `read.maxprecision` forced from 1.0 down to 1e-7, and
  the `FromSTEP` shape-processing sequence disabled - and neither changes any
  measurement here by so much as a digit. The tolerance is assigned inside
  TransferRoots during translation, which those knobs do not reach. Recorded so
  the next person does not spend the afternoon on it again.

  It does not claim to predict a commercial importer. It does not: on three
  variants of the bayonet that SOLIDWORKS grades 1, 0 and 83 faulty faces, this
  measure orders them 4, 6 and 4. That is worth knowing rather than hiding - a
  strict local check is a statement about our own file, and a good one, but it
  is not a proxy for interop.

The tolerance is per file, not one number for the kit. What a coupon may
legitimately need is a property of what it exports: an exact-tier quadric is
written only where the mesh lies on it, so it should hold at 1e-7 -
`Precision::Confusion`, OCCT's own floor and the smallest thing there is to ask
for - while a fitted sweep is matched to within the model's tessellation band and
cannot hold tighter than that. Asking both for the same number would either
excuse the first or fail the second for doing exactly what it says it does.

So each fixture declares what it requires, with `TOLERANCE:` for the analytic run
and `TOLERANCE-APPROX:` for the approximation run - the directives
`tests/stepexportsanitytest.py` already reads. A coupon that declares nothing is
measured at the fallback and **reported as undeclared**, because a requirement
nobody wrote down is not a requirement.

Usage:

    python3 scripts/step-occt-strict.py --kitdir build/interop-kit
    python3 scripts/step-occt-strict.py --kitdir build/interop-kit --tolerance 1e-6
"""

import argparse
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tests"))


def bands(kitdir):
    """Coupon file -> the tessellation band its own model concedes.

    Written by scripts/step-interop-kit.py into results.csv at export time,
    because it is the exporter that computes it from the declared grid. Zero for
    a faceted export, which is entitled to nothing."""
    out = {}
    path = os.path.join(kitdir, "results.csv")
    if not os.path.exists(path):
        return out
    with open(path, newline="", encoding="utf-8-sig") as fh:
        for row in csv.DictReader(fh):
            try:
                out[row["file"]] = float(row.get("band") or 0.0)
            except ValueError:
                pass
    return out


def required_tolerances():
    """Coupon name -> (analytic requirement, approximate requirement, or None).

    Read from the source fixture's own TOLERANCE: / TOLERANCE-APPROX: directives,
    so the number lives beside the model it is a property of rather than in this
    script."""
    import importlib.util
    import re
    out = {}
    try:
        spec = importlib.util.spec_from_file_location(
            "kit", os.path.join(HERE, "step-interop-kit.py"))
        kit = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(kit)
        coupons = kit.COUPONS
    except Exception as exc:
        print("could not read the coupon table: %s" % exc, file=sys.stderr)
        return out
    for entry in coupons:
        name, src = entry[0], entry[1]
        path = os.path.join(ROOT, src)
        if not os.path.exists(path):
            continue
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        def grab(key):
            m = re.search(r"(?<![-\w])" + key + r":\s*([0-9.eE+-]+)", text)
            try:
                return float(m.group(1)) if m else None
            except ValueError:
                return None
        out[name] = (grab("TOLERANCE"), grab("TOLERANCE-APPROX"))
    return out


def corner_stray(shape):
    """How far each face's own corners sit off the surface that face is on.

    This is the quantity a strict reader measures, and the granted tolerance is
    not. A corner is a vertex of the written face: the file asserts it is on
    that surface. OpenCASCADE widens an edge's tolerance until the assertion
    holds - `DE_ShapeFixParameters` lets it spend up to a millimetre doing so -
    and then reports a valid solid. SOLIDWORKS holds the pcurve against the 3D
    curve, refuses, and rebuilds the face from what it can trust.

    Measured over the reference lid, this is the only local number that orders
    the exports the way a commercial kernel does:

        corner-off p95   OpenCASCADE against SOLIDWORKS, by volume
        no fitted face   0.04%
        3.3e-14          0.07%
        6.2e-13          0.13%
        0.0128          14.0%
        0.0218          53%

    The p95 rather than the max, because one corner out of five hundred does not
    stop a kernel: the variant with the intact thread and the one with the
    wrecked thread share a max of 0.21843 and differ by twelve orders of
    magnitude at the 95th percentile.
    """
    import math
    from OCP.TopAbs import TopAbs_FACE, TopAbs_VERTEX
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopoDS import TopoDS
    from OCP.BRep import BRep_Tool
    from OCP.BRepAdaptor import BRepAdaptor_Surface
    from OCP.GeomAbs import GeomAbs_Plane, GeomAbs_Cylinder, GeomAbs_Cone
    from OCP.GeomAPI import GeomAPI_ProjectPointOnSurf

    def off(a, face, p):
        t = a.GetType()
        if t == GeomAbs_Plane:
            return abs(a.Plane().Distance(p))
        if t in (GeomAbs_Cylinder, GeomAbs_Cone):
            q = a.Cylinder() if t == GeomAbs_Cylinder else a.Cone()
            ax = q.Axis()
            d, o = ax.Direction(), ax.Location()
            v = (p.X() - o.X(), p.Y() - o.Y(), p.Z() - o.Z())
            h = v[0] * d.X() + v[1] * d.Y() + v[2] * d.Z()
            r = math.sqrt(max(0.0, sum(x * x for x in v) - h * h))
            if t == GeomAbs_Cylinder:
                return abs(r - q.Radius())
            want = q.RefRadius() + h * math.tan(q.SemiAngle())
            return abs(r - want) * math.cos(q.SemiAngle())
        try:
            pr = GeomAPI_ProjectPointOnSurf(p, BRep_Tool.Surface_s(face))
            return pr.LowerDistance() if pr.NbPoints() > 0 else 0.0
        except Exception:
            return 0.0

    strays = []
    exp = TopExp_Explorer(shape, TopAbs_FACE)
    while exp.More():
        face = TopoDS.Face_s(exp.Current())
        a = BRepAdaptor_Surface(face)
        seen = set()
        v = TopExp_Explorer(face, TopAbs_VERTEX)
        while v.More():
            vt = TopoDS.Vertex_s(v.Current())
            h = vt.TShape().This()
            if h not in seen:
                seen.add(h)
                strays.append(off(a, face, BRep_Tool.Pnt_s(vt)))
            v.Next()
        exp.Next()
    if not strays:
        return 0.0, 0.0
    strays.sort()
    return strays[min(len(strays) - 1, int(len(strays) * 0.95))], strays[-1]


def audit(path, tol):
    from OCP.STEPControl import STEPControl_Reader
    from OCP.IFSelect import IFSelect_ReturnStatus
    from OCP.BRepCheck import BRepCheck_Analyzer
    from OCP.ShapeFix import ShapeFix_ShapeTolerance
    from OCP.ShapeAnalysis import ShapeAnalysis_ShapeTolerance
    from OCP.TopAbs import TopAbs_FACE, TopAbs_EDGE
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopoDS import TopoDS

    reader = STEPControl_Reader()
    if reader.ReadFile(os.path.abspath(path)) != IFSelect_ReturnStatus.IFSelect_RetDone:
        return None
    if reader.TransferRoots() < 1:
        return None
    shape = reader.OneShape()
    if shape.IsNull():
        return None

    # What OCCT granted itself on the way in, before we take it away.
    granted = ShapeAnalysis_ShapeTolerance().Tolerance(shape, 1)
    as_read = BRepCheck_Analyzer(shape).IsValid()
    stray_p95, stray_max = corner_stray(shape)

    ShapeFix_ShapeTolerance().SetTolerance(shape, tol)
    an = BRepCheck_Analyzer(shape)
    faces = bad_faces = edges = bad_edges = 0
    exp = TopExp_Explorer(shape, TopAbs_FACE)
    while exp.More():
        faces += 1
        if not an.IsValid(TopoDS.Face_s(exp.Current())):
            bad_faces += 1
        exp.Next()
    exp = TopExp_Explorer(shape, TopAbs_EDGE)
    while exp.More():
        edges += 1
        if not an.IsValid(TopoDS.Edge_s(exp.Current())):
            bad_edges += 1
        exp.Next()
    return {
        "granted_tolerance": granted, "valid_as_read": as_read,
        "valid_at_tolerance": an.IsValid(),
        "faces": faces, "bad_faces": bad_faces,
        "edges": edges, "bad_edges": bad_edges,
        "corner_off_p95": stray_p95, "corner_off_max": stray_max,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kitdir", default="build/interop-kit")
    ap.add_argument("--tolerance", type=float, default=1e-6)
    ap.add_argument("--out", help="CSV to write (default <kitdir>/occt-strict.csv)")
    args = ap.parse_args()

    kitdir = args.kitdir if os.path.isabs(args.kitdir) else os.path.join(ROOT, args.kitdir)
    files = sorted(f for f in os.listdir(kitdir) if f.endswith(".stp"))
    if not files:
        print("no .stp files in %s" % kitdir, file=sys.stderr)
        return 1

    required = required_tolerances()
    band = bands(kitdir)
    rows = []
    undeclared = []
    over = []
    print("%-32s %9s %10s %13s %13s" % (
        "file", "required", "granted", "bad faces", "bad edges"))
    for name in files:
        stem = name[:-4]
        coupon, _, mode = stem.rpartition("-")
        want, why = args.tolerance, "fallback"
        pair = required.get(coupon)
        if pair:
            declared = pair[1] if mode == "analytic" else pair[0]
            # An analytic coupon exported through the approximation pass is held
            # to TOLERANCE-APPROX where it has one, and to TOLERANCE otherwise.
            if declared is None and mode == "analytic":
                declared = pair[0]
            if declared is not None:
                want, why = declared, "declared"
        if why == "fallback":
            undeclared.append(name)
        r = audit(os.path.join(kitdir, name), want)
        if r is None:
            print("%-32s  unreadable" % name[:32])
            continue
        # The sharper question, and the one that needs no tolerance of ours: did
        # the kernel have to grant this file more slack than its own model says
        # it is entitled to? Within the band, the fit is doing what it claims.
        # Beyond it, something else is wrong.
        entitled = band.get(name)
        if entitled is not None and r["granted_tolerance"] > entitled + 1e-12:
            over.append((name, r["granted_tolerance"], entitled))
        rows.append(dict(file=name, required=want, source=why,
                         entitled=("" if entitled is None else "%.6f" % entitled), **r))
        flag = "  <<<" if (r["bad_faces"] or r["bad_edges"]) else ""
        mark = " " if why == "declared" else "?"
        print("%-32s %8.2e%s %10.6f %10.2e %5d/%-7d %5d/%-7d%s" % (
            name[:32], want, mark, r["granted_tolerance"], r["corner_off_p95"],
            r["bad_faces"], r["faces"], r["bad_edges"], r["edges"], flag))

    out = args.out or os.path.join(kitdir, "occt-strict.csv")
    with open(out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    clean = sum(1 for r in rows if not r["bad_faces"] and not r["bad_edges"])
    worst = max(r["granted_tolerance"] for r in rows)
    print("\n%d of %d files hold together at the tolerance they are held to,"
          % (clean, len(rows)))
    print("without the slack OpenCASCADE granted them on the way in.")
    print("The largest such slack: %.6f" % worst)

    # The column the granted tolerance cannot supply. See corner_stray().
    loose = [r for r in rows if r["corner_off_p95"] > 1e-9]
    print("")
    print("%d of %d files put a face's own corners off the surface that face is on,"
          % (len(loose), len(rows)))
    print("at the 95th percentile, which is what a strict reader measures:")
    if loose:
        print("")
        print("   %-32s %12s %12s" % ("file", "p95", "max"))
        for r in sorted(loose, key=lambda x: -x["corner_off_p95"]):
            print("   %-32s %12.3e %12.3e" % (r["file"][:32], r["corner_off_p95"],
                                              r["corner_off_max"]))
        print("")
        print("   A corner is a vertex of the written face and the file asserts it is on")
        print("   that surface. OpenCASCADE widens an edge until the assertion holds and")
        print("   reports a valid solid either way, so the column above is the one that")
        print("   moves when a commercial kernel stops agreeing about the volume.")
    else:
        print("   none - every corner is on its own surface.")
    if over:
        print("\nThese needed more slack than their own model concedes:\n")
        print("   %-32s %11s %11s" % ("file", "granted", "entitled to"))
        for name, g, e in over:
            print("   %-32s %11.6f %11.6f" % (name[:32], g, e))
        print("\n   A fitted surface is entitled to the tessellation band - the sagitta of")
        print("   its own stations - and no more. A faceted export is entitled to nothing,")
        print("   because planes through mesh vertices are exact. Either way the entitlement")
        print("   comes from the model rather than from what the kernel felt like granting.")
    elif band:
        print("\nEvery file held within the band its own model concedes.")
    if undeclared:
        print("\n%d of %d files declare no tolerance and were measured at the fallback %g."
              % (len(undeclared), len(rows), args.tolerance))
        print("Those rows are marked '?'. A requirement nobody wrote down is not a")
        print("requirement, and the fallback is this script's opinion rather than the")
        print("model's - see the note at the top about which tier needs which number.")
    print("results: %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
