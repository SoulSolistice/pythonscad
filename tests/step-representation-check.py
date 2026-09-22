#!/usr/bin/env python3
"""How a surface is written must not change the solid.

A declared cylinder or cone can be written two ways: as a band's wall, which is
a ring between two rims, or as a trimmed patch on the same surface. Both assert
the *same surface*, so the solid they bound is the same solid, and its volume
and area are properties of that solid rather than of the choice.

That is the assertion here, and it is a property rather than a captured number,
which is what makes it portable: it holds for any model, including those with no
volume anyone can write down.

For *this* model a volume can be written down, and it says which representation
is wrong rather than merely that they differ. The solid is a cylinder plus a
45-degree chamfer frustum minus a vertical slab, and the cut integrates in
closed form - the slab's area inside a disc of radius r is
`4*sqrt(r^2-16) + r^2*asin(4/r) - 112`, valid throughout since r stays above
sqrt(212). Integrating that over the chamfer gives **15647.634695**, and:

    trimmed patch  15647.634695   -1.89e-11 relative   exact
    band's wall    15646.422089   -7.75e-05 relative   loses 1.2126 mm3

The band's rim is written as a 45-degree ELLIPSE instead of the circle it is,
and the curve arcs off the rim between its endpoints. See open item 26; note
that the earlier claim in this file that no absolute volume was available was
simply wrong - it came from integrating the 32-gon when the exporter writes the
analytic surface.

The two representations are obtained with OPENSCAD_STEP_YIELD_SURFACE, which
makes the band pass skip one declared surface so the general pass covers its
whole region instead.

**It passed on 2026-09-22, and the WILL_FAIL marking came off.** R0 landed: the
band pass now measures whether its ring is the whole of what its surface covers
and gives the region up rather than truncating it, so on this model the default
export *is* the trimmed patch and the volume is the derived one to eleven
digits. It had failed by 1.2126 mm3, then by 1.0489 after the rim fix of open
item 26.

Which changes what each half of this check is worth, and it is worth saying
plainly rather than leaving the name to imply the old thing. The comparison is
now between two exports that come out the same, so it no longer *demonstrates*
the defect - it guards against R0 being lost, since the day a band truncates
this region again the two stop agreeing. **The assertion that carries is the
derived volume**, which is the model's own arithmetic and answers to no
representation at all. Both halves are checked, and either one failing fails the
test.
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLAGS = ["--enable=step-analytic-surfaces", "--enable=step-approximate-surfaces"]

# The ladder's own exactness floor, as the corner placement uses it. Two
# descriptions of one solid are not "close enough"; they are the same or the
# file is wrong about one of them.
EXACT = 1e-9

# The model's own volume, in closed form - the derivation is in the docstring
# above, and it was confirmed by independent numerical integration before any
# export was measured against it. Held to the same floor: the measured export
# agrees to 1.9e-11 relative, so 1e-9 is fifty times the observed slack and
# still four orders tighter than the 1.0489 mm3 the truncated claim cost.
#
# It belongs to step-split-surface.scad and nothing else, so a caller pointing
# --source elsewhere gets the property check alone.
DERIVED_SOURCE = "tests/data/scad/step-export/step-split-surface.scad"
DERIVED_VOLUME = 15647.634695


def export(binary, source, target, yield_surface=None):
    env = dict(os.environ)
    env.pop("OPENSCAD_STEP_YIELD_SURFACE", None)
    if yield_surface is not None:
        env["OPENSCAD_STEP_YIELD_SURFACE"] = str(yield_surface)
    proc = subprocess.run([binary, source, "-o", target, "--trust-python"] + FLAGS,
                          capture_output=True, text=True, cwd=ROOT, env=env)
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def measure(path):
    from OCP.STEPControl import STEPControl_Reader
    from OCP.IFSelect import IFSelect_RetDone
    from OCP.BRepGProp import BRepGProp
    from OCP.GProp import GProp_GProps
    reader = STEPControl_Reader()
    if reader.ReadFile(path) != IFSelect_RetDone:
        return None
    reader.TransferRoots()
    shape = reader.OneShape()
    vol, area = GProp_GProps(), GProp_GProps()
    BRepGProp.VolumeProperties_s(shape, vol)
    BRepGProp.SurfaceProperties_s(shape, area)
    return vol.Mass(), area.Mass()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--openscad", required=True)
    ap.add_argument("--source", default="tests/data/scad/step-export/step-split-surface.scad")
    ap.add_argument("--surface", type=int, default=0,
                    help="the declared surface the band pass must yield")
    args, _ = ap.parse_known_args()

    binary = os.path.abspath(args.openscad)
    if not os.path.isfile(binary):
        print("no such binary: " + binary, file=sys.stderr)
        return 1
    try:
        import OCP  # noqa: F401
    except ImportError:
        # Say so rather than skip in silence: a green run that never read a file
        # back with a kernel is not the run this test exists to be.
        print("note: OCP is not installed, so this check cannot run; install "
              "cadquery-ocp==7.8.1.1.post1", file=sys.stderr)
        return 0

    import tempfile
    tmp = tempfile.mkdtemp(prefix="step-representation-")
    results = {}
    for name, yield_surface in (("band", None), ("patch", args.surface)):
        target = os.path.join(tmp, name + ".stp")
        code, output = export(binary, args.source, target, yield_surface)
        if code != 0 or not os.path.isfile(target):
            print("FAIL %-6s export exited %d" % (name, code), file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        errors = [ln for ln in output.splitlines() if "EXPORT-ERROR" in ln]
        if errors:
            print("FAIL %-6s the exporter reported an error" % name, file=sys.stderr)
            for ln in errors:
                print("       " + ln.strip(), file=sys.stderr)
            return 1
        measured = measure(target)
        if measured is None:
            print("FAIL %-6s a kernel could not read it back" % name, file=sys.stderr)
            return 1
        results[name] = measured
        print("ok   %-6s volume %16.6f  area %14.6f" % (name, measured[0], measured[1]),
              file=sys.stderr)

    (vb, ab), (vp, ap_) = results["band"], results["patch"]
    scale_v = max(abs(vb), abs(vp), 1.0)
    scale_a = max(abs(ab), abs(ap_), 1.0)
    dv, da = abs(vb - vp), abs(ab - ap_)
    failed = dv > EXACT * scale_v or da > EXACT * scale_a
    if failed:
        print("\nFAIL the same solid written two ways does not agree:", file=sys.stderr)
        print("       volume %.6f against %.6f - %.6f, %.3e relative"
              % (vb, vp, dv, dv / scale_v), file=sys.stderr)
        print("       area   %.6f against %.6f - %.6f, %.3e relative"
              % (ab, ap_, da, da / scale_a), file=sys.stderr)
        print("     How a surface is represented is not allowed to change the solid.",
              file=sys.stderr)
        return 1
    print("\nthe solid is the same whichever way its surface is written", file=sys.stderr)

    # And the same solid as the model's, which is the half no representation can
    # satisfy by agreeing with itself. Both kernels called the 1.2126 mm3 file
    # valid with zero faults; only this line would have refused it.
    if os.path.normpath(args.source) != os.path.normpath(DERIVED_SOURCE):
        print("no derived volume for %s, so only the property was checked" % args.source,
              file=sys.stderr)
        return 0
    off = abs(vp - DERIVED_VOLUME)
    if off > EXACT * max(abs(DERIVED_VOLUME), 1.0):
        print("\nFAIL the solid is not the one the model describes:", file=sys.stderr)
        print("       volume %.6f against the model's %.6f - %.6f, %.3e relative"
              % (vp, DERIVED_VOLUME, off, off / abs(DERIVED_VOLUME)), file=sys.stderr)
        return 1
    print("and it is the solid the model describes, to %.3e relative"
          % (off / abs(DERIVED_VOLUME)), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
