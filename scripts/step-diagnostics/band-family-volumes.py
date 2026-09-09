"""The two derived volumes of band-family-controls.scad's standalone ridge.

The coupon as shipped has no closed form - a union of a bored prism with a
tapered helical ridge is not an integral anyone writes down - so nothing in the
band family can say whether its solid is the *intended* one rather than merely a
self-consistent one.  The untapered standalone ridge can, twice over, and the
two numbers answer different questions:

    smooth   the screw sweep itself.  In cylindrical coordinates the Jacobian is
             r and the helix is a shear in z with unit Jacobian, so the pitch
             drops out and Pappus is exact:  turns * 2pi * A * (radius + dc).
             This is what an *analytic* export should measure, because the
             declared surface interpolates the stations and follows the helix
             between them.
    mesh     the polyhedron the model actually emits, by the divergence theorem
             over its own face list.  This is what a *faceted* export must
             measure, exactly.  It is not the same solid: a chord under-fills
             the arc it spans, so the mesh is 1.3% short at fn 24 and 0.1% short
             at fn 96, and that gap belongs to the model, not to the exporter.

Both are derived from the model's own literals.  Neither is captured from a run.
OpenCASCADE has access to neither arithmetic, so the two agreeing is the only
thing here that says the solid is right.

    python band-family-volumes.py                  # just the derivations
    python band-family-volumes.py <kitdir>         # and compare the exports,
                                                   # needs the OCP bindings

The kit directory is expected to hold w0t0-fn<NNN>-{analytic,faceted}.stp as
scripts/step-diagnostics/band-family-controls.scad emits them with WALL=0,
TAPER=0.
"""
import math
import os
import sys

# the model's literals, from band-family-controls.scad
ridgeDepth, crestWidth, rootWidth, back = 2.0, 3.0, 8.0, 0.3
radius, height, pitch = 20.0, 40.0, 12.0
turns = height / pitch
FNS = (24, 32, 48, 64, 96)


def rows_of(fn, taper):
    steps = max(24, round(fn * turns))
    rows = []
    for i in range(steps + 1):
        t = i / steps
        a = math.radians(360 * turns * t)
        z = rootWidth / 2 + (height - rootWidth) * t
        f = max(0.0, min(1.0, t / 0.2, (1 - t) / 0.2)) if taper else 1.0
        prof = [(back, -rootWidth / 2), (-ridgeDepth * f, -crestWidth / 2),
                (-ridgeDepth * f, crestWidth / 2), (back, rootWidth / 2)]
        rows.append([((radius + dr) * math.cos(a), (radius + dr) * math.sin(a), z + dz)
                     for dr, dz in prof])
    return steps, rows


def mesh_volume(fn, taper):
    """The emitted polyhedron's volume, over the model's own face list."""
    steps, rows = rows_of(fn, taper)
    pts = [p for row in rows for p in row]
    np_ = 4
    faces = []
    for i in range(steps):
        for j in range(np_):
            a0 = i * np_ + j
            b0 = i * np_ + (j + 1) % np_
            c0 = (i + 1) * np_ + (j + 1) % np_
            d0 = (i + 1) * np_ + j
            faces.append([a0, c0, b0])
            faces.append([a0, d0, c0])
    faces.append([0, 1, 2, 3])
    faces.append([steps * np_ + 3, steps * np_ + 2, steps * np_ + 1, steps * np_])
    v = 0.0
    for f in faces:
        for k in range(1, len(f) - 1):
            a, b, c = pts[f[0]], pts[f[k]], pts[f[k + 1]]
            v += (a[0] * (b[1] * c[2] - b[2] * c[1])
                  - a[1] * (b[0] * c[2] - b[2] * c[0])
                  + a[2] * (b[0] * c[1] - b[1] * c[0]))
    return steps, abs(v) / 6.0


A = (rootWidth + crestWidth) / 2 * (back + ridgeDepth)
dc = back - (back + ridgeDepth) * (rootWidth + 2 * crestWidth) / (
    3 * (rootWidth + crestWidth))
SMOOTH = turns * 2 * math.pi * A * (radius + dc)


def occt_volume(path):
    from OCP.STEPControl import STEPControl_Reader
    from OCP.IFSelect import IFSelect_RetDone
    from OCP.GProp import GProp_GProps
    from OCP.BRepGProp import BRepGProp
    r = STEPControl_Reader()
    if r.ReadFile(path) != IFSelect_RetDone:
        return None
    r.TransferRoots()
    g = GProp_GProps()
    BRepGProp.VolumeProperties_s(r.OneShape(), g)
    return abs(g.Mass())


def main():
    print("untapered profile: A = %.6f   dc = %.12f   Rc = %.12f"
          % (A, dc, radius + dc))
    print("smooth screw sweep, Pappus:  %.6f" % SMOOTH)
    print()
    print("%4s %6s %12s %12s %14s" % ("fn", "steps", "ridge step", "facet step", "mesh volume"))
    for fn in FNS:
        steps, mv = mesh_volume(fn, taper=False)
        rs, fs = 1200.0 / steps, 360.0 / fn
        print("%4d %6d %12.6f %12.6f %14.6f%s"
              % (fn, steps, rs, fs, mv, "   aligned" if abs(rs - fs) < 1e-12 else ""))

    if len(sys.argv) < 2:
        return
    kit = sys.argv[1]
    print()
    print("%4s %-9s %14s %14s %12s   %s"
          % ("fn", "mode", "OpenCASCADE", "derived", "relative", "which"))
    for fn in FNS:
        _, mv = mesh_volume(fn, taper=False)
        for mode, want, label in (("faceted", mv, "mesh"), ("analytic", SMOOTH, "smooth")):
            p = os.path.join(kit, "w0t0-fn%03d-%s.stp" % (fn, mode))
            if not os.path.exists(p):
                continue
            v = occt_volume(p)
            print("%4d %-9s %14.6f %14.6f %12.2e   %s"
                  % (fn, mode, v, want, (v - want) / want, label))


if __name__ == "__main__":
    main()
