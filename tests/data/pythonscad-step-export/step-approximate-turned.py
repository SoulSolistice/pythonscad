"""A cone and a sphere the model never declared, recovered as what turned them.

The companion to step-approximate-cylinder.py, and a different problem despite
looking like the same one. A cylinder has a surface type of its own to declare;
a cone and a sphere do not, and that is deliberate rather than missing. This
exporter expresses a frustum the way primitives.cc does - as two rims matching
declared cylinders - and a sphere as a stack of those bands absorbed into a
spherical zone. So a fit here cannot hand over a shape. It has to hand over
*rings*, and the band pass makes the surfaces out of them with no new emission
code at all.

Finding the axis those rings are measured along is the whole difficulty, and two
closed forms were tried before this one. Both are recorded because both are
plausible and neither works:

  the rims        A frustum has two, and they give the axis directly. A sphere's
                  cap meets the band beside it at the angle of one ring - 11
                  degrees on a 32x16 sphere - which is well inside the smoothing
                  angle, so the caps join the region and it has no boundary left
                  to read.
  a screw fit     Every normal of a turned surface is coplanar with the axis and
                  the radial direction, which is linear in six unknowns and
                  solvable as a null space. A cone and a sphere both have every
                  normal line through one point, and the null space comes out
                  three dimensional rather than one - measured on the frustum
                  below, three eigenvalues at 1e-16.

So the axis is proposed and then verified. A rim proposes one, a cap that joined
the region proposes its normal, and the apex - which every tangent plane of a
cone contains, so a linear least squares finds it - proposes the mean ruling
direction. What accepts a candidate is the ring test: every vertex on the circle
its own height puts it on. That is strict enough that a wrong axis cannot
survive it, which is what lets the proposals be rough.

The frustum's 48 wall facets become one CONICAL_SURFACE, and OpenCASCADE reads
the volume back as 1960.353816 against an exact pi*h*(r1^2+r1*r2+r2^2)/3 of
1960.35. The sphere's 480 become one SPHERICAL_SURFACE of radius exactly 10 -
one face, not the fifteen the rings alone would give, because a SphereSurface
among the declarations lets the zone pass absorb the whole stack of cones.
"""
# EXPECT: no analytic surfaces were declared
# EXPECT-NOT: surface recognised
# APPROX: approximation took 2 of 2 uncovered regions - 0 as cylinders, 2 as rings of a turned surface, 0 as swept grids
# APPROX: 2 surfaces recognised (0 toroidal, 1 spherical, 1 conical, 0 partial), 528 facets replaced
# APPROX: approximation found nothing left to fit
#
# What a kernel makes of each export: the frustum comes back a cone and the ball a sphere, from rings alone.
# Validity says the file is well formed; only this says the surface
# survived as one. A fit read back as the planes it replaced would
# pass every other check in this fixture.
# ROUNDTRIP: Plane=532
# ROUNDTRIP-APPROX: Cone=1 Plane=4 Sphere=1
from pythonscad import *
import math


def turned(profile, fn, at):
    """A surface of revolution as a bare polyhedron, with no record of being one."""
    pts, faces = [], []
    for rr, z in profile:
        for j in range(fn):
            a = 2 * math.pi * j / fn
            pts.append([rr * math.cos(a) + at[0], rr * math.sin(a) + at[1], z + at[2]])
    for i in range(len(profile) - 1):
        for j in range(fn):
            k = (j + 1) % fn
            faces.append([i * fn + j, (i + 1) * fn + j, (i + 1) * fn + k, i * fn + k])
    faces.append(list(range(fn)))
    faces.append(list(range((len(profile) - 1) * fn, len(profile) * fn))[::-1])
    # Clockwise seen from outside, which is OpenSCAD's order for a polyhedron:
    # the right-hand normal of a face points into the solid. Built the other way
    # round these exported inside out.
    faces = [f[::-1] for f in faces]
    return polyhedron(points=pts, faces=faces)


frustum = turned([(4.0, 12.0), (10.0, 0.0)], 48, (0, 0, 0))

R, rings = 10.0, 16
sphere_profile = []
for i in range(rings):
    phi = math.pi * (i + 0.5) / rings
    sphere_profile.append((R * math.sin(phi), R * math.cos(phi)))
ball = turned(sphere_profile, 32, (40, 0, 0))

(frustum + ball).show()
