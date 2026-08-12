#!/usr/bin/env python3
"""Replay the STEP exporter's surface recognition over an already exported file.

An exported STEP file is a complete description of the merged mesh, so parsing
one gives the same input `AnalyticFeatures::recogniseSurfacesOfRevolution()`
sees, with none of the build. That makes this the cheapest way to answer "how
much of this part could ever become analytic, and which rule is stopping each
piece that does not" - see doc/step-export.md, *Method notes*.

Run it on a *faceted* export (no `PYTHONSCAD_STEP_ANALYTIC`). The recogniser is
replayed here in full, so an analytic export would be measuring the answer
rather than the question.

Three subcommands:

  bands     replay the recogniser: every band it fits, and for each band it
            rejects, the rule that rejected it
  surfaces  classify every face of the mesh, to find the ceiling - how much of
            the part lies on a surface of revolution at all
  trace     replay the strip walk for the facets in one region, printing the
            stage each attempt dies at; for working out why a wall that plainly
            fits was never even a candidate

`bands` replays the shipped recogniser, so two of its rules are on by default
and can be switched off to reproduce the behaviour from before item 0 of
doc/step-export.md:

  --no-local-axis   take the band's axis from the whole unconstrained walk
                    again, instead of the seed's immediate neighbourhood
  --no-shared-arcs  require both bands of a shared rim to cover the full turn

Running with and without them is how item 0's gain was measured, and is the
cheapest check that a change to the recogniser has not lost ground.

Examples:

    scripts/step-analytic-probe.py surfaces examples/step_test/foo.stp
    scripts/step-analytic-probe.py bands examples/step_test/foo.stp
    scripts/step-analytic-probe.py bands --no-local-axis --no-shared-arcs foo.stp
    scripts/step-analytic-probe.py trace foo.stp --z 89.25 90.75 --r 78 79.5
"""

import argparse
import math
import re
import sys
from collections import defaultdict

NUM = r'-?\d+\.?\d*(?:[EeDd][-+]?\d+)?'
TOL = 1e-5  # modelling tolerance, matching the exporter's default


# ---------------------------------------------------------------- parsing ---

def parse_entities(path):
    """#id -> (KEYWORD, argument text). Entities may span several lines."""
    ents = {}
    buf = ''
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not buf and not line.startswith('#'):
                continue
            buf += line
            if not buf.endswith(';'):
                continue
            m = re.match(r'#(\d+)\s*=\s*([A-Z_0-9]+)\s*\((.*)\);$', buf)
            buf = ''
            if m:
                ents[int(m.group(1))] = (m.group(2), m.group(3))
    return ents


def refs(arg):
    return [int(x) for x in re.findall(r'#(\d+)', arg)]


def build_mesh(path):
    """Rebuild the loops the exporter had, from the file it wrote.

    Returns (coords, loops, is_hole, face_of_loop). Vertices are canonicalised
    by coordinate, exactly as the exporter canonicalises them, so two loops
    naming the same corner name it with the same index.
    """
    ents = parse_entities(path)

    pts = {}
    for eid, (kind, arg) in ents.items():
        if kind == 'CARTESIAN_POINT':
            nums = re.findall(NUM, arg)
            if len(nums) >= 3:
                pts[eid] = tuple(float(x) for x in nums[:3])

    vert_pt = {eid: refs(arg)[0] for eid, (kind, arg) in ents.items()
               if kind == 'VERTEX_POINT'}

    coord_id, coords, vid = {}, [], {}
    for v, p in vert_pt.items():
        c = pts[p]
        if c not in coord_id:
            coord_id[c] = len(coords)
            coords.append(c)
        vid[v] = coord_id[c]

    edge = {eid: (refs(arg)[0], refs(arg)[1])
            for eid, (kind, arg) in ents.items() if kind == 'EDGE_CURVE'}
    oriented = {eid: (refs(arg)[-1], arg.strip().endswith('.T.'))
                for eid, (kind, arg) in ents.items() if kind == 'ORIENTED_EDGE'}
    eloop = {eid: refs(arg) for eid, (kind, arg) in ents.items() if kind == 'EDGE_LOOP'}
    bound = {eid: (refs(arg)[0], kind == 'FACE_OUTER_BOUND')
             for eid, (kind, arg) in ents.items()
             if kind in ('FACE_OUTER_BOUND', 'FACE_BOUND')}

    loops, is_hole, face_of_loop = [], [], []
    for eid, (kind, arg) in ents.items():
        if kind != 'ADVANCED_FACE':
            continue
        for b in (b for b in refs(arg)[:-1] if b in bound):
            lp, outer = bound[b]
            seq = []
            for oe in eloop[lp]:
                ec, sense = oriented[oe]
                a, bb = edge[ec]
                seq.append(vid[a] if sense else vid[bb])
            loops.append(seq)
            is_hole.append(not outer)
            face_of_loop.append(eid)
    return coords, loops, is_hole, face_of_loop


