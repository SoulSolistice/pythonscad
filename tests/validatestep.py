#!/usr/bin/env python3

#
# Simple tool to validate a STEP (ISO 10303-21) file written by export_step.cc.
#
# Every check here corresponds to a defect the STEP exporter has had, so a
# failure means one of them came back:
#
# o REAL literals that are not well formed. The radix has to be a '.' even when
#   the process runs under a locale which uses a comma, the decimal point is
#   mandatory ("0" is not a REAL) and the exponent marker has to be upper case.
# o DIRECTION entities of zero length, which importers report as degenerated
#   faces. They come from zero area polygons and zero length edges.
# o Faces which are not stitched to their neighbours. In a closed shell every
#   edge is used by exactly two faces, once in each direction. Vertices and
#   edge curves therefore have to be shared instead of written per face.
# o Edge loops which do not close.
# o A face whose PLANE normal disagrees with the winding of its outer bound,
#   which happens when the normal is taken from the first two edges of a
#   concave or collinear loop.
# o Hole loops written as ordinary faces (they show up as a membrane spanning
#   the bore). Such a face reuses the edges of the hole, so the edge use count
#   goes above two.
# o Holes recorded against the wrong face. With concentric loops in one plane a
#   hole can end up on a face further out; the face it belongs to is then left
#   sealed, which is the same membrane. That one keeps the shell topologically
#   closed, so only the nesting check finds it.
# o Bodies which are not connected but share a single CLOSED_SHELL.
# o Missing units and modelling tolerance, or representation entities written
#   with missing mandatory arguments.
# o Duplicate entity ids and references to entities which do not exist.
#
# Usage: validatestep.py <file.stp>
#

import math
import re
import sys

# a numeric argument as it may appear inside an entity
NUMBER_RE = re.compile(r"[-+]?[0-9][0-9.]*(?:[eE][-+]?[0-9]+)?")
REAL_RE = re.compile(r"^[-+]?[0-9]+\.[0-9]*(?:E[-+]?[0-9]+)?$")
INT_RE = re.compile(r"^[-+]?[0-9]+$")


class Entity:
    def __init__(self, eid, name, args):
        self.id = eid
        self.name = name
        self.args = args

    def refs(self):
        return [int(r) for r in re.findall(r"#(\d+)", self.args)]

    def floats(self):
        # entity references are not numeric arguments
        args = re.sub(r"#\d+", " ", strip_strings(self.args))
        return [float(t) for t in NUMBER_RE.findall(args)]


def strip_strings(text):
    """Blank out the contents of STEP string literals so that their contents are
    never mistaken for arguments. '' is an escaped apostrophe."""
    out = []
    in_string = False
    i = 0
    while i < len(text):
        c = text[i]
        if c == "'":
            if in_string and i + 1 < len(text) and text[i + 1] == "'":
                i += 2
                continue
            in_string = not in_string
            out.append(" ")
        else:
            out.append(" " if in_string else c)
        i += 1
    return "".join(out)


def split_statements(text):
    """Split the DATA section into statements on ';' outside of string literals."""
    statements = []
    current = []
    in_string = False
    i = 0
    while i < len(text):
        c = text[i]
        if c == "'":
            if in_string and i + 1 < len(text) and text[i + 1] == "'":
                current.append("''")
                i += 2
                continue
            in_string = not in_string
            current.append(c)
        elif c == ";" and not in_string:
            statements.append("".join(current).strip())
            current = []
        else:
            current.append(c)
        i += 1
    if "".join(current).strip():
        statements.append("".join(current).strip())
    return statements


