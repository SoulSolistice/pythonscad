/*! The arc of the curve where two declared surfaces cross - see CrossingArc.h. */
#include "io/step/CrossingArc.h"

#include <algorithm>
#include <cmath>

#include <Eigen/Dense>

#include "geometry/AnalyticFeatures.h"
#include "geometry/MachinePrecision.h"
#include "io/step/Meridian.h"

namespace step {

using AnalyticFeatures::isQuadric;
using AnalyticFeatures::projectOntoBoth;
using AnalyticFeatures::surfaceImplicit;

bool onCrossing(const Surface *a, const Surface *b, Vector3d& q, const Vector3d& p0, const Vector3d& p1,
                double tol)
{
  // A declared sweep and a surface of revolution about its own axis cross in
  // closed form (Meridian): at the station through `q`, on the profile edge
  // the stretch's two ends lie on - not the one `q` projects to, which near a
  // lens can be the crest the chord passes over.
  Meridian meridian;
  if (Meridian::of(a, b, meridian)) {
    const std::vector<std::size_t> e0 = meridian.edgesOf(p0), e1 = meridian.edgesOf(p1);
    for (const std::size_t e : e0) {
      if (std::find(e1.begin(), e1.end(), e) == e1.end()) continue;
      Vector3d exact;
      if (meridian.crossingOnEdge(meridian.station(q), e, exact)) {
        q = exact;
        return true;
      }
      break;
    }
  }
  Vector3d newton = q;
  if (projectOntoBoth(a, b, newton, tol)) {
    q = newton;
    return true;
  }
  for (const Surface *s : {a, b}) {
    const auto *grid = dynamic_cast<const GridSurface *>(s);
    if (grid == nullptr) continue;
    const Surface *other = s == a ? b : a;
    double u = 0, v = 0, u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    if (!grid->project(q, u, v) || !grid->project(p0, u0, v0) || !grid->project(p1, u1, v1)) continue;
    int du = 0, dv = 0, nr = 0, nc = 0;
    std::vector<Vector3d> net;
    std::vector<double> ku, kv;
    std::vector<int> mu, mv;
    if (!grid->splineForm(du, dv, nr, nc, net, ku, mu, kv, mv)) continue;
    // Which way the stretch advances over the sweep, by distance on it: the
    // line through `q` is taken across that, so the crossing cuts it steeply.
    const double along_u = (grid->evaluate(u1, v0) - grid->evaluate(u0, v0)).norm();
    const double along_v = (grid->evaluate(u0, v1) - grid->evaluate(u0, v0)).norm();
    const bool fix_u = along_u >= along_v;
    auto at = [&](double t) { return fix_u ? grid->evaluate(u, t) : grid->evaluate(t, v); };
    // The bracket: as far either side of `q` along that line as the stretch is
    // long, in the line's own parameter - local, so it holds the crossing near
    // the stretch and not one elsewhere on the sweep - and no further than the
    // creases, knots where the sweep is only C0, either side of the stretch.
    const double here = fix_u ? v : u;
    const double h = 1e-6;
    const double rate = (at(here + h) - at(here - h)).norm() / (2 * h);
    if (!(rate > 0)) continue;
    const double reach = (p1 - p0).norm() / rate;
    const auto& knots = fix_u ? kv : ku;
    const auto& mults = fix_u ? mv : mu;
    const int degree = fix_u ? dv : du;
    const double mid = fix_u ? 0.5 * (v0 + v1) : 0.5 * (u0 + u1);
    double lo = std::max(knots.front(), here - reach), hi = std::min(knots.back(), here + reach);
    for (std::size_t k = 1; k + 1 < knots.size(); k++) {
      if (mults[k] < degree) continue;
      if (knots[k] <= mid) lo = std::max(lo, knots[k]);
      else hi = std::min(hi, knots[k]);
    }
    if (!(hi > lo)) continue;
    auto side = [&](double t, double& f, double& d) {
      Vector3d g;
      return surfaceImplicit(other, at(t), f, g, &d);
    };
    double flo = 0, fhi = 0, dlo = 0, dhi = 0;
    if (!side(lo, flo, dlo) || !side(hi, fhi, dhi)) continue;
    if ((flo > 0) == (fhi > 0)) continue;  // not crossed on this flank
    // Bisection: each step halves the bracket, so it ends at machine precision
    // in the parameter whether or not `tol` is met first.
    double t = 0.5 * (lo + hi);
    for (int step = 0; step < 200; step++) {
      t = 0.5 * (lo + hi);
      double f = 0, d = 0;
      if (!side(t, f, d)) break;
      if (d <= tol || !(hi - lo > 0) || t == lo || t == hi) break;
      if ((f > 0) == (flo > 0)) lo = t;
      else hi = t;
    }
    double f = 0, d = 0;
    if (!side(t, f, d) || d > tol) continue;
    q = at(t);
    return true;
  }
  return false;
}

bool intersectionArc(const Surface *a, const Surface *b, const Vector3d& p0, const Vector3d& p1,
                     double tol, std::vector<Vector3d>& ctrl, ArcOutcome *why)
{
  const double scale = std::max(1.0, std::max(p0.norm(), p1.norm()));
  // The band does NOT belong here, and putting it here was wrong once already.
  // A declared grid's band says how far the *mesh* stands off the surface, which
  // is why a mesh vertex is allowed to miss it by that much. It says nothing
  // about how precisely a point can be placed *on* the interpolant, which is
  // what this solves, nor about how closely a Bezier can follow the curve, which
  // is what the fit measures. Widening either to the band makes `intersectionArc`
  // accept a cubic on its first try and write a curve no better than the chord
  // it replaces - measured: identical boundary deviation to seven digits.
  const double solve_tol = 1e-14 * scale;
  const double fit_tol = tol;
  // A declared sweep's profile is a *polyline*, so the surface is creased along
  // every profile corner and its v derivative does not exist there. Where the
  // other surface crosses a crease the intersection curve has a kink, and no
  // polynomial of any degree follows a kink: raising it only moves the miss
  // around. So record whether this edge straddles one, because that is the
  // difference between a fit that wants more degree and one that wants
  // splitting at the crease.
  if (why != nullptr) {
    for (const Surface *sf : {a, b}) {
      const auto *grid = dynamic_cast<const GridSurface *>(sf);
      if (grid == nullptr) continue;
      // Where the creases are is something the surface *states*, in the same
      // description it hands the STEP writer: `splineForm` returns the v knots
      // and their multiplicities, and a knot the surface is only C0 across is a
      // crease by definition. Counting spans from `cols` would be deducing the
      // same thing from an internal, and getting the answer from the
      // declaration is the whole method this exporter is built on.
      int du = 0, dv = 0, nr = 0, nc = 0;
      std::vector<Vector3d> net;
      std::vector<double> ku, kv;
      std::vector<int> mu, mv;
      if (!grid->splineForm(du, dv, nr, nc, net, ku, mu, kv, mv)) continue;
      double u0 = 0, v0 = 0, u1 = 0, v1 = 0;
      if (!grid->project(p0, u0, v0) || !grid->project(p1, u1, v1)) continue;
      const double lo = std::min(v0, v1), hi = std::max(v0, v1);
      why->straddles_crease = 0;
      for (std::size_t k = 0; k < kv.size(); k++) {
        if (mv[k] < dv) continue;  // still C1 across this one
        if (kv[k] > lo + 1e-12 && kv[k] < hi - 1e-12) why->straddles_crease = 1;
      }
    }
  }
  for (int degree = 3; degree <= 9; degree++) {
    // Sample the true curve at the Bezier's own parameters, by pulling the
    // chord's point at each onto both surfaces.
    std::vector<Vector3d> on_curve(degree + 1);
    bool ok = true;
    on_curve[0] = p0;
    on_curve[degree] = p1;
    for (int i = 1; i < degree && ok; i++) {
      Vector3d q = p0 + (p1 - p0) * (double(i) / degree);
      ok = onCrossing(a, b, q, p0, p1, solve_tol);
      on_curve[i] = q;
    }
    // A degree whose samples will not project is not the end of the fit. The
    // next degree samples at *different* parameters - degree 3 asks for 1/3 and
    // 2/3, degree 4 for 1/4, 1/2, 3/4 - so a chord point that lands somewhere
    // Newton cannot recover from says nothing at all about the one after it.
    // Returning here instead threw away every remaining degree on the strength
    // of a single sample, and it is the reason a boundary that had every corner
    // placed still fell back to chords on 47 edges.
    if (!ok) {
      if (why != nullptr) {
        why->sampling_failed++;
        why->degree = degree;
      }
      continue;
    }

    // Interpolate them: solve the Bernstein collocation system for the control
    // points. Small and dense, and well conditioned at these degrees.
    Eigen::MatrixXd m(degree + 1, degree + 1);
    Eigen::MatrixXd rhs(degree + 1, 3);
    for (int i = 0; i <= degree; i++) {
      const double t = double(i) / degree;
      double binom = 1;
      for (int j = 0; j <= degree; j++) {
        m(i, j) = binom * pow(t, j) * pow(1 - t, degree - j);
        binom = binom * (degree - j) / (j + 1);
      }
      rhs.row(i) = on_curve[i].transpose();
    }
    const Eigen::MatrixXd solved = m.colPivHouseholderQr().solve(rhs);
    ctrl.assign(degree + 1, Vector3d::Zero());
    for (int j = 0; j <= degree; j++) ctrl[j] = solved.row(j).transpose();
    double on_quadric = 0, on_fitted = 0;
    const double err = intersectionArcError(a, b, ctrl, &on_quadric, &on_fitted);
    if (why != nullptr && err < why->best_error) {
      why->degree = degree;
      why->best_error = err;
      why->on_quadric = on_quadric;
      why->on_fitted = on_fitted;
    }
    if (err <= fit_tol) return true;
  }
  ctrl.clear();
  return false;
}

double intersectionArcError(const Surface *a, const Surface *b, const std::vector<Vector3d>& ctrl,
                            double *on_quadric, double *on_fitted)
{
  const int degree = int(ctrl.size()) - 1;
  double worst = 0;
  for (int k = 1; k < 4 * degree; k++) {
    const double t = double(k) / (4 * degree);
    Vector3d p = Vector3d::Zero();
    double binom = 1;
    for (int j = 0; j <= degree; j++) {
      p += ctrl[j] * (binom * pow(t, j) * pow(1 - t, degree - j));
      binom = binom * (degree - j) / (j + 1);
    }
    for (const Surface *s : {a, b}) {
      double f = 0;
      Vector3d g;
      double d = 0;
      if (!surfaceImplicit(s, p, f, g, &d)) return std::numeric_limits<double>::infinity();
      if (g.norm() < 1e-12) return std::numeric_limits<double>::infinity();
      worst = std::max(worst, d);  // the distance to the surface, not to its tangent plane
      // Which of the two the arc is failing is not a detail: a quadric answers
      // by algebra and a grid by projecting, so a large number here may be the
      // curve missing the surface or the surface failing to locate itself.
      double *worst_of_kind = isQuadric(s) ? on_quadric : on_fitted;
      if (worst_of_kind != nullptr) *worst_of_kind = std::max(*worst_of_kind, d);
    }
  }
  return worst;
}

}  // namespace step
