/*! A declared sweep and a coaxial cylinder or cone in the meridian plane - see
 * Meridian.h. */
#include "io/step/Meridian.h"

#include <algorithm>
#include <cmath>

#include "geometry/MachinePrecision.h"

namespace step {

bool Meridian::of(const Surface *a, const Surface *b, Meridian& out)
{
  for (const Surface *s : {a, b}) {
    const auto *sweep = dynamic_cast<const SweepSurface *>(s);
    if (sweep == nullptr) continue;
    const Surface *other = s == a ? b : a;
    Meridian m;
    m.sweep = sweep;
    Vector3d oref, onorm;
    if (const auto *cyl = dynamic_cast<const CylinderSurface *>(other)) {
      m.r0 = cyl->r;
      oref = cyl->refpt;
      onorm = cyl->normdir.normalized();
    } else if (const auto *cone = dynamic_cast<const ConeSurface *>(other)) {
      // ConeSurface::radiusAt measures height along its normdir as stored.
      m.r0 = cone->r;
      m.slope = cone->slope;
      oref = cone->refpt;
      onorm = cone->normdir;
    } else {
      continue;
    }
    const Vector3d axis = sweep->normdir.normalized();
    const Vector3d unit = onorm.normalized();
    if (axis.cross(unit).norm() > recognition::forwardError(8, 1.0)) continue;
    const Vector3d d = sweep->refpt - oref;
    const double scale = 1.0 + sweep->refpt.norm() + oref.norm();
    if ((d - unit * d.dot(unit)).norm() > recognition::forwardError(16, scale)) continue;
    m.h0 = d.dot(onorm);
    m.lift = axis.dot(onorm);
    out = m;
    return true;
  }
  return false;
}

double Meridian::station(const Vector3d& p) const
{
  double t = 0, dr = 0, dz = 0;
  sweep->localCoords(p, t, dr, dz);
  return t;
}

double Meridian::residual(double t, const Vector2d& q) const
{
  const double rho = sweep->laws.radiusAt(t) + q[0];
  const double h = h0 + (sweep->pitch * sweep->turns * t + q[1]) * lift;
  return rho - r0 - slope * h;
}

std::vector<std::size_t> Meridian::edgesOf(const Vector3d& p) const
{
  double t = 0, dr = 0, dz = 0;
  sweep->localCoords(p, t, dr, dz);
  if (!(t >= 0.0 && t <= 1.0)) return {};
  double at = 0;
  (void)sweep->profileDistance(t, dr, dz, &at);
  const std::size_t n = sweep->laws.points();
  const double s = at * double(n);
  const std::size_t i = std::min(n - 1, std::size_t(s));
  // At a vertex the point is on both edges meeting there. "At" is the vertex's
  // own position to the arithmetic of profileDistance.
  const Vector2d q(dr, dz);
  for (const std::size_t k : {i, (i + 1) % n}) {
    const Vector2d v = sweep->laws.pointAt(k, t);
    if ((q - v).norm() <= recognition::forwardError(16, 1.0 + q.norm() + v.norm())) {
      return {(k + n - 1) % n, k};
    }
  }
  return {i};
}

bool Meridian::crossingOnEdge(double t, std::size_t i, Vector3d& out) const
{
  const std::size_t n = sweep->laws.points();
  const Vector2d A = sweep->laws.pointAt(i, t), B = sweep->laws.pointAt((i + 1) % n, t);
  const double ga = residual(t, A), gb = residual(t, B);
  if (ga == gb) return false;  // the edge runs along the line: no one point
  const double s = ga / (ga - gb);
  if (!(s >= 0.0 && s <= 1.0)) return false;
  out = sweep->evaluate(t, (double(i) + s) / double(n));
  return true;
}

std::vector<Vector3d> Meridian::corners(const Vector3d& p0, const Vector3d& p1) const
{
  const double t0 = station(p0), t1 = station(p1);
  const double lo = std::min(t0, t1), hi = std::max(t0, t1);
  if (!(hi > lo)) return {};
  // The residual of a vertex is linear in t between keys, so each interval is
  // one linear root.
  std::vector<double> keys{lo};
  for (const double k : sweep->laws.corners()) {
    if (k > lo && k < hi) keys.push_back(k);
  }
  keys.push_back(hi);
  std::vector<std::pair<double, Vector3d>> found;
  const std::size_t n = sweep->laws.points();
  // Only the vertices of the edges the two ends lie on: a vertex reaching the
  // other surface elsewhere - the far side of a lens - is not this stretch's.
  const std::vector<std::size_t> e0 = edgesOf(p0), e1 = edgesOf(p1);
  std::vector<std::size_t> near;
  for (const auto *edges : {&e0, &e1}) {
    for (const std::size_t e : *edges) {
      near.push_back(e);
      near.push_back((e + 1) % n);
    }
  }
  std::sort(near.begin(), near.end());
  near.erase(std::unique(near.begin(), near.end()), near.end());
  for (const std::size_t i : near) {
    for (std::size_t k = 0; k + 1 < keys.size(); k++) {
      const double ta = keys[k], tb = keys[k + 1];
      const double ga = residual(ta, sweep->laws.pointAt(i, ta));
      const double gb = residual(tb, sweep->laws.pointAt(i, tb));
      if ((ga > 0) == (gb > 0) || ga == gb) continue;
      const double t = ta + (tb - ta) * ga / (ga - gb);
      if (!(t > lo && t < hi)) continue;
      found.emplace_back(t, sweep->evaluate(t, double(i) / double(n)));
    }
  }
  // And where a law turns, the crossing turns with it: the point at that
  // station on the edge both ends lie on.
  for (std::size_t k = 1; k + 1 < keys.size(); k++) {
    for (const std::size_t i : e0) {
      if (std::find(e1.begin(), e1.end(), i) == e1.end()) continue;
      Vector3d at;
      if (crossingOnEdge(keys[k], i, at)) found.emplace_back(keys[k], at);
      break;
    }
  }
  std::sort(found.begin(), found.end(), [&](const auto& x, const auto& y) {
    return t0 <= t1 ? x.first < y.first : x.first > y.first;
  });
  std::vector<Vector3d> out;
  for (const auto& f : found) out.push_back(f.second);
  return out;
}

}  // namespace step