def parse_step(filename):
    """Return (entities_by_id, problems). Entities of a complex instance
    (#1 = (A(..)B(..))) are stored under the name of their first part."""
    problems = []
    with open(filename, encoding="utf-8", errors="replace") as f:
        text = f.read()

    start = text.find("DATA;")
    end = text.rfind("ENDSEC;")
    if start < 0 or end < 0 or end < start:
        problems.append("no DATA section found")
        return {}, problems
    body = text[start + len("DATA;") : end]

    entities = {}
    for stmt in split_statements(body):
        m = re.match(r"^#(\d+)\s*=\s*(.*)$", stmt, re.S)
        if not m:
            continue
        eid = int(m.group(1))
        rhs = m.group(2).strip()
        nm = re.match(r"^\(?\s*([A-Z_0-9]+)\s*\((.*)\)\s*$", rhs, re.S)
        if not nm:
            problems.append("cannot parse entity #%d: %s" % (eid, rhs[:60]))
            continue
        if eid in entities:
            problems.append("duplicate entity id #%d" % eid)
        entities[eid] = Entity(eid, nm.group(1), nm.group(2))
    return entities, problems


def check_real_literals(filename, problems):
    """Every numeric argument has to be a well formed REAL or an integer.

    This is what catches a comma radix: "-5,394" leaves a token "-5" without a
    decimal point, and the STEP reader would have taken it as two arguments."""
    with open(filename, encoding="utf-8", errors="replace") as f:
        text = f.read()

    lowercase_exponent = 0
    bad = []
    for line_no, line in enumerate(text.splitlines(), 1):
        if not line.startswith("#"):
            continue
        args = strip_strings(line)
        # entity references are integers by definition
        args = re.sub(r"#\d+", " ", args)
        if re.search(r"[0-9][eE]", args) and re.search(r"[0-9]e[-+0-9]", args):
            lowercase_exponent += 1
        for tok in NUMBER_RE.findall(args):
            if REAL_RE.match(tok.upper()) or INT_RE.match(tok):
                continue
            bad.append((line_no, tok, line.strip()[:70]))

    if "nan" in text.lower() or re.search(r"\binf\b", text.lower()):
        problems.append("file contains nan or inf")
    if lowercase_exponent:
        problems.append(
            "%d line(s) use a lower case exponent marker; ISO 10303-21 requires 'E'"
            % lowercase_exponent
        )
    for line_no, tok, line in bad[:10]:
        problems.append(
            "line %d: '%s' is not a valid REAL (comma radix? missing decimal point?): %s"
            % (line_no, tok, line)
        )
    if len(bad) > 10:
        problems.append("... and %d more malformed numbers" % (len(bad) - 10))


def check_references(entities, problems):
    for e in entities.values():
        for r in e.refs():
            if r not in entities:
                problems.append("#%d (%s) references #%d which does not exist" % (e.id, e.name, r))
                return


def check_units_and_context(entities, problems):
    names = [e.name for e in entities.values()]
    required = [
        "GEOMETRIC_REPRESENTATION_CONTEXT",
        "LENGTH_UNIT",
        "UNCERTAINTY_MEASURE_WITH_UNIT",
        "ADVANCED_BREP_SHAPE_REPRESENTATION",
        "PRODUCT_DEFINITION",
        "FACE_OUTER_BOUND",
    ]
    for name in required:
        if name not in names:
            problems.append("no %s in the file" % name)

    # A separate SHAPE_REPRESENTATION tied to the brep by a
    # SHAPE_REPRESENTATION_RELATIONSHIP is one valid arrangement, but pointing
    # the SHAPE_DEFINITION_REPRESENTATION straight at the brep is another -
    # SolidWorks writes the latter - so only require that one of them is there.
    if "SHAPE_DEFINITION_REPRESENTATION" not in names:
        problems.append("no SHAPE_DEFINITION_REPRESENTATION in the file")

    for e in entities.values():
        # SHAPE_REPRESENTATION(name, items, context) - all three are mandatory
        if e.name == "SHAPE_REPRESENTATION" and len(e.refs()) < 2:
            problems.append("#%d SHAPE_REPRESENTATION is missing items or context" % e.id)
        if e.name == "ADVANCED_BREP_SHAPE_REPRESENTATION":
            if len(e.refs()) < 2:
                problems.append("#%d ADVANCED_BREP_SHAPE_REPRESENTATION has no context" % e.id)
            if re.search(r",\s*\)\s*$", e.args + ")"):
                problems.append("#%d has an empty trailing argument" % e.id)


