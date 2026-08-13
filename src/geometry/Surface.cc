#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <memory>
#include <typeinfo>
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

bool Surface::sameAs(const Surface& other) const
{
  // The type first: a sphere and a torus drawn about the same axis through the
  // same point agree on everything the base class holds, and dropping one of
  // them for the other loses a surface no measurement can recover.
  if (typeid(*this) != typeid(other)) return false;
  if ((refpt - other.refpt).norm() > 1e-9) return false;
  // antiparallel axes describe the same surface, only parameterised the other
  // way round
  return fabs(fabs(normdir.dot(other.normdir)) - 1.0) < 1e-9;
}

bool containsSurface(const std::vector<std::shared_ptr<Surface>>& list,
                     const std::shared_ptr<Surface>& surface)
{
  for (const auto& s : list) {
    if (s == surface) return true;
    if (s && surface && s->sameAs(*surface)) return true;
  }
  return false;
}

void mergeSurfaces(std::vector<std::shared_ptr<Surface>>& into,
                   const std::vector<std::shared_ptr<Surface>>& from)
{
  for (const auto& surface : from) {
    if (!containsSurface(into, surface)) into.push_back(surface);
  }
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

bool SphereSurface::sameAs(const Surface& other) const
{
  if (!Surface::sameAs(other)) return false;
  return fabs(r - static_cast<const SphereSurface&>(other).r) < 1e-9;
}

int SphereSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  return fabs((pt - refpt).norm() - r) > 1e-5 ? 0 : 1;
}

TorusSurface::TorusSurface(Vector3d refpt, Vector3d normdir, double r_major, double r_minor)
{
  this->refpt = refpt;
  this->normdir = normdir;
  this->r_major = r_major;
  this->r_minor = r_minor;
}

void TorusSurface::display(const std::vector<Vector3d>& vertices)
{
  printf("TorusSurface R=%g r=%g at (%g/%g/%g)\n", r_major, r_minor, refpt[0], refpt[1], refpt[2]);
}

void TorusSurface::reverse(void)
{
  this->normdir = -this->normdir;
}

std::shared_ptr<Surface> TorusSurface::clone() const
{
  return std::make_shared<TorusSurface>(*this);
}

bool TorusSurface::transform(const Transform3d& mat)
{
  if (!Surface::transform(mat)) return false;
  const double scale = (mat.linear() * Vector3d(1, 0, 0)).norm();
  this->r_major *= scale;
  this->r_minor *= scale;
  return true;
}

int TorusSurface::operator==(const TorusSurface& other)
{
  if ((normdir - other.normdir).norm() > 1e-6) return 0;
  if ((refpt - other.refpt).norm() > 1e-6) return 0;
  if (fabs(r_major - other.r_major) > 1e-6) return 0;
  if (fabs(r_minor - other.r_minor) > 1e-6) return 0;
  return 1;
}

bool TorusSurface::sameAs(const Surface& other) const
{
  if (!Surface::sameAs(other)) return false;
  const auto& o = static_cast<const TorusSurface&>(other);
  return fabs(r_major - o.r_major) < 1e-9 && fabs(r_minor - o.r_minor) < 1e-9;
}

int TorusSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  // distance from the tube's centre circle, which is what a torus is
  const Vector3d rel = pt - refpt;
  const double along = rel.dot(normdir);
  const double radial = (rel - normdir * along).norm();
  return fabs(sqrt((radial - r_major) * (radial - r_major) + along * along) - r_minor) > 1e-5 ? 0 : 1;
}

namespace {

/*! de Casteljau, which is what a Bezier is: repeated linear interpolation.
 * Stable, and short enough not to need the Bernstein basis written out. */
Vector3d deCasteljau(std::vector<Vector3d> pts, double t)
{
  for (std::size_t k = pts.size(); k > 1; k--) {
    for (std::size_t i = 0; i + 1 < k; i++) pts[i] = pts[i] * (1 - t) + pts[i + 1] * t;
  }
  return pts[0];
}

/*! The derivative of a Bezier is a Bezier of one degree less, on the
 * differences of consecutive control points. */
Vector3d deCasteljauDeriv(const std::vector<Vector3d>& pts, double t)
{
  if (pts.size() < 2) return Vector3d::Zero();
  std::vector<Vector3d> d;
  d.reserve(pts.size() - 1);
  const double n = static_cast<double>(pts.size() - 1);
  for (std::size_t i = 0; i + 1 < pts.size(); i++) d.push_back((pts[i + 1] - pts[i]) * n);
  return deCasteljau(d, t);
}

}  // namespace

BezierPatchSurface::BezierPatchSurface(int degree_u, int degree_v, std::vector<Vector3d> net)
  : degree_u(degree_u), degree_v(degree_v), net(std::move(net))
{
  // The base class's refpt and normdir are only used for identity, never for
  // geometry: a patch is its control net and nothing else. A corner of the net
  // is stable under everything that can move the patch.
  this->refpt = this->net.empty() ? Vector3d::Zero() : this->net.front();
  this->normdir = Vector3d(0, 0, 1);
}

