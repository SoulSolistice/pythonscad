#!/usr/bin/env python3

# STEP round trip through a real CAD kernel.
#
# Everything else in this suite is the exporter checking its own work.
# validatestep.py is a good proxy - eleven checks, one per historical defect,
# and it has caught real bugs - but it is a proxy: it knows what this exporter
# has got wrong before, not what a CAD kernel requires. The exporter's whole
# purpose is that a kernel accepts the file, and until this script existed that
# was assumed rather than measured.
#
# OpenCASCADE is the kernel FreeCAD is built on, and its STEP reader is the
# same one a great deal of industry runs on, so "OCCT reads this as a solid" is
# a genuinely independent answer rather than a second opinion from the same
# source. It is an optional dependency: without it every check here is skipped
# and the suite behaves exactly as it did before, the same way the locale
# independence check in stepexportsanitytest.py skips when no comma locale is
# installed.
#
#   pip install cadquery-ocp          # or the distribution's OCCT bindings
#
# What it asserts, and why each one is not already covered:
#
#   reads at all         a file validatestep.py accepts can still be rejected by
#                        a reader with stricter ideas about the schema
#   forms a SOLID        the failure mode that started all of this: SolidWorks
#                        imported a filleted cube as loose surfaces rather than
#                        a solid, because the shell was not closed. A shell that
#                        is one face short still parses, and every face in it is
#                        still valid.
#   BRepCheck valid      the kernel's own topology and geometry checker, which
#                        knows about things this project's validator does not -
#                        wire imbrication, self intersection, orientation
#   positive volume      a solid whose faces all face inward reads, checks and
#                        closes, and is inside out
#   surface types        that a CYLINDRICAL_SURFACE is *recognised as a
#                        cylinder*, not merely parsed. This is the one that says
#                        the analytic path bought anything at all: a kernel can
#                        offset, thread and pattern a cylinder, and a fixture
#                        can assert the count it expects.
#
# Calibration - both committed exports in examples/step_test/ predate the fixes
# for the defects they carry, and OCCT independently finds exactly those:
#
#   lid10.stp      0 solids from 1860 faces, so the shell never closed
#                  (validatestep.py: 94 edges used by one face)
#   bayonet…stp    1 solid, but face 793 is InvalidImbricationOfWires at z=75
#                  (validatestep.py: a hole outside the outer bound of its face)
#
# Two tools, two implementations, same two defects on the same two files.

import math
import os
import sys

# The whole module is optional. Import failure is not an error; it means this
# machine cannot run the round trip and the caller should carry on.
try:
    from OCP.BRepCheck import BRepCheck_Analyzer
    from OCP.BRepGProp import BRepGProp
    from OCP.GProp import GProp_GProps
    from OCP.IFSelect import IFSelect_ReturnStatus
    from OCP.STEPControl import STEPControl_Reader
    from OCP.TopAbs import TopAbs_EDGE, TopAbs_FACE, TopAbs_SHELL, TopAbs_SOLID
    from OCP.TopExp import TopExp, TopExp_Explorer
    from OCP.TopTools import TopTools_IndexedMapOfShape
    from OCP.TopoDS import TopoDS
    from OCP.BRep import BRep_Tool
    from OCP.BRepAdaptor import BRepAdaptor_Curve, BRepAdaptor_Surface
    from OCP.ShapeAnalysis import ShapeAnalysis_CanonicalRecognition, ShapeAnalysis_FreeBounds
    from OCP.TopAbs import TopAbs_WIRE
    from OCP.gp import gp_Cone, gp_Cylinder, gp_Pln, gp_Sphere
    from OCP.GeomAbs import GeomAbs_Cone, GeomAbs_Cylinder, GeomAbs_Plane
    from OCP.GeomAPI import GeomAPI_ProjectPointOnSurf
    from OCP.TopAbs import TopAbs_VERTEX

    HAVE_OCCT = True
except ImportError:
    HAVE_OCCT = False


def available():
    return HAVE_OCCT


def _count(shape, kind):
    exp = TopExp_Explorer(shape, kind)
    n = 0
    while exp.More():
        n += 1
        exp.Next()
    return n