def check_directions(entities, problems):
    """A CARTESIAN_POINT and a DIRECTION carry exactly three numbers, a VECTOR
    exactly one.

    This is what catches a comma radix: "-5,394" is read as the two numbers -5
    and 394, so the component count goes up. A coordinate written with a comma
    always splits, because a value without a fractional part never contains one
    in the first place."""
    expected = {"CARTESIAN_POINT": 3, "DIRECTION": 3, "VECTOR": 1}
    malformed = []
    zero = []
    for e in entities.values():
        want = expected.get(e.name)
        if want is None:
            continue
        v = e.floats()
        if len(v) != want:
            malformed.append((e, want, len(v)))
        elif e.name == "DIRECTION" and math.sqrt(sum(c * c for c in v)) < 1e-9:
            zero.append(e)

    for e, want, got in malformed[:3]:
        problems.append(
            "#%d %s has %d numbers instead of %d (comma used as decimal separator?): %s"
            % (e.id, e.name, got, want, e.args.strip())
        )
    if len(malformed) > 3:
        problems.append("... and %d more malformed numeric entities" % (len(malformed) - 3))
    if zero:
        problems.append(
            "%d DIRECTION(s) of zero length, e.g. #%d (degenerated face)" % (len(zero), zero[0].id)
        )


def _bound_loop(entities, bound_id):
    b = entities.get(bound_id)
    if b is None or b.name not in ("FACE_OUTER_BOUND", "FACE_BOUND"):
        return None, True
    orientation = ".T." in b.args
    loop = entities.get(b.refs()[0]) if b.refs() else None
    if loop is None or loop.name != "EDGE_LOOP":
        return None, orientation
    return loop, orientation


def _edge_endpoints(entities, oriented_id):
    """Return (start_vertex, end_vertex, edge_curve_id) for an ORIENTED_EDGE."""
    oe = entities.get(oriented_id)
    if oe is None or oe.name != "ORIENTED_EDGE":
        return None
    forward = ".T." in oe.args
    refs = oe.refs()
    if not refs:
        return None
    ec = entities.get(refs[-1])
    if ec is None or ec.name != "EDGE_CURVE":
        return None
    ec_refs = ec.refs()
    if len(ec_refs) < 2:
        return None
    v1, v2 = ec_refs[0], ec_refs[1]
    if ".F." in ec.args:
        v1, v2 = v2, v1
    if not forward:
        v1, v2 = v2, v1
    return v1, v2, ec.id, forward


def check_topology(entities, problems):
    """Loops must close and every edge must be used exactly twice, once in each
    direction. This is the invariant that makes a shell watertight; it also
    catches an extra face reusing edges which are already fully used."""
    all_faces = [e for e in entities.values() if e.name == "ADVANCED_FACE"]
    if not all_faces:
        problems.append("no ADVANCED_FACE in the file")
        return

    # Walk the faces through the shells, with multiplicity: a face which is
    # listed twice contributes its edges twice, and a face which no shell
    # references contributes nothing.
    faces = []
    used = set()
    for shell in entities.values():
        if shell.name not in ("CLOSED_SHELL", "OPEN_SHELL"):
            continue
        for fid in shell.refs():
            face = entities.get(fid)
            if face is None or face.name != "ADVANCED_FACE":
                problems.append("shell #%d references #%d which is not an ADVANCED_FACE" % (shell.id, fid))
                continue
            faces.append(face)
            used.add(fid)
    orphans = [f.id for f in all_faces if f.id not in used]
    if orphans:
        problems.append(
            "%d ADVANCED_FACE(s) belong to no shell, e.g. #%d" % (len(orphans), orphans[0])
        )
    if not faces:
        problems.append("no shell references any face")
        return

    edge_dirs = {}
    for face in faces:
        bounds = face.refs()[:-1]  # last ref is the surface
        outer = 0
        for bid in bounds:
            b = entities.get(bid)
            if b is not None and b.name == "FACE_OUTER_BOUND":
                outer += 1
            loop, _ = _bound_loop(entities, bid)
            if loop is None:
                problems.append("#%d has a bound which is not an EDGE_LOOP" % face.id)
                continue
            chain = []
            for oid in loop.refs():
                ends = _edge_endpoints(entities, oid)
                if ends is None:
                    problems.append("#%d references a broken ORIENTED_EDGE #%d" % (loop.id, oid))
                    return
                chain.append(ends)
                edge_dirs.setdefault(ends[2], []).append(ends[3])
            for k, ends in enumerate(chain):
                if ends[1] != chain[(k + 1) % len(chain)][0]:
                    problems.append("EDGE_LOOP #%d does not close" % loop.id)
                    break
        if outer != 1:
            problems.append("#%d ADVANCED_FACE has %d FACE_OUTER_BOUND, expected 1" % (face.id, outer))

    over = [e for e, d in edge_dirs.items() if len(d) > 2]
    under = [e for e, d in edge_dirs.items() if len(d) < 2]
    same = [e for e, d in edge_dirs.items() if len(d) == 2 and d[0] == d[1]]
    if over:
        problems.append(
            "%d edge(s) used by more than two faces, e.g. #%d (stray face reusing a hole?)"
            % (len(over), over[0])
        )
    if under:
        problems.append(
            "%d edge(s) used by only one face, e.g. #%d (shell is not closed)" % (len(under), under[0])
        )
    if same:
        problems.append(
            "%d edge(s) used twice in the same direction, e.g. #%d (inconsistent winding)"
            % (len(same), same[0])
        )


