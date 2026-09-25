#include "geometry/AnalyticFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

#include "geometry/Surface.h"
#include "geometry/recognition/Internal.h"

namespace AnalyticFeatures {

/*! Closest point on a declared surface, where one can be written down.
 *
 * Not every kind: a Bezier patch and a grid answer by projection, which can
 * fail to converge, and the caller has to treat that as "not this surface"
 * rather than as an answer. */
const char *pointOriginName(PointOrigin origin)
{
  switch (origin) {
  case PointOrigin::Provisional:    return "provisional";
  case PointOrigin::Declared:       return "declared";
  case PointOrigin::Tessellation:   return "tessellation";
  case PointOrigin::Placed:         return "placed";
  case PointOrigin::ReaderInvented: return "reader-invented";
  }
  return "unknown";
}

bool closestOnSurface(const Surface *s, const Vector3d& p, Vector3d& out)
{
  if (s == nullptr) return false;
  if (s->closestPoint(p, out)) return true;
  if (const auto *grid = dynamic_cast<const GridSurface *>(s)) {
    double u = 0, v = 0;
    if (!grid->project(p, u, v)) return false;
    out = grid->evaluate(u, v);
    return true;
  }
  return false;
}

Vector3d perpendicular(const Vector3d& norm)
{
  const Vector3d axis = fabs(norm[0]) < 0.9 ? Vector3d(1, 0, 0) : Vector3d(0, 1, 0);
  return norm.cross(axis).normalized();
}

bool fitCircleCentre(const std::vector<Vector3d>& vertices, const std::vector<int>& ids,
                     const Vector3d& axis, double level, Vector3d& centre)
{
  if (ids.size() < 3) return false;

  const Vector3d u = perpendicular(axis);
  const Vector3d w = axis.cross(u);
  const Vector3d origin = vertices[ids[0]];

  Matrix3d ata = Matrix3d::Zero();
  Vector3d atb = Vector3d::Zero();
  for (const int id : ids) {
    const Vector3d rel = vertices[id] - origin;
    const Vector3d row(2 * rel.dot(u), 2 * rel.dot(w), 1.0);
    const double val = rel.dot(u) * rel.dot(u) + rel.dot(w) * rel.dot(w);
    ata += row * row.transpose();
    atb += row * val;
  }

  Eigen::FullPivLU<Matrix3d> lu(ata);
  if (!lu.isInvertible()) return false;
  const Vector3d sol = lu.solve(atb);
  if (!sol.allFinite()) return false;

  centre = origin + sol[0] * u + sol[1] * w;
  centre -= axis * (axis.dot(centre) - level);
  return true;
}

/*! The plane through a set of points, if they all lie on one.
 *
 * Three points fix a plane and the rest are the test. The three are picked for
 * spread rather than taken in order, because consecutive vertices of a rim are
 * nearly collinear and a normal taken from those is worth nothing.
 */
bool fitPlane(const std::vector<Vector3d>& vertices, const std::vector<int>& ids, double tol,
              Vector3d& normal)
{
  if (ids.size() < 3) return false;
  Vector3d centroid(0, 0, 0);
  for (const int v : ids) centroid += vertices[v];
  centroid /= double(ids.size());

  double best = 0;
  Vector3d u(0, 0, 0);
  for (const int v : ids) {
    const Vector3d rel = vertices[v] - centroid;
    if (rel.squaredNorm() > best) {
      best = rel.squaredNorm();
      u = rel;
    }
  }
  if (u.norm() < tol) return false;

  best = 0;
  Vector3d n(0, 0, 0);
  for (const int v : ids) {
    const Vector3d c = u.cross(vertices[v] - centroid);
    if (c.norm() > best) {
      best = c.norm();
      n = c;
    }
  }
  if (n.norm() < tol) return false;

  normal = n.normalized();
  for (const int v : ids) {
    if (fabs(normal.dot(vertices[v] - centroid)) > tol) return false;
  }
  return true;
}

double distanceToAxis(const Vector3d& pt, const Vector3d& base, const Vector3d& axis)
{
  const Vector3d rel = pt - base;
  return (rel - axis * axis.dot(rel)).norm();
}

std::string format(const char *fmt, ...)
{
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  return buf;
}

/*! A quadric written implicitly: the value of f at a point, and its gradient.
 *
 * f is zero on the surface, and the gradient points off it, which is all a
 * Newton step needs. Distances are not wanted here and are not returned - f is
 * the square of a radius, not a length.
 */
bool quadricImplicit(const Surface *surface, const Vector3d& p, double& f, Vector3d& grad)
{
  return surface != nullptr && surface->isQuadric() && surface->implicitAt(p, f, grad);
}

/*! A foot-point implicit for a surface that has no algebraic one.
 *
 * `GridSurface` - and so `SweepSurface` - and `BezierPatchSurface` answer
 * membership by projecting rather than by evaluating a polynomial, so there is
 * no `f(x,y,z)` to differentiate. There is something just as good for Newton:
 * project, take the foot point `q` and the unit normal `n` there, and use the
 * plane through `q` as the local implicit. `f = n . (p - q)` is signed, changes
 * sign as `p` crosses the surface, and `|f| / |grad|` is a distance in
 * millimetres because `|n|` is one - which is exactly what `projectOntoBoth`'s
 * residual test asks for.
 *
 * It is first order where a quadric's is exact, so Newton converges linearly
 * here rather than quadratically. That costs iterations and not correctness:
 * the residual test is on the answer, so a linear approach still has to arrive
 * before it is believed.
 *
 * The normal is taken by central differences in the surface's own parameters,
 * stepped inward at the edges of the rectangle so a foot point on the boundary
 * still gets two directions to cross.
 */
template <class S>
bool footPointImplicit(const S *surface, const Vector3d& p, double& f, Vector3d& grad,
                       double *true_distance)
{
  double u = 0, v = 0;
  if (!surface->project(p, u, v)) return false;
  const double h = 1e-5;
  const double u0 = std::max(0.0, std::min(1.0 - h, u - h)), u1 = std::min(1.0, u0 + 2 * h);
  const double v0 = std::max(0.0, std::min(1.0 - h, v - h)), v1 = std::min(1.0, v0 + 2 * h);
  const Vector3d du = surface->evaluate(u1, v) - surface->evaluate(u0, v);
  const Vector3d dv = surface->evaluate(u, v1) - surface->evaluate(u, v0);
  Vector3d n = du.cross(dv);
  const double len = n.norm();
  if (!(len > 0) || !std::isfinite(len)) return false;  // degenerate, or a pole
  n /= len;
  const Vector3d foot = surface->evaluate(u, v);
  f = n.dot(p - foot);
  grad = n;
  // The whole distance, not the part along the normal, and the two differ in a
  // way that matters at the edge of the patch.
  //
  // `evaluate` clamps its parameters and `project` returns the nearest point of
  // the *bounded* rectangle, so for a point off the end of a sweep the foot sits
  // on the boundary and `p - foot` is mostly *in* the tangent plane. `n . (p -
  // foot)` is then near zero however far away `p` is: a flat grid over the unit
  // square and a point at (2, 0.5, 0) gives a residual of exactly zero, one
  // whole unit outside the surface. Believing that residual is believing a
  // point is on a patch it has run off the end of, and every acceptance test
  // downstream - the solver's, the fit's, and the one asking whether a mesh
  // vertex is on a declared surface - would inherit it.
  //
  // So Newton gets `f` and `grad`, which are what it needs to step, and every
  // *acceptance* test gets this instead.
  if (true_distance != nullptr) *true_distance = (p - foot).norm();
  return true;
}

/*! The implicit form of whatever surface this is, exact where one exists.
 *
 * Quadrics keep their algebra - it is exact and quadratically convergent, and
 * nothing here should trade that for a projection. Everything else that can
 * answer `project` and `evaluate` gets the foot-point plane instead. A surface
 * that can do neither has no crossing curve, which is the honest answer for a
 * face whose shape this exporter cannot state.
 */
bool surfaceImplicit(const Surface *surface, const Vector3d& p, double& f, Vector3d& grad,
                     double *true_distance)
{
  // A plane states itself algebraically too, and more simply than any quadric:
  // the residual *is* the distance, because the gradient is a unit normal. It is
  // here rather than in `quadricImplicit` because a plane is not a quadric and
  // `isQuadric` decides other things - which pass writes a boundary, and whether
  // a planar crossing is a conic something else will write exactly.
  if (dynamic_cast<const PlaneSurface *>(surface) != nullptr) {
    if (!surface->implicitAt(p, f, grad)) return false;
    if (true_distance != nullptr) *true_distance = fabs(f);
    return true;
  }
  if (quadricImplicit(surface, p, f, grad)) {
    // A quadric's implicit is global - there is no patch to run off - so the
    // Newton distance *is* the distance.
    if (true_distance != nullptr) {
      const double g = grad.norm();
      *true_distance = g > 0 ? fabs(f) / g : fabs(f);
    }
    return true;
  }
  if (const auto *grid = dynamic_cast<const GridSurface *>(surface)) {
    return footPointImplicit(grid, p, f, grad, true_distance);
  }
  if (const auto *patch = dynamic_cast<const BezierPatchSurface *>(surface)) {
    return footPointImplicit(patch, p, f, grad, true_distance);
  }
  return false;
}

/*! What a surface is entitled to be missed by, from its own declaration.
 *
 * A quadric states where it is exactly, so nothing is owed it and this is zero;
 * every caller keeps whatever exactness it was already demanding. A declared
 * grid does not: it describes the smooth surface a generator meant, the mesh is
 * its tessellation, and a boolean cuts the *tessellation*, so the vertices it
 * makes lie on facets standing off the smooth surface by up to a station's
 * sagitta. `GridSurface::membershipTolerance` is that figure, computed by the
 * surface from the points it was declared with.
 *
 * Using it here is not a loosened tolerance chosen to admit more curves. It is
 * the same number the declaration channel already answers membership with, and
 * a crossing curve is refused or accepted on exactly the terms the surface's
 * own face is. Demanding 1e-9 of a tessellated vertex instead refuses every
 * sweep before the question is asked, which is what happened the first time.
 */
/*! Whether this surface states where it is algebraically. */
bool isQuadric(const Surface *surface)
{
  return surface != nullptr && surface->isQuadric();
}

double declaredBand(const Surface *surface)
{
  if (const auto *grid = dynamic_cast<const GridSurface *>(surface)) {
    return grid->membershipTolerance();
  }
  return 0.0;
}

/*! Pull a point onto both surfaces at once.
 *
 * Two Newton equations in the two directions that matter - along each surface's
 * own gradient - and nowhere else, so the point slides along the intersection
 * rather than away down it. Fails where the two are tangent, which is where the
 * 2x2 goes singular and where there is no honest answer anyway.
 *
 * `tol` is a distance, and the test is that the point *is* on both surfaces to
 * it - not that the last step was shorter than it. Those differ in exactly the
 * case this has to serve. A quadric's implicit is exact and Newton on it is
 * quadratically convergent, so the step collapses to nothing and either test
 * passes; a surface whose implicit is itself the result of an iteration -
 * anything answered by `project` rather than by algebra - inherits that
 * iteration's floor and can sit *on* both surfaces while still taking steps
 * above it forever. Asked for a step, it never converges and every crossing
 * curve over such a surface is refused; asked whether it has arrived, it says
 * yes. That distinction was measured: it is the difference between 93 of 507
 * crossing curves and all 507.
 */
/*! The crossing of a surface that answers by projecting with one stated
 * algebraically, solved in the projecting surface's own parameters.
 *
 * Newton on two implicits at once converges quadratically against a quadric,
 * whose implicit is algebraic, and only linearly against a sweep, whose
 * "implicit" is the tangent plane at a foot point - `footPointImplicit` says so
 * itself. Measured on the band family's 47 remaining chords: the traced points
 * sit on the quadric to 2.1e-13 and off the sweep by 8.2e-02. The fit was never
 * the problem; it interpolated points that were not on the crossing curve.
 *
 * So the sweep is not asked to converge at all. Every iterate is put *on* it by
 * evaluation - project to (u, v), take S(u, v) - and only the quadric's residual
 * is solved, by stepping in the surface's own tangent plane:
 *
 *     f + grad . (su Tu + sv Tv) = 0,   the shortest such step
 *
 * The same shape as the surface-against-line placement, which solves along the
 * line so that "every candidate is on the line by construction, and the planes
 * come out exact". Here every candidate is on the sweep by construction, and the
 * sweep comes out exact: what remains is the projection's own accuracy, not a
 * convergence rate.
 *
 * Returns whether the quadric's residual met `tol`; `p` is left at the best
 * iterate either way, which is the contract projectOntoBoth already has. */
template <typename S>
static bool crossInParameters(const S *surface, const Surface *quadric, Vector3d& p, double tol)
{
  // Two equations in the surface's own two parameters:
  //
  //     F(S(u, v))            = 0     the quadric, stated algebraically
  //     (S(u, v) - anchor).t  = 0     the station along the curve, held
  //
  // The first is the crossing. The second is what makes the answer *this*
  // crossing point rather than any other: without it the step is free to slide
  // along the curve, and it does - zeroing F by walking the tangent plane took
  // samples far enough along the band family's trim that the Bezier through
  // them was no curve at all, 593 crossing curves falling to 405. `t` is the
  // curve's own tangent, the quadric's gradient crossed with the surface's
  // normal, recomputed each step.
  //
  // Every iterate is S(u, v), so the sweep is satisfied by construction rather
  // than by convergence - which is the whole point, `footPointImplicit` being a
  // first-order model that leaves Newton linear on a sweep (open item 10, A2).
  const Vector3d anchor = p;
  Vector3d best = p;
  double best_residual = std::numeric_limits<double>::infinity();
  double u = 0, v = 0;
  if (!surface->project(p, u, v)) return false;
  for (int step = 0; step < 64; step++) {
    const Vector3d here = surface->evaluate(u, v);
    double f = 0;
    Vector3d grad;
    if (!quadricImplicit(quadric, here, f, grad)) break;
    const double gn = grad.norm();
    if (!(gn > 0)) break;
    const double residual = fabs(f) / gn;
    if (residual < best_residual) {
      best_residual = residual;
      best = here;
    }
    if (residual <= tol) break;
    // The tangent plane, differenced inside the parameter square so that a step
    // never straddles a profile corner - the same reason GridSurface::project
    // differences within a span.
    const double h = 1e-5;
    const double u0 = std::max(0.0, std::min(1.0 - h, u - h)), u1 = std::min(1.0, u0 + 2 * h);
    const double v0 = std::max(0.0, std::min(1.0 - h, v - h)), v1 = std::min(1.0, v0 + 2 * h);
    if (!(u1 > u0) || !(v1 > v0)) break;
    const Vector3d tu = (surface->evaluate(u1, v) - surface->evaluate(u0, v)) / (u1 - u0);
    const Vector3d tv = (surface->evaluate(u, v1) - surface->evaluate(u, v0)) / (v1 - v0);
    Vector3d along = grad.cross(tu.cross(tv));
    const double an = along.norm();
    if (!(an > 0)) break;  // tangent surfaces: no curve to hold a station on
    along /= an;
    Eigen::Matrix2d jac;
    jac << grad.dot(tu), grad.dot(tv), along.dot(tu), along.dot(tv);
    const double det = jac.determinant();
    if (!(fabs(det) > 0)) break;
    Eigen::Vector2d rhs(-f, -(here - anchor).dot(along));
    const Eigen::Vector2d d = jac.inverse() * rhs;
    if (!d.allFinite()) break;
    const double nu = std::clamp(u + d[0], 0.0, 1.0), nv = std::clamp(v + d[1], 0.0, 1.0);
    if (fabs(nu - u) < 1e-16 && fabs(nv - v) < 1e-16) break;
    u = nu;
    v = nv;
  }
  p = best;
  return best_residual <= tol;
}

bool projectOntoBoth(const Surface *a, const Surface *b, Vector3d& p, double tol)
{
  // Where one of the two answers by projecting and the other states itself
  // algebraically, the crossing is traced in the first one's parameters - see
  // crossInParameters. Newton below would land on the quadric and stall on the
  // other, which is what left the band family with 47 chorded edges.
  const bool a_quadric = isQuadric(a), b_quadric = isQuadric(b);
  if (a_quadric != b_quadric) {
    const Surface *quadric = a_quadric ? a : b;
    const Surface *other = a_quadric ? b : a;
    const Vector3d start = p;
    bool traced = false, met = false;
    if (const auto *grid = dynamic_cast<const GridSurface *>(other)) {
      traced = true;
      met = crossInParameters(grid, quadric, p, tol);
    } else if (const auto *patch = dynamic_cast<const BezierPatchSurface *>(other)) {
      traced = true;
      met = crossInParameters(patch, quadric, p, tol);
    }
    if (met) return true;
    // It does not always converge: where the crossing lies off the end of the
    // patch, or a step lands in another profile span, the trace has nowhere to
    // go. Measured on the band family, 208 of 15396 samples end that way against
    // 13371 reaching 1e-14 with the point *on* the sweep. Those fall back to
    // Newton on the two implicits, which answers for the quadric and stalls on
    // the sweep - no worse than this exporter had before, and the fit's own
    // acceptance is what decides either way.
    if (traced) p = start;
  }

  // The best point seen, not the last one. Newton's final iterate is not
  // necessarily its closest approach - a step near tangency can throw it
  // further out than where it started - and the caller's acceptance test is a
  // better judge of a candidate than this function's own tolerance is. Keeping
  // the best and handing it back either way is what lets a caller take a point
  // that missed `tol` but satisfies what it actually needs; discarding it cost
  // 42 corners of the band family that the acceptance test would have kept.
  Vector3d best = p;
  double best_residual = std::numeric_limits<double>::infinity();
  for (int step = 0; step <= 256; step++) {
    double fa = 0, fb = 0;
    Vector3d ga, gb;
    double da = 0, db = 0;
    if (!surfaceImplicit(a, p, fa, ga, &da) || !surfaceImplicit(b, p, fb, gb, &db)) break;
    const double residual = std::max(da, db);
    if (residual < best_residual) {
      best_residual = residual;
      best = p;
    }
    if (residual <= tol) break;
    if (step == 256) break;
    const double aa = ga.dot(ga), ab = ga.dot(gb), bb = gb.dot(gb);
    const double det = aa * bb - ab * ab;
    if (fabs(det) < 1e-18 * std::max(1.0, aa * bb)) break;  // tangent, or worse
    const double alpha = (-fa * bb + fb * ab) / det;
    const double beta = (-fb * aa + fa * ab) / det;
    const Vector3d stepv = ga * alpha + gb * beta;
    if (!stepv.allFinite()) break;
    p += stepv;
    if (!p.allFinite()) break;
  }
  if (!std::isfinite(best_residual)) return false;
  p = best;
  return best_residual <= tol;
}

/*! The point three surfaces meet in, nearest to a given start.
 *
 * Three surfaces meet in a *point*, where two meet in a curve - so this is the
 * best determined a corner ever is, and the one case the ladder used to decline
 * outright ("more than two analytic faces: leave it"). A corner where a sweep
 * runs into the crease between a cone and the bore beside it is exactly this,
 * and leaving it put it where the mesh had it, off the sweep its own face is
 * written on.
 *
 * Newton on all three residuals at once, the Jacobian being the three surfaces'
 * own gradients. For quadrics and planes those are exact and it converges
 * quadratically; a grid answers with its foot-point plane, which is first order,
 * so it converges linearly there and the acceptance test is what decides either
 * way. No tolerance is invented here: the caller asks for a residual and checks
 * the answer against the same 1e-9 the two-surface rung does.
 *
 * `projectOntoSurfaceAndPlanes` stays the route for a surface and two planes,
 * because solving *along* their shared line leaves the planes exact to rounding
 * and finds the near root of the two a line meets a quadric in. This is the
 * general case that has no such line.
 *
 * `travel` confines the search to a ball of that radius about the start, for the
 * same reason the line solver confines its window: a far root is a true solution
 * that is the wrong answer to the question. Where two of the three meet at a
 * grazing angle the answer is also far - lid10's thread crosses the crease at
 * the taper at 1.6 degrees, so the tessellation's own 0.037 displaces the exact
 * triple point 1.338 along the crease. Saying no root is near is the answer
 * there, not a failure to converge, and the caller reports it.
 */
bool projectOntoThree(const Surface *a, const Surface *b, const Surface *c, Vector3d& p, double tol,
                      double travel)
{
  const Surface *s[3] = {a, b, c};
  const Vector3d start = p;
  Vector3d best = p;
  double best_residual = std::numeric_limits<double>::infinity();
  for (int step = 0; step <= 128; step++) {
    Eigen::Matrix3d J;
    Vector3d f;
    double residual = 0;
    bool ok = true;
    for (int i = 0; i < 3; i++) {
      double fi = 0, di = 0;
      Vector3d gi;
      if (!surfaceImplicit(s[i], p, fi, gi, &di)) {
        ok = false;
        break;
      }
      f[i] = fi;
      J.row(i) = gi.transpose();
      residual = std::max(residual, di);
    }
    if (!ok) break;
    if (residual < best_residual) {
      best_residual = residual;
      best = p;
    }
    if (residual <= tol || step == 128) break;
    // Three surfaces crossing in a point have three independent normals there.
    // A singular Jacobian says they do not - two of them are tangent, or all
    // three share a direction - and then there is no point to find rather than
    // a step to take.
    const Eigen::FullPivLU<Eigen::Matrix3d> lu(J);
    if (!lu.isInvertible()) break;
    const Vector3d stepv = lu.solve(-f);
    if (!stepv.allFinite()) break;
    p += stepv;
    if (!p.allFinite()) break;
    // Confined rather than clipped, as the line solver does it: a step leaving
    // the window is walked to its edge, so the whole of what is allowed is still
    // explored.
    if (travel > 0) {
      const Vector3d off = p - start;
      if (off.norm() > travel) p = start + off.normalized() * travel;
    }
  }
  if (!std::isfinite(best_residual)) return false;
  p = best;
  return best_residual <= tol && (travel <= 0 || (best - start).norm() <= travel);
}

/*! The point where a surface meets the line two planes cross in, nearest to a
 *  given start.
 *
 * The line is solved *along*, not around. Writing it as `base + t.dhat` leaves
 * one unknown and one equation, `f(base + t.dhat) = 0`, and Newton on that is
 * one division per step. Two things follow that matter more than the speed.
 *
 * **The planes come out exact.** Every candidate is on the line by
 * construction, so `n.p - d` is zero to rounding rather than to a tolerance,
 * and there is no residual left for a caller to check.
 *
 * **The near root is the one found.** A line meets a quadric twice, and the
 * far crossing is a real solution of the system that is the wrong answer to the
 * question - on `step-shared-arc` it is 2.0 away and takes a cone that was
 * exact out by 1.41. Alternating projection and a 3x3 Newton both wander to it:
 * measured over the fixtures, every one of the 184 corners this used to give up
 * on had converged, none of them off the surface or off a plane, and the worst
 * landed 1453 times the allowed travel away. Searching a window around the
 * start cannot reach it.
 *
 * `travel` is the half width of that window, in millimetres, and the search is
 * confined to it: a root outside is not a near root and saying so is the
 * answer, not a failure to converge.
 */
bool projectOntoSurfaceAndPlanes(const Surface *surface, const Vector3d& n1, double d1,
                                 const Vector3d& n2, double d2, Vector3d& p, double tol, double travel,
                                 double *closest)
{
  const Vector3d dir = n1.cross(n2);
  const double dirn = dir.norm();
  if (dirn < 1e-12) return false;  // the planes are parallel; there is no line
  const Vector3d dhat = dir / dirn;
  // A point on the line: solve n1.x = d1, n2.x = d2 within span(n1, n2).
  const double c = n1.dot(n2), det = 1 - c * c;
  if (fabs(det) < 1e-12) return false;
  const Vector3d base = n1 * ((d1 - d2 * c) / det) + n2 * ((d2 - d1 * c) / det);

  const double t0 = dhat.dot(p - base);
  const double lo = t0 - travel, hi = t0 + travel;
  double t = t0, best_t = t0, best = std::numeric_limits<double>::infinity();
  for (int step = 0; step <= 32; step++) {
    const Vector3d at = base + dhat * t;
    double f = 0, d = 0;
    Vector3d g;
    if (!surfaceImplicit(surface, at, f, g, &d)) break;
    if (d < best) {
      best = d;
      best_t = t;
    }
    if (d <= tol) break;
    if (step == 32) break;
    // df/dt = grad f . dhat, zero where the line runs tangent along the
    // surface - there is no crossing there to walk to.
    const double slope = g.dot(dhat);
    if (fabs(slope) < 1e-12 * std::max(1.0, g.norm())) break;
    double next = t - f / slope;
    if (!std::isfinite(next)) break;
    // Confined rather than clipped: a step that leaves the window is walked to
    // its edge, so the search still explores the whole of what is allowed.
    next = std::min(hi, std::max(lo, next));
    if (fabs(next - t) < 1e-16 * std::max(1.0, fabs(t))) break;
    t = next;
  }
  if (closest != nullptr) *closest = best;
  if (!std::isfinite(best)) return false;
  p = base + dhat * best_t;
  return best <= tol;
}

// Forwarding overloads for callers holding a Mesh rather than the graph.
std::vector<Patch> recogniseBezierPatches(const Mesh& mesh,
                                          const std::vector<std::shared_ptr<Surface>>& surfaces,
                                          const std::vector<char>& consumed,
                                          std::vector<std::string>& report, ClaimLedger *ledger,
                                          std::size_t declared_count)
{
  return recogniseBezierPatches(recognition::FacetGraph(mesh), surfaces, consumed, report, ledger,
                                declared_count);
}

std::vector<Patch> recogniseGridPatches(const Mesh& mesh,
                                        const std::vector<std::shared_ptr<Surface>>& surfaces,
                                        const std::vector<char>& consumed,
                                        std::vector<std::string>& report, ClaimLedger *ledger,
                                        std::size_t declared_count)
{
  return recogniseGridPatches(recognition::FacetGraph(mesh), surfaces, consumed, report, ledger,
                              declared_count);
}

Result recogniseSurfacesOfRevolution(const Mesh& mesh,
                                     const std::vector<std::shared_ptr<Surface>>& surfaces, double tol,
                                     std::size_t declared_count)
{
  return recogniseSurfacesOfRevolution(recognition::FacetGraph(mesh), surfaces, tol, declared_count);
}

}  // namespace AnalyticFeatures
