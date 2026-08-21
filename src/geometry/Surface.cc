#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <Eigen/Dense>
#include <Eigen/LU>
#include <limits>
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

/*! sameAs(), plus the one case it deliberately does not cover.
 *
 * Surface::sameAs() compares refpt exactly, which is right for a sphere or a
 * torus - move the centre and it is a different surface. A cylinder is not like
 * that: it is infinite along its axis, so two records with the same axis and
 * radius whose reference points differ *along* that axis describe the same
 * surface, and the point only ever says where the circle that declared it was.
 *
 * That distinction shows up as soon as two generators declare the same wall:
 * `declare_cylinder()` places the point where the model says and an extrusion
 * places it at the base of the sweep. Without this they are two records, and
 * every count a fixture asserts moves for a surface nothing gained.
 */
static bool describesSameSurface(const Surface& a, const Surface& b)
{
  if (a.sameAs(b)) return true;
  const auto *ca = dynamic_cast<const CylinderSurface *>(&a);
  const auto *cb = dynamic_cast<const CylinderSurface *>(&b);
  if (ca == nullptr || cb == nullptr) return false;
  if (fabs(ca->r - cb->r) > 1e-9) return false;
  if (fabs(fabs(ca->normdir.dot(cb->normdir)) - 1.0) > 1e-9) return false;
  const Vector3d between = cb->refpt - ca->refpt;
  return (between - between.dot(ca->normdir) * ca->normdir).norm() <= 1e-9;
}