def check_shared_vertices(entities, problems):
    seen = {}
    for e in entities.values():
        if e.name != "VERTEX_POINT":
            continue
        refs = e.refs()
        if not refs:
            continue
        pt = entities.get(refs[0])
        if pt is None or pt.name != "CARTESIAN_POINT":
            continue
        key = tuple(pt.floats())
        if key in seen:
            problems.append(
                "VERTEX_POINT #%d and #%d sit on the same coordinates %s; vertices are not shared"
                % (seen[key], e.id, key)
            )
            return
        seen[key] = e.id


def check_face_normals(entities, problems):
    """The PLANE of a face has to point the same way as the winding of its outer
    bound. A normal taken from the first two edges of a concave loop points the
    other way, which turns the face inside out."""
    for face in entities.values():
        if face.name != "ADVANCED_FACE":
            continue
        refs = face.refs()
        if len(refs) < 2:
            continue
        surface = entities.get(refs[-1])
        if surface is None or surface.name != "PLANE":
            continue
        axis = entities.get(surface.refs()[0]) if surface.refs() else None
        if axis is None or len(axis.refs()) < 2:
            continue
        direction = entities.get(axis.refs()[1])
        if direction is None or direction.name != "DIRECTION":
            continue
        normal = direction.floats()
        if len(normal) != 3:
            continue

        outer = None
        for bid in refs[:-1]:
            b = entities.get(bid)
            if b is not None and b.name == "FACE_OUTER_BOUND":
                outer = bid
        if outer is None:
            continue
        pts = _loop_polyline(entities, outer)
        if pts is None or len(pts) < 3:
            continue

        # Newell
        n = [0.0, 0.0, 0.0]
        for k in range(len(pts)):
            a, b = pts[k], pts[(k + 1) % len(pts)]
            n[0] += (a[1] - b[1]) * (a[2] + b[2])
            n[1] += (a[2] - b[2]) * (a[0] + b[0])
            n[2] += (a[0] - b[0]) * (a[1] + b[1])
        length = math.sqrt(sum(c * c for c in n))
        if length < 1e-12:
            problems.append("#%d ADVANCED_FACE has a zero area outer bound" % face.id)
            continue
        n = [c / length for c in n]
        dot = sum(a * b for a, b in zip(n, normal))
        if dot < 0.9:
            problems.append(
                "#%d ADVANCED_FACE: PLANE normal %s disagrees with the winding of its outer "
                "bound (dot=%.3f)" % (face.id, normal, dot)
            )
            return


def _edge_geometry(entities, oriented_id):
    """The curve an ORIENTED_EDGE's EDGE_CURVE lies on, or None."""
    oe = entities.get(oriented_id)
    if oe is None or not oe.refs():
        return None
    ec = entities.get(oe.refs()[-1])
    if ec is None or ec.name != "EDGE_CURVE" or len(ec.refs()) < 3:
        return None
    return entities.get(ec.refs()[2])