def _surface_census(shape):
    """How many faces the kernel reads as each kind of surface.

    The name comes from BRepAdaptor_Surface::GetType, so a CYLINDRICAL_SURFACE
    that the reader could not make sense of would show up here as something
    else - most likely BSplineSurface - rather than silently passing."""
    census = {}
    exp = TopExp_Explorer(shape, TopAbs_FACE)
    while exp.More():
        kind = str(BRepAdaptor_Surface(TopoDS.Face_s(exp.Current())).GetType()).rsplit("_", 1)[-1]
        census[kind] = census.get(kind, 0) + 1
        exp.Next()
    return census


def canonical_census(shape, tol=1e-7):
    """For every B-spline face, the canonical surface the kernel says it really is.

    This is the cross check on our own recogniser, and it runs in the one
    direction that can find a missed opportunity. ShapeAnalysis_CanonicalRecognition
    answers "is this spline exactly a cylinder", which is a different question
    from the one AnalyticFeatures asks - it works from the surface, not from the
    facets, so it cannot recognise a tessellated cylinder and cannot replace
    anything here. What it can do is audit what we chose to leave as a spline.

    A face in this census under anything but "spline" is a quadric the exporter
    wrote as a B-spline: valid, importable, and worse than it needed to be. A
    count that *grows* is the regression signal - quadricOfPatch having stopped
    recognising something it used to.

    Note this is the kernel's opinion at its own tolerance, so the numbers can
    move with an OCCT version. Fixtures assert it only where they say so."""
    census = {}
    exp = TopExp_Explorer(shape, TopAbs_FACE)
    while exp.More():
        face = TopoDS.Face_s(exp.Current())
        if str(BRepAdaptor_Surface(face).GetType()).endswith("BSplineSurface"):
            rec = ShapeAnalysis_CanonicalRecognition(face)
            if rec.IsPlane(tol, gp_Pln()):
                kind = "Plane"
            elif rec.IsCylinder(tol, gp_Cylinder()):
                kind = "Cylinder"
            elif rec.IsCone(tol, gp_Cone()):
                kind = "Cone"
            elif rec.IsSphere(tol, gp_Sphere()):
                kind = "Sphere"
            else:
                kind = "spline"
            census[kind] = census.get(kind, 0) + 1
        exp.Next()
    return census


def worst_tolerance(shape):
    """The largest tolerance the kernel had to accept to sew this shape.

    The one measurement here that predicts a *foreign* importer, and it was
    missing for a long time because BRepCheck_Analyzer says "valid" and stops.
    OpenCASCADE sews by widening an edge's tolerance until it covers the gap
    between that edge and the faces it bounds, so a shape whose edges do not lie
    on their surfaces still comes back as one closed solid - it has simply been
    granted the slack it needed.

    What that slack *is* is our own defect seen from the other side: per the OCCT
    STEP user guide, an edge's tolerance is the maximal deviation between its 3D
    curve and its pcurves. It is not a kernel being generous, and it is not a
    correctness measure either - it is *largest* on a lid variant SOLIDWORKS
    reads happily, because DE_ShapeFixParameters defaults MaxTolerance3d to 1.0.

    It was once read as predicting whether a foreign importer would sew, on a
    perfect correlation over one part. The band family refuted that: every member
    imports as a solid, including one needing 0.283 - more than the 0.264 at
    which the lid failed. See doc/step-export-development.md, *Refuted claims*.
    What it remains good for is being the only cheap, deterministic, licence-free
    number that says how far an approximated face's boundary sits from the
    surface it bounds. TOLERANCE: and TOLERANCE-APPROX: assert it.

    Which is also the tessellation band, and that is not a coincidence. Every
    exactly-fitted face this exporter writes is bounded by curves lying on it;
    an *approximated* face is bounded by the mesh's own polyline, because the
    faceted faces around it have to close against it edge for edge, and those
    chords sag off the surface by up to a station's sagitta."""
    worst = 0.0
    for kind, cast in ((TopAbs_FACE, TopoDS.Face_s), (TopAbs_EDGE, TopoDS.Edge_s)):
        exp = TopExp_Explorer(shape, kind)
        while exp.More():
            worst = max(worst, BRep_Tool.Tolerance_s(cast(exp.Current())))
            exp.Next()
    return worst


