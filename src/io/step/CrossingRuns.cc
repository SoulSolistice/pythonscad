/*! The curve two declared surfaces cross along, over a whole run - see
 * CrossingRuns.h. */
#include "io/step/CrossingRuns.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <Eigen/Sparse>
#include <Eigen/SparseLU>

#include "geometry/AnalyticFeatures.h"
#include "io/step/CrossingArc.h"
#include "io/step/Meridian.h"

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

/*! The degree+1 basis functions of `degree` not zero at `u` in span `i`: A2.2. */
void basisFuns(int degree, int i, double u, const std::vector<double>& U, double *N)
{
  double left[kDegree + 1], right[kDegree + 1];
  N[0] = 1.0;
  for (int j = 1; j <= degree; j++) {
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
    basisFuns(kDegree, span, fit.params[k], U, N);
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
  basisFuns(kDegree, span, t, U, N);
  Vector3d p = Vector3d::Zero();
  for (int j = 0; j <= kDegree; j++) p += fit.ctrl[span - kDegree + j] * N[j];
  return p;
}

namespace {

/*! The derivative of `fit` at `t`, exactly: the degree-2 spline on the same
 * knots whose control points are the differences of `fit`'s (A3.3). */
Vector3d tangent(const CurveFit& fit, double t)
{
  const std::vector<double> U = fullKnots(fit);
  const int count = int(fit.ctrl.size());
  const int span = findSpan(count - 1, t, U);
  double N[kDegree + 1];
  basisFuns(kDegree - 1, span, t, U, N);
  Vector3d d = Vector3d::Zero();
  for (int j = 0; j < kDegree; j++) {
    const int i = span - kDegree + j;
    const double width = U[i + kDegree + 1] - U[i + 1];
    if (width > 0) d += (fit.ctrl[i + 1] - fit.ctrl[i]) * (kDegree / width) * N[j];
  }
  return d;
}

}  // namespace

std::size_t intervalsTurningBack(const CurveFit& fit)
{
  std::size_t backward = 0;
  for (std::size_t i = 0; i + 1 < fit.points.size() && i + 1 < fit.params.size(); i++) {
    const Vector3d chord = fit.points[i + 1] - fit.points[i];
    bool turns_back = false;
    for (const double s : {0.0, 0.25, 0.5, 0.75, 1.0}) {
      const double t = fit.params[i] + (fit.params[i + 1] - fit.params[i]) * s;
      turns_back = turns_back || !(tangent(fit, t).dot(chord) > 0.0);
    }
    backward += turns_back ? 1 : 0;
  }
  return backward;
}

std::vector<Vector3d> creaseCorners(const Surface *a, const Surface *b, const Vector3d& p0,
                                    const Vector3d& p1)
{
  // A declared sweep and a coaxial surface of revolution: the corners in closed
  // form, where a profile vertex reaches the other surface and where a law turns.
  Meridian meridian;
  if (Meridian::of(a, b, meridian)) {
    std::vector<Vector3d> out;
    const Vector3d span = p1 - p0;
    for (const Vector3d& c : meridian.corners(p0, p1)) {
      const double along = span.squaredNorm() > 0 ? (c - p0).dot(span) / span.squaredNorm() : -1.0;
      if (along > 0.0 && along < 1.0) out.push_back(c);
    }
    return out;
  }
  const double scale = std::max(1.0, std::max(p0.norm(), p1.norm()));
  const double solve_tol = 1e-14 * scale;  // the precision the crossing's own points are solved to
  const Vector3d chord = p1 - p0;
  if (!(chord.squaredNorm() > 0)) return {};
  std::vector<std::pair<double, Vector3d>> found;
  for (const Surface *s : {a, b}) {
    const auto *grid = dynamic_cast<const GridSurface *>(s);
    if (grid == nullptr) continue;
    const Surface *other = s == a ? b : a;
    int du = 0, dv = 0, nr = 0, nc = 0;
    std::vector<Vector3d> net;
    std::vector<double> ku, kv;
    std::vector<int> mu, mv;
    double u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    if (!grid->splineForm(du, dv, nr, nc, net, ku, mu, kv, mv)) continue;
    if (!grid->project(p0, u0, v0) || !grid->project(p1, u1, v1)) return {};
    // One pass for creases across v (a line of fixed v, u free), one for u.
    for (const bool across_v : {true, false}) {
      const auto& knots = across_v ? kv : ku;
      const auto& mults = across_v ? mv : mu;
      const int degree = across_v ? dv : du;
      const double lo = across_v ? std::min(v0, v1) : std::min(u0, u1);
      const double hi = across_v ? std::max(v0, v1) : std::max(u0, u1);
      for (std::size_t k = 1; k + 1 < knots.size(); k++) {
        if (mults[k] < degree || !(knots[k] > lo && knots[k] < hi)) continue;
        const double fixed = knots[k];
        const double frac =
          (fixed - (across_v ? v0 : u0)) / ((across_v ? v1 : u1) - (across_v ? v0 : u0));
        double t = across_v ? u0 + (u1 - u0) * frac : v0 + (v1 - v0) * frac;
        auto at = [&](double free) {
          return across_v ? grid->evaluate(free, fixed) : grid->evaluate(fixed, free);
        };
        // Newton on the free parameter, for where the crease meets `other`.
        Vector3d p = at(t);
        bool solved = false;
        for (int step = 0; step < 50 && !solved; step++) {
          double f = 0, d = 0;
          Vector3d g;
          if (!AnalyticFeatures::surfaceImplicit(other, p, f, g, &d) || g.norm() < 1e-12) return {};
          if (d <= solve_tol) {
            solved = true;
            break;
          }
          const double h = 1e-7;
          const Vector3d dp = (at(t + h) - at(t - h)) / (2 * h);
          const double slope = g.dot(dp);
          if (slope == 0) return {};
          t -= f / slope;
          p = at(t);
        }
        if (!solved) return {};
        const double along = (p - p0).dot(chord) / chord.squaredNorm();
        if (!(along > 0 && along < 1)) return {};
        found.emplace_back(along, p);
      }
    }
  }
  std::sort(found.begin(), found.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
  std::vector<Vector3d> out;
  for (const auto& f : found) out.push_back(f.second);
  return out;
}

bool fitCrossingRun(const Surface *a, const Surface *b, const std::vector<Vector3d>& run, double tol,
                    CurveFit& out)
{
  if (run.size() < 2) return false;
  double scale = 1.0;
  for (const auto& p : run) scale = std::max(scale, p.norm());
  // The same solve precision the per-edge arc holds its samples to.
  const double solve_tol = 1e-14 * scale;
  // A point of the crossing near `q`, on the stretch from `from` to `to`.
  auto on_both = [&](Vector3d q, const Vector3d& from, const Vector3d& to, std::vector<Vector3d>& into) {
    if (!onCrossing(a, b, q, from, to, solve_tol)) return false;
    into.push_back(q);
    return true;
  };

  // The run's end vertices as they are - the curve ends on them - and between
  // them points of the curve evenly spaced along the run, each put on both
  // surfaces from the chord it falls on. Evenly spaced and not the run's own
  // vertices: those are wherever the mesh's two tessellations happened to cross
  // each other, a bore's facets and a sweep's stations interleaving, and gaps a
  // hundred times apart make an interpolating cubic ring between them. The fit
  // then stalls above what it asks for however many points it adds, and the run
  // is cut into pieces nothing meets. The inner vertices need not be
  // interpolated - they are not vertices of the file, the run being one edge -
  // only passed within the tolerance, which the fit is held to everywhere.
  std::vector<double> along(run.size(), 0.0);
  for (std::size_t k = 1; k < run.size(); k++) along[k] = along[k - 1] + (run[k] - run[k - 1]).norm();
  const double length = along.back();
  if (!(length > 0)) return false;
  // Two points between each two vertices, as there were, and three at least,
  // which is what a cubic needs.
  const std::size_t intervals = std::max<std::size_t>(3, 2 * (run.size() - 1));
  std::vector<Vector3d> points;
  points.push_back(run.front());
  std::size_t k = 1;
  for (std::size_t i = 1; i < intervals; i++) {
    const double s = length * double(i) / double(intervals);
    while (k + 1 < run.size() && along[k] < s) k++;
    const double step = along[k] - along[k - 1];
    const double t = step > 0 ? (s - along[k - 1]) / step : 0.5;
    if (!on_both(run[k - 1] * (1 - t) + run[k] * t, run[k - 1], run[k], points)) return false;
  }
  points.push_back(run.back());

  double previous = std::numeric_limits<double>::infinity();
  std::size_t previous_backward = std::numeric_limits<std::size_t>::max();
  for (;;) {
    CurveFit fit;
    if (!interpolateCubic(points, fit)) return false;
    // How far the curve strays between each two of its points, from either
    // surface: where it is on both it is the crossing. And whether it runs
    // *along* the crossing, which being on both surfaces does not say - see
    // intervalsTurningBack.
    const std::size_t count = points.size();
    double global = 0.0;
    for (std::size_t i = 0; i + 1 < count; i++) {
      for (const double s : {0.25, 0.5, 0.75}) {
        const Vector3d p = evaluate(fit, fit.params[i] + (fit.params[i + 1] - fit.params[i]) * s);
        global = std::max(global, std::max(distanceTo(a, p), distanceTo(b, p)));
      }
    }
    const std::size_t backward = intervalsTurningBack(fit);
    if (global <= tol && backward == 0) {
      out = std::move(fit);
      return true;
    }
    // Twice as many points of the true curve, everywhere and not only where it
    // missed, so they stay evenly spaced: refining only the intervals that
    // missed puts a short one beside a long one, and the cubic rings between
    // them - which is how a fit that should gain sixteenfold with each halving
    // got worse instead.
    //
    // Refused once that stops helping: when neither the number of intervals
    // turning back falls, nor the miss at least halves. Twice the points of a
    // smooth curve take a cubic interpolant's miss down sixteenfold; one that
    // does not even halve is not converging on the curve at all, and is
    // missing for a reason more points cannot cure - on step-declare-grid-scad
    // a declared grid and a cylinder that do not cross to 1e-7 there, which
    // went from 1.3e-2 to 1.02e-2 over 49153 points while "any decrease"
    // counted as progress. The number turning back, not the share: doubling
    // the points halves the share of a fixed few bad intervals for ever.
    if (!(global <= previous / 2) && !(backward < previous_backward)) return false;
    previous = std::min(previous, global);
    previous_backward = std::min(previous_backward, backward);
    std::vector<Vector3d> refined;
    refined.reserve(count * 2);
    for (std::size_t i = 0; i + 1 < count; i++) {
      refined.push_back(points[i]);
      if (!on_both(evaluate(fit, (fit.params[i] + fit.params[i + 1]) * 0.5), points[i], points[i + 1],
                   refined)) {
        return false;
      }
    }
    refined.push_back(points.back());
    points.swap(refined);
  }
}

}  // namespace step
