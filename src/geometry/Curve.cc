#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "geometry/linalg.h"
#include "geometry/Curve.h"
#include "geometry/Surface.h"
#include "geometry/recognition/Exact.h"

void Curve::display(const std::vector<Vector3d>& vertices)
{
  Vector3d start = vertices[this->start];
  Vector3d end = vertices[this->end];
  printf("%d(%g/%g/%g) - %d(%g/%g/%g)\n", this->start, start[0], start[1], start[2], this->end, end[0],
         end[1], end[2]);
}

void Curve::reverse(void)
{
  int tmp = this->start;
  this->start = this->end;
  this->end = tmp;
}

int Curve::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  Vector3d p1 = vertices[start];
  Vector3d p2 = vertices[end];
  Vector3d dir = p2 - p1;
  if (fabs(p2[0] - p1[0]) < 1e-5) return 0;
  double fact = (pt[0] - p1[0]) / (p2[0] - p1[0]);
  Vector3d px = p1 + dir * fact;
  if (fabs(px[1] - pt[1]) > 1e-5) return 0;
  if (fabs(px[2] - pt[2]) > 1e-5) return 0;
  return 1;
}

bool Curve::sameAs(const Curve&) const
{
  return false;
}

std::shared_ptr<Curve> Curve::clone() const
{
  return std::make_shared<Curve>(*this);
}

/*! A straight curve is defined purely by its end vertices, which the caller
 * transforms along with the rest of the geometry, so there is nothing to do. */
bool Curve::transform(const Transform3d& mat)
{
  return true;
}

std::shared_ptr<Curve> ArcCurve::clone() const
{
  return std::make_shared<ArcCurve>(*this);
}

bool ArcCurve::transform(const Transform3d& mat)
{
  // Only a similarity keeps an arc circular; see Surface::transform.
  const Matrix3d m = mat.linear();
  const Matrix3d mtm = m.transpose() * m;
  const double s2 = mtm(0, 0);
  if (!(s2 > 0)) return false;
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      const double expected = (i == j) ? s2 : 0.0;
      if (fabs(mtm(i, j) - expected) > 1e-9 * s2) return false;
    }
  }
  this->center = mat * this->center;
  this->normdir = (m * this->normdir).normalized();
  this->r *= sqrt(s2);
  return true;
}
ArcCurve::ArcCurve(Vector3d cent, Vector3d normdir, double r)
{
  this->center = cent;
  this->normdir = normdir;
  this->r = r;
}
void ArcCurve::display(const std::vector<Vector3d>& vertices)
{
  Vector3d start = vertices[this->start];
  Vector3d end = vertices[this->end];
  printf("%d(%g/%g/%g) - %d(%g/%g/%g) cent=(%g/%g/%g), normdir=(%g/%g/%g) r=%g\n", this->start, start[0],
         start[1], start[2], this->end, end[0], end[1], end[2], center[0], center[1], center[2],
         normdir[0], normdir[1], normdir[2], r);
}

void ArcCurve::reverse(void)
{
  int tmp = this->start;
  this->start = this->end;
  this->end = tmp;
  this->normdir = -this->normdir;
}

bool ArcCurve::sameAs(const Curve& other) const
{
  const auto *o = dynamic_cast<const ArcCurve *>(&other);
  if (o == nullptr || start != o->start || end != o->end) return false;
  if (!recognition::parallel(normdir, o->normdir)) return false;
  // A point is on a circle where it is on the circle's plane and on the sphere
  // about its centre through it, each at D0.
  auto on = [](const ArcCurve& c, const Vector3d& p) {
    return PlaneSurface(c.center, c.normdir).residual(p).exact() &&
           SphereSurface(c.center, c.normdir, c.r).residual(p).exact();
  };
  auto three = [](const ArcCurve& c) {
    const Vector3d n = c.normdir.normalized();
    int k = 0;
    for (int i = 1; i < 3; i++) {
      if (std::fabs(n[i]) < std::fabs(n[k])) k = i;
    }
    Vector3d axis(0, 0, 0);
    axis[k] = 1;
    const Vector3d e1 = n.cross(axis).normalized(), e2 = n.cross(e1);
    return std::vector<Vector3d>{c.center + e1 * c.r, c.center + e2 * c.r, c.center - e1 * c.r};
  };
  for (const Vector3d& p : three(*o)) {
    if (!on(*this, p)) return false;
  }
  for (const Vector3d& p : three(*this)) {
    if (!on(*o, p)) return false;
  }
  return true;
}

double ArcCurve::calcAngle(Vector3d refdir, Vector3d dir, Vector3d normdir)
{
  double ang = acos(refdir.dot(dir));
  if ((dir.cross(refdir)).dot(normdir) > 0) ang = -ang;
  if (ang < -1e-5) ang += 2 * M_PI;
  return ang;
}

int ArcCurve::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  // check if on plane
  if (fabs((pt - center).dot(normdir)) > 1e-5) return 0;
  if (fabs((pt - center).norm() - r) > 1e-5) return 0;
  // TODO check start and endangle
  Vector3d startdir = (vertices[start] - center).normalized();

  Vector3d enddir = (vertices[end] - center).normalized();
  double endang = calcAngle(startdir, enddir, normdir);

  Vector3d ptdir = (pt - center).normalized();
  double ptang = calcAngle(ptdir, enddir, normdir);

  if (ptang > endang + 1e-2) return 0;
  return 1;
}
