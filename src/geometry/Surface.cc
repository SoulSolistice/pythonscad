#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "geometry/linalg.h"
#include "geometry/Surface.h"

void Surface::display(const std::vector<Vector3d>& vertices)
{
  printf("refpt is (%g/%g/%g)\n", this->refpt[0], this->refpt[1], this->refpt[2]);
}

void Surface::reverse(void)
{
}

std::shared_ptr<Surface> Surface::clone() const
{
  return std::make_shared<Surface>(*this);
}

/*! A similarity is a transform whose linear part satisfies M^T M = s^2 I: a
 * rotation and a uniform scale, possibly mirrored. Anything else (non uniform
 * scale, shear) turns a circular cross section into an ellipse, which none of
 * these surface types can describe. */
static bool similarityScale(const Transform3d& mat, double& scale)
{
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
  scale = sqrt(s2);
  return true;
}

bool Surface::transform(const Transform3d& mat)
{
  double scale;
  if (!similarityScale(mat, scale)) return false;
  refpt = mat * refpt;
  normdir = (mat.linear() * normdir).normalized();
  return true;
}

int Surface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  double dist = (pt - refpt).dot(normdir);
  if (fabs(dist) > 1e-5) return 0;
  return 1;
}

int Surface::operator==(const Surface& other)
{
  return 0;
}

SphereSurface::SphereSurface(Vector3d refpt, Vector3d normdir, double r)
{
  this->refpt = refpt;
  this->normdir = normdir;
  this->r = r;
}

void SphereSurface::display(const std::vector<Vector3d>& vertices)
{
  printf("SphereSurface r=%g at (%g/%g/%g)\n", r, refpt[0], refpt[1], refpt[2]);
}

std::shared_ptr<Surface> SphereSurface::clone() const
{
  return std::make_shared<SphereSurface>(*this);
}

bool SphereSurface::transform(const Transform3d& mat)
{
  if (!Surface::transform(mat)) return false;
  this->r *= (mat.linear() * Vector3d(1, 0, 0)).norm();
  return true;
}

int SphereSurface::operator==(const SphereSurface& other)
{
  // the polar axis is not compared: it only orients the parameterisation, and
  // two records of the same sphere describe the same surface whichever way
  // their poles point
  if ((refpt - other.refpt).norm() > 1e-6) return 0;
  if (fabs(r - other.r) > 1e-6) return 0;
  return 1;
}

int SphereSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  return fabs((pt - refpt).norm() - r) > 1e-5 ? 0 : 1;
}

CylinderSurface::CylinderSurface(Vector3d refpt, Vector3d normdir, double r)
{
  this->refpt = refpt;
  this->normdir = normdir;
  this->r = r;
}

void CylinderSurface::display(const std::vector<Vector3d>& vertices)
{
  printf("CylinderSurface\n");
  //    printf("(%g/%g/%g) - %d(%g/%g/%g) cent=(%g/%g/%g), normdir=(%g/%g/%g) r=%g\n",>start, start[0],
  //    start[1], start[2], this->end, end[0], end[1], end[2], refpt[0], refpt[1], refpt[2], normdir[0],
  //    normdir[1], normdir[2], r);
}

void CylinderSurface::reverse(void)
{
  this->normdir = -this->normdir;
}

std::shared_ptr<Surface> CylinderSurface::clone() const
{
  return std::make_shared<CylinderSurface>(*this);
}

bool CylinderSurface::transform(const Transform3d& mat)
{
  if (!Surface::transform(mat)) return false;
  // Surface::transform has established that the linear part is a similarity,
  // so any column of it gives the uniform scale factor.
  this->r *= (mat.linear() * Vector3d(1, 0, 0)).norm();
  return true;
}

int CylinderSurface::operator==(const CylinderSurface& other)
{
  if ((normdir - other.normdir).norm() > 1e-6) return 0;
  if ((refpt - other.refpt).norm() > 1e-6) return 0;
  if (fabs(r - other.r) > 1e-6) return 0;
  return 1;
}

int CylinderSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  // check if on plane
  double dist = (pt - refpt).dot(normdir);

  if (fabs((pt - dist * normdir - refpt).norm() - r) > 1e-5) return 0;

  return 1;
}
