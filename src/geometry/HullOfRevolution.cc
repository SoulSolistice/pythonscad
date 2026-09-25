/*! The surfaces a hull of coaxial solids of revolution makes - see
 * HullOfRevolution.h. */
#include "geometry/HullOfRevolution.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "geometry/Profile2d.h"
#include "geometry/Surface.h"
#include "geometry/SweptSurfaces.h"
#include "geometry/linalg.h"
#include "geometry/recognition/Exact.h"

using recognition::forwardError;

namespace {

/*! One operand's profile about the common axis: its radius at each cap. */
struct OperandProfile {
  double t0 = 0, t1 = 0;  // the caps' levels along the axis
  double r0 = 0, r1 = 0;  // the lateral surface's radius there
};

/*! The axis a lateral surface turns about. */
struct Axis {
  Vector3d origin{0, 0, 0}, dir{0, 0, 1};
};

/*! Whether two axes are one line, at machine precision: parallel (two
 * normalisations, 9 each, and a cross, 9 more) and the second's point on the
 * first line (a difference, a dot, a scale, a difference, a norm: 20). */
bool sameLine(const Axis& a, const Axis& b)
{
  if (a.dir.cross(b.dir).norm() > forwardError(27, 2.0)) return false;
  const Vector3d d = b.origin - a.origin;
  const double scale = a.origin.norm() + b.origin.norm();
  return (d - a.dir * d.dot(a.dir)).norm() <= forwardError(20, scale);
}

/*! An operand's profile, from its face declarations alone: exactly one lateral
 * surface - a cylinder or a cone - and exactly two planes across its axis. */
bool profileOf(const std::vector<std::shared_ptr<Surface>>& records, Axis& axis, OperandProfile& out)
{
  const Surface *lateral = nullptr;
  std::vector<const Surface *> planes;
  for (const auto& record : records) {
    if (record == nullptr || !record->declaresFace()) continue;
    const Surface *s = record.get();
    if (dynamic_cast<const CylinderSurface *>(s) != nullptr ||
        dynamic_cast<const ConeSurface *>(s) != nullptr) {
      if (lateral != nullptr) return false;  // two lateral surfaces: not one solid of revolution
      lateral = s;
    } else if (dynamic_cast<const PlaneSurface *>(s) != nullptr) {
      planes.push_back(s);
    } else {
      return false;  // a sphere, a torus, a sweep: not this shape
    }
  }
  if (lateral == nullptr || planes.size() != 2) return false;
  axis.origin = lateral->refpt;
  axis.dir = lateral->normdir.normalized();
  double t[2];
  for (int i = 0; i < 2; i++) {
    const Vector3d n = planes[i]->normdir.normalized();
    // across the axis: the plane's normal along it, at machine precision
    if (n.cross(axis.dir).norm() > forwardError(27, 2.0)) return false;
    t[i] = (planes[i]->refpt - axis.origin).dot(axis.dir);
  }
  auto radius = [&](double level) {
    if (const auto *cyl = dynamic_cast<const CylinderSurface *>(lateral)) return cyl->r;
    return static_cast<const ConeSurface *>(lateral)->radiusAt(axis.origin + axis.dir * level);
  };
  out.t0 = std::min(t[0], t[1]);
  out.t1 = std::max(t[0], t[1]);
  out.r0 = radius(out.t0);
  out.r1 = radius(out.t1);
  return out.r0 >= 0 && out.r1 >= 0;
}

double cross(const Vector2d& o, const Vector2d& a, const Vector2d& b)
{
  return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
}

/*! The convex hull of `points`, counter-clockwise, by Andrew's monotone chain. */
std::vector<Vector2d> convexHull(std::vector<Vector2d> points)
{
  auto less = [](const Vector2d& a, const Vector2d& b) {
    return a[0] < b[0] || (a[0] == b[0] && a[1] < b[1]);
  };
  std::sort(points.begin(), points.end(), less);
  points.erase(std::unique(points.begin(), points.end()), points.end());
  if (points.size() < 3) return points;
  std::vector<Vector2d> hull(2 * points.size());
  std::size_t k = 0;
  for (const Vector2d& p : points) {
    while (k >= 2 && cross(hull[k - 2], hull[k - 1], p) <= 0) k--;
    hull[k++] = p;
  }
  for (std::size_t i = points.size() - 1, lower = k + 1; i-- > 0;) {
    while (k >= lower && cross(hull[k - 2], hull[k - 1], points[i]) <= 0) k--;
    hull[k++] = points[i];
  }
  hull.resize(k - 1);
  return hull;
}

}  // namespace

std::vector<std::shared_ptr<Surface>> declareHullOfRevolution(
  const std::vector<std::vector<std::shared_ptr<Surface>>>& operands)
{
  std::vector<std::shared_ptr<Surface>> out;
  if (operands.size() < 2) return out;
  Axis axis;
  std::vector<OperandProfile> profiles;
  for (std::size_t i = 0; i < operands.size(); i++) {
    Axis own;
    OperandProfile profile;
    if (!profileOf(operands[i], own, profile)) return out;
    if (i == 0) {
      axis = own;
    } else if (!sameLine(axis, own)) {
      return out;
    } else {
      // the same line, possibly measured from another point: re-level
      const double shift = (own.origin - axis.origin).dot(axis.dir);
      const double sign = own.dir.dot(axis.dir) < 0 ? -1.0 : 1.0;
      OperandProfile p = profile;
      p.t0 = shift + sign * profile.t0;
      p.t1 = shift + sign * profile.t1;
      if (p.t0 > p.t1) {
        std::swap(p.t0, p.t1);
        std::swap(p.r0, p.r1);
      }
      profile = p;
    }
    profiles.push_back(profile);
  }

  // The hull of the profiles in the (radius, level) half-plane, the axis
  // included: every rim, and its foot on the axis.
  std::vector<Vector2d> rims;
  for (const OperandProfile& p : profiles) {
    rims.emplace_back(p.r0, p.t0);
    rims.emplace_back(p.r1, p.t1);
    rims.emplace_back(0., p.t0);
    rims.emplace_back(0., p.t1);
  }
  const std::vector<Vector2d> hull = convexHull(std::move(rims));
  if (hull.size() < 3) return out;

  // The hull, a profile of straight edges in the (radius, level) half-plane, is
  // what gets turned: the sweep table says what each edge makes - the cone of
  // a sloped edge, and the planes and cylinders the operands declared already.
  Profile2d profile;
  profile.loops.emplace_back();
  for (std::size_t i = 0; i < hull.size(); i++) {
    const auto& a = hull[i];
    const auto& b = hull[(i + 1) % hull.size()];
    ProfileEdge edge;
    edge.a = a;
    edge.b = b;
    profile.loops.back().push_back(edge);
  }
  Revolution sweep;
  sweep.origin = axis.origin;
  sweep.axis = axis.dir;
  out = revolve(profile, sweep);
  return out;
}
