#pragma once

/*! D0 membership of a point: is it on a declared surface, exactly?
 *
 * doc/step-rungs.md, D0: *fixed by declarations alone - no fit, no tolerance,
 * no sample*. So "on the surface" at D0 admits no chosen threshold; the only
 * allowance is what the arithmetic itself cannot avoid. A point the generator
 * emitted on a surface, asked of that surface in floating point, comes back
 * with a residual that is not zero - and is bounded, by the forward error of
 * the evaluation, which is a property of the formula and of `DBL_EPSILON`, not
 * of the model or the mesh.
 *
 * The bound used is the classical one: an expression of n rounded operations
 * over inputs whose magnitudes sum to `scale` is off by at most
 * gamma_n = n eps / (1 - n eps) times `scale`. n is counted per surface kind
 * below from the residual as written, every operation counted whether or not
 * it lies on the longest dependency chain, so it bounds rather than estimates.
 * `scale` is |p| + |refpt| + the surface's own radii - every magnitude the
 * residual subtracts.
 *
 * What the bound does not state, and why that is not a gap measured today: the
 * point and the declaration are themselves computed - r cos(theta) by the
 * generator, each through the same chain of transforms - and their rounding
 * arrives as further error of the same kind. Measured on 2026-09-24 over every
 * reference part and fixture that carries provenance, both tiers: every mesh
 * vertex within 1e-7 of a declared curved surface is within one eps x scale of
 * it (worst 2.8e-14 in absolute terms, on lid10 and the bayonet), and none lies
 * anywhere between that and 1e-7. The 1e-7 this replaces in the quadric pass was
 * a threshold standing where only machine precision belongs, and the entity
 * census of all 106 exports shows it decided nothing there.
 *
 * Asked here by the quadric pass, by export_step.cc's `runsThrough` (ownership
 * and presence) and by the band pass's sphere and torus zones. The first export
 * it was not neutral on was step-rounded-profile, whose four tori became 32
 * cones: offset()'s points came off Clipper's 2^-27 grid, half a cell off the
 * arcs they were computed on, and the relative 1e-7 had hidden it. Fixed at the
 * origin - Polygon2d::placeOnArcs, after the offset and after every 2D boolean -
 * and asserted by step-rounded-profile-notched.
 *
 * Not yet asked here: the band pass still compares an axis and a radius it
 * measured from the mesh against the declaration, at a relative 1e-7, where the
 * D0 question is the generator's points against the declaration itself; a
 * declared grid still matches its net through a 1e-9 quantisation; and a Bezier
 * patch, D1, keeps its projection's tolerance.
 *
 * Anything that fails this is not on the surface at D0, however close. Its
 * membership is then a D4 question - the tessellation band - and is labelled so.
 */

#include <cfloat>
#include <cmath>
#include <limits>

#include "geometry/Surface.h"
#include "geometry/linalg.h"

namespace recognition {

/*! A residual and the machine-precision bound on computing it. */
struct Residual {
  double value = std::numeric_limits<double>::infinity();
  double bound = 0;
  /*! On the surface, as exactly as the arithmetic can say. */
  [[nodiscard]] bool exact() const { return value <= bound; }
};

/*! gamma_n x scale: the forward error of n rounded operations. */
inline double forwardError(int n, double scale)
{
  const double ne = n * DBL_EPSILON;
  return ne / (1 - ne) * scale;
}

/*! How far `p` is from a declared plane, cylinder, cone, sphere or torus, and the
 * bound the arithmetic puts on that figure. Any other kind of surface answers
 * with an infinite residual: it has no closed-form residual here. */
inline Residual residualOf(const Surface *s, const Vector3d& p)
{
  Residual out;
  if (s == nullptr) return out;
  const Vector3d rel = p - s->refpt;
  const double base = p.norm() + s->refpt.norm();
  if (const auto *cyl = dynamic_cast<const CylinderSurface *>(s)) {
    // axis: 3 mul, 2 add, sqrt, 3 div (9); rel: 3 (12); rel.axis: 3 mul, 2 add
    // (17); axis * t: 3 (20); rel - axis t: 3 (23); norm: 3 mul, 2 add, sqrt
    // (29); minus r (30).
    const Vector3d axis = cyl->normdir.normalized();
    out.value = fabs((rel - axis * rel.dot(axis)).norm() - cyl->r);
    out.bound = forwardError(30, base + fabs(cyl->r));
    return out;
  }
  if (const auto *cone = dynamic_cast<const ConeSurface *>(s)) {
    // The cylinder's 29 to the radial distance; radiusAt: its own axis (9),
    // rel (3), dot (5), times slope (1), plus r (1) = 19; the difference (1).
    const Vector3d axis = cone->normdir.normalized();
    const double want = cone->radiusAt(p);
    out.value = fabs((rel - axis * rel.dot(axis)).norm() - want);
    out.bound = forwardError(49, base + fabs(cone->r) + fabs(want));
    return out;
  }
  if (const auto *sph = dynamic_cast<const SphereSurface *>(s)) {
    // rel: 3; norm: 6; minus r: 1.
    out.value = fabs(rel.norm() - sph->r);
    out.bound = forwardError(10, base + fabs(sph->r));
    return out;
  }
  if (dynamic_cast<const PlaneSurface *>(s) != nullptr) {
    // normal: 9; rel: 3; dot: 5 - the distance along the normal.
    out.value = fabs(rel.dot(s->normdir.normalized()));
    out.bound = forwardError(17, base);
    return out;
  }
  if (const auto *tor = dynamic_cast<const TorusSurface *>(s)) {
    // axis 9, rel 3, along 5, radial: 3 + 3 + 6 (26); radial - R: 1; hypot: 2
    // mul, 1 add, sqrt (4); minus r: 1 - 32 in all.
    const Vector3d axis = tor->normdir.normalized();
    const double along = rel.dot(axis);
    const double radial = (rel - axis * along).norm();
    out.value = fabs(hypot(radial - tor->r_major, along) - tor->r_minor);
    out.bound = forwardError(32, base + fabs(tor->r_major) + fabs(tor->r_minor));
    return out;
  }
  return out;
}

/*! Whether `p` is on `s` at D0 - see the file comment. */
inline bool onExactly(const Surface *s, const Vector3d& p)
{
  return residualOf(s, p).exact();
}

}  // namespace recognition