# --------------------------------------------------------------- geometry ---

def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(a):
    return math.sqrt(dot(a, a))


def unit(a):
    n = norm(a)
    return (a[0] / n, a[1] / n, a[2] / n) if n else (0., 0., 0.)


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def newell(coords, loop):
    """Twice the area vector. Stable where the cross product of the first two
    edges is not - reflex corners, collinear first three vertices."""
    n = [0., 0., 0.]
    for i in range(len(loop)):
        a, b = coords[loop[i]], coords[loop[(i + 1) % len(loop)]]
        n[0] += (a[1] - b[1]) * (a[2] + b[2])
        n[1] += (a[2] - b[2]) * (a[0] + b[0])
        n[2] += (a[0] - b[0]) * (a[1] + b[1])
    return tuple(n)


def dist_axis(pt, base, axis):
    rel = sub(pt, base)
    return norm(sub(rel, scale(axis, dot(axis, rel))))


def perpendicular(n):
    a = (1., 0., 0.) if abs(n[0]) < 0.9 else (0., 1., 0.)
    return unit(cross(n, a))


def solve3(m, b):
    a = [row[:] + [b[i]] for i, row in enumerate(m)]
    for col in range(3):
        piv = max(range(col, 3), key=lambda r: abs(a[r][col]))
        if abs(a[piv][col]) < 1e-18:
            return None
        a[col], a[piv] = a[piv], a[col]
        for r in range(3):
            if r == col:
                continue
            f = a[r][col] / a[col][col]
            for c in range(col, 4):
                a[r][c] -= f * a[col][c]
    return [a[i][3] / a[i][i] for i in range(3)]


def fit_centre(coords, ids, axis, level):
    """Kasa's linearised circle fit, projected onto the plane at `level`.

    Averaging is not good enough: the centroid of an arc sits inside its chord,
    which puts the axis somewhere else entirely."""
    if len(ids) < 3:
        return None
    u = perpendicular(axis)
    w = cross(axis, u)
    o = coords[ids[0]]
    ata = [[0.] * 3 for _ in range(3)]
    atb = [0.] * 3
    for i in ids:
        rel = sub(coords[i], o)
        row = (2 * dot(rel, u), 2 * dot(rel, w), 1.0)
        val = dot(rel, u) ** 2 + dot(rel, w) ** 2
        for r in range(3):
            for c in range(3):
                ata[r][c] += row[r] * row[c]
            atb[r] += row[r] * val
    sol = solve3(ata, atb)
    if sol is None:
        return None
    c = tuple(o[k] + sol[0] * u[k] + sol[1] * w[k] for k in range(3))
    return sub(c, scale(axis, dot(axis, c) - level))


