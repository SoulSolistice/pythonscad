"""step-rotate-extrude.scad's stepped tube, revolved with the profile lifted
five units along the axis by rotate_extrude's own `offset` - a Python-only
parameter, which is why this twin is Python.

rotate_extrude hands its declarations the profile as it is swept, offset
already applied, and until 2026-09-25 the planes the two annuli sweep added the
offset a second time: both were declared five units above the faces they are
the planes of, so neither was written on a plane the model declared, and the
census saw two declarations with no mesh on them. The curved records never had
it - they were computed from the swept profile alone - so nothing but the
planes could see it, and no fixture used `offset`.

Derived from the model: a translation along the axis changes nothing but where
things are, so every count is the SCAD twin's - three walls and a cone, their
four regions written whole by the declaration, and the two annuli on their two
declared planes. The volume and the kernel's census are the twin's too.
"""
# EXPECT: 4 analytic surfaces available (3 cylindrical, 0 spherical, 0 toroidal, 0 Bezier, 1 conical), and 2 declared planes
# EXPECT: 4 surfaces recognised (0 toroidal, 0 spherical, 1 conical, 0 partial), 128 facets replaced
# EXPECT: 2 planar faces written on a plane the model declared rather than one fitted to its own corners
# EXPECT: 2 declared planes: 2 written as a face, 0 read to identify a surface without one standing on them, 0 with no mesh left on them, 0 unexplained
# ROUNDTRIP: Cone=1 Cylinder=3 Plane=2
# VOLUME: 2907.0203975
from pythonscad import *

profile = polygon([[8, 0], [10, 0], [10, 6], [12, 10], [12, 16], [8, 16]])
profile.rotate_extrude(offset=[0, 5], fn=32).show()
