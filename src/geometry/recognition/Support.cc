/*! How far a solid reaches in a direction - see Support.h. */
#include "geometry/recognition/Support.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "geometry/MachinePrecision.h"
#include "geometry/Surface.h"
#include "geometry/recognition/Exact.h"

namespace recognition {

namespace {

/*! A declared primitive's support value, read off its bounds. */
bool declaredSupport(const DeclaredSolid& solid, const Vector3d& n, double& h, double& err)
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
  if (sphere != nullptr) {
    // A whole ball - one sliced by planes reaches less far, by how much its
    // bounds alone do not say here.
    if (axial != nullptr || !planes.empty()) return false;
    h = n.dot(sphere->refpt) + sphere->r * size;
    err = forwardError(8, size * (sphere->refpt.norm() + sphere->r));
    return true;
  }
  if (axial != nullptr) {
    // A frustum between two caps across its axis: it reaches furthest at a cap's
    // rim, the point of the rim furthest along n's part across the axis.
    const Vector3d a = axial->normdir.normalized();
    std::vector<double> at;
    for (const PlaneSurface *p : planes) {
      if (!parallel(p->normdir, a)) return false;  // a slice's flat sides: not this shape
      at.push_back(a.dot(p->refpt - axial->refpt));
    }
    if (at.size() != 2) return false;
    const auto *cone = dynamic_cast<const ConeSurface *>(axial);
    const Vector3d across = n - a * a.dot(n);
    h = -std::numeric_limits<double>::infinity();
    err = 0;
    for (const double t : at) {
      const auto *cyl = dynamic_cast<const CylinderSurface *>(axial);
      const double r = cone != nullptr ? cone->r + cone->slope * t : cyl->r;
      if (r < 0) return false;
      h = std::max(h, n.dot(axial->refpt + a * t) + r * across.norm());
      err = std::max(err, forwardError(16, size * (axial->refpt.norm() + std::fabs(t) + r)));
    }
    return true;
  }
  // A polytope: its vertices, each where three of its planes cross and inside
  // every other - and bounded, which its generator states by its extent.
  if (planes.size() < 4 || solid.extent.isEmpty()) return false;
  double scale = solid.interior.norm();
  for (const PlaneSurface *p : planes) scale += p->refpt.norm();
  const double tol = forwardError(32, scale);
  bool any = false;
  h = -std::numeric_limits<double>::infinity();
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
        if (!inside) continue;
        any = true;
        h = std::max(h, n.dot(x));
      }
    }
  }
  err = forwardError(32, size * scale);
  return any;
}

}  // namespace

bool supportValue(const SolidTree& tree, const Vector3d& n, double& h, double& err)
{
  using Op = SolidTree::Op;
  switch (tree.op()) {
  case Op::Declared:  return declaredSupport(*tree.solid(), n, h, err);
  // A union reaches as far as its farthest operand, and so does a hull - whose
  // held solids are its operands.
  case Op::Union:
  case Op::Enveloped: {
    if (tree.children().empty()) return false;
    h = -std::numeric_limits<double>::infinity();
    err = 0;
    for (const auto& child : tree.children()) {
      double hc = 0, ec = 0;
      if (!supportValue(*child, n, hc, ec)) return false;
      h = std::max(h, hc);
      err = std::max(err, ec);
    }
    return true;
  }
  default: return false;
  }
}

}  // namespace recognition
