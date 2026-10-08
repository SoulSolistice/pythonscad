/*! The model's own vertices and curve points - see Arrangement.h. */
#include "geometry/model/Arrangement.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

#include "geometry/MachinePrecision.h"
#include "geometry/Surface.h"
#include "geometry/recognition/Exact.h"

namespace model {

using recognition::forwardError;
using recognition::onLine;
using recognition::parallel;
using recognition::perpendicular;
using recognition::sameLine;
using recognition::samePoint;

namespace {

/*! A surface the arrangement can intersect: a plane (c a point, a its unit
 * normal), or a quadric of revolution about the line through c along unit a -
 * a cylinder of radius r, a cone of radius r + slope h at height h, a sphere
 * of radius r about c. */
struct Shape {
  enum Kind { Plane, Cylinder, Cone, Sphere } kind;
  Vector3d c, a;
  double r = 0, slope = 0;
  const Surface *surface;
};

std::optional<Shape> shapeOf(const Surface *s)
{
  if (const auto *p = dynamic_cast<const PlaneSurface *>(s))
    return Shape{Shape::Plane, p->refpt, p->normdir.normalized(), 0, 0, s};
  if (const auto *c = dynamic_cast<const CylinderSurface *>(s))
    return Shape{Shape::Cylinder, c->refpt, c->normdir.normalized(), c->r, 0, s};
  if (const auto *k = dynamic_cast<const ConeSurface *>(s))
    return Shape{Shape::Cone, k->refpt, k->normdir.normalized(), k->r, k->slope, s};
  if (const auto *b = dynamic_cast<const SphereSurface *>(s))
    return Shape{Shape::Sphere, b->refpt, Vector3d(0, 0, 1), b->r, 0, s};
  return std::nullopt;
}

double scaleOf(const Shape& s)
{
  return s.c.norm() + std::fabs(s.r);
}

/*! Two unit vectors completing unit a to a frame. */
void frameOf(const Vector3d& a, Vector3d& e1, Vector3d& e2)
{
  e1 = a.unitOrthogonal();
  e2 = a.cross(e1);
}

/*! The roots of A t^2 + B t + C. Within its forward error of zero the
 * discriminant is zero - a tangency, the model's statement at machine
 * precision - and the double root -B / 2A is well conditioned. A line lying on
 * the surface (every coefficient zero) has no isolated point: none. */
bool roots(double A, double B, double C, double scale, std::vector<double>& t)
{
  const double tiny = forwardError(16, scale);
  if (std::fabs(A) <= tiny) {
    if (std::fabs(B) <= tiny) return true;  // on the surface throughout, or never on it
    t.push_back(-C / B);
    return true;
  }
  const double disc = B * B - 4 * A * C;
  if (std::fabs(disc) <= forwardError(16, B * B + std::fabs(4 * A * C))) {
    t.push_back(-B / (2 * A));
    return true;
  }
  if (disc < 0) return true;
  // The root without cancellation first, the other from the product.
  const double q = -(B + std::copysign(std::sqrt(disc), B)) / 2;
  t.push_back(q / A);
  if (q != 0) t.push_back(C / q);
  return true;
}

/*! Where the line p + t d meets a quadric. */
bool lineMeets(const Shape& q, const Vector3d& p, const Vector3d& d, std::vector<Vector3d>& out)
{
  const Vector3d w = p - q.c;
  double A = 0, B = 0, C = 0;
  if (q.kind == Shape::Sphere) {
    A = d.squaredNorm();
    B = 2 * w.dot(d);
    C = w.squaredNorm() - q.r * q.r;
  } else {
    const Vector3d wp = w - q.a * q.a.dot(w), dp = d - q.a * q.a.dot(d);
    const double h0 = q.a.dot(w), h1 = q.a.dot(d);
    const double r0 = q.r + q.slope * h0, r1 = q.slope * h1;
    A = dp.squaredNorm() - r1 * r1;
    B = 2 * (wp.dot(dp) - r0 * r1);
    C = wp.squaredNorm() - r0 * r0;
  }
  std::vector<double> t;
  if (!roots(A, B, C, scaleOf(q) + p.norm() + d.norm(), t)) return false;
  for (const double s : t) {
    const Vector3d x = p + d * s;
    // A cone's other nappe is no part of it.
    if (q.kind == Shape::Cone && q.r + q.slope * q.a.dot(x - q.c) < 0) continue;
    out.push_back(x);
  }
  return true;
}

/*! The line two planes meet in, as a point and a direction; false if parallel. */
bool planesMeet(const Shape& a, const Shape& b, Vector3d& p, Vector3d& d)
{
  d = a.a.cross(b.a);
  if (d.norm() <= forwardError(8, 1.0)) return false;
  Matrix3d m;
  m.row(0) = a.a.transpose();
  m.row(1) = b.a.transpose();
  m.row(2) = d.transpose();
  p = m.inverse() * Vector3d(a.a.dot(a.c), b.a.dot(b.c), d.dot(a.c));
  return true;
}

/*! A circle: centre q, unit axis a, radius rho. */
struct Circle {
  Vector3d q, a;
  double rho;
};

/*! Whether two quadrics share an axis - a sphere's being any line through its
 * centre - and if so the circles they meet in, about the first's axis. False
 * if they do not share one: their curve is not a conic. */
bool coaxialCircles(const Shape& u, const Shape& v, std::vector<Circle>& out, bool& coaxial)
{
  coaxial = false;
  const Shape& axial = u.kind != Shape::Sphere ? u : v;
  const Shape& other = &axial == &u ? v : u;
  if (axial.kind == Shape::Sphere) {
    // Two spheres: concentric ones never meet unless they are one.
    coaxial = samePoint(u.c, v.c);
    return true;
  }
  if (other.kind == Shape::Sphere) {
    coaxial = onLine(axial.c, axial.a, other.c);
  } else {
    coaxial = sameLine(axial.c, axial.a, other.c, other.a);
  }
  if (!coaxial) return true;
  const Vector3d a = axial.a;
  // Radius at height h along a from axial.c, and its square, for each.
  auto radiusOf = [&](const Shape& s, double h) {
    if (s.kind == Shape::Cylinder) return s.r;
    const double hs = a.dot(s.c - axial.c), sign = s.a.dot(a) < 0 ? -1.0 : 1.0;
    return s.r + s.slope * sign * (h - hs);
  };
  std::vector<double> hs;
  const double scale = scaleOf(u) + scaleOf(v);
  if (other.kind == Shape::Sphere) {
    // rho(h)^2 + (h - h_s)^2 = R^2, rho linear in h.
    const double h_s = a.dot(other.c - axial.c);
    const double r0 = radiusOf(axial, 0), r1 = radiusOf(axial, 1) - r0;
    if (!roots(r1 * r1 + 1, 2 * (r0 * r1 - h_s), r0 * r0 + h_s * h_s - other.r * other.r, scale, hs))
      return false;
  } else {
    const double r0 = radiusOf(axial, 0) - radiusOf(other, 0);
    const double r1 = (radiusOf(axial, 1) - radiusOf(other, 1)) - r0;
    if (std::fabs(r1) > forwardError(8, scale)) hs.push_back(-r0 / r1);
  }
  for (const double h : hs) {
    const double rho = radiusOf(axial, h);
    if (rho < 0) continue;
    out.push_back(Circle{axial.c + a * h, a, rho});
  }
  return true;
}

/*! Where a circle meets a plane. */
bool circleMeetsPlane(const Circle& k, const Shape& plane, std::vector<Vector3d>& out)
{
  Vector3d e1, e2;
  frameOf(k.a, e1, e2);
  const double A = k.rho * plane.a.dot(e1), B = k.rho * plane.a.dot(e2), C = plane.a.dot(k.q - plane.c);
  const double K = std::hypot(A, B);
  const double tol = forwardError(16, k.q.norm() + k.rho + plane.c.norm());
  if (K <= tol) return true;  // parallel: none, or the whole circle - no isolated point
  if (std::fabs(std::fabs(C) - K) <= tol) {
    // Tangent: the one point where the plane touches the circle.
    const double t = std::atan2(B, A) + (C > 0 ? M_PI : 0.0);
    out.push_back(k.q + (e1 * cos(t) + e2 * sin(t)) * k.rho);
    return true;
  }
  if (std::fabs(C) > K) return true;
  const double phi = std::atan2(B, A), spread = std::acos(-C / K);
  for (const double t : {phi + spread, phi - spread})
    out.push_back(k.q + (e1 * cos(t) + e2 * sin(t)) * k.rho);
  return true;
}

/*! The two points of a circle where n . x is stationary - one, where n is
 * normal to its plane and every point is. */
void circleCritical(const Circle& k, const Vector3d& n, std::vector<Vector3d>& out)
{
  const Vector3d across = n - k.a * k.a.dot(n);
  Vector3d e1, e2;
  frameOf(k.a, e1, e2);
  const Vector3d dir = across.norm() > 0 ? Vector3d(across.normalized()) : e1;
  out.push_back(k.q + dir * k.rho);
  if (across.norm() > 0) out.push_back(k.q - dir * k.rho);
}

}  // namespace

bool leafSurfaces(const SolidTree& tree, std::vector<const Surface *>& out)
{
  using Op = SolidTree::Op;
  switch (tree.op()) {
  case Op::Declared:
    for (const auto& b : tree.solid()->bounds) {
      bool known = false;
      for (const Surface *s : out) known = known || s == b.get() || s->sameAs(*b);
      if (!known) out.push_back(b.get());
    }
    return true;
  case Op::Union:
  case Op::Intersection:
  case Op::Difference:
    for (const auto& child : tree.children()) {
      if (!leafSurfaces(*child, out)) return false;
    }
    return true;
  default: return false;
  }
}

bool triplePoints(const std::vector<const Surface *>& surfaces, std::vector<ArrangementPoint>& out)
{
  std::vector<Shape> shapes;
  for (const Surface *s : surfaces) {
    const auto shape = shapeOf(s);
    if (!shape) return false;
    shapes.push_back(*shape);
  }
  const std::size_t n = shapes.size();
  for (std::size_t i = 0; i < n; i++) {
    for (std::size_t j = i + 1; j < n; j++) {
      for (std::size_t k = j + 1; k < n; k++) {
        std::vector<const Shape *> planes, quadrics;
        for (const std::size_t x : {i, j, k})
          (shapes[x].kind == Shape::Plane ? planes : quadrics).push_back(&shapes[x]);
        const std::vector<const Surface *> on{shapes[i].surface, shapes[j].surface, shapes[k].surface};
        std::vector<Vector3d> found;
        if (planes.size() == 3) {
          Matrix3d m;
          m.row(0) = planes[0]->a.transpose();
          m.row(1) = planes[1]->a.transpose();
          m.row(2) = planes[2]->a.transpose();
          if (std::fabs(m.determinant()) <= forwardError(12, 1.0)) continue;  // a line or none
          found.push_back(m.inverse() * Vector3d(planes[0]->a.dot(planes[0]->c),
                                                 planes[1]->a.dot(planes[1]->c),
                                                 planes[2]->a.dot(planes[2]->c)));
        } else if (planes.size() == 2) {
          Vector3d p, d;
          if (!planesMeet(*planes[0], *planes[1], p, d)) continue;
          if (!lineMeets(*quadrics[0], p, d, found)) return false;
        } else {
          // A plane and two quadrics, or three quadrics: only where the
          // quadrics share an axis, meeting in circles.
          std::vector<Circle> circles;
          bool coaxial = false;
          if (!coaxialCircles(*quadrics[0], *quadrics[1], circles, coaxial)) return false;
          if (!coaxial) return false;
          if (planes.size() == 1) {
            for (const Circle& c : circles) {
              if (!circleMeetsPlane(c, *planes[0], found)) return false;
            }
          } else {
            // A third coaxial quadric meets those circles nowhere isolated, or
            // all of them; a third on another axis meets them in a quartic.
            bool third = false;
            std::vector<Circle> more;
            if (!coaxialCircles(*quadrics[0], *quadrics[2], more, third)) return false;
            if (!third) return false;
          }
        }
        for (const Vector3d& x : found) out.push_back(ArrangementPoint{x, on});
      }
    }
  }
  return true;
}

bool criticalPoints(const std::vector<const Surface *>& surfaces, const Vector3d& n,
                    std::vector<ArrangementPoint>& out)
{
  std::vector<Shape> shapes;
  for (const Surface *s : surfaces) {
    const auto shape = shapeOf(s);
    if (!shape) return false;
    shapes.push_back(*shape);
  }
  const Vector3d unit = n.normalized();
  for (const Shape& s : shapes) {
    if (s.kind == Shape::Sphere) {
      out.push_back(ArrangementPoint{s.c + unit * s.r, {s.surface}});
      out.push_back(ArrangementPoint{s.c - unit * s.r, {s.surface}});
    }
  }
  for (std::size_t i = 0; i < shapes.size(); i++) {
    for (std::size_t j = i + 1; j < shapes.size(); j++) {
      const Shape& u = shapes[i];
      const Shape& v = shapes[j];
      const std::vector<const Surface *> on{u.surface, v.surface};
      std::vector<Vector3d> found;
      if (u.kind == Shape::Plane && v.kind == Shape::Plane) continue;  // a line: its ends are vertices
      if (u.kind == Shape::Plane || v.kind == Shape::Plane) {
        const Shape& plane = u.kind == Shape::Plane ? u : v;
        const Shape& q = &plane == &u ? v : u;
        if (q.kind == Shape::Sphere) {
          const double d = plane.a.dot(q.c - plane.c);
          if (std::fabs(d) < q.r) {
            circleCritical(Circle{q.c - plane.a * d, plane.a, std::sqrt(q.r * q.r - d * d)}, n, found);
          }
        } else if (parallel(plane.a, q.a)) {
          // Square to the axis: a circle at the plane's height.
          const double h = q.a.dot(plane.c - q.c);
          const double rho = q.r + q.slope * h;
          if (rho >= 0) circleCritical(Circle{q.c + q.a * h, q.a, rho}, n, found);
        } else if (q.kind == Shape::Cylinder && !perpendicular(plane.a, q.a)) {
          // Oblique across a cylinder: centre + u cos t + v sin t.
          Vector3d e1, e2;
          frameOf(q.a, e1, e2);
          const double ma = plane.a.dot(q.a);
          const Vector3d centre = q.c + q.a * (plane.a.dot(plane.c - q.c) / ma);
          const Vector3d du = e1 * q.r - q.a * (q.r * plane.a.dot(e1) / ma);
          const Vector3d dv = e2 * q.r - q.a * (q.r * plane.a.dot(e2) / ma);
          const double t = std::atan2(n.dot(dv), n.dot(du));
          found.push_back(centre + du * cos(t) + dv * sin(t));
          found.push_back(centre - du * cos(t) - dv * sin(t));
        } else if (q.kind == Shape::Cylinder) {
          continue;  // along the axis: lines, whose ends are vertices
        } else {
          return false;  // an oblique section of a cone: a conic this does not solve yet
        }
      } else {
        std::vector<Circle> circles;
        bool coaxial = false;
        if (!coaxialCircles(u, v, circles, coaxial)) return false;
        if (!coaxial) return false;
        for (const Circle& c : circles) circleCritical(c, n, found);
      }
      for (const Vector3d& x : found) out.push_back(ArrangementPoint{x, on});
    }
  }
  return true;
}

}  // namespace model