def fit_radius_line(pts):
    """Least squares r = a + b*z over (radius, height) pairs.

    A facet lies on a cylinder or a cone about the axis exactly when all its
    vertices satisfy one such line: b = 0 is a cylinder, b != 0 a cone. That
    is the test which separates a face whose *surface* is missing from one
    whose surface is there and only the trim is not planar - the first needs a
    new surface type, the second needs a trim curve.

    Returns (a, b, largest residual)."""
    n = len(pts)
    sz = sum(z for _, z in pts)
    sr = sum(r for r, _ in pts)
    szz = sum(z * z for _, z in pts)
    szr = sum(z * r for r, z in pts)
    den = n * szz - sz * sz
    if abs(den) < 1e-18:  # every vertex at one height: a cylinder iff r is constant
        rs = [r for r, _ in pts]
        return (sum(rs) / n, 0.0, max(rs) - min(rs))
    b = (n * szr - sz * sr) / den
    a = (sr - b * sz) / n
    return (a, b, max(abs(r - (a + b * z)) for r, z in pts))


def edge_key(a, b):
    return (min(a, b), max(a, b))


def edge_index(loops):
    out = defaultdict(list)
    for i, lp in enumerate(loops):
        for j in range(len(lp)):
            out[edge_key(lp[j], lp[(j + 1) % len(lp)])].append(i)
    return out


# ------------------------------------------------------------ recognition ---

