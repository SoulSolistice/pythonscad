/*! A 2D profile as its makers declared it - see Profile2d.h. */
#include "geometry/Profile2d.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "geometry/recognition/Exact.h"

using recognition::forwardError;

bool onArc(const Vector2d& p, const Arc2d& arc)
{
  const Vector2d c(arc.centre[0], arc.centre[1]);
  // |p - c| - r: 2 subtractions, 2 squares, a sum, a root, a difference
  return fabs((p - c).norm() - arc.r) <= forwardError(7, p.norm() + c.norm() + arc.r);
}

namespace {

/*! Whether both ends lie in the segment's control box. A Bezier lies in the
 * convex hull of its control points, so a chord's ends do; the slack is the one
 * the linear extruder always had, and it errs towards calling an edge curved.
 * Awaiting identity - see Profile2d.h. */
bool inControlBox(const Vector2d& p, const Vector2d& q, const Bezier2d& bez)
{
  Vector2d lo = Vector2d(bez.ctrl[0]), hi = lo;
  for (int i = 1; i <= bez.degree; i++) {
    lo = lo.cwiseMin(Vector2d(bez.ctrl[i]));
    hi = hi.cwiseMax(Vector2d(bez.ctrl[i]));
  }
  const double tol = 1e-9 * std::max(1.0, (hi - lo).norm());
  const Vector2d slack(tol, tol);
  auto in = [&](const Vector2d& v) {
    return (v.array() >= (lo - slack).array()).all() && (v.array() <= (hi + slack).array()).all();
  };
  return in(p) && in(q);
}

}  // namespace

Profile2d classifyProfile(const std::vector<VectorOfVector2d>& loops, std::vector<Arc2d> arcs,
                          std::vector<Bezier2d> beziers)
{
  Profile2d out;
  out.arcs = std::move(arcs);
  out.beziers = std::move(beziers);
  for (const auto& loop : loops) {
    std::vector<ProfileEdge> edges;
    const std::size_t n = loop.size();
    edges.reserve(n);
    for (std::size_t i = 0; i < n; i++) {
      ProfileEdge e;
      e.a = loop[i];
      e.b = loop[(i + 1) % n];
      for (std::size_t k = 0; k < out.arcs.size() && e.record < 0; k++) {
        if (out.arcs[k].r > 0 && onArc(e.a, out.arcs[k]) && onArc(e.b, out.arcs[k])) {
          e.kind = EdgeKind::Arc;
          e.record = int(k);
        }
      }
      for (std::size_t k = 0; k < out.beziers.size() && e.record < 0; k++) {
        if (inControlBox(e.a, e.b, out.beziers[k])) {
          e.kind = EdgeKind::Bezier;
          e.record = int(k);
        }
      }
      edges.push_back(e);
    }
    out.loops.push_back(std::move(edges));
  }
  return out;
}

Profile2d classifyProfile(const Polygon2d& poly)
{
  std::vector<VectorOfVector2d> loops;
  for (const auto& outline : poly.untransformedOutlines()) loops.push_back(outline.vertices);
  return classifyProfile(loops, poly.arcs, poly.beziers);
}