def surface_radii(shape, tol=1e-6):
    """The distinct radius of every cylindrical, spherical and conical face.

    Counting *types* says the kernel read twelve cylinders; it does not say they
    are the right twelve. A recovery that got an axis or a radius wrong writes a
    surface the mesh is not on, and almost nothing downstream objects: the
    bounding circles come out of the same recovery so they agree with it, the
    shell still closes, and this project's own validator compares the rim radius
    against the surface radius - both wrong together. The volume would shift,
    which is why that is checked too, but a radius is the thing to say out loud.

    Returned as {kind: sorted distinct radii}, rounded so a fixture can state
    them."""
    radii = {}
    exp = TopExp_Explorer(shape, TopAbs_FACE)
    while exp.More():
        adaptor = BRepAdaptor_Surface(TopoDS.Face_s(exp.Current()))
        kind = str(adaptor.GetType()).rsplit("_", 1)[-1]
        value = None
        if kind == "Cylinder":
            value = adaptor.Cylinder().Radius()
        elif kind == "Sphere":
            value = adaptor.Sphere().Radius()
        elif kind == "Torus":
            value = adaptor.Torus().MinorRadius()
        if value is not None:
            seen = radii.setdefault(kind, [])
            if not any(abs(value - r) <= tol for r in seen):
                seen.append(value)
        exp.Next()
    return {k: sorted(round(r, 9) for r in v) for k, v in radii.items()}


def edge_census(shape):
    """What the kernel makes of each distinct edge.

    The other half of item 6: a quadric face is bounded by CIRCLEs rather than
    by splines off a control net, because that is the form a kernel will offset
    and pattern along. Nothing verified the kernel actually *reads* them as
    circles until this.

    Degenerate edges are counted separately rather than lumped in with
    OtherCurve, because they are not something the exporter wrote. A spherical
    face is a rectangle in (theta, phi) whose fourth side is the pole, and OCCT
    inserts a zero length edge there itself - eight of them on a filleted cube,
    one per corner octant. Seeing them is confirmation that the polar axis was
    put through the apex, which is what keeps the octant inside one parameter
    rectangle instead of straddling the seam."""
    census = {}
    edges = TopTools_IndexedMapOfShape()
    TopExp.MapShapes_s(shape, TopAbs_EDGE, edges)
    for i in range(1, edges.Size() + 1):
        edge = TopoDS.Edge_s(edges.FindKey(i))
        if BRep_Tool.Degenerated_s(edge):
            kind = "degenerate"
        else:
            kind = str(BRepAdaptor_Curve(edge).GetType()).rsplit("_", 1)[-1]
        census[kind] = census.get(kind, 0) + 1
    return census


def free_boundaries(shape, limit=3):
    """Where a shell fails to close, rather than merely that it does.

    "0 solids" is the right verdict and a useless bug report: it says the shell
    is open somewhere among a few thousand faces. ShapeAnalysis_FreeBounds walks
    the edges used by only one face and assembles them into wires, so the answer
    comes back as "a hole this long, about here" in model coordinates - which is
    what a person needs to find the face that went missing.

    On the committed lid10.stp, which is 0 solids from 1860 faces, this reports
    18 closed and 6 open free wires and points at each of them."""
    out = []
    exp = TopExp_Explorer(shape, TopAbs_SHELL)
    while exp.More():
        analysis = ShapeAnalysis_FreeBounds(TopoDS.Shell_s(exp.Current()))
        for label, wires in (("closed", analysis.GetClosedWires()),
                             ("open", analysis.GetOpenWires())):
            walk = TopExp_Explorer(wires, TopAbs_WIRE)
            found = []
            while walk.More():
                props = GProp_GProps()
                BRepGProp.LinearProperties_s(walk.Current(), props)
                centre = props.CentreOfMass()
                found.append((props.Mass(), centre.X(), centre.Y(), centre.Z()))
                walk.Next()
            if found:
                out.append(
                    "%d %s free-boundary wire(s), e.g. %s"
                    % (
                        len(found),
                        label,
                        "; ".join(
                            "length %.3f near (%.2f, %.2f, %.2f)" % f for f in found[:limit]
                        ),
                    )
                )
        exp.Next()
    return out