class Recogniser:
    """The band recogniser, replayed.

    Deliberately without the intent gate: the file carries no provenance, so
    this measures the geometry and topology gates only. The intent gate can
    lower every number here, never raise one, which is what makes them a
    ceiling rather than a prediction."""

    def __init__(self, coords, loops, is_hole, local_axis=False, shared_arcs=False):
        self.coords, self.loops, self.is_hole = coords, loops, is_hole
        self.local_axis, self.shared_arcs = local_axis, shared_arcs
        self.loop_edges = edge_index(loops)
        self.normals = [unit(newell(coords, lp)) for lp in loops]
        self.consumed = [False] * len(loops)
        self.band_of_loop = [-1] * len(loops)
        self.bands = []

    # -- the strip walk ----------------------------------------------------
    def walk(self, seed, side, on_surface=None, ignore_consumed=False):
        """Grow across ruling edges. Entering a quad through one ruling fixes
        which pair of its edges are rulings, so this needs no axis and works on
        a frustum, whose rulings are each tilted differently."""
        walls, entry = [], {}
        stack = [(seed, side)]
        while stack:
            f, s = stack.pop()
            if f in entry:
                continue
            entry[f] = s
            walls.append(f)
            lp = self.loops[f]
            for sd in (s, (s + 2) % 4):
                a, b = lp[sd], lp[(sd + 1) % 4]
                for nb in self.loop_edges.get(edge_key(a, b), ()):
                    if nb == f or nb in entry:
                        continue
                    if not ignore_consumed and self.consumed[nb]:
                        continue
                    if self.is_hole[nb] or len(self.loops[nb]) != 4:
                        continue
                    if on_surface and not on_surface(nb):
                        continue
                    for j in range(4):
                        if edge_key(self.loops[nb][j],
                                    self.loops[nb][(j + 1) % 4]) == edge_key(a, b):
                            stack.append((nb, j))
                            break
        return walls, entry

    def chords_of(self, walls, entry):
        """The edges which are not rulings. They all lie in a plane
        perpendicular to the axis."""
        out = []
        for f in walls:
            r = entry[f]
            for c in ((r + 1) % 4, (r + 3) % 4):
                d = sub(self.coords[self.loops[f][(c + 1) % 4]],
                        self.coords[self.loops[f][c]])
                if norm(d) > 1e-12:
                    out.append(unit(d))
        return out

    @staticmethod
    def axis_from(chords):
        """Two chords which are not parallel fix the axis exactly."""
        for c in chords[1:]:
            n = cross(chords[0], c) if chords else None
            if n and norm(n) > 1e-9:
                a = unit(n)
                return scale(a, -1) if (a[2] < 0 or (a[2] == 0 and a[0] < 0)) else a
        return None

    def seed_neighbourhood(self, seed, side, walls):
        """The seed and the facets directly joined to it across a ruling."""
        near = [seed]
        seed_rulings = {edge_key(self.loops[seed][s], self.loops[seed][(s + 1) % 4])
                        for s in (side, (side + 2) % 4)}
        for f in walls:
            if f == seed:
                continue
            if any(edge_key(self.loops[f][j], self.loops[f][(j + 1) % 4]) in seed_rulings
                   for j in range(4)):
                near.append(f)
        return near

    def fit_band(self, seed, side):
        """One attempt. Returns a band dict, or None with no explanation - a
        seed that is not on a band is the overwhelmingly common case, and
        reporting each one would bury the bands that were rejected."""
        coords, loops = self.coords, self.loops

        walls, entry = self.walk(seed, side)
        if len(walls) < 3:
            return None

        if self.local_axis:
            # The free walk cannot be trusted for the axis: where it runs off
            # the surface it drags foreign chords in, and the perpendicularity
            # test then throws the candidate away before the constrained walk
            # ever gets to clean it up.
            axis = self.axis_from(self.chords_of(
                self.seed_neighbourhood(seed, side, walls), entry))
        else:
            chords = self.chords_of(walls, entry)
            axis = self.axis_from(chords)
            if axis and any(abs(dot(c, axis)) > 1e-9 for c in chords):
                axis = None
        if axis is None:
            return None

        # Fit from the seed and its first two neighbours, then walk again
        # admitting only facets which sit on that surface.
        probe = {v: dot(axis, coords[v]) for f in walls[:3] for v in loops[f]}
        plo, phi = min(probe.values()), max(probe.values())
        pb = [v for v, t in probe.items() if abs(t - plo) < TOL]
        pt = [v for v, t in probe.items() if abs(t - phi) < TOL]
        pbase = fit_centre(coords, pb, axis, plo)
        if pbase is None or fit_centre(coords, pt, axis, phi) is None:
            return None
        pr0 = sum(dist_axis(coords[v], pbase, axis) for v in pb) / len(pb)
        pr1 = sum(dist_axis(coords[v], pbase, axis) for v in pt) / len(pt)
        pscale = max(pr0, pr1)
        if pscale < TOL:
            return None

        def on_surface(f):
            for v in loops[f]:
                t = dot(axis, coords[v])
                want = pr0 if abs(t - plo) < TOL else (pr1 if abs(t - phi) < TOL else -1.)
                if want < 0:
                    return False
                if abs(dist_axis(coords[v], pbase, axis) - want) > 1e-7 * pscale:
                    return False
            return True

        walls, entry = self.walk(seed, side, on_surface)
        if len(walls) < 3:
            return None
        if self.local_axis:
            # now that the walk is confined, the whole band has to agree with
            # the axis the seed's neighbourhood gave
            if any(abs(dot(c, axis)) > 1e-9 for c in self.chords_of(walls, entry)):
                return None

        along = {v: dot(axis, coords[v]) for f in walls for v in loops[f]}
        lo, hi = min(along.values()), max(along.values())
        if hi - lo < TOL:
            return None
        bottom = [v for v, t in along.items() if abs(t - lo) < TOL]
        top = [v for v, t in along.items() if abs(t - hi) < TOL]
        if len(bottom) + len(top) != len(along):
            return None  # a vertex off both rims

        # A band which closes on itself has one rim vertex per facet; one which
        # stops short of a full turn has one more, the far end of the last.
        full = len(bottom) == len(walls) and len(top) == len(walls)
        part = len(bottom) == len(walls) + 1 and len(top) == len(walls) + 1
        if not full and not part:
            return None

        base = fit_centre(coords, bottom, axis, lo)
        tc = fit_centre(coords, top, axis, hi)
        if base is None or tc is None or dist_axis(tc, base, axis) > 1e-6:
            return None
        r0 = sum(dist_axis(coords[v], base, axis) for v in bottom) / len(bottom)
        r1 = sum(dist_axis(coords[v], base, axis) for v in top) / len(top)
        sc = max(r0, r1)
        if sc < TOL:
            return None
        dev = max(max(abs(dist_axis(coords[v], base, axis) - r0) for v in bottom),
                  max(abs(dist_axis(coords[v], base, axis) - r1) for v in top))
        if dev > 1e-7 * sc:
            return None

        return dict(walls=walls, axis=axis, base=base, r0=r0, r1=r1, h=hi - lo,
                    closed=full, bottom=bottom, top=top, alive=True, why=None,
                    bad_rim=None)

    def find_bands(self):
        for seed in range(len(self.loops)):
            if self.consumed[seed] or self.is_hole[seed] or len(self.loops[seed]) != 4:
                continue
            for side in range(4):
                band = self.fit_band(seed, side)
                if band is None:
                    continue
                for f in band['walls']:
                    self.consumed[f] = True
                    self.band_of_loop[f] = len(self.bands)
                self.bands.append(band)
                break
        return self.bands

    # -- the rim rules -----------------------------------------------------
    def rim_edges(self, band, bottom):
        level = set(band['bottom'] if bottom else band['top'])
        out = set()
        for f in band['walls']:
            lp = self.loops[f]
            for j in range(len(lp)):
                a, b = lp[j], lp[(j + 1) % len(lp)]
                if a in level and b in level:
                    out.add(edge_key(a, b))
        return out

    def resolve_rim(self, bi, bottom):
        """(reason, None) when the rim cannot be collapsed, (None, ref) when it
        can. `reason` is a string, or a tuple carrying detail for the report."""
        band = self.bands[bi]
        edges = self.rim_edges(band, bottom)
        if not edges:
            return 'no rim edges', None
        in_band = set(band['walls'])

        others = set()
        for e in edges:
            outside = [u for u in self.loop_edges[e] if u not in in_band]
            if len(outside) != 1:
                return 'a rim edge is used by more than two faces', None
            others.add(outside[0])

        if len(others) == 1:
            nb = next(iter(others))
            if self.band_of_loop[nb] != -1:
                return 'the rim borders a single facet of another band', None
            if self.consumed[nb]:
                return 'the neighbouring face was dropped', None
            lp = self.loops[nb]
            n = len(lp)
            if len(set(lp)) == n and len(edges) == n:
                return None, ('WHOLE_LOOP', nb, len(edges))
            on = [edge_key(lp[j], lp[(j + 1) % n]) in edges for j in range(n)]
            cnt = sum(on)
            if cnt != len(edges) or cnt >= n:
                return "the rim is not a run of its neighbour's edges", None
            starts = [j for j in range(n) if on[j] and not on[(j - 1) % n]]
            if len(starts) != 1:
                return "the rim is split across its neighbour's loop", None
            return None, ('LOOP_RUN', nb, cnt)

        nb_bands = {self.band_of_loop[f] for f in others}
        if len(nb_bands) != 1 or -1 in nb_bands:
            shapes = defaultdict(int)
            for f in others:
                shapes[len(self.loops[f])] += 1
            return ('the rim borders one face per facet',
                    len(others), dict(sorted(shapes.items()))), None
        other = next(iter(nb_bands))
        if not self.bands[other]['alive']:
            return 'the band sharing this rim was dropped', None
        if band['closed'] != self.bands[other]['closed']:
            return 'a shared rim needs both bands to be the same shape', None
        if not band['closed']:
            if not self.shared_arcs:
                return 'a shared rim needs both bands to cover the full turn', None
            # Both partial. The arc is shared only if the two bands meet along
            # the whole of it, so neither has rim edges the other lacks.
            for other_bottom in (True, False):
                if self.rim_edges(self.bands[other], other_bottom) == edges:
                    return None, ('OTHER_BAND_ARC', other, len(edges))
            return 'the two partial bands share only part of the rim', None
        if len(others) != len(self.bands[other]['walls']):
            return 'the shared rim does not cover the whole neighbouring band', None
        return None, ('OTHER_BAND', other, len(edges))

    def resolve_rims(self):
        """Runs to a fixed point: dropping a band can leave a neighbour's
        shared rim unresolvable in turn. Dropping is monotone, so it ends."""
        ok_partial = ('LOOP_RUN', 'OTHER_BAND_ARC')
        changed = True
        while changed:
            changed = False
            for i, band in enumerate(self.bands):
                if not band['alive']:
                    continue
                refs_, dead = [], False
                for bottom in (True, False):
                    why, ref = self.resolve_rim(i, bottom)
                    if why is not None:
                        band.update(alive=False, why=why,
                                    bad_rim='bottom' if bottom else 'top')
                        changed = dead = True
                        break
                    refs_.append(ref)
                if dead:
                    continue
                shapes_ok = ((refs_[0][0] != 'LOOP_RUN' and refs_[1][0] != 'LOOP_RUN')
                             if band['closed'] else
                             (refs_[0][0] in ok_partial and refs_[1][0] in ok_partial))
                if not shapes_ok:
                    band.update(
                        alive=False,
                        why='a rim is a run of a loop, but the band covers the full turn')
                    changed = True
                    continue
                band['rims'] = refs_
        return self.bands


