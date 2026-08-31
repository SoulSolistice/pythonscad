"""The approximation pass writing a surface the model never declared.

Everywhere else this exporter refuses to guess, and on purpose: a hexagonal
prism and a six sided tessellation of a cylinder are the same mesh, so a
recogniser that called every ring of quads a cylinder would silently round off
somebody's nut. The exact path therefore writes only what a generator declared.

This model has no generator to speak for it - a hand written polyhedron over a
computed point list, which is what an imported mesh or a library sweep looks
like from the exporter's side. Under step-approximate-surfaces its wall is
written as a CYLINDRICAL_SURFACE anyway, and what makes that defensible is the
*region* rather than the fit.

Regions are grown across edges meeting at less than the smoothing angle, 25
degrees by default and settable with OPENSCAD_STEP_SMOOTH_ANGLE. A genuine
hexagonal prism has 60 degree dihedrals and never forms a region at all, so it
is never a candidate. This wall's are 5.6 degrees. That threshold is the whole
of the intent judgement, and it is one number in one place rather than a
heuristic spread through the recogniser.

The fit itself is not an approximation, which is worth being precise about. A
tessellated cylinder has its vertices *on* the cylinder - only its facets fall
inside it - so the axis and radius come back exact and the acceptance test is
the ordinary modelling tolerance, not the region's band. A region whose vertices
do not lie on one common cylinder is refused outright.

What the pass then does with the fit is the part worth copying: it *declares*
it, and lets the ordinary recogniser do everything else. So a fitted surface
goes through exactly the checks a declared one does - the same fit test, the
same rim topology, the same refusals - and nothing downstream has to know the
declaration came from the exporter rather than from the model.

66 facets become 3 faces: one cylinder and two planes. OpenCASCADE reads the
radius back as exactly 10 and the volume as 3769.911184, against an exact
pi*100*12 of 3769.911184 - the fitted solid is closer to the intended one than
the mesh it replaced, which had the volume of a 64-gon.
"""
# EXPECT: no analytic surfaces were declared
# EXPECT-NOT: surface recognised
# APPROX: approximation took 1 of 1 uncovered regions - 1 as cylinders, 0 as rings of a turned surface, 0 as swept grids
# APPROX: 1 surface recognised (0 toroidal, 0 spherical, 0 conical, 0 partial), 64 facets replaced
# APPROX: approximation found nothing left to fit
from pythonscad import *
import math

fn, r, h = 64, 10.0, 12.0
pts, faces = [], []
for i in range(fn):
    a = 2 * math.pi * i / fn
    pts.append([r * math.cos(a), r * math.sin(a), 0.0])
for i in range(fn):
    a = 2 * math.pi * i / fn
    pts.append([r * math.cos(a), r * math.sin(a), h])
for i in range(fn):
    j = (i + 1) % fn
    faces.append([i, j, j + fn, i + fn])
faces.append(list(range(fn - 1, -1, -1)))
faces.append(list(range(fn, 2 * fn)))
# Clockwise seen from outside, which is OpenSCAD's order for a polyhedron: the
# right-hand normal of a face points into the solid. Built the other way round
# this exported inside out.
faces = [f[::-1] for f in faces]
polyhedron(points=pts, faces=faces).show()
