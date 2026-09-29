/*! The surfaces a hull of coaxial solids of revolution makes - see
 * HullOfRevolution.h. */
#include "geometry/HullOfRevolution.h"
#include "geometry/EdgeIdentity.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "geometry/Profile2d.h"
#include "geometry/SolidTree.h"
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

/*! Whether two axes are one line, at machine precision - recognition::sameLine. */
bool sameLine(const Axis& a, const Axis& b)
{
  return recognition::sameLine(a.origin, a.dir, b.origin, b.dir);
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

namespace {

/*! The hull of the operands' profiles in the (radius, level) half-plane about
 * their common axis, the axis included - every rim and its foot on it - or
 * false where the operands are not coaxial solids of revolution. */
bool hullProfile(const std::vector<std::vector<std::shared_ptr<Surface>>>& operands, Axis& axis,
                 std::vector<Vector2d>& hull)
{
  if (operands.size() < 2) return false;
  std::vector<OperandProfile> profiles;
  for (std::size_t i = 0; i < operands.size(); i++) {
    Axis own;
    OperandProfile profile;
    if (!profileOf(operands[i], own, profile)) return false;
    if (i == 0) {
      axis = own;
    } else if (!sameLine(axis, own)) {
      return false;
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
  std::vector<Vector2d> rims;
  for (const OperandProfile& p : profiles) {
    rims.emplace_back(p.r0, p.t0);
    rims.emplace_back(p.r1, p.t1);
    rims.emplace_back(0., p.t0);
    rims.emplace_back(0., p.t1);
  }
  hull = convexHull(std::move(rims));
  return hull.size() >= 3;
}

/*! The hull profile turned: the sweep table says what each edge makes - the
 * cone of a sloped edge, and the planes and cylinders the operands declared. */
std::vector<std::shared_ptr<Surface>> turned(const Axis& axis, const std::vector<Vector2d>& hull,
                                             SweptFaces *faces = nullptr)
{
  Profile2d profile;
  profile.loops.emplace_back();
  for (std::size_t i = 0; i < hull.size(); i++) {
    // Straight by construction: an edge of the convex hull of the rims, which
    // the hull states as the maker of this profile (E7). Named i + 1, so
    // `faces` says which record each edge turns into.
    ProfileEdge edge;
    edge.a = hull[i];
    edge.b = hull[(i + 1) % hull.size()];
    edge.kind = EdgeKind::Line;
    edge.declared = true;
    edge.id = EdgeId(i + 1);
    profile.loops.back().push_back(edge);
  }
  Revolution sweep;
  sweep.origin = axis.origin;
  sweep.axis = axis.dir;
  return revolve(profile, sweep, nullptr, faces);
}

/*! A unit vector across `dir`. */
Vector3d across(const Vector3d& dir)
{
  const Vector3d pick = std::fabs(dir[0]) < 0.9 ? Vector3d(1, 0, 0) : Vector3d(0, 1, 0);
  return dir.cross(pick).normalized();
}

}  // namespace

std::vector<std::shared_ptr<Surface>> declareHullOfRevolution(
  const std::vector<std::vector<std::shared_ptr<Surface>>>& operands)
{
  Axis axis;
  std::vector<Vector2d> hull;
  if (!hullProfile(operands, axis, hull)) return {};
  return turned(axis, hull);
}

std::vector<int> hullFacetRecords(const std::vector<std::vector<std::shared_ptr<Surface>>>& operands,
                                  const std::vector<Vector3d>& vertices,
                                  const std::vector<std::array<std::size_t, 3>>& triangles,
                                  std::vector<std::shared_ptr<Surface>>& records)
{
  Axis axis;
  std::vector<Vector2d> hull;
  records.clear();
  if (!hullProfile(operands, axis, hull)) return {};
  SweptFaces faces;
  records = turned(axis, hull, &faces);
  const Vector3d dir = axis.dir.normalized();

  // Each vertex's radius and level about the axis. A few roundings from the
  // point the operand's generator emitted - the forward error of this.
  auto rt = [&](const Vector3d& p) {
    const Vector3d rel = p - axis.origin;
    const double t = rel.dot(dir);
    return Vector2d((rel - dir * t).norm(), t);
  };
  auto same = [](double u, double v, double scale) {
    return std::fabs(u - v) <= recognition::forwardError(24, scale);
  };
  const auto n = hull.size();
  // Whether q lies on edge k of the profile, from hull[k] to hull[k + 1]:
  // on its line and between its ends, each at machine precision.
  auto onEdge = [&](const Vector2d& q, std::size_t k) {
    const Vector2d a = hull[k], b = hull[(k + 1) % n];
    const Vector2d d = b - a, w = q - a;
    const double scale = (std::fabs(q[0]) + std::fabs(q[1]) + a.norm() + b.norm());
    if (!same(d[0] * w[1] - d[1] * w[0], 0., scale * d.norm())) return false;
    const double along = w.dot(d);
    return along >= -recognition::forwardError(24, scale * d.norm()) &&
           along <= d.squaredNorm() + recognition::forwardError(24, scale * d.norm());
  };
  auto recordOf = [&](std::size_t k) {
    const auto it = faces.walls.find(EdgeId(k + 1));
    return it == faces.walls.end() ? -1 : it->second;
  };

  std::vector<int> out(triangles.size(), -1);
  for (std::size_t t = 0; t < triangles.size(); t++) {
    std::vector<Vector2d> rings;
    for (const std::size_t v : triangles[t]) {
      if (v >= vertices.size()) {
        rings.clear();
        break;
      }
      const Vector2d q = rt(vertices[v]);
      bool known = false;
      for (const Vector2d& r : rings) {
        const double scale = std::fabs(q[0]) + std::fabs(q[1]) + std::fabs(r[0]) + std::fabs(r[1]);
        known = known || (same(q[0], r[0], scale) && same(q[1], r[1], scale));
      }
      if (!known) rings.push_back(q);
    }
    if (rings.empty()) continue;
    int found = -1, count = 0;
    for (std::size_t k = 0; k < n; k++) {
      const Vector2d a = hull[k], b = hull[(k + 1) % n];
      bool all = true;
      for (const Vector2d& q : rings) all = all && onEdge(q, k);
      if (!all) continue;
      // One ring: the triangle is in that ring's plane, so only a cap - an edge
      // at one level - holds it; a ring at a corner of a sloped edge does not.
      const double scale = std::fabs(a[1]) + std::fabs(b[1]);
      if (rings.size() == 1 && !same(a[1], b[1], scale)) continue;
      found = recordOf(k);
      count++;
    }
    if (count == 1) out[t] = found;
  }
  return out;
}

std::shared_ptr<const DeclaredSolid> solidOfHullOfRevolution(
  const std::vector<std::vector<std::shared_ptr<Surface>>>& operands)
{
  Axis axis;
  std::vector<Vector2d> hull;
  if (!hullProfile(operands, axis, hull)) return nullptr;
  auto solid = std::make_shared<DeclaredSolid>();
  for (const auto& record : turned(axis, hull)) {
    if (record != nullptr && !record->evidence_only) solid->bounds.push_back(record);
  }
  // Every edge of the profile off the axis has to be a bound, or the
  // intersection is larger than the hull and would call air material: each
  // needs a bound holding both its ends, turned to any one angle, exactly.
  const Vector3d out = across(axis.dir);
  auto at = [&](const Vector2d& rt) { return axis.origin + axis.dir * rt[1] + out * rt[0]; };
  for (std::size_t i = 0; i < hull.size(); i++) {
    const Vector2d& a = hull[i];
    const Vector2d& b = hull[(i + 1) % hull.size()];
    if (a[0] == 0 && b[0] == 0) continue;  // along the axis: no surface
    bool held = false;
    for (const auto& bound : solid->bounds) {
      held = held || (bound->residual(at(a)).exact() && bound->residual(at(b)).exact());
    }
    if (!held) return nullptr;
  }
  // Inside a convex profile: the mean of its corners, off the axis.
  Vector2d mean(0, 0);
  double reach = 0, lo = hull.front()[1], hi = lo;
  for (const auto& p : hull) {
    mean += p;
    reach = std::max(reach, p[0]);
    lo = std::min(lo, p[1]);
    hi = std::max(hi, p[1]);
  }
  mean /= double(hull.size());
  solid->interior = at(mean);
  for (const double t : {lo, hi}) {
    const Vector3d c = axis.origin + axis.dir * t;
    Vector3d spread;
    for (int k = 0; k < 3; k++)
      spread[k] = reach * std::sqrt(std::max(0.0, 1 - axis.dir[k] * axis.dir[k]));
    solid->extent.extend(c - spread);
    solid->extent.extend(c + spread);
  }
  return solid;
}