# --------------------------------------------------------------- commands ---

def band_kind(b):
    return 'cone' if abs(b['r0'] - b['r1']) > 1e-9 * max(b['r0'], b['r1']) else 'cyl '


def cmd_bands(args):
    coords, loops, is_hole, _ = build_mesh(args.file)
    rec = Recogniser(coords, loops, is_hole, args.local_axis, args.shared_arcs)
    rec.find_bands()
    rec.resolve_rims()
    bands = rec.bands

    print(f'{args.file}: {len(coords)} vertices, {len(loops)} loops '
          f'({sum(is_hole)} holes)')
    print(f'{len(bands)} bands fit exactly '
          f'({sum(len(b["walls"]) for b in bands)} facets)')
    alive = [b for b in bands if b['alive']]
    print(f'{len(alive)} survive the rim rules '
          f'({sum(len(b["walls"]) for b in alive)} facets replaced)\n')

    print('all bands:')
    for i, b in enumerate(bands):
        z0 = dot(b['axis'], b['base'])
        state = 'KEPT' if b['alive'] else 'drop: ' + str(
            b['why'][0] if isinstance(b['why'], tuple) else b['why'])
        print(f'  #{i:3d} {band_kind(b)} r={b["r0"]:9.5g}..{b["r1"]:<9.5g} '
              f'z={z0:9.5g}..{z0 + b["h"]:<9.5g} {len(b["walls"]):4d}f '
              f'{"closed " if b["closed"] else "partial"} {state}')

    rejected = [b for b in bands if not b['alive']]
    if not rejected:
        print('\nno band was rejected')
        return
    buckets = defaultdict(lambda: [0, 0])
    for b in rejected:
        label = b['why'][0] if isinstance(b['why'], tuple) else b['why']
        buckets[label][0] += 1
        buckets[label][1] += len(b['walls'])
    print('\nrejected bands, by rule:')
    for label, (cnt, facets) in sorted(buckets.items(), key=lambda kv: -kv[1][1]):
        print(f'  {facets:5d} facets in {cnt:3d} band(s): {label}')

    print('\nrejected bands, individually:')
    for b in sorted(rejected, key=lambda b: -len(b['walls'])):
        w = b['why']
        extra = (f' - {b["bad_rim"]} rim borders {w[1]} faces, by vertex count {w[2]}'
                 if isinstance(w, tuple) else '')
        print(f'  r={b["r0"]:.5g}..{b["r1"]:.5g} h={b["h"]:.5g} '
              f'{len(b["walls"]):4d} facets '
              f'{"closed" if b["closed"] else "partial"}: '
              f'{w[0] if isinstance(w, tuple) else w}{extra}')