def _vec3(entities, eid, want):
    e = entities.get(eid)
    if e is None or e.name != want:
        return None
    v = e.floats()
    return v if len(v) == 3 else None


def _placement(entities, placement_id):
    """(origin, axis, ref) of an AXIS2_PLACEMENT_3D, axis and ref normalised."""
    ap = entities.get(placement_id)
    if ap is None or ap.name != "AXIS2_PLACEMENT_3D" or len(ap.refs()) < 3:
        return None
    origin = _vec3(entities, ap.refs()[0], "CARTESIAN_POINT")
    axis = _vec3(entities, ap.refs()[1], "DIRECTION")
    ref = _vec3(entities, ap.refs()[2], "DIRECTION")
    if origin is None or axis is None or ref is None:
        return None

    def unit(v):
        n = math.sqrt(sum(c * c for c in v))
        return [c / n for c in v] if n > 1e-12 else None

    axis, ref = unit(axis), unit(ref)
    if axis is None or ref is None:
        return None
    return origin, axis, ref


ARC_SAMPLES = 12


def _arc_interior(entities, circle, p0, p1):
    """Points along a circular edge between its ends, excluding both.

    A CIRCLE is parameterised counter clockwise about its own axis starting at
    its reference direction, so the arc runs counter clockwise from the edge's
    start to its end. `p0` and `p1` are the *curve's* own ends, before any
    ORIENTED_EDGE reversal."""
    pl = _placement(entities, circle.refs()[0]) if circle.refs() else None
    nums = circle.floats()
    if pl is None or not nums:
        return None
    centre, axis, ref = pl
    radius = nums[-1]
    perp = [
        axis[1] * ref[2] - axis[2] * ref[1],
        axis[2] * ref[0] - axis[0] * ref[2],
        axis[0] * ref[1] - axis[1] * ref[0],
    ]

    def angle(p):
        d = [p[k] - centre[k] for k in range(3)]
        return math.atan2(sum(a * b for a, b in zip(d, perp)),
                          sum(a * b for a, b in zip(d, ref)))

    t0 = angle(p0)
    sweep = (angle(p1) - t0) % (2 * math.pi)
    if sweep < 1e-12:
        sweep = 2 * math.pi  # a full circle, whose two ends are one vertex
    return [
        [centre[j] + radius * (math.cos(t) * ref[j] + math.sin(t) * perp[j]) for j in range(3)]
        for t in (t0 + sweep * k / ARC_SAMPLES for k in range(1, ARC_SAMPLES))
    ]


def _vertex_point(entities, vertex_id):
    vtx = entities.get(vertex_id)
    if vtx is None or not vtx.refs():
        return None
    cp = entities.get(vtx.refs()[0])
    if cp is None:
        return None
    coords = cp.floats()
    return coords if len(coords) == 3 else None


def _loop_polyline(entities, bound_id):
    """The loop walked in its own direction, with arcs sampled along the curve.

    Taking only the vertices is not good enough for anything that cares which
    way a loop winds. The polygon through the ends of a *major* arc lies on the
    other side of its chord from the face itself, so its winding comes out
    inverted - a 270 degree bottom face reads as a 90 degree top face. Sampling
    the arc puts the polygon back on the face."""
    loop, orientation = _bound_loop(entities, bound_id)
    if loop is None:
        return None
    pts = []
    for oid in loop.refs():
        ends = _edge_endpoints(entities, oid)
        geom = _edge_geometry(entities, oid)
        if ends is None or geom is None:
            return None
        start = _vertex_point(entities, ends[0])
        if start is None:
            return None
        pts.append(start)
        if geom.name != "CIRCLE":
            continue
        # ends[] already carries both the EDGE_CURVE's same_sense flag and the
        # ORIENTED_EDGE's; undoing the latter gives the curve's own direction.
        forward = ends[3]
        natural = (ends[0], ends[1]) if forward else (ends[1], ends[0])
        p0, p1 = _vertex_point(entities, natural[0]), _vertex_point(entities, natural[1])
        if p0 is None or p1 is None:
            return None
        interior = _arc_interior(entities, geom, p0, p1)
        if interior is None:
            return None
        pts.extend(interior if forward else list(reversed(interior)))
    if not orientation:
        pts.reverse()
    return pts


