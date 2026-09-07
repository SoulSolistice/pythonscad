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
1960.3538158. The sphere's 480 become one SPHERICAL_SURFACE of radius exactly
10 - one face, not the fifteen the rings alone would give, because a
SphereSurface among the declarations lets the zone pass absorb the whole stack
of cones.

The ball also closes at its poles, and it is worth being clear about why, since
nothing here was declared. The profile above is OpenSCAD's own sphere
tessellation written out by hand - phi = pi(i + 0.5)/rings, so no pole vertex
and a flat disc at either end - and the pole closure does not ask what the model
meant. It asks three structural questions of the mesh: do the run's bands lie on
one sphere, does the run reach the last ring at either end, and is each end
closed by a flat disc across the axis. A fitted sphere answers them exactly as a
declared one does, so the ball is written as the whole quadric and the two discs
go with it. That is the difference between Plane=4 here and Plane=2, and between
a ball 0.1454535 short and one that is (4/3)pi R^3.

The frustum's own two caps stay, and they are the control: its ends are real
flats that the model put there, at radius 4 and radius 10, nowhere near a pole
of anything. A closure that ate those would be the bug this pass has to avoid.
"""
# EXPECT: no analytic surfaces were declared
# EXPECT-NOT: surface recognised
# A cone is now fitted directly rather than decomposed into rings. The
# report counts it separately because the two are not the same claim: a
# ring says only that some circle sits at that height, and it takes the
# band pass to make a cone out of a stack of them, which the rim rules
# then often refuse. Fitting the cone says what the surface is.
# APPROX: approximation took 2 of 2 uncovered regions - 0 as cylinders, 1 as cones, 1 as rings of a turned surface, 0 as swept grids
# 530 facets: the frustum's one band of 48, the ball's 15 bands of 32 = 480,
# and the ball's two polar caps, which this face now replaces as well.
# APPROX: 2 surfaces recognised (0 toroidal, 1 spherical, 1 conical, 0 partial), 530 facets replaced
# APPROX: approximation found nothing left to fit
#
# What a kernel makes of each export: the frustum comes back a cone and the ball a sphere, from rings alone.
# Validity says the file is well formed; only this says the surface
# survived as one. A fit read back as the planes it replaced would
# pass every other check in this fixture.
# ROUNDTRIP: Plane=532
# Two planes, not four: the frustum keeps both of its caps and the ball has none.
# ROUNDTRIP-APPROX: Cone=1 Plane=2 Sphere=1
#
# Derived, and new to this fixture - it asserted no volume at all before, which
# left the approximation's *shape* unchecked. Two disjoint solids, so the
# volumes add:
#
#   frustum   pi*h*(r1^2 + r1*r2 + r2^2)/3, r1 = 10, r2 = 4, h = 12  1960.3538158
#   ball      (4/3)*pi*R^3, R = 10                                   4188.7902048
#                                                                    ------------
#                                                                    6149.1440206
#
# Neither term comes from the exporter. The frustum's is the closed form for the
# solid the profile describes, the ball's is the sphere the rings lie on, and a
# capped ball would miss it by 0.1454535 - 2.4e-5 relative, which is 24 times the
# default tolerance and so a failure rather than a rounding.
# VOLUME-APPROX: 6149.1440206
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
