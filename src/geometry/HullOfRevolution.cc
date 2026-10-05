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

namespace {

/*! A solid of revolution's own bounds, read from a declared solid: exactly one
 * lateral surface, exactly two planes across its axis, and every other bound a
 * plane through its axis - which cut it to a sector, kept on its interior's
 * side. `kept` holds those planes' normals turned to the side kept. */
struct Revolved {
  std::vector<std::shared_ptr<Surface>> base;  // the lateral surface and its caps
  Axis axis;
  OperandProfile profile;
  const Surface *lateral = nullptr;
  std::vector<Vector3d> kept;
};

/*! Whether `p` contains the line of `axis`, at machine precision. */
bool throughAxis(const PlaneSurface& p, const Axis& axis)
{
  const Vector3d n = p.normdir.normalized();
  return recognition::onPlane(p.refpt, n, axis.origin) &&
         recognition::onPlane(p.refpt, n, axis.origin + axis.dir);
}

bool readRevolved(const DeclaredSolid& solid, Revolved& out)
{
  std::vector<std::shared_ptr<Surface>> lateral, planes;
  for (const auto& bound : solid.bounds) {
    if (bound == nullptr) return false;
    if (dynamic_cast<const CylinderSurface *>(bound.get()) != nullptr ||
        dynamic_cast<const ConeSurface *>(bound.get()) != nullptr) {
      lateral.push_back(bound);
    } else if (dynamic_cast<const PlaneSurface *>(bound.get()) != nullptr) {
      planes.push_back(bound);
    } else {
      return false;
    }
  }
  if (lateral.size() != 1) return false;
  out.lateral = lateral.front().get();
  out.axis.origin = out.lateral->refpt;
  out.axis.dir = out.lateral->normdir.normalized();
  out.base = {lateral.front()};
  out.kept.clear();
  for (const auto& plane : planes) {
    const auto& p = static_cast<const PlaneSurface&>(*plane);
    if (throughAxis(p, out.axis)) {
      Vector3d n = p.normdir.normalized();
      if (n.dot(solid.interior - p.refpt) < 0) n = -n;
      out.kept.push_back(n);
    } else {
      out.base.push_back(plane);
    }
  }
  return out.kept.size() <= 2 && profileOf(out.base, out.axis, out.profile);
}

/*! The radius of a lateral surface where `axis` passes level `t`. */
double radiusAtLevel(const Surface *lateral, const Axis& axis, double t)
{
  if (const auto *cyl = dynamic_cast<const CylinderSurface *>(lateral)) return cyl->r;
  return static_cast<const ConeSurface *>(lateral)->radiusAt(axis.origin + axis.dir * t);
}

/*! Whether the whole of `base` lies on the side of `plane` that `interior` is
 * on: the solid is the hull of its two rims, and over a rim of centre c and
 * radius r about unit axis a the least of m.x is m.c - r |m - (m.a) a| - in
 * closed form, against its forward error. */
bool baseWithin(const Revolved& base, const PlaneSurface& plane, const Vector3d& interior)
{
  Vector3d m = plane.normdir.normalized();
  if (m.dot(interior - plane.refpt) < 0) m = -m;
  const double across = (m - base.axis.dir * m.dot(base.axis.dir)).norm();
  for (const double t : {base.profile.t0, base.profile.t1}) {
    const Vector3d c = base.axis.origin + base.axis.dir * t;
    const double r = radiusAtLevel(base.lateral, base.axis, t);
    const double least = m.dot(c - plane.refpt) - r * across;
    if (least < -forwardError(24, c.norm() + plane.refpt.norm() + r)) return false;
  }
  return true;
}

/*! An operand's solid tree read as a sector of a solid of revolution. */
bool readSector(const SolidTree& tree, Revolved& out)
{
  if (tree.op() == SolidTree::Op::Declared)
    return tree.solid() != nullptr && readRevolved(*tree.solid(), out);
  if (tree.op() != SolidTree::Op::Difference || tree.children().empty()) return false;
  const auto& first = tree.children().front();
  if (first == nullptr || first->op() != SolidTree::Op::Declared || first->solid() == nullptr ||
      !readRevolved(*first->solid(), out)) {
    return false;
  }
  // What is cut away: each solid subtracted, a union of them being each of its
  // members - difference() { a; b; for (...) c; } subtracts the group the loop
  // makes.
  std::vector<std::shared_ptr<const SolidTree>> cuts;
  std::vector<std::shared_ptr<const SolidTree>> open(tree.children().begin() + 1, tree.children().end());
  while (!open.empty()) {
    const auto next = open.back();
    open.pop_back();
    if (next != nullptr && next->op() == SolidTree::Op::Union) {
      open.insert(open.end(), next->children().begin(), next->children().end());
    } else {
      cuts.push_back(next);
    }
  }
  for (const auto& cut : cuts) {
    if (cut == nullptr || cut->op() != SolidTree::Op::Declared || cut->solid() == nullptr) return false;
    const DeclaredSolid& solid = *cut->solid();
    // A coaxial bore inside the lateral surface at both caps leaves every rim
    // of the outside where it was.
    Revolved bore;
    if (readRevolved(solid, bore) && bore.kept.empty() && sameLine(bore.axis, out.axis)) {
      bool inside = true;
      for (const double t : {out.profile.t0, out.profile.t1}) {
        const Vector3d at = out.axis.origin + out.axis.dir * t;
        const double rb =
          radiusAtLevel(bore.lateral, bore.axis, (at - bore.axis.origin).dot(bore.axis.dir));
        const double ro = radiusAtLevel(out.lateral, out.axis, t);
        inside = inside && rb < ro - forwardError(24, at.norm() + ro);
      }
      if (inside) continue;
      return false;
    }
    // A solid removing one side of a plane through the axis, over the whole
    // base: exactly one of its bounds is such a plane, every bound a plane,
    // and the base within every other one - so within the base it is that
    // half-space and nothing more. The side kept is the other one.
    const PlaneSurface *cutting = nullptr;
    bool half_space = true;
    for (const auto& bound : solid.bounds) {
      const auto *plane = dynamic_cast<const PlaneSurface *>(bound.get());
      if (plane == nullptr) {
        half_space = false;
        break;
      }
      if (throughAxis(*plane, out.axis)) {
        if (cutting != nullptr) {
          half_space = false;
          break;
        }
        cutting = plane;
      } else if (!baseWithin(out, *plane, solid.interior)) {
        half_space = false;
        break;
      }
    }
    if (!half_space || cutting == nullptr) return false;
    Vector3d n = cutting->normdir.normalized();
    if (n.dot(solid.interior - cutting->refpt) > 0) n = -n;
    out.kept.push_back(n);
  }
  // At most two planes, and not two opposite ones: a wedge with an inside, of
  // at most half a turn.
  if (out.kept.size() > 2) return false;
  if (out.kept.size() == 2 && recognition::parallel(out.kept[0], out.kept[1]) &&
      out.kept[0].dot(out.kept[1]) < 0) {
    return false;
  }
  return true;
}

/*! Whether two wedges are one: the same planes, keeping the same sides. */
bool sameWedge(const std::vector<Vector3d>& a, const std::vector<Vector3d>& b)
{
  if (a.size() != b.size()) return false;
  for (const Vector3d& n : a) {
    bool found = false;
    for (const Vector3d& m : b) found = found || (recognition::parallel(n, m) && n.dot(m) > 0);
    if (!found) return false;
  }
  return true;
}

}  // namespace