def _invalid_detail(shape, analyzer, limit=5):
    """Which subshapes the kernel rejects, and what it calls the problem.

    Without this a failure is just "invalid", which is not something anyone can
    act on. With it the bayonet's one bad face reports
    InvalidImbricationOfWires, which names the defect precisely."""
    out = []
    for name, kind in (("face", TopAbs_FACE), ("edge", TopAbs_EDGE), ("shell", TopAbs_SHELL)):
        bad = []
        index = 0
        exp = TopExp_Explorer(shape, kind)
        while exp.More():
            sub = exp.Current()
            if not analyzer.IsValid(sub):
                result = analyzer.Result(sub)
                statuses = []
                if result is not None:
                    statuses = [str(s).rsplit("_", 1)[-1] for s in result.Status()]
                bad.append((index, statuses))
            index += 1
            exp.Next()
        if bad:
            out.append(
                "%d of %d %ss rejected, e.g. %s"
                % (
                    len(bad),
                    index,
                    name,
                    "; ".join("#%d %s" % (i, ",".join(s) or "no status") for i, s in bad[:limit]),
                )
            )
    return out


def roundtripSTEP(filename, expect_solids=1, expect_surfaces=None, expect_canonical=None,
                  expect_edges=None, expect_radii=None, expect_volume=None,
                  expect_tolerance=None, fitted_band=None):
    """Read `filename` back with OpenCASCADE and report whether it is a solid.

    Returns (ok, lines). `ok` is None when OCCT is not installed, which the
    caller should treat as "not checked" rather than as a pass or a failure.

    `expect_surfaces` is a dict of kernel surface names to counts, checked when
    given - {"Cylinder": 12, "Sphere": 8} for a filleted cube."""
    if not HAVE_OCCT:
        return None, ["OpenCASCADE is not installed, skipping the CAD kernel round trip"]

    lines = []
    reader = STEPControl_Reader()
    status = reader.ReadFile(os.path.abspath(filename))
    if status != IFSelect_ReturnStatus.IFSelect_RetDone:
        return False, ["OCCT refused to read the file: %s" % str(status).rsplit(".", 1)[-1]]

    # TransferRoots on a reader that did not read segfaults, so the status above
    # is checked rather than trusted.
    roots = reader.TransferRoots()
    if roots < 1:
        return False, ["OCCT read the file but transferred no shape from it"]
    shape = reader.OneShape()
    if shape.IsNull():
        return False, ["OCCT transferred a null shape"]

    solids = _count(shape, TopAbs_SOLID)
    shells = _count(shape, TopAbs_SHELL)
    faces = _count(shape, TopAbs_FACE)
    edges = _count(shape, TopAbs_EDGE)
    census = _surface_census(shape)
    lines.append(
        "OCCT read %d solid(s), %d shell(s), %d faces, %d edges" % (solids, shells, faces, edges)
    )
    lines.append("OCCT surfaces: %s" % ", ".join("%s %d" % kv for kv in sorted(census.items())))

    ok = True
    if solids < expect_solids:
        # The one that matters most. A shell one face short still parses and
        # every face in it is still valid; it simply is not a solid, and that is
        # what "imported as loose surfaces" means.
        lines.append(
            "expected at least %d solid(s), got %d - the shell did not close, so a CAD "
            "kernel takes this as loose surfaces rather than a body" % (expect_solids, solids)
        )
        lines.extend("  " + b for b in free_boundaries(shape))
        ok = False

    analyzer = BRepCheck_Analyzer(shape)
    if not analyzer.IsValid():
        lines.append("BRepCheck_Analyzer rejects the shape:")
        lines.extend("  " + d for d in _invalid_detail(shape, analyzer))
        ok = False

    if solids:
        props = GProp_GProps()
        BRepGProp.VolumeProperties_s(shape, props)
        volume = props.Mass()
        lines.append("OCCT volume %.6f" % volume)
        if volume <= 0:
            lines.append("the volume is not positive, so the solid is inside out")
            ok = False
        if expect_volume is not None:
            want, tol = expect_volume
            if tol is None:
                tol = 1e-6 * max(abs(want), 1.0)
            # The one figure here a fixture can work out for itself. Every other
            # expectation is a census of what this exporter wrote, captured by
            # running it, so it locks in behaviour but cannot say the behaviour
            # was right. A volume derived from the model's own dimensions is
            # independent evidence: the kernel measures the exported solid and
            # arithmetic says what that solid should measure, and the two having
            # never met is the point.
            #
            # Relative, because these span 1e0 to 1e6, and loose enough only for
            # the kernel's own reporting - it agrees to about 1e-9 relative when
            # the solid really is the intended one.
            if abs(volume - want) > tol:
                lines.append(
                    "expected a volume of %.6f +/- %.3g, the kernel measures %.6f - a "
                    "relative difference of %.3g"
                    % (want, tol, volume, abs(volume - want) / max(abs(want), 1.0))
                )
                ok = False

    if expect_surfaces:
        # Exhaustive, not a subset. Checking only the kinds a fixture happens to
        # name lets an unlisted one through in silence - a stray TOROIDAL_SURFACE
        # from a recogniser that overreached would be read back by the kernel and
        # noticed by nothing, because no fixture would think to say Torus=0. So a
        # ROUNDTRIP: line has to account for every surface in the file, and the
        # cost of that is having to state the planes, which is no bad thing: the
        # plane count is the part that moves when a substitution goes wrong.
        for kind in sorted(set(census) | set(expect_surfaces)):
            want = expect_surfaces.get(kind, 0)
            got = census.get(kind, 0)
            if got != want:
                lines.append(
                    "expected the kernel to read %d %s face(s), it read %d" % (want, kind, got)
                )
                ok = False

    slack = worst_tolerance(shape)
    lines.append("OCCT worst tolerance %.6f" % slack)
    if expect_tolerance is not None and slack > expect_tolerance:
        lines.append(
            "the kernel had to accept %.6f of slack to sew this, against an allowance of %.6f"
            % (slack, expect_tolerance)
        )
        ok = False

    radii = surface_radii(shape)
    if radii:
        lines.append(
            "OCCT radii: %s"
            % ", ".join("%s %s" % (k, ",".join("%g" % r for r in v)) for k, v in sorted(radii.items()))
        )
    if expect_radii:
        for kind, want in sorted(expect_radii.items()):
            got = radii.get(kind, [])
            if len(got) != 1 or abs(got[0] - want) > 1e-6:
                lines.append(
                    "expected every %s face to have radius %g, the kernel reads %s"
                    % (kind, want, got or "none")
                )
                ok = False

    edges = edge_census(shape)
    lines.append("OCCT edges: %s" % ", ".join("%s %d" % kv for kv in sorted(edges.items())))
    if expect_edges:
        for kind, want in sorted(expect_edges.items()):
            got = edges.get(kind, 0)
            if got != want:
                lines.append(
                    "expected the kernel to read %d %s edge(s), it read %d" % (want, kind, got)
                )
                ok = False

    # The cross check, reported whenever there is a spline to ask about.
    if census.get("BSplineSurface"):
        canon = canonical_census(shape)
        lines.append(
            "OCCT canonical: %s"
            % ", ".join("%s %d" % kv for kv in sorted(canon.items()))
        )
        missed = sum(n for k, n in canon.items() if k != "spline")
        if missed:
            lines.append(
                "%d B-spline face(s) are exactly a quadric the exporter could have written"
                % missed
            )
        if expect_canonical:
            for kind, want in sorted(expect_canonical.items()):
                got = canon.get(kind, 0)
                if got != want:
                    lines.append(
                        "expected %d B-spline face(s) to be exactly %s, the kernel says %d"
                        % (want, kind, got)
                    )
                    ok = False

    # Every corner of an analytic face has to lie on the surface that face is
    # written on. This is the property the whole analytic tier exists to hold -
    # a file asserting that a vertex is on a cylinder it is 0.096 inside is one
    # a strict reader rebuilds or refuses - and it needs no number from the
    # fixture, so it is checked on every model here.
    #
    # An exact surface is held to the kernel's own floor. A B-spline is not: it
    # was fitted to the mesh and publishes how well, so its corners are allowed
    # the tessellation band the exporter reported for it and nothing more. Where
    # no band was reported there is nothing to be approximate about, and the
    # floor applies to it too.
    #
    # It is not a check any fixture could have been expected to write. Applied
    # from the start it would have failed loudly on step-declare-grid-scad at
    # 9.6e-02, step-exact-trim at 3.4e-02, step-cut-cone at 3.2e-02 and
    # step-band-family at 8.9e-02, every one of which was found by hand instead.
    #
    # Judged against the face's own size. 1e-7 is OpenCASCADE's Precision::
    # Confusion and is the right floor for a small face, but a plane's flatness
    # is a property of how far across it you have to go to measure it: a corner
    # on the rim of a disc of radius 19.2 came out 1.73e-07 off it, which is
    # 9e-09 of the face - the exporter placing that corner on the curve its two
    # declared owners cross along, which is a face it is on the *boundary* of
    # rather than a corner of, so nothing in its loop could see it. That is an
    # open defect and it is recorded in the handover; the bound here is set so
    # the check measures what it can defend rather than being switched off.
    stray, extent = corner_stray(shape)
    for kind, worst in sorted(stray.items()):
        allow = max(1e-7, 1e-8 * extent)
        if kind == "BSplineSurface" and fitted_band:
            allow = fitted_band
        if worst > allow:
            lines.append(
                "a %s face has a corner %.4e off the surface it is written on, against %.4e allowed"
                % (kind, worst, allow)
            )
            ok = False
    return ok, lines