void BezierPatchSurface::display(const std::vector<Vector3d>& vertices)
{
  printf("BezierPatchSurface degree (%d,%d), %zu control points\n", degree_u, degree_v, net.size());
}

std::shared_ptr<Surface> BezierPatchSurface::clone() const
{
  return std::make_shared<BezierPatchSurface>(*this);
}

bool BezierPatchSurface::transform(const Transform3d& mat)
{
  // A Bezier is affine invariant: transforming the control points transforms
  // the surface exactly. Unlike a cylinder, this survives a non uniform scale
  // and a shear as well, so there is no similarity test to pass.
  for (auto& p : net) p = mat * p;
  refpt = net.empty() ? Vector3d::Zero() : net.front();
  return true;
}

bool BezierPatchSurface::sameAs(const Surface& other) const
{
  if (typeid(*this) != typeid(other)) return false;
  const auto& o = static_cast<const BezierPatchSurface&>(other);
  if (degree_u != o.degree_u || degree_v != o.degree_v || net.size() != o.net.size()) return false;
  for (std::size_t i = 0; i < net.size(); i++) {
    if ((net[i] - o.net[i]).norm() > 1e-9) return false;
  }
  return true;
}

Vector3d BezierPatchSurface::evaluate(double u, double v) const
{
  std::vector<Vector3d> along_u;
  along_u.reserve(degree_u + 1);
  for (int i = 0; i <= degree_u; i++) {
    std::vector<Vector3d> row;
    row.reserve(degree_v + 1);
    for (int j = 0; j <= degree_v; j++) row.push_back(control(i, j));
    along_u.push_back(deCasteljau(row, v));
  }
  return deCasteljau(along_u, u);
}

std::vector<Vector3d> BezierPatchSurface::boundary(bool along_u, bool far) const
{
  std::vector<Vector3d> out;
  if (along_u) {
    const int j = far ? degree_v : 0;
    for (int i = 0; i <= degree_u; i++) out.push_back(control(i, j));
  } else {
    const int i = far ? degree_u : 0;
    for (int j = 0; j <= degree_v; j++) out.push_back(control(i, j));
  }
  return out;
}

bool BezierPatchSurface::degenerateAt(bool along_u, bool far) const
{
  const std::vector<Vector3d> edge = boundary(along_u, far);
  for (const auto& p : edge) {
    if ((p - edge.front()).norm() > 1e-9) return false;
  }
  return true;
}

bool BezierPatchSurface::project(const Vector3d& pt, double& u, double& v) const
{
  // Minimise |S(u,v) - pt|^2 by Newton. A patch this shallow has no local
  // minima worth worrying about, but a corner fillet is degenerate at its apex
  // and the derivative vanishes there, so start from a grid rather than the
  // middle and keep the best answer.
  double best = -1;
  for (int gi = 0; gi <= 4; gi++) {
    for (int gj = 0; gj <= 4; gj++) {
      double cu = gi / 4.0, cv = gj / 4.0;
      for (int iter = 0; iter < 40; iter++) {
        std::vector<Vector3d> along_u, dv_u;
        for (int i = 0; i <= degree_u; i++) {
          std::vector<Vector3d> row;
          for (int j = 0; j <= degree_v; j++) row.push_back(control(i, j));
          along_u.push_back(deCasteljau(row, cv));
          dv_u.push_back(deCasteljauDeriv(row, cv));
        }
        const Vector3d s = deCasteljau(along_u, cu);
        const Vector3d su = deCasteljauDeriv(along_u, cu);
        const Vector3d sv = deCasteljau(dv_u, cu);
        const Vector3d r = s - pt;

        // gradient and Gauss-Newton approximation of the Hessian
        const Vector2d g(r.dot(su), r.dot(sv));
        Eigen::Matrix2d h;
        h << su.dot(su), su.dot(sv), su.dot(sv), sv.dot(sv);
        h(0, 0) += 1e-12;
        h(1, 1) += 1e-12;
        const Vector2d step = h.fullPivLu().solve(-g);
        if (!step.allFinite()) break;
        const double nu = std::min(1.0, std::max(0.0, cu + step[0]));
        const double nv = std::min(1.0, std::max(0.0, cv + step[1]));
        const bool done = fabs(nu - cu) < 1e-14 && fabs(nv - cv) < 1e-14;
        cu = nu;
        cv = nv;
        if (done) break;
      }
      const double d = (evaluate(cu, cv) - pt).norm();
      if (best < 0 || d < best) {
        best = d;
        u = cu;
        v = cv;
      }
    }
  }
  return best >= 0;
}

int BezierPatchSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  double u = 0, v = 0;
  if (!project(pt, u, v)) return 0;
  return (evaluate(u, v) - pt).norm() > 1e-7 ? 0 : 1;
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

bool CylinderSurface::sameAs(const Surface& other) const
{
  if (!Surface::sameAs(other)) return false;
  return fabs(r - static_cast<const CylinderSurface&>(other).r) < 1e-9;
}

int CylinderSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  // check if on plane
  double dist = (pt - refpt).dot(normdir);

  if (fabs((pt - dist * normdir - refpt).norm() - r) > 1e-5) return 0;

  return 1;
}