std::vector<std::vector<std::shared_ptr<Surface>>> revolutionOperands(
  const std::vector<std::vector<std::shared_ptr<Surface>>>& records,
  const std::vector<std::shared_ptr<const SolidTree>>& trees)
{
  if (trees.size() != records.size() || records.size() < 2) return records;
  std::vector<Revolved> sectors(trees.size());
  for (std::size_t i = 0; i < trees.size(); i++) {
    if (trees[i] == nullptr || !readSector(*trees[i], sectors[i])) return records;
  }
  // Whole solids of revolution are what the operands' own records already say.
  if (sectors.front().kept.empty()) return records;
  for (const Revolved& s : sectors) {
    if (!sameWedge(s.kept, sectors.front().kept)) return records;
  }
  std::vector<std::vector<std::shared_ptr<Surface>>> bases;
  bases.reserve(sectors.size());
  for (const Revolved& s : sectors) bases.push_back(s.base);
  return bases;
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
    // A triangle whose plane holds the axis lies in a meridian plane, and so on
    // no turned surface: a facet of a cone or a cylinder spans two angles. A
    // sector's cut leaves such a side, every corner of which can still be on
    // the rings of one profile edge - three corners on the r 10 rings of a
    // half cylinder's flat side are - so ring membership alone would name it
    // for the cylinder (open item 39, R29).
    bool meridian = true;
    for (const std::size_t v : triangles[t]) meridian = meridian && v < vertices.size();
    if (meridian) {
      const Vector3d& a = vertices[triangles[t][0]];
      const Vector3d& b = vertices[triangles[t][1]];
      const Vector3d& c = vertices[triangles[t][2]];
      meridian = recognition::coplanar(a, b, c, axis.origin) &&
                 recognition::coplanar(a, b, c, axis.origin + dir * (1.0 + a.norm()));
    }
    if (meridian) continue;
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
