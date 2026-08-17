"""Prove check_bspline_faces rejects what it is there to reject.

The topology checks already catch a missed substitution, a seam shared with the
wrong sense and a loop that does not close. What they cannot see is a bounding
curve with the right two end vertices, used twice in opposite directions so the
shell still closes, but built from the wrong edge of the patch - a face that
bulges the wrong way and validates. That is the mutation below.

Run from the repository root: python3 tests/bspline-check-mutations.py
"""

import os, sys, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validatestep import parse_step, check_bspline_faces

# one degree (2,1) patch: net rows (u) of 2 columns (v), bounded by its v=0 rail
BODY = """ISO-10303-21;
HEADER;
FILE_DESCRIPTION((''),'2;1');
FILE_NAME('t','',(''),(''),' ',' ',' ');
FILE_SCHEMA(('CONFIG_CONTROL_DESIGN'));
ENDSEC;
DATA;
#1 = CARTESIAN_POINT('', (1.,0.,0.));
#2 = CARTESIAN_POINT('', (1.,0.,5.));
#3 = CARTESIAN_POINT('', (0.,0.,0.));
#4 = CARTESIAN_POINT('', (0.,0.,5.));
#5 = CARTESIAN_POINT('', (0.,1.,0.));
#6 = CARTESIAN_POINT('', (0.,1.,5.));
#10 = B_SPLINE_SURFACE_WITH_KNOTS('',2,1,((#1,#2),(#3,#4),(#5,#6)),.UNSPECIFIED.,.F.,.F.,.F.,(3,3),(2,2),(0.,1.),(0.,1.),.UNSPECIFIED.);
#11 = CARTESIAN_POINT('', (1.,0.,0.));
#12 = CARTESIAN_POINT('', (0.,0.,0.));
#13 = CARTESIAN_POINT('', (0.,1.,0.));
#14 = B_SPLINE_CURVE_WITH_KNOTS('',2,(#11,#12,#13),.UNSPECIFIED.,.F.,.F.,(3,3),(0.,1.),.UNSPECIFIED.);
#20 = VERTEX_POINT('', #11);
#21 = VERTEX_POINT('', #13);
#22 = EDGE_CURVE('', #20, #21,#14,.T.);
#23 = ORIENTED_EDGE('',*,*,#22,.T.);
#24 = EDGE_LOOP('',(#23));
#25 = FACE_OUTER_BOUND('',#24,.T.);
#26 = ADVANCED_FACE('',(#25),#10,.T.);
ENDSEC;
END-ISO-10303-21;
"""

failures = []


def run(label, text, expect):
    """Check one mutation. `expect` is "accepted" or "rejected"."""
    import tempfile, os
    fd, path = tempfile.mkstemp(suffix='.stp')
    os.write(fd, text.encode())
    os.close(fd)
    ents, _ = parse_step(path)
    os.unlink(path)
    problems = []
    check_bspline_faces(ents, problems)
    got = "rejected" if problems else "accepted"
    detail = problems[0][:70] if problems else ""
    ok = got == expect
    if not ok:
        failures.append(f"{label}: expected {expect}, got {got}")
    print(f"{'ok  ' if ok else 'FAIL'} {label:44s} {got:8s} {detail}")


run("as written (curve is the v=0 column)", BODY, "accepted")
# the curve built from the wrong edge of the net: right endpoints, wrong shape.
# This is the one the topology checks cannot see - the shell still closes.
run("curve from the wrong edge of the net",
    BODY.replace("#14 = B_SPLINE_CURVE_WITH_KNOTS('',2,(#11,#12,#13)",
                 "#14 = B_SPLINE_CURVE_WITH_KNOTS('',2,(#11,#2,#13)"),
    "rejected")
# a knot vector that is not a Bezier's
run("knots that are not a Bezier's",
    BODY.replace("(3,3),(0.,1.),.UNSPECIFIED.);\n#20", "(2,2),(0.,1.),.UNSPECIFIED.);\n#20"),
    "rejected")
run("surface knots wrong",
    BODY.replace(".F.,.F.,.F.,(3,3),(2,2)", ".F.,.F.,.F.,(2,2),(2,2)"),
    "rejected")
# A reversed curve is still an edge of the net, so it describes the same
# boundary and has to stay acceptable; the sense it is used with is the
# edge-use rule's business, not this check's.
run("curve reversed (still a net column)",
    BODY.replace("(#11,#12,#13)", "(#13,#12,#11)"), "accepted")

if failures:
    print()
    for f in failures:
        print("FAILED:", f, file=sys.stderr)
    sys.exit(1)
print()
print("%d mutations behaved as expected" % 5)