def cmd_surfaces(args):
    """The ceiling: how much of the part lies on a surface of revolution at all.

    A recogniser of any sophistication can only ever collapse faces which do.
    Radii are measured about the given axis through the origin, which is where
    OpenSCAD's primitives put it."""
    coords, loops, is_hole, _ = build_mesh(args.file)
    axis = tuple(args.axis)

    def rz(v):
        p = coords[v]
        along = dot(axis, p)
        return (dist_axis(p, (0., 0., 0.), axis), along)

    stats = defaultdict(int)
    revol = defaultdict(list)
    trimmed = defaultdict(int)
    outer = 0
    for i, lp in enumerate(loops):
        if is_hole[i]:
            continue
        outer += 1
        pol = [rz(v) for v in lp]
        rs = sorted({round(r, 6) for r, _ in pol})
        zs = sorted({round(z, 6) for _, z in pol})
        n = unit(newell(coords, lp))

        if abs(abs(dot(n, axis)) - 1.) < 1e-9:
            label = 'planar, perpendicular to the axis'
        elif len(zs) == 2 and len(rs) <= 2 and len(lp) == 4:
            label = 'facet of a surface of revolution'
            revol[(tuple(rs), tuple(zs))].append(i)
        else:
            # The surface may still be there with only the trim non-planar,
            # which is a different item of work entirely.
            a, b, residual = fit_radius_line(pol)
            if residual <= 1e-7 * max(1.0, max(r for r, _ in pol)):
                kind = 'cylinder' if abs(b) < 1e-9 else 'cone'
                label = f'on a {kind} about the axis, but the trim is not planar'
                trimmed[(round(a, 4), round(b, 6))] += 1
            else:
                label = 'on no surface of revolution at all'
        stats[label] += 1

    print(f'{args.file}: {len(loops)} loops ({outer} outer, {sum(is_hole)} holes)\n')
    for label, cnt in sorted(stats.items(), key=lambda kv: -kv[1]):
        print(f'  {cnt:5d} loops  {100 * cnt / outer:5.1f}%  {label}')

    if trimmed:
        print(f'\nthe non-planar-trim faces lie on {len(trimmed)} surfaces that '
              f'are already recognisable, and need only a trim curve:')
        for (a, b), cnt in sorted(trimmed.items(), key=lambda kv: -kv[1])[:args.top]:
            what = f'cylinder r={a:g}' if abs(b) < 1e-9 else f'cone r={a:g}{b:+g}*z'
            print(f'  {cnt:5d} facets  {what}')

    rows = sorted(revol.items(), key=lambda kv: -len(kv[1]))
    total = sum(len(ids) for _, ids in rows)
    print(f'\n{total} facets ({100 * total / outer:.1f}% of outer loops) lie on '
          f'{len(rows)} distinct surfaces of revolution:')
    for (rs, zs), ids in rows[:args.top]:
        kind = 'cylinder' if len(rs) == 1 else 'cone / torus ring'
        print(f'  {len(ids):5d} facets  r={rs[0]:9.5g}..{rs[-1]:<9.5g} '
              f'z={zs[0]:9.5g}..{zs[-1]:<9.5g}  {kind}')
    if len(rows) > args.top:
        print(f'  ... {len(rows) - args.top} more, '
              f'{sum(len(v) for _, v in rows[args.top:])} facets')