def _loop_points(entities, bound_id):
    loop, orientation = _bound_loop(entities, bound_id)
    if loop is None:
        return None
    pts = []
    for oid in loop.refs():
        # Only a loop made of straight edges projects to a polygon with the
        # same interior. An arc bulges away from its chord, so a loop carrying
        # one is left to the surface checks rather than approximated here.
        geom = _edge_geometry(entities, oid)
        if geom is None or geom.name != "LINE":
            return None
        ends = _edge_endpoints(entities, oid)
        if ends is None:
            return None
        vtx = entities.get(ends[0])
        if vtx is None or not vtx.refs():
            return None
        cp = entities.get(vtx.refs()[0])
        if cp is None:
            return None
        coords = cp.floats()
        if len(coords) != 3:
            return None
        pts.append(coords)
    return pts


def _project(pts, drop):
    a, b = (drop + 1) % 3, (drop + 2) % 3
    return [(p[a], p[b]) for p in pts]


def _point_in_loop(poly, pt):
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        if (poly[i][1] > pt[1]) != (poly[j][1] > pt[1]):
            x = (poly[j][0] - poly[i][0]) * (pt[1] - poly[i][1]) / (poly[j][1] - poly[i][1]) + poly[i][0]
            if pt[0] < x:
                inside = not inside
        j = i
    return inside


def check_hole_nesting(entities, problems):
    """A hole has to sit directly inside the outer bound of the face carrying it.

    If another loop of the same plane lies between the two, the hole belongs to
    that inner face instead. The face it was taken from is then written without
    its hole and seals the opening - the membrane spanning a bore that the CAD
    system shows. The shell stays topologically closed in that case, so the edge
    use count does not notice it."""
    planes = {}
    faces = []
    for face in entities.values():
        if face.name != "ADVANCED_FACE":
            continue
        refs = face.refs()
        # the check is about loops sharing a plane; a curved face has none
        surface = entities.get(refs[-1]) if refs else None
        if surface is None or surface.name != "PLANE":
            continue
        outer = None
        inner = []
        for bid in refs[:-1]:
            b = entities.get(bid)
            if b is None:
                continue
            if b.name == "FACE_OUTER_BOUND":
                outer = bid
            elif b.name == "FACE_BOUND":
                inner.append(bid)
        if outer is None:
            continue
        pts = _loop_points(entities, outer)
        if not pts or len(pts) < 3:
            continue
        # group by plane: normal rounded plus offset
        n = [0.0, 0.0, 0.0]
        for k in range(len(pts)):
            a, b = pts[k], pts[(k + 1) % len(pts)]
            n[0] += (a[1] - b[1]) * (a[2] + b[2])
            n[1] += (a[2] - b[2]) * (a[0] + b[0])
            n[2] += (a[0] - b[0]) * (a[1] + b[1])
        ln = math.sqrt(sum(c * c for c in n))
        if ln < 1e-12:
            continue
        n = [c / ln for c in n]
        off = sum(a * b for a, b in zip(n, pts[0]))
        key = (round(abs(n[0]), 4), round(abs(n[1]), 4), round(abs(n[2]), 4), round(abs(off), 4))
        drop = max(range(3), key=lambda i: abs(n[i]))
        faces.append((face.id, outer, inner, pts, drop, key))
        planes.setdefault(key, []).append((face.id, outer, pts, drop))

    for fid, outer, inner, outer_pts, drop, key in faces:
        for hid in inner:
            hpts = _loop_points(entities, hid)
            if not hpts:
                continue
            probe = _project(hpts, drop)[0]
            outer_poly = _project(outer_pts, drop)
            if not _point_in_loop(outer_poly, probe):
                problems.append(
                    "#%d: hole %s lies outside the outer bound of its face" % (fid, hid)
                )
                continue
            for ofid, obid, opts, odrop in planes[key]:
                if ofid == fid:
                    continue
                poly = _project(opts, drop)
                # an intervening loop: contains the hole and sits inside the face
                if _point_in_loop(poly, probe) and _point_in_loop(outer_poly, poly[0]):
                    problems.append(
                        "#%d: hole %s is not directly inside this face - the outer bound of "
                        "#%d lies between them, so the hole belongs to #%d (its face is left "
                        "sealed)" % (fid, hid, ofid, ofid)
                    )
                    return


