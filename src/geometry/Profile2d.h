#pragma once

/*! A 2D profile as its makers declared it: every edge, and what it is.
 *
 * Both extruders, and anything else that sweeps a profile, need the same answer
 * before they can say what a sweep makes: *is this edge a straight segment, or a
 * chord of a curve the profile was sampled from?* A straight edge swept is a
 * plane, a cylinder or a cone; a chord swept is a tessellation of the curve's
 * surface, and declaring the chord's own surface would put an artefact of `$fn`
 * on a channel whose whole point is that a declaration does not move.
 *
 * Until 2026-09-25 each extruder asked that question itself, and they answered
 * it differently: `rotate_extrude` exactly, `linear_extrude` within a relative
 * 1e-6 - a threshold, where only machine precision belongs (checklist C2) - and
 * with a Bezier refused wholesale by one and tested by bounding box by the
 * other. This is the one answer. What each kind of edge sweeps is then a table
 * (SweptSurfaces.h), not a guard in each sweep.
 *
 * **Declared where the maker said, measured where it did not.** The generator
 * knows what each edge is - `square()` makes lines, `circle()` chords of its arc
 * - and says so (EdgeIdentity.h), and identity carried through Clipper names each
 * edge of a boolean's or an offset's result by the edge it came from. That is
 * the answer wherever it exists. Where it does not - a producer that names
 * nothing, or an edge Clipper merged out of two - the edge's kind is recovered
 * from the records (Arc2d, Bezier2d) by asking where its ends lie: an arc at
 * machine precision, which is exact once Polygon2d::placeOnArcs has put the
 * points Clipper rounded back on it, and a Bezier by the bounding-box test it
 * had, which errs towards calling an edge curved and so declaring nothing. The
 * measurement cannot tell a D-shaft's flat, both ends on its circle, from a
 * chord; identity can.
 */

#include <vector>

#include "geometry/EdgeIdentity.h"
#include "geometry/Polygon2d.h"
#include "geometry/linalg.h"

struct ProfileEdge {
  Vector2d a{0., 0.}, b{0., 0.};
  EdgeKind kind = EdgeKind::Line;
  /*! The record a curved edge is a chord of - an index into the profile's arcs
   * or beziers - or -1 where it has none. */
  int record = -1;
  /*! Whether the maker said what this edge is, rather than its ends being
   * measured against the records. */
  bool declared = false;
};

/*! Every edge of every closed outline, classified, together with the records the
 * curved ones are chords of - all in one frame. */
struct Profile2d {
  std::vector<std::vector<ProfileEdge>> loops;
  std::vector<Arc2d> arcs;
  std::vector<Bezier2d> beziers;
};

/*! Whether `p` is on `arc`, at machine precision: the forward error of
 * |p - c| - r, and nothing chosen. */
bool onArc(const Vector2d& p, const Arc2d& arc);

/*! Classify `loops` against `arcs` and `beziers`, which must be in the same frame.
 * An edge whose maker named it - `ids[loop][edge]` declared in `decls` - is
 * what its maker declared. Any other is an Arc when both its ends are on one
 * recorded circle, a Bezier when both lie in one recorded segment's control
 * box, and a Line otherwise. */
Profile2d classifyProfile(const std::vector<VectorOfVector2d>& loops, std::vector<Arc2d> arcs,
                          std::vector<Bezier2d> beziers,
                          const std::vector<std::vector<EdgeId>>& ids = {},
                          const EdgeDecls *decls = nullptr);

/*! The polygon's own outlines, untransformed - the frame its records are in. */
Profile2d classifyProfile(const Polygon2d& poly);

/*! The same profile moved by `by`: its edges and its records. */
Profile2d translated(Profile2d profile, const Vector2d& by);