def cmd_trace(args):
    """Replay the walk for the facets in one region, printing where it dies.

    For the case a rejected-band report cannot explain, because the band was
    never a candidate at all - a wall that plainly fits and is simply absent
    from the list."""
    coords, loops, is_hole, _ = build_mesh(args.file)
    rec = Recogniser(coords, loops, is_hole, args.local_axis, args.shared_arcs)
    axis0 = tuple(args.axis)
    zlo, zhi = args.z
    rlo, rhi = args.r

    def rz(v):
        p = coords[v]
        return (dist_axis(p, (0., 0., 0.), axis0), dot(axis0, p))

    targets = []
    for i, lp in enumerate(loops):
        if is_hole[i] or len(lp) != 4:
            continue
        pol = [rz(v) for v in lp]
        if (all(zlo - 1e-6 <= z <= zhi + 1e-6 for _, z in pol)
                and all(rlo - 1e-6 <= r <= rhi + 1e-6 for r, _ in pol)
                and max(z for _, z in pol) - min(z for _, z in pol) > 1e-6):
            targets.append(i)
    print(f'{len(targets)} facets in the region\n')
    if not targets:
        return

    seed = targets[0]
    print('seed loop %d: %s' % (seed, ', '.join(
        f'(r={r:.5g},z={z:.5g})' for r, z in (rz(v) for v in loops[seed]))))

    for side in range(4):
        walls, entry = rec.walk(seed, side, ignore_consumed=True)
        zs = [rz(v)[1] for f in walls for v in loops[f]]
        rs = [rz(v)[0] for f in walls for v in loops[f]]
        print(f'\n  side {side}: free walk reaches {len(walls)} facets, '
              f'z {min(zs):.5g}..{max(zs):.5g}  r {min(rs):.5g}..{max(rs):.5g}')
        if len(walls) < 3:
            print('    -> fewer than 3 facets, discarded')
            continue
        chords = rec.chords_of(walls, entry)
        axis = rec.axis_from(chords)
        if axis is None:
            print('    -> no two chords span a plane, discarded')
            continue
        bad = [c for c in chords if abs(dot(c, axis)) > 1e-9]
        print(f'    axis from the free walk {tuple(round(x, 6) for x in axis)}, '
              f'{len(bad)} of {len(chords)} chords not perpendicular to it')
        near = rec.seed_neighbourhood(seed, side, walls)
        local = rec.axis_from(rec.chords_of(near, entry))
        print(f'    axis from the seed neighbourhood ({len(near)} facets) '
              f'{tuple(round(x, 6) for x in local) if local else None}')
        if bad:
            print('    -> the free walk left the surface. The shipped code '
                  'discards the candidate here; --local-axis does not.')
        else:
            print('    -> axis accepted, continues to the fit')


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest='cmd', required=True)

    def common(p, flags=True):
        p.add_argument('file', help='a faceted STEP export')
        p.add_argument('--axis', type=float, nargs=3, default=[0., 0., 1.],
                       metavar=('X', 'Y', 'Z'), help='axis of revolution (default Z)')
        if flags:
            p.add_argument('--local-axis', action=argparse.BooleanOptionalAction,
                           default=True,
                           help="take the band's axis from the seed's "
                                'neighbourhood, not the unconstrained walk '
                                '(default: on, as shipped)')
            p.add_argument('--shared-arcs', action=argparse.BooleanOptionalAction,
                           default=True,
                           help='let two partial bands share a rim as an arc '
                                '(default: on, as shipped)')

    p = sp.add_parser('bands', help="replay the recogniser and report each band's fate")
    common(p)
    p.set_defaults(func=cmd_bands)

    p = sp.add_parser('surfaces', help='classify every face; find the ceiling')
    common(p, flags=False)
    p.add_argument('--top', type=int, default=25, help='surfaces to list (default 25)')
    p.set_defaults(func=cmd_surfaces)

    p = sp.add_parser('trace', help='replay the walk for one region of the mesh')
    common(p)
    p.add_argument('--z', type=float, nargs=2, required=True, metavar=('LO', 'HI'),
                   help='height range along the axis')
    p.add_argument('--r', type=float, nargs=2, required=True, metavar=('LO', 'HI'),
                   help='radius range about the axis')
    p.set_defaults(func=cmd_trace)

    args = ap.parse_args(argv)
    args.axis = list(unit(tuple(args.axis)))
    args.func(args)
    return 0


if __name__ == '__main__':
    sys.exit(main())
