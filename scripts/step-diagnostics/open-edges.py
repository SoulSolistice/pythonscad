#!/usr/bin/env python3
"""Every edge of a STEP file that only one face uses - "the shell is not
closed" - and what stands on the other side of it.

validatestep.py counts these and names one. This says, per open edge: its
curve, its two vertices and where they are, the face using it and that face's
surface, and every *other* edge between the same two vertices, with its curve
and the faces using it. An open edge whose twin exists with a different curve
is two faces that meet but do not agree on how - a line on one side, an arc on
the other; one with no twin at all is a gap.

    python scripts/step-diagnostics/open-edges.py <file.stp> [--limit N]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tests"))
import validatestep as vs  # noqa: E402


def faces_of(entities):
    """Per EDGE_CURVE id, the (face id, surface kind) of every face using it."""
    users = {}
    for face in entities.values():
        if face.name != "ADVANCED_FACE":
            continue
        refs = face.refs()
        surface = entities.get(refs[-1]) if refs else None
        kind = surface.name if surface is not None else "?"
        for bid in refs[:-1]:
            loop, _ = vs._bound_loop(entities, bid)
            if loop is None:
                continue
            for oid in loop.refs():
                ends = vs._edge_endpoints(entities, oid)
                if ends is not None:
                    users.setdefault(ends[2], []).append((face.id, kind))
    return users


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("step")
    ap.add_argument("--limit", type=int, default=20)
    args = ap.parse_args()
    entities, problems = vs.parse_step(args.step)
    for p in problems:
        print("parse:", p)
    users = faces_of(entities)

    # every edge between two vertices, either way round
    between = {}
    for eid, ent in entities.items():
        if ent.name != "EDGE_CURVE" or len(ent.refs()) < 3:
            continue
        v1, v2 = ent.refs()[0], ent.refs()[1]
        between.setdefault(frozenset((v1, v2)), []).append(eid)

    def curve(eid):
        c = entities.get(entities[eid].refs()[2])
        return c.name if c is not None else "?"

    def point(vid):
        p = vs._vertex_point(entities, vid)
        return "(%s)" % ", ".join("%.6g" % x for x in p) if p else "?"

    open_edges = sorted(e for e, u in users.items() if len(u) < 2)
    print("%d edge(s) used by only one face" % len(open_edges))
    for eid in open_edges[: args.limit]:
        v1, v2 = entities[eid].refs()[0], entities[eid].refs()[1]
        face, kind = users[eid][0]
        print("#%d %s, #%d %s -> #%d %s, used by #%d on %s" % (
            eid, curve(eid), v1, point(v1), v2, point(v2), face, kind))
        twins = [t for t in between.get(frozenset((v1, v2)), []) if t != eid]
        if not twins:
            print("    no other edge joins these two vertices: a gap")
        for t in twins:
            on = ", ".join("#%d on %s" % fk for fk in users.get(t, [])) or "no face"
            print("    twin #%d %s, used by %s" % (t, curve(t), on))


if __name__ == "__main__":
    main()