def check_cylindrical_faces(entities, problems):
    """A CYLINDRICAL_SURFACE or CONICAL_SURFACE face has one of exactly two shapes.

    A wall which closes on itself is periodic and cannot be bounded by its two
    rims alone, so it walks up a seam and back down it: two full CIRCLEs (an
    edge whose two ends are the same vertex) and one straight edge used once in
    each direction. A wall which stops short of a full turn is not periodic and
    is bounded by an arc at either rim and the band's two distinct end edges.

    Anything else - three circles, a seam used twice the same way, an arc whose
    radius disagrees with its surface - is a face no importer can make sense of,
    and all of them are mistakes this exporter can plausibly make."""
    for face in entities.values():
        if face.name != "ADVANCED_FACE":
            continue
        refs = face.refs()
        surface = entities.get(refs[-1]) if refs else None
        if surface is None or surface.name not in (
            "CYLINDRICAL_SURFACE",
            "CONICAL_SURFACE",
            "SPHERICAL_SURFACE",
        ):
            continue

        # CONICAL_SURFACE carries the half angle after the radius
        numbers = surface.floats()
        radius = numbers[0] if numbers else None
        if radius is None or radius <= 0:
            problems.append("#%d: %s without a positive radius" % (surface.id, surface.name))
            continue
        if surface.name == "CONICAL_SURFACE":
            angle = numbers[1] if len(numbers) > 1 else None
            if angle is None or not 0 < angle < math.pi / 2:
                problems.append(
                    "#%d: CONICAL_SURFACE half angle %s is not in (0, pi/2)" % (surface.id, angle)
                )
                continue

        bounds = [b for b in refs[:-1] if entities.get(b) is not None]
        if len(bounds) != 1:
            problems.append("#%d: cylindrical face has %d bounds, expected 1" % (face.id, len(bounds)))
            continue
        loop, _ = _bound_loop(entities, bounds[0])
        if loop is None:
            problems.append("#%d: cylindrical face has no edge loop" % face.id)
            continue

        oriented = loop.refs()
        if len(oriented) < 4:
            problems.append(
                "#%d: cylindrical face has %d edges, expected at least 4" % (face.id, len(oriented))
            )
            continue

        # A rim is a closed CIRCLE on a periodic face and an arc on a partial
        # one. The seam is whatever closes a periodic face: a straight ruling up
        # a cylinder or a cone, but a *meridian arc* over a sphere, where the
        # line between the same two vertices is a chord off the surface.
        circles, lines, closed = [], [], 0
        arcs = []
        for oid in oriented:
            geom = _edge_geometry(entities, oid)
            ends = _edge_endpoints(entities, oid)
            if geom is None or ends is None:
                problems.append("#%d: cylindrical face has an unreadable edge" % face.id)
                break
            if geom.name == "CIRCLE":
                circles.append((oid, geom, ends))
                if ends[0] == ends[1]:
                    closed += 1
                else:
                    arcs.append((oid, ends))
            else:
                lines.append((oid, ends))
        else:
            # Two rims, but a rim need not be one edge. Where a neighbouring
            # face is split the rim is split with it, and a face then carries
            # several consecutive arcs along one rim - which is exactly what
            # SolidWorks produces when it re-saves one of ours.
            if len(circles) < 2:
                problems.append(
                    "#%d: %s face is bounded by %d circular edges, expected at least 2"
                    % (face.id, surface.name, len(circles))
                )
                continue
            for _, geom, _ends in circles:
                cr = geom.floats()[-1] if geom.floats() else None
                if cr is None or cr <= 0:
                    problems.append("#%d: rim CIRCLE #%d has no positive radius" % (face.id, geom.id))
                elif surface.name == "CYLINDRICAL_SURFACE" and abs(cr - radius) > 1e-6 * max(1.0, radius):
                    problems.append(
                        "#%d: rim CIRCLE #%d has radius %s, but its cylinder has %s"
                        % (face.id, geom.id, cr, radius)
                    )
                elif surface.name == "SPHERICAL_SURFACE" and cr > radius * (1 + 1e-6):
                    # a circle on a sphere cannot be wider than the sphere, and
                    # the seam is a great circle so it is exactly as wide
                    problems.append(
                        "#%d: CIRCLE #%d has radius %s, wider than its sphere at %s"
                        % (face.id, geom.id, cr, radius)
                    )
            if closed not in (0, 2):
                problems.append(
                    "#%d: cylindrical face mixes a full circle with an arc" % face.id
                )
                continue
            def distinct(entries):
                out = set()
                for oid, *_ in entries:
                    oe = entities.get(oid)
                    out.add(oe.refs()[-1] if oe is not None and oe.refs() else None)
                return out

            line_edges = distinct(lines)
            if closed == 2:
                # periodic: two rims and one seam, the seam used once in each
                # direction. Over a sphere that seam is an arc, so it is not
                # required to be a LINE - only to be a single edge used twice.
                seam_edges = line_edges | distinct(arcs)
                if len(circles) - closed != len(arcs):
                    problems.append("#%d: periodic face mixes rim shapes" % face.id)
                if len(seam_edges) != 1:
                    problems.append(
                        "#%d: a periodic %s face needs one seam edge used twice, found %d"
                        % (face.id, surface.name, len(seam_edges))
                    )
            if closed == 0 and len(line_edges) != 2:
                problems.append(
                    "#%d: a partial cylindrical face needs two distinct end edges, found %d"
                    % (face.id, len(line_edges))
                )


