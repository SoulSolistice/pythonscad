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
 * **Measured, not yet declared.** The generator knows what each edge is -
 * `square()` makes lines, `circle()` and a round join make arcs, a glyph makes
 * lines and Beziers - but a 2D boolean or an offset runs the outline through
 * Clipper, which keeps no identity, so the edge's kind is recovered here from
 * the records (Arc2d, Bezier2d) by asking where its ends lie. An arc is asked at
 * machine precision, which is exact once Polygon2d::placeOnArcs has put the
 * points Clipper rounded back on it. A Bezier has no such restoration yet, and
 * keeps the bounding-box test it had, which errs towards calling an edge curved
 * and so declaring nothing. Carrying the identity through Clipper replaces both
 * with the maker's own answer - doc/step-export-wip.md, *Inferred where a
 * declaration was possible*, item 2.
 */

#include <vector>

#include "geometry/Polygon2d.h"
#include "geometry/linalg.h"

/*! What a profile edge is. */
enum class EdgeKind {
  Line,    //!< a straight segment of the profile
  Arc,     //!< a chord of a recorded circle - `record` indexes the profile's arcs
  Bezier,  //!< a chord of a recorded Bezier - `record` indexes the profile's beziers
};

struct ProfileEdge {
  Vector2d a{0., 0.}, b{0., 0.};
  EdgeKind kind = EdgeKind::Line;
  int record = -1;
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
 * An edge is an Arc when both its ends are on one recorded circle, a Bezier when
 * both lie in one recorded segment's control box, and a Line otherwise. */
Profile2d classifyProfile(const std::vector<VectorOfVector2d>& loops, std::vector<Arc2d> arcs,
                          std::vector<Bezier2d> beziers);

/*! The polygon's own outlines, untransformed - the frame its records are in. */
Profile2d classifyProfile(const Polygon2d& poly);
