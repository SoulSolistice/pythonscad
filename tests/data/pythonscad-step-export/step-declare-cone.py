"""The same cone declaration through the Python API, as a method on the object.

step-declare-cone.scad has the argument for why a cone needs a channel of its
own: the rule it replaces - both rims matching a declared cylinder - cannot
state a cone whose far rim is only where a boolean cut it. This is the other
front end for it, and the two are worth keeping in step, as declare_cylinder
and its siblings already are.

`declare_cone` takes r1, r2 and h rather than an apex and a half angle, because
that is how cylinder() already names the same shape and because a chamfer's
apex is usually nowhere near the part. Too many arguments for the DECLARE_ENTRY
macro that generates the other three, so this binding is written out; that is
the only reason it could drift from its SCAD twin, and this fixture is what
would notice.

The body is a cone truncated by an intersection, so the surviving frustum runs
from a declared r=10 to an r=6 that no primitive named.
"""
# EXPECT: 5 analytic surfaces available (4 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 conical)
# EXPECT: 2 surfaces recognised (0 toroidal, 0 spherical, 1 conical, 0 partial), 64 facets replaced
# EXPECT-NOT: left faceted
# ROUNDTRIP: Cone=1 Cylinder=1 Plane=2
from pythonscad import *

body = (cylinder(r=10, h=10, fn=32) | cylinder(r1=10, r2=2, h=8, fn=32).translate([0, 0, 10])) \
    & cylinder(r=100, h=14, fn=32)
body.declare_cone(r1=10, r2=2, h=8, center=[0, 0, 10]).show()