void addSurfaceUnique(std::vector<std::shared_ptr<Surface>>& list,
                      const std::shared_ptr<Surface>& surface)
{
  if (surface == nullptr) return;
  for (const auto& s : list) {
    if (s != nullptr && describesSameSurface(*s, *surface)) return;
  }
  list.push_back(surface);
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
/*! The same recursion on plain numbers, for the weights of a rational Bezier. */
double deCasteljau1d(std::vector<double> w, double t)
{
  for (std::size_t k = w.size(); k > 1; k--) {
    for (std::size_t i = 0; i + 1 < k; i++) w[i] = w[i] * (1 - t) + w[i + 1] * t;
  }
  return w.empty() ? 1.0 : w[0];
}

Vector3d deCasteljau(std::vector<Vector3d> pts, double t)
{
  for (std::size_t k = pts.size(); k > 1; k--) {
    for (std::size_t i = 0; i + 1 < k; i++) pts[i] = pts[i] * (1 - t) + pts[i + 1] * t;
  }
  return pts[0];
}

/*! The same derivative on plain numbers, for a rational Bezier's weights. */
double deCasteljauDeriv1d(const std::vector<double>& w, double t)
{
  if (w.size() < 2) return 0.0;
  std::vector<double> d;
  d.reserve(w.size() - 1);
  const double n = static_cast<double>(w.size() - 1);
  for (std::size_t i = 0; i + 1 < w.size(); i++) d.push_back((w[i + 1] - w[i]) * n);
  return deCasteljau1d(d, t);
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

BezierPatchSurface::BezierPatchSurface(int degree_u, int degree_v, std::vector<Vector3d> net,
                                       std::vector<double> weights)
  : degree_u(degree_u), degree_v(degree_v), net(std::move(net)), weights(std::move(weights))
{
  // A weight list that is all ones describes the same surface as no weight list
  // at all, and dropping it keeps the polynomial arithmetic - and every number
  // that came out of it before - untouched.
  bool trivial = true;
  for (const double w : this->weights) {
    if (w != 1.0) {
      trivial = false;
      break;
    }
  }
  if (trivial || this->weights.size() != this->net.size()) this->weights.clear();
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
  if (weights.size() != o.weights.size()) return false;
  for (std::size_t i = 0; i < net.size(); i++) {
    if ((net[i] - o.net[i]).norm() > 1e-9) return false;
  }
  // Two patches on one net but different weights are different surfaces - a
  // parabola and a circular arc through the same three points.
  for (std::size_t i = 0; i < weights.size(); i++) {
    if (fabs(weights[i] - o.weights[i]) > 1e-9) return false;
  }
  return true;
}

Vector3d BezierPatchSurface::evaluate(double u, double v) const
{
  if (!isRational()) {
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

  // Rational: run de Casteljau on (w*P, w) and divide at the end. Interpolating
  // the weighted points and the weights separately is the same thing and is what
  // makes a rational Bezier a projection of a polynomial one a dimension up.
  std::vector<Vector3d> along_u;
  std::vector<double> wu;
  along_u.reserve(degree_u + 1);
  wu.reserve(degree_u + 1);
  for (int i = 0; i <= degree_u; i++) {
    std::vector<Vector3d> row;
    std::vector<double> row_w;
    row.reserve(degree_v + 1);
    row_w.reserve(degree_v + 1);
    for (int j = 0; j <= degree_v; j++) {
      row.push_back(control(i, j) * weight(i, j));
      row_w.push_back(weight(i, j));
    }
    along_u.push_back(deCasteljau(row, v));
    wu.push_back(deCasteljau1d(row_w, v));
  }
  const Vector3d num = deCasteljau(along_u, u);
  const double den = deCasteljau1d(wu, u);
  return den == 0.0 ? num : num / den;
}

std::vector<double> BezierPatchSurface::boundaryWeights(bool along_u, bool far) const
{
  std::vector<double> out;
  if (!isRational()) return out;
  if (along_u) {
    const int j = far ? degree_v : 0;
    for (int i = 0; i <= degree_u; i++) out.push_back(weight(i, j));
  } else {
    const int i = far ? degree_u : 0;
    for (int j = 0; j <= degree_v; j++) out.push_back(weight(i, j));
  }
  return out;
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
        // The weighted control points and the weights are two polynomial
        // surfaces, N and W, and the patch is their quotient. Differentiating a
        // rational patch with the polynomial rule is what a Newton step must not
        // do: it converges on the wrong point, and pointMember() then rejects
        // vertices that lie exactly on the surface. For a polynomial patch W is
        // 1 everywhere and this reduces to the previous arithmetic.
        std::vector<Vector3d> along_u, dv_u;
        std::vector<double> w_u, dwv_u;
        for (int i = 0; i <= degree_u; i++) {
          std::vector<Vector3d> row;
          std::vector<double> row_w;
          for (int j = 0; j <= degree_v; j++) {
            row.push_back(control(i, j) * weight(i, j));
            row_w.push_back(weight(i, j));
          }
          along_u.push_back(deCasteljau(row, cv));
          dv_u.push_back(deCasteljauDeriv(row, cv));
          w_u.push_back(deCasteljau1d(row_w, cv));
          dwv_u.push_back(deCasteljauDeriv1d(row_w, cv));
        }
        const Vector3d n = deCasteljau(along_u, cu);
        const Vector3d n_du = deCasteljauDeriv(along_u, cu);
        const Vector3d n_dv = deCasteljau(dv_u, cu);
        const double w = deCasteljau1d(w_u, cu);
        const double wu_d = deCasteljauDeriv1d(w_u, cu);
        const double wv_d = deCasteljau1d(dwv_u, cu);
        if (w == 0.0) break;
        // quotient rule, which for W == 1 and its derivatives 0 is the plain one
        const Vector3d s = n / w;
        const Vector3d su = (n_du - s * wu_d) / w;
        const Vector3d sv = (n_dv - s * wv_d) / w;
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

namespace {
/*! The 1e-9 grid a GridSurface indexes its points on. */
std::tuple<int64_t, int64_t, int64_t> gridKey(const Vector3d& pt)
{
  const double res = 1e-9;
  return {(int64_t)llround(pt[0] / res), (int64_t)llround(pt[1] / res), (int64_t)llround(pt[2] / res)};
}
}  // namespace

namespace {

constexpr int GRID_DEGREE = 3;

/*! Which knot span `t` falls in, for a clamped knot vector. */
int findSpan(const std::vector<double>& knots, int n, double t)
{
  if (t >= knots[n]) return n - 1;
  if (t <= knots[GRID_DEGREE]) return GRID_DEGREE;
  int lo = GRID_DEGREE, hi = n, mid = (lo + hi) / 2;
  while (t < knots[mid] || t >= knots[mid + 1]) {
    if (t < knots[mid]) hi = mid;
    else lo = mid;
    mid = (lo + hi) / 2;
  }
  return mid;
}

/*! The four non-zero cubic basis functions at `t` in its span. */
void basisFuns(const std::vector<double>& knots, int span, double t, double out[GRID_DEGREE + 1])
{
  double left[GRID_DEGREE + 1], right[GRID_DEGREE + 1];
  out[0] = 1.0;
  for (int j = 1; j <= GRID_DEGREE; j++) {
    left[j] = t - knots[span + 1 - j];
    right[j] = knots[span + j] - t;
    double saved = 0.0;
    for (int r = 0; r < j; r++) {
      const double denom = right[r + 1] + left[j - r];
      const double temp = denom != 0.0 ? out[r] / denom : 0.0;
      out[r] = saved + right[r + 1] * temp;
      saved = left[j - r] * temp;
    }
    out[j] = saved;
  }
}

}  // namespace

void GridSurface::buildSpline()
{
  uknots.clear();
  poles.clear();
  // Below four stations there is no cubic to fit; evaluate() then reads the
  // declared points directly, which is exact at them and linear between.
  if (rows < GRID_DEGREE + 1 || cols < 1) return;

  const int m = rows;
  // Chord length parameters, averaged over the columns. They have to share one
  // parameterisation, or the result is not a tensor product and cannot be
  // written as one surface.
  std::vector<double> t(m, 0.0);
  for (int j = 0; j < cols; j++) {
    double total = 0.0;
    std::vector<double> run(m, 0.0);
    for (int i = 1; i < m; i++) {
      total += (at(i, j) - at(i - 1, j)).norm();
      run[i] = total;
    }
    if (!(total > 0)) return;
    for (int i = 1; i < m; i++) t[i] += run[i] / total / cols;
  }
  t[0] = 0.0;
  t[m - 1] = 1.0;
  for (int i = 1; i < m; i++) {
    if (!(t[i] > t[i - 1])) return;  // coincident stations leave no parameter
  }

  // Clamped averaged knots - the standard choice for interpolation, and the one
  // that keeps the system banded and non singular.
  uknots.assign(std::size_t(m) + GRID_DEGREE + 1, 0.0);
  for (int i = 0; i <= GRID_DEGREE; i++) uknots[m + i] = 1.0;
  for (int j = 1; j <= m - GRID_DEGREE - 1; j++) {
    double acc = 0.0;
    for (int i = j; i <= j + GRID_DEGREE - 1; i++) acc += t[i];
    uknots[j + GRID_DEGREE] = acc / GRID_DEGREE;
  }

  // The matrix depends only on the parameters, so it is factored once and
  // applied to every column.
  Eigen::MatrixXd A = Eigen::MatrixXd::Zero(m, m);
  for (int i = 0; i < m; i++) {
    const int span = findSpan(uknots, m, t[i]);
    double basis[GRID_DEGREE + 1];
    basisFuns(uknots, span, t[i], basis);
    for (int k = 0; k <= GRID_DEGREE; k++) A(i, span - GRID_DEGREE + k) = basis[k];
  }
  const Eigen::PartialPivLU<Eigen::MatrixXd> lu(A);

  poles.assign(std::size_t(m) * cols, Vector3d::Zero());
  Eigen::MatrixXd rhs(m, 3);
  for (int j = 0; j < cols; j++) {
    for (int i = 0; i < m; i++) rhs.row(i) = at(i, j).transpose();
    const Eigen::MatrixXd sol = lu.solve(rhs);
    for (int i = 0; i < m; i++) poles[std::size_t(i) * cols + j] = sol.row(i).transpose();
  }

  // How far this surface stands off the facets that approximate it: the widest
  // gap between the interpolant and the chords through the points it was built
  // from. See tessellationBand() for why membership needs it.
  band = 0;
  for (int j = 0; j < cols; j++) {
    for (int i = 0; i + 1 < m; i++) {
      const double mid = (t[i] + t[i + 1]) / 2;
      const double v = vspans() > 0 ? double(j) / vspans() : 0.0;
      band = std::max(band, (evaluate(mid, v) - (at(i, j) + at(i + 1, j)) / 2).norm());
    }
  }
}

std::vector<Vector3d> GridSurface::withClosingColumn(const std::vector<Vector3d>& src) const
{
  if (!closed_v || cols < 1) return src;
  std::vector<Vector3d> out;
  out.reserve(src.size() + rows);
  for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) out.push_back(src[std::size_t(i) * cols + j]);
    out.push_back(src[std::size_t(i) * cols]);
  }
  return out;
}

bool GridSurface::splineForm(int& degree_u, int& degree_v, int& rows_out, int& cols_out,
                             std::vector<Vector3d>& ctrl, std::vector<double>& knots_u,
                             std::vector<int>& mults_u, std::vector<double>& knots_v,
                             std::vector<int>& mults_v) const
{
  if (rows < 2 || cols < 2) return false;

  // Across the profile the surface is ruled, so the control points are the
  // declared points themselves and the knots are the parameters evaluate()
  // interpolates at: uniform, clamped, one span per column pair.
  degree_v = 1;
  // A closed profile is written with its first column repeated at the end,
  // which is the strip evaluate() covers and no column of the net does. Writing
  // cols columns instead would leave the face open along that strip, on a
  // surface that is closed there.
  const int segs = vspans();
  cols_out = closed_v ? cols + 1 : cols;
  knots_v.clear();
  mults_v.clear();
  for (int j = 0; j <= segs; j++) {
    knots_v.push_back(double(j) / segs);
    mults_v.push_back(j == 0 || j == segs ? 2 : 1);
  }

  rows_out = rows;
  ctrl.clear();
  if (poles.empty()) {
    // No cubic was fitted - fewer than four stations, or stations that repeat.
    // evaluate() then walks the declared points linearly, and that is a degree
    // 1 B-spline with the same uniform clamped knots as the other direction.
    degree_u = 1;
    knots_u.clear();
    mults_u.clear();
    for (int i = 0; i < rows; i++) {
      knots_u.push_back(double(i) / (rows - 1));
      mults_u.push_back(i == 0 || i == rows - 1 ? 2 : 1);
    }
    ctrl = withClosingColumn(net);
    return true;
  }

  degree_u = GRID_DEGREE;
  ctrl = withClosingColumn(poles);
  // uknots is the full knot vector, values repeated; a STEP file wants each
  // value once with the count beside it.
  knots_u.clear();
  mults_u.clear();
  for (const double k : uknots) {
    if (!knots_u.empty() && std::abs(k - knots_u.back()) <= 1e-12) {
      mults_u.back()++;
      continue;
    }
    knots_u.push_back(k);
    mults_u.push_back(1);
  }
  return true;
}

Vector3d GridSurface::evaluate(double u, double v) const
{
  u = std::clamp(u, 0.0, 1.0);
  v = std::clamp(v, 0.0, 1.0);
  // A profile declared closed has one more span than it has columns: the strip
  // from the last column back to the first is part of the sweep, and leaving it
  // out made the surface cover only three sides of a four sided ridge. Facets
  // there could then be claimed by position and never by projection, which is
  // exactly the half the boolean destroys.
  const int segs = vspans();
  const double vs = v * segs;
  int j0 = segs > 0 ? int(vs) : 0;
  if (j0 > segs - 1) j0 = std::max(0, segs - 1);
  const double vf = segs > 0 ? vs - j0 : 0.0;

  auto column = [&](int j) -> Vector3d {
    if (poles.empty()) {
      const double us = u * (rows - 1);
      int i0 = int(us);
      if (i0 > rows - 2) i0 = std::max(0, rows - 2);
      const double uf = rows > 1 ? us - i0 : 0.0;
      return at(i0, j) * (1 - uf) + at(std::min(i0 + 1, rows - 1), j) * uf;
    }
    const int span = findSpan(uknots, rows, u);
    double basis[GRID_DEGREE + 1];
    basisFuns(uknots, span, u, basis);
    Vector3d acc = Vector3d::Zero();
    for (int k = 0; k <= GRID_DEGREE; k++) {
      acc += poles[std::size_t(span - GRID_DEGREE + k) * cols + j] * basis[k];
    }
    return acc;
  };
  if (segs < 1) return column(0);
  return column(j0) * (1 - vf) + column((j0 + 1) % cols) * vf;
}

bool GridSurface::project(const Vector3d& pt, double& u, double& v) const
{
  if (rows < 2 || cols < 1) return false;
  // A coarse sample first. Newton on a swept surface has as many local minima
  // as the sweep has turns - a helix passes near itself once a pitch - so a
  // single start finds the wrong one. Sampling at the stations cannot be out by
  // more than a span.
  double best = std::numeric_limits<double>::infinity();
  double bu = 0, bv = 0;
  const int usamples = std::max(rows * 2, 8);
  const int vsamples = std::max(vspans() * 2, 2);
  for (int i = 0; i <= usamples; i++) {
    const double su = double(i) / usamples;
    for (int j = 0; j <= vsamples; j++) {
      const double sv = double(j) / vsamples;
      const double d = (evaluate(su, sv) - pt).squaredNorm();
      if (d < best) {
        best = d;
        bu = su;
        bv = sv;
      }
    }
  }
  // Then Gauss-Newton on the squared distance, by finite differences: the
  // surface is piecewise polynomial and this only has to converge locally.
  const double h = 1e-6;
  for (int iter = 0; iter < 24; iter++) {
    const Vector3d r = evaluate(bu, bv) - pt;
    const Vector3d du =
      (evaluate(std::min(1.0, bu + h), bv) - evaluate(std::max(0.0, bu - h), bv)) / (2 * h);
    const Vector3d dv =
      (evaluate(bu, std::min(1.0, bv + h)) - evaluate(bu, std::max(0.0, bv - h))) / (2 * h);
    Eigen::Matrix2d jtj;
    jtj << du.dot(du), du.dot(dv), du.dot(dv), dv.dot(dv);
    const Eigen::Vector2d rhs(-r.dot(du), -r.dot(dv));
    if (fabs(jtj.determinant()) < 1e-20) break;
    const Eigen::Vector2d step = jtj.inverse() * rhs;
    const double nu = std::clamp(bu + step[0], 0.0, 1.0);
    const double nv = std::clamp(bv + step[1], 0.0, 1.0);
    const bool settled = fabs(nu - bu) < 1e-12 && fabs(nv - bv) < 1e-12;
    bu = nu;
    bv = nv;
    if (settled) break;
  }
  u = bu;
  v = bv;
  return true;
}

bool GridSurface::onSurface(const Vector3d& pt, double tol) const
{
  double u = 0, v = 0;
  if (!project(pt, u, v)) return false;
  return (evaluate(u, v) - pt).norm() <= tol;
}

bool GridSurface::isDeclaredPoint(const Vector3d& pt) const
{
  const auto key = gridKey(pt);
  for (int64_t dx = -1; dx <= 1; dx++) {
    for (int64_t dy = -1; dy <= 1; dy++) {
      for (int64_t dz = -1; dz <= 1; dz++) {
        if (lookup.count({std::get<0>(key) + dx, std::get<1>(key) + dy, std::get<2>(key) + dz})) {
          return true;
        }
      }
    }
  }
  return false;
}

GridSurface::GridSurface(int rows, int cols, std::vector<Vector3d> net, bool closed_v)
  : rows(rows), cols(cols), closed_v(closed_v), net(std::move(net))
{
  // refpt is the grid's own first point and normdir a direction along the
  // sweep, so that the base class members mean something for a caller which
  // only knows it has a Surface.
  if (!this->net.empty()) {
    refpt = this->net.front();
    if (rows > 1) {
      const Vector3d along = this->net[cols] - this->net[0];
      normdir = along.norm() > 0 ? along.normalized() : Vector3d(0, 0, 1);
    } else {
      normdir = Vector3d(0, 0, 1);
    }
  }
  reindex();
  buildSpline();
}

void GridSurface::reindex()
{
  lookup.clear();
  for (std::size_t i = 0; i < net.size(); i++) lookup.emplace(gridKey(net[i]), int(i));
}

void GridSurface::display(const std::vector<Vector3d>& vertices)
{
  printf("GridSurface %dx%d%s\n", rows, cols, closed_v ? " (closed)" : "");
}

int GridSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  // On the *surface*, which is what every other Surface means by this and what
  // the exporter needs. A declared point is on it by construction and is
  // checked first, because that is a lookup rather than a projection - but a
  // point a boolean created is on it too, and refusing those was the whole
  // shortfall of declaring the grid alone.
  if (isDeclaredPoint(pt)) return 1;
  return onSurface(pt, std::max(band, 1e-7)) ? 1 : 0;
}

std::shared_ptr<Surface> GridSurface::clone() const
{
  return std::make_shared<GridSurface>(*this);
}

bool GridSurface::transform(const Transform3d& mat)
{
  // Unlike a cylinder this can never fail: the record is a list of points, and
  // points go anywhere an affine map sends them. A sheared thread is still a
  // swept grid, just not the one it started as.
  for (auto& p : net) p = mat * p;
  refpt = mat * refpt;
  const Vector3d dir = mat.linear() * normdir;
  if (dir.norm() > 0) normdir = dir.normalized();
  reindex();
  buildSpline();
  return true;
}

bool GridSurface::sameAs(const Surface& other) const
{
  const auto *o = dynamic_cast<const GridSurface *>(&other);
  if (o == nullptr) return false;
  if (o->rows != rows || o->cols != cols || o->closed_v != closed_v) return false;
  for (std::size_t i = 0; i < net.size(); i++) {
    if ((net[i] - o->net[i]).norm() > 1e-9) return false;
  }
  return true;
}
