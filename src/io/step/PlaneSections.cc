/*! Where a plane meets a quadric about an axis - see PlaneSections.h. */
#include "io/step/PlaneSections.h"
#include "geometry/recognition/Exact.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>

#include <Eigen/Eigenvalues>

#include "geometry/AnalyticFeatures.h"

namespace step {

bool planeSectionEllipse(const Surface *surface, const Vector3d& normal, const Vector3d& on_plane,
                         SectionEllipse& out)
{
  if (const auto *cyl = dynamic_cast<const CylinderSurface *>(surface)) {
    const Vector3d axis = cyl->normdir.normalized();
    const double cos_tilt = normal.dot(axis);
    if (fabs(cos_tilt) < 1e-9) return false;
    out.centre = cyl->refpt + axis * (normal.dot(on_plane - cyl->refpt) / cos_tilt);
    const Vector3d in_plane = axis - normal * cos_tilt;
    if (in_plane.norm() < 1e-12) return false;
    out.major = in_plane.normalized();
    out.a = cyl->r / fabs(cos_tilt);
    out.b = cyl->r;
    return true;
  }
  const auto *cone = dynamic_cast<const ConeSurface *>(surface);
  if (cone == nullptr) {
    // Any other quadric about an axis: the closed section is the one its form
    // gives (planeSectionConic), where that is an ellipse.
    if (dynamic_cast<const AxialSurface *>(surface) == nullptr) return false;
    SectionConic conic;
    if (!planeSectionConic(surface, normal, on_plane, conic) || conic.kind != SectionConic::ELLIPSE) {
      return false;
    }
    out.centre = conic.centre;
    out.major = conic.major;
    out.a = conic.a;
    out.b = conic.b;
    return true;
  }
  if (fabs(cone->slope) < 1e-12) return false;
  const Vector3d axis = cone->normdir.normalized();
  const Vector3d apex = cone->refpt + axis * (-cone->r / cone->slope);
  const double cos_a = 1.0 / sqrt(1.0 + cone->slope * cone->slope);
  const double sin_a = fabs(cone->slope) * cos_a;
  if (fabs(normal.dot(axis)) <= sin_a) return false;  // a parabola or a hyperbola
  Vector3d across = normal - axis * normal.dot(axis);
  if (across.norm() < 1e-12) return false;
  across.normalize();
  // Out from the apex is whichever way the radius grows.
  const Vector3d out_dir = cone->slope > 0 ? axis : -axis;
  Vector3d ends[2];
  for (int side = 0; side < 2; side++) {
    const Vector3d gen = out_dir * cos_a + across * (side == 0 ? sin_a : -sin_a);
    const double denom = normal.dot(gen);
    if (fabs(denom) < 1e-12) return false;
    const double along = normal.dot(on_plane - apex) / denom;
    if (along <= 0) return false;  // the other nappe, so not one closed section
    ends[side] = apex + gen * along;
  }
  out.centre = (ends[0] + ends[1]) * 0.5;
  const Vector3d half = (ends[0] - ends[1]) * 0.5;
  out.a = half.norm();
  if (out.a < 1e-12) return false;
  out.major = half / out.a;
  const Vector3d rel = out.centre - apex;
  const double up = rel.dot(axis);
  const double b2 = up * up / (cos_a * cos_a) - rel.squaredNorm();
  if (b2 <= 0) return false;
  out.b = sqrt(b2);
  return out.a >= out.b;
}

bool planeSectionConic(const Surface *surface, const Vector3d& normal_in, const Vector3d& on_plane,
                       SectionConic& out)
{
  const Vector3d n = normal_in.normalized();
  if (!(n.norm() > 0)) return false;

  // The surface states itself as a quadric form (Surface::quadricForm) - a
  // cylinder, a cone, an elliptic cylinder - and the section follows from that
  // alone.
  Eigen::Matrix3d M;
  Vector3d origin;      // the quadric's own reference point
  double constant = 0;  // the quadric's form equals this
  if (surface == nullptr || dynamic_cast<const SphereSurface *>(surface) != nullptr ||
      !surface->quadricForm(M, origin, constant)) {
    return false;
  }

  Vector3d e1 = AnalyticFeatures::perpendicular(n);
  if (e1.norm() < 1e-12) return false;
  e1.normalize();
  const Vector3d e2 = n.cross(e1);
  const Vector3d d = on_plane - origin;

  // q(u, v) = [u v] A [u v]' + 2 [u v] . B + C
  Eigen::Matrix2d A;
  A(0, 0) = e1.dot(M * e1);
  A(0, 1) = e1.dot(M * e2);
  A(1, 0) = A(0, 1);
  A(1, 1) = e2.dot(M * e2);
  Eigen::Vector2d B(e1.dot(M * d), e2.dot(M * d));
  const double C = d.dot(M * d) - constant;

  const double scale = std::max(fabs(A(0, 0)) + fabs(A(1, 1)), 1e-30);
  const double det = A(0, 0) * A(1, 1) - A(0, 1) * A(0, 1);
  // What the arithmetic can say of det and of the linear part: each entry of A
  // and B is a dot of unit vectors through M, some ten rounded operations on
  // terms bounded by M's entries (and by d for B), and det two products of
  // those less one another. A det within its own forward error of zero is a
  // parabola as far as the machine can tell; one outside it has a sign, and is
  // the ellipse or the hyperbola it says. Until 2026-10-05 the line was a chosen
  // 1e-9 of the scale squared, which called sections a hyperbola far from
  // parabolic a parabola - and wrote it so (open item 39, R30).
  const double m_abs = M.cwiseAbs().sum();
  const double entry_err = recognition::forwardError(10, m_abs);
  const double det_err =
    entry_err * (fabs(A(0, 0)) + fabs(A(1, 1)) + 2 * fabs(A(0, 1)) + 2 * entry_err) +
    recognition::forwardError(3, fabs(A(0, 0) * A(1, 1)) + A(0, 1) * A(0, 1));
  const Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> es(A);
  if (es.info() != Eigen::Success) return false;
  const Eigen::Vector2d lam = es.eigenvalues();
  const Eigen::Matrix2d vec = es.eigenvectors();

  auto in_space = [&](const Eigen::Vector2d& uv) { return on_plane + e1 * uv(0) + e2 * uv(1); };

  if (fabs(det) <= det_err) {
    // Parabolic: one eigenvalue is zero, and the curve has no centre. With the
    // axes turned onto the eigenvectors, lam_k w^2 + 2(b1 s + b2 w) + C = 0 for
    // the non-zero lam_k, which is s - s0 = -(lam_k / (2 b1)) (w - w0)^2 - a
    // parabola s = w^2 / (4 f) of focal distance f = |b1| / (2 |lam_k|) about
    // the s direction. Written |lam_k| / (4 |b1|) until 2026-10-05, a quarter
    // of what it is on the cone a roof cuts at its own half angle: every
    // parabola the file held missed its own ends (open item 39, R30; W5).
    const int k = fabs(lam(0)) >= fabs(lam(1)) ? 0 : 1;  // the curved direction
    const int z = 1 - k;
    const Eigen::Vector2d ek = vec.col(k), ez = vec.col(z);
    const double bz = B.dot(ez), bk = B.dot(ek);
    // A linear part within its forward error of zero along the open direction
    // is a line pair, not a parabola.
    if (fabs(bz) <= recognition::forwardError(10, m_abs * (d.norm() + 1))) return false;
    const double focal = fabs(bz) / (2 * fabs(lam(k)));
    if (!(focal > 0) || !std::isfinite(focal)) return false;
    // The vertex: where the curve turns, at w = -bk / lam_k.
    const double w0 = -bk / lam(k);
    const double s0 = -(lam(k) * w0 * w0 + 2 * bk * w0 + C) / (2 * bz);
    const Eigen::Vector2d vertex = ek * w0 + ez * s0;
    // The axis of symmetry points the way the curve opens, which is the sign
    // that makes s grow.
    const double sense = (lam(k) / bz) < 0 ? 1.0 : -1.0;
    out.kind = SectionConic::PARABOLA;
    out.centre = in_space(vertex);
    out.major = (e1 * ez(0) + e2 * ez(1)) * sense;
    out.normal = n;
    out.a = focal;
    out.b = 0;
    return out.major.norm() > 0.5;
  }

  // Centred: A x = -B.
  const Eigen::Vector2d c = A.ldlt().solve(-B);
  if (!c.allFinite()) return false;
  const double cc = C + B.dot(c);                               // the constant at the centre
  if (fabs(cc) < 1e-15 * std::max(1.0, fabs(C))) return false;  // the degenerate point/line pair

  // lam1 x^2 + lam2 y^2 = -cc, so a semi-axis exists where -cc / lam has the
  // right sign; an ellipse needs both, a hyperbola takes the one that does.
  const double r0 = -cc / lam(0), r1 = -cc / lam(1);
  if (det > 0) {
    if (r0 <= 0 || r1 <= 0) return false;  // an imaginary ellipse: no real points
    const double s0 = sqrt(r0), s1 = sqrt(r1);
    const int big = s0 >= s1 ? 0 : 1;
    out.kind = SectionConic::ELLIPSE;
    out.a = std::max(s0, s1);
    out.b = std::min(s0, s1);
    out.major = (e1 * vec(0, big) + e2 * vec(1, big)).normalized();
  } else {
    const int real = r0 > 0 ? 0 : 1;
    const int imag = 1 - real;
    if (-cc / lam(real) <= 0 || -cc / lam(imag) >= 0) return false;
    out.kind = SectionConic::HYPERBOLA;
    out.a = sqrt(-cc / lam(real));
    out.b = sqrt(cc / lam(imag));
    out.major = (e1 * vec(0, real) + e2 * vec(1, real)).normalized();
  }
  out.centre = in_space(c);
  out.normal = n;
  return out.a > 0 && std::isfinite(out.a) && std::isfinite(out.b);
}

namespace {

/*! A quadric as the symmetric 4 x 4 Q with X' Q X = 0 on it, X = (x, 1), in
 * the form its own residual is written in, about its own reference point:
 * y' M y + l . y + c with y = x - refpt. A cylinder and a cone are
 * |perp|^2 - W^2, W = r + slope (normdir . y) - the cone by its radius and
 * slope, as it is stored, and not about its apex: quadricForm's apex form
 * cancels to nothing as a cone nears a cylinder, and on lid10 a cone whose
 * apex is 7875 away put its pencil's plane 1.0e-9 off the crossing's own
 * points. A sphere is |y|^2 - r^2; an elliptic cylinder, which states its
 * form about a point of its axis, is that form. */
bool homogeneous(const Surface *s, Eigen::Matrix4d& Q)
{
  Matrix3d M;
  Vector3d l = Vector3d::Zero(), o;
  double c = 0;
  if (const auto *sphere = dynamic_cast<const SphereSurface *>(s)) {
    M = Matrix3d::Identity();
    o = sphere->refpt;
    c = -sphere->r * sphere->r;
  } else if (const auto *cyl = dynamic_cast<const CylinderSurface *>(s)) {
    const Vector3d a = cyl->normdir.normalized();
    M = Matrix3d::Identity() - a * a.transpose();
    o = cyl->refpt;
    c = -cyl->r * cyl->r;
  } else if (const auto *cone = dynamic_cast<const ConeSurface *>(s)) {
    const Vector3d a = cone->normdir.normalized();
    const Vector3d& d = cone->normdir;
    M = Matrix3d::Identity() - a * a.transpose() - cone->slope * cone->slope * d * d.transpose();
    l = -2 * cone->r * cone->slope * d;
    o = cone->refpt;
    c = -cone->r * cone->r;
  } else if (s != nullptr && s->quadricForm(M, o, c)) {
    c = -c;
  } else {
    return false;
  }
  // y = x - o: y' M y + l . y + c = x' M x + (l - 2 M o) . x + (o' M o - l . o + c).
  const Vector3d Mo = M * o;
  const Vector3d lin = l - 2 * Mo;
  Q.setZero();
  Q.topLeftCorner<3, 3>() = M;
  Q.topRightCorner<3, 1>() = lin / 2;
  Q.bottomLeftCorner<1, 3>() = lin.transpose() / 2;
  Q(3, 3) = o.dot(Mo) - l.dot(o) + c;
  return true;
}

}  // namespace

bool coaxialRevolution(const Surface *a, const Surface *b)
{
  // The axis line of a surface of revolution whose sections across it are
  // circles; a sphere has every line through its centre, so it states a point.
  auto axis_of = [](const Surface *s, Vector3d& origin, Vector3d& dir) {
    const auto *axial = dynamic_cast<const AxialSurface *>(s);
    if (axial == nullptr || !axial->circularSections()) return false;
    origin = axial->refpt;
    dir = axial->axis();
    return true;
  };
  const auto *sa = dynamic_cast<const SphereSurface *>(a);
  const auto *sb = dynamic_cast<const SphereSurface *>(b);
  Vector3d oa, da, ob, db;
  if (sa != nullptr && sb != nullptr) return recognition::samePoint(sa->refpt, sb->refpt);
  if (sa != nullptr) return axis_of(b, ob, db) && recognition::onLine(ob, db, sa->refpt);
  if (sb != nullptr) return axis_of(a, oa, da) && recognition::onLine(oa, da, sb->refpt);
  return axis_of(a, oa, da) && axis_of(b, ob, db) && recognition::sameLine(oa, da, ob, db);
}

std::vector<std::pair<Vector3d, Vector3d>> pencilPlanes(const Surface *a, const Surface *b)
{
  std::vector<std::pair<Vector3d, Vector3d>> out;
  Eigen::Matrix4d A, B;
  if (!homogeneous(a, A) || !homogeneous(b, B)) return out;
  // Each to unit size, so that the bounds below are relative to the matrices
  // and the eigenvalues of the pencil are of order one.
  A /= A.norm();
  B /= B.norm();
  // Where the pencil is singular: the generalized eigenvalues of (A, B). Only a
  // real one can be a pair of real planes; it is real where its imaginary part
  // is within what the QZ step can say of it - backward stable, as below, at
  // n^3 = 64 roundings of the unit matrices.
  const Eigen::GeneralizedEigenSolver<Eigen::Matrix4d> qz(A, B, false);
  if (qz.info() != Eigen::Success) return out;
  const auto alphas = qz.alphas();
  const auto betas = qz.betas();
  for (int k = 0; k < 4; k++) {
    const double beta = betas(k);
    if (beta == 0) continue;  // the member B itself: a cylinder, a cone - rank 3 or 4, never two planes
    const std::complex<double> lam = alphas(k) / beta;
    if (fabs(lam.imag()) > recognition::forwardError(64, std::abs(lam) + 1)) continue;
    const double l = lam.real();
    // The member at it, and its eigenvalues: two at zero is a pair of planes.
    // Zero to the eigensolvers' backward error: each is backward stable at a
    // multiple of the unit roundoff that grows as n^3 for an n x n matrix, so
    // n^3 = 64 roundings of the member's size, which the normalisation makes
    // 1 + |lambda|.
    const Eigen::Matrix4d S = A - l * B;
    const Eigen::SelfAdjointEigenSolver<Eigen::Matrix4d> es(S);
    if (es.info() != Eigen::Success) continue;
    const double zero = recognition::forwardError(64, 1 + fabs(l));
    std::vector<int> big;
    for (int j = 0; j < 4; j++) {
      if (fabs(es.eigenvalues()(j)) > zero) big.push_back(j);
    }
    if (big.size() != 2) continue;
    const double e0 = es.eigenvalues()(big[0]), e1 = es.eigenvalues()(big[1]);
    // Of one sign the two planes are imaginary and meet in a line: no conic.
    if (!(e0 * e1 < 0)) continue;
    const Eigen::Vector4d u = es.eigenvectors().col(big[0]) * sqrt(fabs(e0));
    const Eigen::Vector4d v = es.eigenvectors().col(big[1]) * sqrt(fabs(e1));
    // S = u u' - v v' (signs aside) = (u + v)(u - v)' symmetrised: the planes
    // (u + v) . X = 0 and (u - v) . X = 0.
    for (const Eigen::Vector4d& h : {Eigen::Vector4d(u + v), Eigen::Vector4d(u - v)}) {
      const Vector3d n = h.head<3>();
      const double len = n.norm();
      if (!(len > 0)) continue;  // the plane at infinity
      const Vector3d normal = n / len;
      const Vector3d on_plane = -h(3) / len * normal;
      bool have = false;
      for (const auto& seen : out) {
        have = have || recognition::samePlane(seen.first, seen.second, on_plane, normal);
      }
      if (!have) out.emplace_back(on_plane, normal);
    }
  }
  return out;
}

double sectionTiltFloor(const Surface *surface)
{
  // A cylinder's rulings are parallel, so every plane but one along them closes;
  // a cone's meet, and past the half angle the section runs away to infinity.
  // The surface's lean, then - zero for a cylinder, whose one plane that does
  // not close is the one along its axis, which coplanarStretches asks at machine
  // precision. It was 1e-6 for a surface leaning none, refusing every plane
  // within that of along the axis, a chosen number (open item 39, R2).
  const auto *axial = dynamic_cast<const AxialSurface *>(surface);
  return axial != nullptr ? axial->lean() : 0.0;
}

bool surfaceAxis(const Surface *surface, Vector3d& axis)
{
  const auto *axial = dynamic_cast<const AxialSurface *>(surface);
  if (axial == nullptr) return false;
  axis = axial->axis();
  return true;
}

std::vector<PlaneStretch> coplanarStretches(const std::vector<int>& cycle,
                                            const std::vector<Vector3d>& vertices, const Vector3d& axis,
                                            double min_cos_tilt)
{
  std::vector<PlaneStretch> out;
  const std::size_t n = cycle.size();
  if (n < 4) return out;
  auto at = [&](std::size_t k) -> const Vector3d& { return vertices[cycle[k % n]]; };

  // The longest run of vertices starting at s that shares one plane, and that
  // plane. Collinear leading vertices do not fix a plane, so the seed keeps
  // reaching forward until it finds one that does. Both questions at machine
  // precision (recognition/Exact.h) - whether three vertices state a plane, and
  // whether the next is on it - where both were asked against 1e-9 of the
  // cycle's box, a chosen number that grouped a vertex 1e-10 off a plane at
  // 100 mm into it (open item 39, R2). The answer still depends on nothing but
  // the vertices, so two faces seeing one stretch from either side agree.
  auto grow = [&](std::size_t s, Vector3d& normal) -> std::size_t {
    const Vector3d& p0 = at(s);
    std::size_t k = 2;
    for (; k < n; k++) {
      if (recognition::statesPlane(p0, at(s + 1), at(s + k))) break;
    }
    if (k >= n) return 0;
    const Vector3d nn = (at(s + 1) - p0).cross(at(s + k) - p0).normalized();
    std::size_t count = k + 1;
    while (count < n && recognition::onPlane(p0, nn, at(s + count))) count++;
    normal = nn;
    return count;
  };

  std::vector<std::size_t> len(n, 0);
  std::vector<Vector3d> nor(n, Vector3d::Zero());
  for (std::size_t s = 0; s < n; s++) len[s] = grow(s, nor[s]);

  std::vector<char> spent(n, 0);  // spent[i]: the edge from vertex i to i + 1
  for (;;) {
    std::size_t best_s = n, best_len = 0;
    for (std::size_t s = 0; s < n; s++) {
      if (len[s] < 3 || spent[s]) continue;
      std::size_t l = 1;
      while (l < len[s] && !spent[(s + l - 1) % n]) l++;
      if (l >= 3 && l > best_len) {
        best_len = l;
        best_s = s;
      }
    }
    if (best_s == n) break;
    for (std::size_t k = 0; k + 1 < best_len; k++) spent[(best_s + k) % n] = 1;
    // Perpendicular to the axis is a circle, which the arc pass already writes.
    // The floor below it belongs to the surface: on a cylinder any other tilt
    // gives an ellipse, but on a cone the section is one only while the plane
    // still crosses every generator - at the half angle it is a parabola and
    // past it a hyperbola, and neither of those closes. Either way the edges are
    // still spent, so the search does not offer the same stretch back.
    // Across the axis is the circle, along it no closed curve at all, each at
    // machine precision; it was within 1e-9 of across (open item 39, R2).
    const double cos_tilt = fabs(nor[best_s].dot(axis));
    if (recognition::parallel(nor[best_s], axis) || recognition::perpendicular(nor[best_s], axis) ||
        cos_tilt < min_cos_tilt) {
      continue;
    }
    PlaneStretch st;
    st.start = best_s;
    st.count = best_len;
    st.normal = nor[best_s];
    out.push_back(st);
  }
  return out;
}

}  // namespace step