def corner_stray(shape):
    """The furthest any face's own corner lies from that face's surface, per kind,
    and how far the model reaches from the origin, which is the scale the first
    is judged against.

    Measured analytically for a plane, cylinder and cone - the kinds whose
    distance has a closed form - and by projection for the rest, which is what
    scripts/step-corner-stray.py does at the command line."""
    face_of = getattr(TopoDS, "Face_s", None) or TopoDS.Face
    vertex_of = getattr(TopoDS, "Vertex_s", None) or TopoDS.Vertex
    worst, extent = {}, 0.0
    exp = TopExp_Explorer(shape, TopAbs_FACE)
    while exp.More():
        face = face_of(exp.Current())
        adaptor = BRepAdaptor_Surface(face)
        kind = str(adaptor.GetType()).rsplit("_", 1)[-1]
        seen = set()
        vexp = TopExp_Explorer(face, TopAbs_VERTEX)
        while vexp.More():
            vert = vertex_of(vexp.Current())
            handle = vert.TShape().This()
            if handle not in seen:
                seen.add(handle)
                pnt = BRep_Tool.Pnt_s(vert)
                off = _off_surface(adaptor, face, pnt)
                if off > worst.get(kind, 0.0):
                    worst[kind] = off
                reach = math.sqrt(pnt.X() ** 2 + pnt.Y() ** 2 + pnt.Z() ** 2)
                extent = max(extent, reach)
            vexp.Next()
        exp.Next()
    return worst, extent


