#pragma once

/*! The curve two declared surfaces cross along, fitted over a whole run of mesh
 * edges rather than one edge at a time.
 *
 * Where two faces meet, the boundary between them is one curve of the model,
 * from a vertex where something else meets it to the next. The mesh carries it
 * as hundreds of edges; the corner placement has put every one of their
 * vertices on both surfaces (1e-11 on lid10), so what the file owes is the
 * curve through them - one edge, not the 559 a per-edge fit writes, which a
 * CAD view draws as a zig-zag round the helicoid's lead-in.
 *
 * ISO 10303 has no entity for the intersection of two quadrics, let alone of a
 * quadric and a swept B-spline, so the curve is approximated, as the per-edge
 * Bezier is: a clamped cubic B-spline interpolating points *on the true curve*
 * - the run's two end vertices exactly, and between them points projected onto
 * both surfaces - held to the same tolerance against both surfaces. Where it
 * misses, a point of the true curve is added in that span and the curve fitted
 * again; the fit is refused where that stops helping, and the caller falls back
 * to shorter runs.
 *
 * Its parameters and knots are kept (CurveFit), because a pcurve of it is the
 * same interpolation of the same points in a surface's parameters: sharing the
 * knots is what makes the two agree at every interpolated point exactly. */

#include <vector>

#include <Eigen/Core>

#include "geometry/Surface.h"
#include "geometry/linalg.h"

namespace step {

/*! A clamped cubic B-spline interpolating `points` at `params`. */
struct CurveFit {
  static constexpr int degree = 3;
  std::vector<Vector3d> ctrl;
  std::vector<double> knots;  // distinct, increasing
  std::vector<int> mults;     // parallel to `knots`
  std::vector<double> params;
  std::vector<Vector3d> points;
};

/*! Interpolate `points` - at least four, consecutive ones distinct - by a
 * clamped cubic at chord-length parameters on [0, 1], its knots the averages
 * of those parameters (Piegl and Tiller, *The NURBS Book*, 9.2.1). */
bool interpolateCubic(const std::vector<Vector3d>& points, CurveFit& out);

/*! The same interpolation of other values - a curve's image in a surface's
 * parameters - at `fit`'s parameters with `fit`'s knots: control points of the
 * curve through `values[i]` at `fit.params[i]`. Empty where it cannot be
 * solved. */
std::vector<Eigen::Vector2d> interpolateLike(const CurveFit& fit,
                                             const std::vector<Eigen::Vector2d>& values);

/*! `fit` at parameter `t` in [0, 1]. */
Vector3d evaluate(const CurveFit& fit, double t);

/*! Curves fitted one after another, each ending where the next begins, as one
 * curve: C0 where they meet - a knot of multiplicity `degree` there - and each
 * part exactly itself in between, over a share of [0, 1] in proportion to its
 * length. Where two declared surfaces cross, a sweep's crease is a corner of
 * the curve; this is how the curve keeps it without being cut into two edges at
 * a vertex nothing else meets. */
bool joinC0(const std::vector<CurveFit>& parts, CurveFit& out);

/*! The curve `a` and `b` cross along, through `run` - vertices already on both,
 * consecutive along the curve - held to `tol` against each surface. False
 * where the points cannot be put on both surfaces, or where adding points of
 * the true curve stops bringing the fit closer. */
bool fitCrossingRun(const Surface *a, const Surface *b, const std::vector<Vector3d>& run, double tol,
                    CurveFit& out);

}  // namespace step
