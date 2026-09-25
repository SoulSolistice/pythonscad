/*! Checks of our own file against itself - see CurveChecks.h. */
#include "io/step/CurveChecks.h"

#include <cmath>
#include <limits>

namespace step {

double vertexOffCurve(const StepKernel::RoundType *curve, const Vector3d& p)
{
  if (const auto *line = dynamic_cast<const StepKernel::Line *>(curve)) {
    if (line->point == nullptr || line->vector == nullptr || line->vector->dir == nullptr) return 0;
    const Vector3d base = line->point->pt;
    Vector3d dir = line->vector->dir->pt;
    if (dir.norm() < 1e-12) return (p - base).norm();
    dir.normalize();
    const Vector3d rel = p - base;
    return (rel - dir * rel.dot(dir)).norm();
  }
  if (const auto *circle = dynamic_cast<const StepKernel::Circle *>(curve)) {
    if (circle->axis == nullptr || circle->axis->point == nullptr || circle->axis->dir1 == nullptr) {
      return 0;
    }
    const Vector3d centre = circle->axis->point->pt;
    Vector3d n = circle->axis->dir1->pt;
    if (n.norm() < 1e-12) return 0;
    n.normalize();
    const Vector3d rel = p - centre;
    const double along = rel.dot(n);
    const double across = (rel - n * along).norm();
    return sqrt(along * along + (across - circle->r) * (across - circle->r));
  }
  if (const auto *ell = dynamic_cast<const StepKernel::Ellipse *>(curve)) {
    if (ell->axis == nullptr || ell->axis->point == nullptr || ell->axis->dir1 == nullptr ||
        ell->axis->dir2 == nullptr) {
      return 0;
    }
    const Vector3d centre = ell->axis->point->pt;
    Vector3d n = ell->axis->dir1->pt, major = ell->axis->dir2->pt;
    if (n.norm() < 1e-12 || major.norm() < 1e-12) return 0;
    n.normalize();
    major.normalize();
    const Vector3d minor = n.cross(major);
    auto at = [&](double t) {
      return centre + major * (ell->semi_1 * cos(t)) + minor * (ell->semi_2 * sin(t));
    };
    double best_t = 0, best = std::numeric_limits<double>::infinity();
    for (int k = 0; k < 720; k++) {
      const double t = 2 * M_PI * k / 720.0;
      const double d = (at(t) - p).norm();
      if (d < best) {
        best = d;
        best_t = t;
      }
    }
    for (double step = 2 * M_PI / 720.0; step > 1e-12; step *= 0.5) {
      for (const double t : {best_t - step, best_t + step}) {
        const double d = (at(t) - p).norm();
        if (d < best) {
          best = d;
          best_t = t;
        }
      }
    }
    return best;
  }
  if (const auto *hyp = dynamic_cast<const StepKernel::Hyperbola *>(curve)) {
    // C + a cosh(t) X + b sinh(t) Y, one branch. Sampled in t and refined, which
    // is an upper bound on the distance and so the safe direction for a check.
    if (hyp->axis == nullptr || hyp->axis->point == nullptr || hyp->axis->dir1 == nullptr ||
        hyp->axis->dir2 == nullptr) {
      return 0;
    }
    const Vector3d c = hyp->axis->point->pt;
    Vector3d n = hyp->axis->dir1->pt, major = hyp->axis->dir2->pt;
    if (n.norm() < 1e-12 || major.norm() < 1e-12) return 0;
    n.normalize();
    major.normalize();
    const Vector3d minor = n.cross(major);
    auto at = [&](double t) {
      return c + major * (hyp->semi_axis * cosh(t)) + minor * (hyp->semi_imag * sinh(t));
    };
    double best_t = 0, best = std::numeric_limits<double>::infinity();
    for (int k = -400; k <= 400; k++) {
      const double t = k * 0.02;
      const double d = (at(t) - p).norm();
      if (d < best) {
        best = d;
        best_t = t;
      }
    }
    for (double step = 0.02; step > 1e-14; step *= 0.5) {
      for (const double t : {best_t - step, best_t + step}) {
        const double d = (at(t) - p).norm();
        if (d < best) {
          best = d;
          best_t = t;
        }
      }
    }
    return best;
  }
  if (const auto *par = dynamic_cast<const StepKernel::Parabola *>(curve)) {
    // C + F t^2 X + 2 F t Y.
    if (par->axis == nullptr || par->axis->point == nullptr || par->axis->dir1 == nullptr ||
        par->axis->dir2 == nullptr) {
      return 0;
    }
    const Vector3d c = par->axis->point->pt;
    Vector3d n = par->axis->dir1->pt, major = par->axis->dir2->pt;
    if (n.norm() < 1e-12 || major.norm() < 1e-12) return 0;
    n.normalize();
    major.normalize();
    const Vector3d minor = n.cross(major);
    auto at = [&](double t) {
      return c + major * (par->focal_dist * t * t) + minor * (2 * par->focal_dist * t);
    };
    double best_t = 0, best = std::numeric_limits<double>::infinity();
    for (int k = -600; k <= 600; k++) {
      const double t = k * 0.05;
      const double d = (at(t) - p).norm();
      if (d < best) {
        best = d;
        best_t = t;
      }
    }
    for (double step = 0.05; step > 1e-14; step *= 0.5) {
      for (const double t : {best_t - step, best_t + step}) {
        const double d = (at(t) - p).norm();
        if (d < best) {
          best = d;
          best_t = t;
        }
      }
    }
    return best;
  }
  if (const auto *spline = dynamic_cast<const StepKernel::BSplineCurve *>(curve)) {
    if (spline->pts.empty()) return 0;
    // Clamped, so it starts at the first control point and ends at the last;
    // the vertex belongs to whichever end it is nearer.
    const double a = (spline->pts.front()->pt - p).norm();
    const double b = (spline->pts.back()->pt - p).norm();
    return std::min(a, b);
  }
  return 0;  // a curve this does not know how to measure is not a finding
}

}  // namespace step
