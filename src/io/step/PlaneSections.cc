/*! Where a plane meets a quadric about an axis - see PlaneSections.h. */
#include "io/step/PlaneSections.h"

#include <algorithm>
#include <cmath>

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
  const Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> es(A);
  if (es.info() != Eigen::Success) return false;
  const Eigen::Vector2d lam = es.eigenvalues();
  const Eigen::Matrix2d vec = es.eigenvectors();

  auto in_space = [&](const Eigen::Vector2d& uv) { return on_plane + e1 * uv(0) + e2 * uv(1); };

  if (fabs(det) <= 1e-9 * scale * scale) {
    // Parabolic: one eigenvalue is zero, and the curve has no centre. With the
    // axes turned onto the eigenvectors, lam_k w^2 + 2(b1 s + b2 w) + C = 0 for
    // the non-zero lam_k, which is s = -(lam_k w^2 + 2 b2 w + C) / (2 b1) - a
    // parabola of focal distance |lam_k| / (4 |b1|) about the s direction.
    const int k = fabs(lam(0)) >= fabs(lam(1)) ? 0 : 1;  // the curved direction
    const int z = 1 - k;
    const Eigen::Vector2d ek = vec.col(k), ez = vec.col(z);
    const double bz = B.dot(ez), bk = B.dot(ek);
    if (fabs(bz) < 1e-12 * scale) return false;  // degenerate: a line pair, not a parabola
    const double focal = fabs(lam(k)) / (4 * fabs(bz));
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

double sectionTiltFloor(const Surface *surface)
{
  // A cylinder's rulings are parallel, so every plane but one along them closes;
  // a cone's meet, and past the half angle the section runs away to infinity.
  const auto *axial = dynamic_cast<const AxialSurface *>(surface);
  if (axial != nullptr && axial->lean() > 0) return axial->lean();
  return 1e-6;
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
  // Scale the flatness test to the cycle, so it means the same thing on a coupon
  // measured in millimetres and on one measured in metres. From the bounding
  // box, not from a distance to some chosen vertex: two faces meeting along one
  // stretch see the cycle rotated and reversed, and they have to agree about the
  // stretch or the shell opens - so nothing here may depend on where the cycle
  // starts.
  Vector3d lo = at(0), hi = at(0);
  for (std::size_t k = 1; k < n; k++) {
    lo = lo.cwiseMin(at(k));
    hi = hi.cwiseMax(at(k));
  }
  const double tol = std::max(1e-12, 1e-9 * (hi - lo).norm());

  // The longest run of vertices starting at s that shares one plane, and that
  // plane. Collinear leading vertices do not fix a plane, so the seed keeps
  // reaching forward until it finds one that does.
  auto grow = [&](std::size_t s, Vector3d& normal) -> std::size_t {
    const Vector3d& p0 = at(s);
    std::size_t k = 2;
    Vector3d nn = Vector3d::Zero();
    for (; k < n; k++) {
      nn = (at(s + 1) - p0).cross(at(s + k) - p0);
      if (nn.norm() > tol) break;
    }
    if (k >= n) return 0;
    nn.normalize();
    std::size_t count = k + 1;
    while (count < n && fabs((at(s + count) - p0).dot(nn)) <= tol) count++;
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
    const double cos_tilt = fabs(nor[best_s].dot(axis));
    if (cos_tilt < min_cos_tilt || cos_tilt > 1 - 1e-9) continue;
    PlaneStretch st;
    st.start = best_s;
    st.count = best_len;
    st.normal = nor[best_s];
    out.push_back(st);
  }
  return out;
}

const Surface *planeIsFacetOfCurved(const std::vector<std::shared_ptr<Surface>>& surfaces,
                                    const Vector3d& pnorm, const std::vector<Vector3d>& on_plane)
{
  const Vector3d n = pnorm.normalized();
  if (!(n.norm() > 0)) return nullptr;
  for (const auto& d : surfaces) {
    const Surface *sf = d.get();
    if (sf == nullptr || dynamic_cast<const PlaneSurface *>(sf) != nullptr) continue;
    for (const Vector3d& p : on_plane) {
      const double scale = std::max(1.0, p.norm());
      const double owed = 1e-9 * scale;
      Vector3d q0;
      if (!AnalyticFeatures::closestOnSurface(sf, p, q0)) continue;
      const double off0 = (q0 - p).norm();
      if (off0 > owed) continue;  // not on this surface: it classifies nothing
      // Step along the plane's normal. A facet's normal is the surface's, so
      // the distance grows at the full rate; a cap's does not grow at all.
      const double eps = 1e-4 * scale;
      Vector3d q1;
      if (!AnalyticFeatures::closestOnSurface(sf, p + n * eps, q1)) continue;
      const double along = fabs(((p + n * eps) - q1).norm() - off0) / eps;
      if (along >= cos(M_PI / 6)) return sf;
    }
  }
  return nullptr;
}

}  // namespace step