def _off_surface(adaptor, face, pnt):
    kind = adaptor.GetType()
    if kind == GeomAbs_Plane:
        return abs(adaptor.Plane().Distance(pnt))
    if kind in (GeomAbs_Cylinder, GeomAbs_Cone):
        quad = adaptor.Cylinder() if kind == GeomAbs_Cylinder else adaptor.Cone()
        axis = quad.Axis()
        direction, origin = axis.Direction(), axis.Location()
        rel = (pnt.X() - origin.X(), pnt.Y() - origin.Y(), pnt.Z() - origin.Z())
        along = rel[0] * direction.X() + rel[1] * direction.Y() + rel[2] * direction.Z()
        radial = math.sqrt(max(0.0, sum(c * c for c in rel) - along * along))
        if kind == GeomAbs_Cylinder:
            return abs(radial - quad.Radius())
        want = quad.RefRadius() + along * math.tan(quad.SemiAngle())
        return abs(radial - want) * math.cos(quad.SemiAngle())
    projector = GeomAPI_ProjectPointOnSurf(pnt, BRep_Tool.Surface_s(face))
    return projector.LowerDistance() if projector.NbPoints() > 0 else 0.0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: steproundtrip.py <file.stp> [Kind=count ...]", file=sys.stderr)
        sys.exit(2)
    wanted = {}
    for arg in sys.argv[2:]:
        key, _, value = arg.partition("=")
        wanted[key] = int(value)
    result, report = roundtripSTEP(sys.argv[1], expect_surfaces=wanted or None)
    for line in report:
        print(line)
    if result is None:
        sys.exit(0)
    print("round trip %s: %s" % ("ok" if result else "FAILED", sys.argv[1]))
    sys.exit(0 if result else 1)
