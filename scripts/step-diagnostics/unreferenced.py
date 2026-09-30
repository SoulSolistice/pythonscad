#!/usr/bin/env python3
"""Every entity in a STEP file that nothing references, by type.

A STEP file is a graph rooted in its product structure. An entity no other
entity refers to is either one of those roots - there are four, by design, in
the files this exporter writes - or an orphan: something written and reached by
nothing, a point or a curve or a face that is in the file and in no solid.

Asked for open item 39: a translator that writes the file from its B-rep can
reproduce the file's entity census only if every live entity is reachable from
the shells. An orphan is an entity the B-rep never had - so either the census
changes when the translator lands, or the orphan is the defect.

    python3 scripts/step-diagnostics/unreferenced.py build/foo.stp [more.stp ...]

Reads the STEP text; no kernel involved.
"""
import re
import sys
from collections import Counter

# Referenced by nothing, by design: what the product structure hangs from.
ROOTS = {
    "SHAPE_DEFINITION_REPRESENTATION",
    "SHAPE_REPRESENTATION_RELATIONSHIP",
    "APPLICATION_PROTOCOL_DEFINITION",
    "PRODUCT_RELATED_PRODUCT_CATEGORY",
}

ENTITY = re.compile(r"#(\d+)\s*=\s*(.*)", re.S)
REF = re.compile(r"#(\d+)")


def entity_type(body):
    """The entity's type, or the first of a complex instance's."""
    body = body.strip()
    if body.startswith("("):
        m = re.match(r"\(\s*([A-Z_0-9]+)", body)
    else:
        m = re.match(r"([A-Z_0-9]+)", body)
    return m.group(1) if m else "?"


def orphans(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    data = text[text.index("DATA;") + 5:text.index("ENDSEC;", text.index("DATA;"))]
    types, refs = {}, set()
    for record in data.split(";"):
        m = ENTITY.match(record.strip())
        if not m:
            continue
        eid, body = int(m.group(1)), m.group(2)
        types[eid] = entity_type(body)
        refs.update(int(r) for r in REF.findall(body))
    return Counter(t for eid, t in types.items() if eid not in refs and t not in ROOTS), len(types)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    worst = 0
    for path in sys.argv[1:]:
        found, total = orphans(path)
        n = sum(found.values())
        worst = max(worst, n)
        detail = ", ".join("%d %s" % (c, t) for t, c in found.most_common())
        print("%-60s %6d entities, %d unreferenced%s" % (path, total, n, (": " + detail) if n else ""))
    return 1 if worst else 0


if __name__ == "__main__":
    sys.exit(main())
