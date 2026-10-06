/*! The exact 2D region a profile's makers declared - see ProfileRegion.h. */
#include "geometry/ProfileRegion.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "geometry/MachinePrecision.h"

using recognition::forwardError;

namespace {

double cross(const Vector2d& a, const Vector2d& b)
{
  return a[0] * b[1] - a[1] * b[0];
}

/*! Twice the signed area of a, b, q: positive where q is left of a -> b. */
double orient(const Vector2d& a, const Vector2d& b, const Vector2d& q)
{
  return cross(b - a, q - a);
}

double toSegment(const Vector2d& q, const Vector2d& a, const Vector2d& b)
{
  const Vector2d e = b - a;
  const double len2 = e.squaredNorm();
  const double s = len2 > 0 ? std::clamp((q - a).dot(e) / len2, 0.0, 1.0) : 0.0;
  return (a + e * s - q).norm();
}

/*! A circle or an ellipse as one shape: centre + u cos t + v sin t. */
struct Conic {
  Vector2d c, u, v;
  double longest = 0, shortest = 0;  // its semi-axes' bounds, for distances
};

bool conicOf(const Profile2d& p, const ProfileEdge& e, Conic& out)
{
  if (e.kind == EdgeKind::Arc && e.record >= 0 && std::size_t(e.record) < p.arcs.size()) {
    const Arc2d& a = p.arcs[e.record];
    out = Conic{Vector2d(a.centre[0], a.centre[1]), Vector2d(a.r, 0), Vector2d(0, a.r), a.r, a.r};
    return a.r > 0;
  }
  if (e.kind == EdgeKind::Ellipse && e.record >= 0 && std::size_t(e.record) < p.ellipses.size()) {
    const Ellipse2d& el = p.ellipses[e.record];
    const Vector2d u(el.u[0], el.u[1]), v(el.v[0], el.v[1]);
    // The semi-axes of centre + u cos t + v sin t are the singular values of
    // [u v], which bound how far a unit of (alpha, beta) reaches.
    const double a = u.squaredNorm(), b = u.dot(v), d = v.squaredNorm();
    const double mean = (a + d) / 2, half = std::sqrt(std::max(0.0, (a - d) * (a - d) / 4 + b * b));
    out = Conic{Vector2d(el.centre[0], el.centre[1]), u, v, std::sqrt(mean + half),
                std::sqrt(std::max(0.0, mean - half))};
    return out.shortest > 0;
  }
  return false;
}

/*! Where q is against the conic, as (alpha, beta)'s radius: 1 on it. */
double radiusOf(const Conic& k, const Vector2d& q)
{
  const Vector2d rel = q - k.c;
  const double det = cross(k.u, k.v);
  const double alpha = cross(rel, k.v) / det, beta = cross(k.u, rel) / det;
  return std::hypot(alpha, beta);
}

}  // namespace

ProfileRegion::ProfileRegion(Profile2d profile) : profile_(std::move(profile))
{
  double area = 0;
  for (const auto& loop : profile_.loops) {
    for (const ProfileEdge& e : loop) {
      area += cross(e.a, e.b);
      reach_ = std::max({reach_, e.a.norm(), e.b.norm()});
      if (e.kind == EdgeKind::Line) continue;
      Conic k;
      if (!conicOf(profile_, e, k)) {
        exact_ = false;  // a Bezier, or a chord of nothing recorded
        continue;
      }
      reach_ = std::max(reach_, k.c.norm() + k.longest);
      // A chord through the centre has no minor arc to stand for.
      const double s_c = orient(e.a, e.b, k.c);
      if (std::fabs(s_c) <= forwardError(8, (e.b - e.a).norm() * ((k.c - e.a).norm() + k.longest)))
        exact_ = false;
    }
  }
  if (profile_.loops.empty() || area == 0) exact_ = false;
  side_ = area >= 0 ? 1.0 : -1.0;
}

ProfileRegion::Answer ProfileRegion::classify(const Vector2d& q, double err) const
{
  if (!exact_) return Answer{};
  // What this arithmetic adds to the caller's error: a few roundings of
  // quantities the size of the profile and of the point.
  const double tol = err + forwardError(16, q.norm() + reach_);

  // On an edge of the region: a line, or a curve on its minor arc's side or at
  // its ends.
  bool near_chord = false;
  Where by_bulge = Where::Unknown;
  for (std::size_t l = 0; l < profile_.loops.size(); l++) {
    const auto& loop = profile_.loops[l];
    for (std::size_t i = 0; i < loop.size(); i++) {
      const ProfileEdge& e = loop[i];
      if (e.kind == EdgeKind::Line) {
        if (toSegment(q, e.a, e.b) <= tol) return Answer{Where::On, int(l), int(i)};
        continue;
      }
      Conic k;
      conicOf(profile_, e, k);
      const double rho = radiusOf(k, q);
      const double s_c = orient(e.a, e.b, k.c);
      const bool arc_side = orient(e.a, e.b, q) * s_c < 0;
      const bool at_end = (q - e.a).norm() <= tol || (q - e.b).norm() <= tol;
      const double off_long = std::fabs(rho - 1) * k.longest,
                   off_short = std::fabs(rho - 1) * k.shortest;
      if (at_end || (arc_side && off_long <= tol)) return Answer{Where::On, int(l), int(i)};
      // Between "certainly on" and "certainly off" the curve's distance is not
      // known well enough to say.
      if (arc_side && off_short <= tol) return Answer{};
      if (toSegment(q, e.a, e.b) <= tol) {
        near_chord = true;
        // The region's interior on the centre's side: the arc bulges outwards
        // and the chord is inside it.
        by_bulge = s_c * side_ > 0 ? Where::In : Where::Out;
      }
    }
  }
  if (near_chord) return Answer{by_bulge};

  // Off every edge and chord by more than either is known to: the polygon's
  // crossings and the segments' parities, each read the same however the
  // arithmetic rounds.
  bool in_polygon = false, in_segments = false;
  for (const auto& loop : profile_.loops) {
    for (const ProfileEdge& e : loop) {
      if ((e.a[1] > q[1]) != (e.b[1] > q[1])) {
        const double x = e.a[0] + (q[1] - e.a[1]) * (e.b[0] - e.a[0]) / (e.b[1] - e.a[1]);
        if (x > q[0]) in_polygon = !in_polygon;
      }
      if (e.kind == EdgeKind::Line) continue;
      Conic k;
      conicOf(profile_, e, k);
      if (radiusOf(k, q) < 1 && orient(e.a, e.b, q) * orient(e.a, e.b, k.c) < 0)
        in_segments = !in_segments;
    }
  }
  return Answer{in_polygon != in_segments ? Where::In : Where::Out};
}
