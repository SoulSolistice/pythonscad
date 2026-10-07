/*! How far a solid reaches in a direction - see Support.h. */
#include "geometry/recognition/Support.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "geometry/MachinePrecision.h"
#include "geometry/Surface.h"
#include "geometry/recognition/Arrangement.h"
#include "geometry/recognition/Classify.h"
#include "geometry/recognition/Exact.h"

namespace recognition {

namespace {

/*! Keep the candidates that reach as far as the farthest, within its error. */
void attaining(Reach& r, const std::vector<std::pair<double, Vector3d>>& candidates)
{
  r.h = -std::numeric_limits<double>::infinity();
  for (const auto& [v, x] : candidates) r.h = std::max(r.h, v);
  for (const auto& [v, x] : candidates) {
    if (v >= r.h - 2 * r.err) r.at.push_back(x);
  }
}

/*! A declared primitive's reach, read off its bounds. */
bool declaredReach(const DeclaredSolid& solid, const Vector3d& n, Reach& out)
{
  const SphereSurface *sphere = nullptr;
  const AxialSurface *axial = nullptr;
  std::vector<const PlaneSurface *> planes;
  for (const auto& b : solid.bounds) {
    if (const auto *s = dynamic_cast<const SphereSurface *>(b.get())) {
      if (sphere != nullptr) return false;
      sphere = s;
    } else if (const auto *p = dynamic_cast<const PlaneSurface *>(b.get())) {
      planes.push_back(p);
    } else if (const auto *a = dynamic_cast<const AxialSurface *>(b.get())) {
      if (axial != nullptr || (dynamic_cast<const CylinderSurface *>(a) == nullptr &&
                               dynamic_cast<const ConeSurface *>(a) == nullptr))
        return false;
      axial = a;
    } else {
      return false;
    }
  }
  const double size = n.norm();
  if (size == 0) return false;
  const Vector3d unit = n / size;
  if (sphere != nullptr) {
    // A whole ball - one sliced by planes reaches less far, by how much its
    // bounds alone do not say here.
    if (axial != nullptr || !planes.empty()) return false;
    out.err = forwardError(8, size * (sphere->refpt.norm() + sphere->r));
    attaining(out, {{n.dot(sphere->refpt) + sphere->r * size, sphere->refpt + unit * sphere->r}});
    return true;
  }
  if (axial != nullptr) {
    // A frustum between two caps across its axis: it reaches furthest at a cap's
    // rim, the point of the rim furthest along n's part across the axis - or,
    // with n along the axis, at a whole cap.
    const Vector3d a = axial->normdir.normalized();
    std::vector<double> at;
    for (const PlaneSurface *p : planes) {
      if (!parallel(p->normdir, a)) return false;  // a slice's flat sides: not this shape
      at.push_back(a.dot(p->refpt - axial->refpt));
    }
    if (at.size() != 2) return false;
    const auto *cone = dynamic_cast<const ConeSurface *>(axial);
    const auto *cyl = dynamic_cast<const CylinderSurface *>(axial);
    const Vector3d across = n - a * a.dot(n);
    const bool square = parallel(n, a);
    std::vector<std::pair<double, Vector3d>> rims;
    out.err = 0;
    for (const double t : at) {
      const double r = cone != nullptr ? cone->r + cone->slope * t : cyl->r;
      if (r < 0) return false;
      const Vector3d centre = axial->refpt + a * t;
      const Vector3d rim = square ? centre : Vector3d(centre + across.normalized() * r);
      rims.emplace_back(n.dot(centre) + r * across.norm(), rim);
      out.err = std::max(out.err, forwardError(16, size * (axial->refpt.norm() + std::fabs(t) + r)));
    }
    attaining(out, rims);
    out.finite = !square;
    return true;
  }
  // A polytope: its vertices, each where three of its planes cross and inside
  // every other - and bounded, which its generator states by its extent.
  if (planes.size() < 4 || solid.extent.isEmpty()) return false;
  double scale = solid.interior.norm();
  for (const PlaneSurface *p : planes) scale += p->refpt.norm();
  const double tol = forwardError(32, scale);
  std::vector<std::pair<double, Vector3d>> vertices;
  for (std::size_t i = 0; i < planes.size(); i++) {
    for (std::size_t j = i + 1; j < planes.size(); j++) {
      for (std::size_t k = j + 1; k < planes.size(); k++) {
        Matrix3d m;
        m.row(0) = planes[i]->normdir.transpose();
        m.row(1) = planes[j]->normdir.transpose();
        m.row(2) = planes[k]->normdir.transpose();
        if (std::fabs(m.determinant()) <= forwardError(12, 1.0)) continue;
        const Vector3d rhs(planes[i]->normdir.dot(planes[i]->refpt),
                           planes[j]->normdir.dot(planes[j]->refpt),
                           planes[k]->normdir.dot(planes[k]->refpt));
        const Vector3d x = m.inverse() * rhs;
        bool inside = true;
        for (const PlaneSurface *p : planes) {
          const double sx = p->normdir.dot(x - p->refpt), si = p->normdir.dot(solid.interior - p->refpt);
          inside = inside && (std::fabs(sx) <= tol || (sx < 0) == (si < 0));
        }
        if (inside) vertices.emplace_back(n.dot(x), x);
      }
    }
  }
  if (vertices.empty()) return false;
  out.err = forwardError(32, size * scale);
  // A polytope's support set is the hull of the vertices that reach as far.
  attaining(out, vertices);
  return true;
}

/*! What a boolean of declared primitives keeps of its arrangement, once: its
 * leaf surfaces, and the vertices it holds - neither depends on n. */
struct Arranged {
  bool complete = false;
  std::vector<const Surface *> surfaces;
  std::vector<ArrangementPoint> vertices;
};

const Arranged& arrangedOf(const SolidTree& tree)
{
  if (const auto *kept = static_cast<const Arranged *>(tree.derived().get())) return *kept;
  auto made = std::make_shared<Arranged>();
  std::vector<ArrangementPoint> points;
  made->complete = leafSurfaces(tree, made->surfaces) && triplePoints(made->surfaces, points);
  for (const ArrangementPoint& p : points) {
    if (!made->complete) break;
    const Classification c = classifyAt(tree, p.at, p.on);
    if (c.where == Where::Unknown) made->complete = false;
    else if (c.where != Where::Out) made->vertices.push_back(p);
  }
  tree.keepDerived(made);
  return *made;
}

/*! A boolean of declared primitives: its farthest point along n is stationary
 * on some piece of its boundary - a surface's own point, a curve's, or a
 * vertex (Arrangement.h) - so it is the farthest of those the solid holds.
 * Each is asked as lying on the surfaces it was computed from; one the model
 * cannot place leaves the reach unstated rather than too small. */
bool booleanReach(const SolidTree& tree, const Vector3d& n, Reach& out)
{
  const Arranged& arranged = arrangedOf(tree);
  if (!arranged.complete) return false;
  std::vector<ArrangementPoint> critical;
  if (!criticalPoints(arranged.surfaces, n, critical)) return false;
  std::vector<std::pair<double, Vector3d>> candidates;
  std::vector<const ArrangementPoint *> sources;
  double scale = 0;
  for (const ArrangementPoint& p : arranged.vertices) {
    candidates.emplace_back(n.dot(p.at), p.at);
    sources.push_back(&p);
    scale = std::max(scale, p.at.norm());
  }
  for (const ArrangementPoint& p : critical) {
    const Classification c = classifyAt(tree, p.at, p.on);
    if (c.where == Where::Unknown) return false;
    if (c.where == Where::Out) continue;
    candidates.emplace_back(n.dot(p.at), p.at);
    sources.push_back(&p);
    scale = std::max(scale, p.at.norm());
  }
  if (candidates.empty()) return false;
  out.err = forwardError(64, n.norm() * scale);
  attaining(out, candidates);
  // Where an attaining point is on a plane square to n, or on a surface about
  // an axis along n, the whole face or rim reaches as far: not these points
  // alone.
  for (std::size_t k = 0; k < candidates.size(); k++) {
    if (candidates[k].first < out.h - 2 * out.err) continue;
    for (const Surface *s : sources[k]->on) {
      if (const auto *plane = dynamic_cast<const PlaneSurface *>(s)) {
        if (parallel(plane->normdir, n)) out.finite = false;
      } else if (const auto *axial = dynamic_cast<const AxialSurface *>(s)) {
        if (parallel(axial->normdir, n)) out.finite = false;
      }
    }
  }
  return true;
}

}  // namespace

bool reachOf(const SolidTree& tree, const Vector3d& n, Reach& out)
{
  using Op = SolidTree::Op;
  out = Reach{};
  switch (tree.op()) {
  case Op::Declared:     return declaredReach(*tree.solid(), n, out);
  case Op::Difference:
  case Op::Intersection: return booleanReach(tree, n, out);
  // A union reaches as far as its farthest operand, and so does a hull - whose
  // held solids are its operands.
  case Op::Union:
  case Op::Enveloped:    {
    if (tree.children().empty()) return false;
    std::vector<Reach> parts;
    out.h = -std::numeric_limits<double>::infinity();
    for (const auto& child : tree.children()) {
      Reach r;
      if (!reachOf(*child, n, r)) return false;
      out.h = std::max(out.h, r.h);
      out.err = std::max(out.err, r.err);
      parts.push_back(std::move(r));
    }
    for (const Reach& r : parts) {
      if (r.h < out.h - 2 * out.err) continue;
      out.at.insert(out.at.end(), r.at.begin(), r.at.end());
      out.finite = out.finite && r.finite;
    }
    return true;
  }
  default: return false;
  }
}

bool supportValue(const SolidTree& tree, const Vector3d& n, double& h, double& err)
{
  Reach r;
  if (!reachOf(tree, n, r)) return false;
  h = r.h;
  err = r.err;
  return true;
}

}  // namespace recognition
