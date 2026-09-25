/*! The curve two declared surfaces cross along, over a whole run - see
 * CrossingRuns.h. */
#include "io/step/CrossingRuns.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <Eigen/Sparse>
#include <Eigen/SparseLU>

#include "geometry/AnalyticFeatures.h"

namespace step {

namespace {

constexpr int kDegree = CurveFit::degree;

std::vector<double> fullKnots(const CurveFit& fit)
{
  std::vector<double> full;
  for (std::size_t i = 0; i < fit.knots.size(); i++) full.insert(full.end(), fit.mults[i], fit.knots[i]);
  return full;
}

/*! The span `u` lies in: The NURBS Book, A2.1. */
int findSpan(int n, double u, const std::vector<double>& U)
{
  if (u >= U[n + 1]) return n;
  if (u <= U[kDegree]) return kDegree;
  int low = kDegree, high = n + 1, mid = (low + high) / 2;
  while (u < U[mid] || u >= U[mid + 1]) {
    if (u < U[mid]) high = mid;
    else low = mid;
    mid = (low + high) / 2;
  }
  return mid;
}

/*! The degree+1 basis functions not zero at `u` in span `i`: A2.2. */
void basisFuns(int i, double u, const std::vector<double>& U, double *N)
{
  double left[kDegree + 1], right[kDegree + 1];
  N[0] = 1.0;
  for (int j = 1; j <= kDegree; j++) {
    left[j] = u - U[i + 1 - j];
    right[j] = U[i + j] - u;
    double saved = 0.0;
    for (int r = 0; r < j; r++) {
      const double temp = N[r] / (right[r + 1] + left[j - r]);
      N[r] = saved + right[r + 1] * temp;
      saved = left[j - r] * temp;
    }
    N[j] = saved;
  }
}

/*! Control points through `rhs`'s rows at `fit`'s parameters, with its knots. */
bool solveInterpolation(const CurveFit& fit, const Eigen::MatrixXd& rhs, Eigen::MatrixXd& ctrl)
{
  const int count = int(fit.params.size());
  const std::vector<double> U = fullKnots(fit);
  if (int(U.size()) != count + kDegree + 1) return false;
  std::vector<Eigen::Triplet<double>> entries;
  entries.reserve(std::size_t(count) * (kDegree + 1));
  double N[kDegree + 1];
  for (int k = 0; k < count; k++) {
    const int span = findSpan(count - 1, fit.params[k], U);
    basisFuns(span, fit.params[k], U, N);
    for (int j = 0; j <= kDegree; j++) {
      if (N[j] != 0.0) entries.emplace_back(k, span - kDegree + j, N[j]);
    }
  }
  Eigen::SparseMatrix<double> A(count, count);
  A.setFromTriplets(entries.begin(), entries.end());
  Eigen::SparseLU<Eigen::SparseMatrix<double>> lu;
  lu.compute(A);
  if (lu.info() != Eigen::Success) return false;
  ctrl = lu.solve(rhs);
  return lu.info() == Eigen::Success && ctrl.allFinite();
}

/*! How far `p` is from `s`, or infinity where the surface cannot say. */
double distanceTo(const Surface *s, const Vector3d& p)
{
  double f = 0, d = 0;
  Vector3d g;
  if (!AnalyticFeatures::surfaceImplicit(s, p, f, g, &d) || g.norm() < 1e-12) {
    return std::numeric_limits<double>::infinity();
  }
  return d;
}

}  // namespace

bool interpolateCubic(const std::vector<Vector3d>& points, CurveFit& out)
{
  const std::size_t count = points.size();
  if (count < kDegree + 1) return false;
  // Chord-length parameters on [0, 1].
  std::vector<double> params(count, 0.0);
  double total = 0.0;
  for (std::size_t k = 1; k < count; k++) {
    const double step = (points[k] - points[k - 1]).norm();
    if (!(step > 0)) return false;  // two points the same: no parameter between them
    total += step;
    params[k] = total;
  }
  for (std::size_t k = 1; k < count; k++) params[k] /= total;
  params.back() = 1.0;

  // Clamped, with each interior knot the average of `degree` consecutive
  // parameters: that is what keeps the system banded and nonsingular.
  CurveFit fit;
  fit.knots.push_back(0.0);
  fit.mults.push_back(kDegree + 1);
  for (std::size_t j = 1; j + kDegree < count; j++) {
    double sum = 0.0;
    for (std::size_t i = j; i < j + kDegree; i++) sum += params[i];
    fit.knots.push_back(sum / kDegree);
    fit.mults.push_back(1);
  }
  fit.knots.push_back(1.0);
  fit.mults.push_back(kDegree + 1);
  fit.params = params;
  fit.points = points;

  Eigen::MatrixXd rhs(count, 3);
  for (std::size_t k = 0; k < count; k++) rhs.row(long(k)) = points[k].transpose();
  Eigen::MatrixXd ctrl;
  if (!solveInterpolation(fit, rhs, ctrl)) return false;
  fit.ctrl.resize(count);
  for (std::size_t k = 0; k < count; k++) fit.ctrl[k] = ctrl.row(long(k)).transpose();
  // A clamped curve starts and ends at its end control points; those are the
  // run's own vertices, so they are the vertices, not a solve's rounding of them.
  fit.ctrl.front() = points.front();
  fit.ctrl.back() = points.back();
  out = std::move(fit);
  return true;
}

std::vector<Eigen::Vector2d> interpolateLike(const CurveFit& fit,
                                             const std::vector<Eigen::Vector2d>& values)
{
  std::vector<Eigen::Vector2d> out;
  if (values.size() != fit.params.size()) return out;
  Eigen::MatrixXd rhs(long(values.size()), 2);
  for (std::size_t k = 0; k < values.size(); k++) rhs.row(long(k)) = values[k].transpose();
  Eigen::MatrixXd ctrl;
  if (!solveInterpolation(fit, rhs, ctrl)) return out;
  out.resize(values.size());
  for (std::size_t k = 0; k < values.size(); k++) out[k] = ctrl.row(long(k)).transpose();
  out.front() = values.front();
  out.back() = values.back();
  return out;
}

Vector3d evaluate(const CurveFit& fit, double t)
{
  const std::vector<double> U = fullKnots(fit);
  const int count = int(fit.ctrl.size());
  const int span = findSpan(count - 1, t, U);
  double N[kDegree + 1];
  basisFuns(span, t, U, N);
  Vector3d p = Vector3d::Zero();
  for (int j = 0; j <= kDegree; j++) p += fit.ctrl[span - kDegree + j] * N[j];
  return p;
}

bool joinC0(const std::vector<CurveFit>& parts, CurveFit& out)
{
  if (parts.empty()) return false;
  if (parts.size() == 1) {
    out = parts.front();
    return true;
  }
  // Each part's share of the parameter, by the length of the points it runs
  // through - the same measure its own parameters were spread by.
  std::vector<double> length(parts.size(), 0.0);
  double total = 0.0;
  for (std::size_t j = 0; j < parts.size(); j++) {
    const auto& pts = parts[j].points;
    if (pts.size() < 2 || parts[j].ctrl.empty()) return false;
    for (std::size_t k = 1; k < pts.size(); k++) length[j] += (pts[k] - pts[k - 1]).norm();
    total += length[j];
  }
  if (!(total > 0)) return false;
  CurveFit joined;
  joined.knots.push_back(0.0);
  joined.mults.push_back(kDegree + 1);
  double from = 0.0;
  for (std::size_t j = 0; j < parts.size(); j++) {
    const CurveFit& part = parts[j];
    const double to = j + 1 == parts.size() ? 1.0 : from + length[j] / total;
    auto map = [&](double t) { return from + (to - from) * t; };
    // Its interior knots, where it lies in the whole; then, unless it is the
    // last, the corner it ends on, at multiplicity `degree`.
    for (std::size_t k = 1; k + 1 < part.knots.size(); k++) {
      joined.knots.push_back(map(part.knots[k]));
      joined.mults.push_back(part.mults[k]);
    }
    if (j + 1 < parts.size()) {
      joined.knots.push_back(to);
      joined.mults.push_back(kDegree);
    }
    // Its control points, and the points it interpolates at their parameters;
    // the first of each after the first is the corner the one before ended on.
    const std::size_t skip = j == 0 ? 0 : 1;
    joined.ctrl.insert(joined.ctrl.end(), part.ctrl.begin() + long(skip), part.ctrl.end());
    for (std::size_t k = skip; k < part.points.size(); k++) {
      joined.points.push_back(part.points[k]);
      joined.params.push_back(map(part.params[k]));
    }
    from = to;
  }
  joined.knots.push_back(1.0);
  joined.mults.push_back(kDegree + 1);
  out = std::move(joined);
  return true;
}

bool fitCrossingRun(const Surface *a, const Surface *b, const std::vector<Vector3d>& run, double tol,
                    CurveFit& out)
{
  if (run.size() < 2) return false;
  double scale = 1.0;
  for (const auto& p : run) scale = std::max(scale, p.norm());
  // The same solve precision the per-edge arc holds its samples to.
  const double solve_tol = 1e-14 * scale;
  auto on_both = [&](Vector3d q, std::vector<Vector3d>& into) {
    if (!AnalyticFeatures::projectOntoBoth(a, b, q, solve_tol)) return false;
    into.push_back(q);
    return true;
  };

  // The run's end vertices as they are - the curve ends on them - and every
  // point between them put on both surfaces: the interior vertices, and points
  // of the curve between each two, found from the chord. One between each two
  // unless that leaves fewer than a cubic needs - a run of one edge - and then
  // two, at the chord's thirds.
  const int between = run.size() == 2 ? 2 : 1;
  std::vector<Vector3d> points;
  points.push_back(run.front());
  for (std::size_t k = 1; k < run.size(); k++) {
    for (int s = 1; s <= between; s++) {
      const double t = double(s) / (between + 1);
      if (!on_both(run[k - 1] * (1 - t) + run[k] * t, points)) return false;
    }
    if (k + 1 < run.size()) {
      if (!on_both(run[k], points)) return false;
    } else {
      points.push_back(run.back());
    }
  }

  double previous = std::numeric_limits<double>::infinity();
  for (;;) {
    CurveFit fit;
    if (!interpolateCubic(points, fit)) return false;
    // How far the curve strays between each two of its points, from either
    // surface: where it is on both it is the crossing.
    const std::size_t count = points.size();
    std::vector<double> worst(count - 1, 0.0);
    double global = 0.0;
    for (std::size_t i = 0; i + 1 < count; i++) {
      for (const double s : {0.25, 0.5, 0.75}) {
        const Vector3d p = evaluate(fit, fit.params[i] + (fit.params[i + 1] - fit.params[i]) * s);
        worst[i] = std::max(worst[i], std::max(distanceTo(a, p), distanceTo(b, p)));
      }
      global = std::max(global, worst[i]);
    }
    if (global <= tol) {
      out = std::move(fit);
      return true;
    }
    // More of the true curve where it missed; refused once that stops helping.
    if (!(global < previous)) return false;
    previous = global;
    std::vector<Vector3d> refined;
    refined.reserve(count * 2);
    for (std::size_t i = 0; i + 1 < count; i++) {
      refined.push_back(points[i]);
      if (worst[i] > tol &&
          !on_both(evaluate(fit, (fit.params[i] + fit.params[i + 1]) * 0.5), refined)) {
        return false;
      }
    }
    refined.push_back(points.back());
    points.swap(refined);
  }
}

}  // namespace step
