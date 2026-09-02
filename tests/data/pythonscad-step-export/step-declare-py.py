"""The same declaration through the Python API, as a method on the object.

The point of the item was a user facing declaration, and PythonSCAD is where it
belongs: a solid is an object, so saying what part of it was meant to be is a
call on that object rather than a module wrapped around it. The two forms build
the same node and are worth keeping in step - see
tests/data/scad/step-export/step-declare.scad for the SCAD one and for why a
generator cannot make this declaration on the model's behalf.

Expected: 1 analytic surface available, 1 surface recognised, none conical,
none partial, 32 facets replaced, 34 faces down to 3.
"""
# EXPECT: 1 analytic surface available (1 cylindrical, 0 spherical, 0 toroidal, 0 Bezier)
# EXPECT: 1 surface recognised (0 toroidal, 0 spherical, 0 conical, 0 partial), 32 facets replaced
#
# pi*10^2*20, the true cylinder - the same figure its SCAD twin states, which
# is what says the two front ends build the same declaration.
# ROUNDTRIP: Cylinder=1 Plane=2
# VOLUME: 6283.1853072
from pythonscad import *

wall = linear_extrude(circle(r=10, fn=32), height=20)
wall.declare_cylinder(r=10).show()