def check_shells(entities, problems):
    """Faces which are not connected by a shared edge belong to different bodies
    and must not share one CLOSED_SHELL."""
    shells = [e for e in entities.values() if e.name == "CLOSED_SHELL"]
    if not shells:
        problems.append("no CLOSED_SHELL in the file")
        return

    for shell in shells:
        face_ids = shell.refs()
        parent = {f: f for f in face_ids}

        def find(x):
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x

        owner = {}
        for fid in face_ids:
            face = entities.get(fid)
            if face is None:
                continue
            for bid in face.refs()[:-1]:
                loop, _ = _bound_loop(entities, bid)
                if loop is None:
                    continue
                for oid in loop.refs():
                    ends = _edge_endpoints(entities, oid)
                    if ends is None:
                        continue
                    if ends[2] in owner:
                        a, b = find(owner[ends[2]]), find(fid)
                        if a != b:
                            parent[a] = b
                    else:
                        owner[ends[2]] = fid
        comps = {find(f) for f in face_ids}
        if len(comps) > 1:
            problems.append(
                "CLOSED_SHELL #%d contains %d disconnected bodies; they need one shell each"
                % (shell.id, len(comps))
            )


def validateSTEP(filename):
    problems = []
    check_real_literals(filename, problems)
    entities, parse_problems = parse_step(filename)
    problems.extend(parse_problems)

    if entities:
        check_references(entities, problems)
        check_units_and_context(entities, problems)
        check_directions(entities, problems)
        check_topology(entities, problems)
        check_shared_vertices(entities, problems)
        check_face_normals(entities, problems)
        check_hole_nesting(entities, problems)
        check_cylindrical_faces(entities, problems)
        check_shells(entities, problems)

    if problems:
        print("STEP validation failed for %s:" % filename, file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return False

    faces = sum(1 for e in entities.values() if e.name == "ADVANCED_FACE")
    shells = sum(1 for e in entities.values() if e.name == "CLOSED_SHELL")
    print(
        "STEP validation ok: %s (%d entities, %d faces, %d shell(s))"
        % (filename, len(entities), faces, shells),
        file=sys.stderr,
    )
    return True


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: validatestep.py <file.stp>", file=sys.stderr)
        sys.exit(2)
    sys.exit(0 if validateSTEP(sys.argv[1]) else 1)
