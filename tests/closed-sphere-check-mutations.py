"""Prove check_closed_sphere rejects what it is there to reject.

A whole sphere exports as one face bounded by its seam meridian and nothing
else, which is two oriented edges over one edge - fewer than any other face of
revolution has, and so a shape the rules for those would have rejected. Adding
it to validatestep.py is adding an acceptance, and an acceptance that is never
shown to refuse anything is indistinguishable from deleting the check.

So each mutation below is a file that is *nearly* a closed sphere: the topology
still closes, the surface is still a SPHERICAL_SURFACE of the right radius, and
the face still has its two oriented edges. Only the seam is wrong, in one of the
ways this exporter could get it wrong - a small circle instead of a great one, a
seam that does not reach the poles, one that reaches the same pole twice, two
edges where there should be one used twice.

What is deliberately NOT here is the defect that prompted the shape: a sphere
exported with its polar caps left on. That file is *valid* - two planes and a
spherical band, every one of them well formed - and no census of faces can tell
it from a whole sphere, because both read as one Sphere plus some planes. It is
caught by the volume a kernel measures against (4/3)pi r^3, in
step-sphere.scad and step-sphere-closed.scad, and this file must not pretend
otherwise.

Run from the repository root: python3 tests/closed-sphere-check-mutations.py
"""

import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validatestep import parse_step, check_cylindrical_faces

# A radius 5 sphere at the origin, closed on itself: the seam is the meridian at
# longitude 0, parameterised from the south pole so that a half turn reaches the
# north one, and it is the face's whole boundary.
BODY = """ISO-10303-21;
HEADER;
FILE_DESCRIPTION((''),'2;1');
FILE_NAME('t','',(''),(''),' ',' ',' ');
FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));
ENDSEC;
DATA;
#1 = CARTESIAN_POINT('',(0.,0.,0.));
#2 = DIRECTION('',(0.,0.,1.));
#3 = DIRECTION('',(1.,0.,0.));
#4 = AXIS2_PLACEMENT_3D('',#1,#2,#3);
#5 = SPHERICAL_SURFACE('',#4,5.);
#6 = CARTESIAN_POINT('',(0.,0.,-5.));
#7 = VERTEX_POINT('',#6);
#8 = CARTESIAN_POINT('',(0.,0.,5.));
#9 = VERTEX_POINT('',#8);
#10 = DIRECTION('',(0.,-1.,0.));
#11 = DIRECTION('',(0.,0.,-1.));
#12 = AXIS2_PLACEMENT_3D('',#1,#10,#11);
#13 = CIRCLE('',#12,5.);
#14 = EDGE_CURVE('',#7,#9,#13,.T.);
#15 = ORIENTED_EDGE('',*,*,#14,.T.);
#16 = ORIENTED_EDGE('',*,*,#14,.F.);
#17 = EDGE_LOOP('',(#15,#16));
#18 = FACE_OUTER_BOUND('',#17,.T.);
#19 = ADVANCED_FACE('',(#18),#5,.T.);
ENDSEC;
END-ISO-10303-21;
"""

failures = []


def run(label, text, expect):
    """Check one mutation. `expect` is "accepted" or "rejected"."""
    import tempfile

    fd, path = tempfile.mkstemp(suffix=".stp")
    os.write(fd, text.encode())
    os.close(fd)
    ents, _ = parse_step(path)
    os.unlink(path)
    problems = []
    check_cylindrical_faces(ents, problems)
    got = "rejected" if problems else "accepted"
    detail = problems[0][:70] if problems else ""
    ok = got == expect
    if not ok:
        failures.append("%s: expected %s, got %s" % (label, expect, got))
    print("%s %-46s %-8s %s" % ("ok  " if ok else "FAIL", label, got, detail))


run("as written (seam is the pole to pole meridian)", BODY, "accepted")

# A meridian is a *great* circle. A smaller one is a circle of latitude, which
# is a rim of some other sphere - and it cannot reach the poles, so the face it
# bounds is a patch with its boundary missing.
run("seam on a small circle", BODY.replace("#13 = CIRCLE('',#12,5.);", "#13 = CIRCLE('',#12,4.);"),
    "rejected")

# The seam has to end at the poles. Off the axis it is some other arc of the
# same great circle, and the caps it was supposed to close are still open.
run("seam does not reach the pole",
    BODY.replace("#8 = CARTESIAN_POINT('',(0.,0.,5.));", "#8 = CARTESIAN_POINT('',(5.,0.,0.));"),
    "rejected")

# Both ends at the same pole: the "seam" is a closed loop round one pole, and
# the sphere below it is not covered at all.
run("seam ends twice at the same pole",
    BODY.replace("#8 = CARTESIAN_POINT('',(0.,0.,5.));", "#8 = CARTESIAN_POINT('',(0.,0.,-5.));"),
    "rejected")

# One edge used once in either direction, not two edges. Two makes the face a
# lune between two different meridians, which is a patch and not the sphere.
run("two distinct seam edges",
    BODY.replace("#16 = ORIENTED_EDGE('',*,*,#14,.F.);",
                 "#161 = EDGE_CURVE('',#7,#9,#13,.T.);\n#16 = ORIENTED_EDGE('',*,*,#161,.F.);"),
    "rejected")

# Used twice the same way round the loop does not close.
run("seam used twice the same way",
    BODY.replace("#16 = ORIENTED_EDGE('',*,*,#14,.F.);", "#16 = ORIENTED_EDGE('',*,*,#14,.T.);"),
    "rejected")

# A seam that is a straight line between the poles is a diameter through the
# middle of the solid, not a curve on the surface.
run("seam is a LINE through the centre",
    BODY.replace("#13 = CIRCLE('',#12,5.);",
                 "#131 = VECTOR('',#2,10.);\n#13 = LINE('',#6,#131);"),
    "rejected")

print()
if failures:
    for f in failures:
        print("FAIL " + f)
    sys.exit(1)
print("all closed-sphere mutations behaved as expected")
