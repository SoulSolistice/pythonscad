#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <Eigen/Dense>
#include <Eigen/LU>
#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#include <limits>
#include <memory>
#include <typeinfo>
#include <vector>
#include "geometry/linalg.h"
#include "geometry/Surface.h"
#include "geometry/recognition/Exact.h"

#include <Eigen/SVD>

// Tolerances in this file, and how they relate to the ones next door.
//
// AnalyticFeatures.cc scales its tolerances to the geometry - `1e-9 *
// max(r_bottom, r_top)` for a radius, `1e-6 * max(1.0, (hi - lo).norm())` for a
// span, `1e-7 * radius` for a curve. The two here do not:
//
//   sameAs      1e-9 absolute, comparing two surface *records* - a radius, an
//               axis, a reference point. Records come from declarations rather
//               than from measurement, so two that mean the same surface agree
//               to the last place or were never the same surface. Absolute is
//               right here.
//
//   pointMember 1e-5 absolute, asking whether a point lies on a surface. This
//               one is scale dependent and it is the real gap. On a part a few
//               hundred units across it is tight but workable; on a model in
//               microns it would accept almost anything, and on one in
//               kilometres it would accept almost nothing and quietly drop
//               every surface to facets.
//
// It has not been made relative, and deliberately so: the obvious form,
// `1e-5 * max(1.0, r)`, is 78 times looser on the bayonet lid than what every
// measurement in doc/step-export-development.md was taken against, and no
// fixture in the suite is far enough from unit scale to show whether that is an
// improvement or a new source of wrong surfaces. It wants a large-model fixture
// and a measurement first, not a change made on the strength of the argument
// above.

void Surface::reverse(void)
{
}

std::shared_ptr<Surface> Surface::clone() const
{
  return std::make_shared<Surface>(*this);
}

bool Surface::transform(const Transform3d& mat, const Similarity& similarity)
{
  // Only a map its maker states to be a similarity - a rotation and a uniform
  // scale, possibly mirrored - keeps a circular section circular; any other
  // turns it into an ellipse, which none of the kinds moved through here can
  // describe. Stated, never measured: see Similarity.
  if (!similarity.stated) return false;
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

namespace {

/*! A unit vector across `axis`: its cross product with the coordinate axis it
 * leans on least, so the product never vanishes. */
Vector3d across(const Vector3d& axis)
{
  const Vector3d a = axis.normalized();
  int k = 0;
  for (int i = 1; i < 3; i++) {
    if (std::fabs(a[i]) < std::fabs(a[k])) k = i;
  }
  Vector3d e(0, 0, 0);
  e[k] = 1;
  return a.cross(e).normalized();
}

/*! Every one of `points` on `on`, at D0. */
bool allOn(const Surface& on, const std::vector<Vector3d>& points)
{
  for (const Vector3d& p : points) {
    if (!on.residual(p).exact()) return false;
  }
  return true;
}

}  // namespace

bool Surface::sameAs(const Surface& other) const
{
  // A kind with no rule of its own: the same record, exactly. Every kind
  // declared here has its own.
  return typeid(*this) == typeid(other) && refpt == other.refpt && normdir == other.normdir;
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

/*! Two generators declaring one wall - `declare_cylinder()` where the model
 * says, an extrusion at the base of its sweep - make one record, or every count
 * a fixture asserts moves for a surface nothing gained. */
bool addSurfaceUnique(std::vector<std::shared_ptr<Surface>>& list,
                      const std::shared_ptr<Surface>& surface)
{
  if (surface == nullptr) return false;
  for (auto& s : list) {
    // Not added, but its declarers are: a face offered after the same surface
    // was offered as evidence makes the kept record a face.
    if (s != nullptr && s->sameAs(*surface)) {
      s = withDeclarersOf(s, *surface);
      return false;
    }
  }
  list.push_back(surface);
  return true;
}

std::vector<std::string> bindLabels(std::vector<std::shared_ptr<Surface>>& list,
                                    const std::vector<std::string>& labels,
                                    std::shared_ptr<Surface> record)
{
  std::vector<std::string> bound;
  if (record == nullptr) return bound;
  auto named = record->clone();
  std::vector<std::shared_ptr<Surface>> kept;
  kept.reserve(list.size());
  for (const auto& s : list) {
    const bool wanted = s != nullptr && !s->label.empty() &&
                        std::find(labels.begin(), labels.end(), s->label) != labels.end();
    if (!wanted) {
      kept.push_back(s);
      continue;
    }
    for (const uint32_t t : s->tags) {
      if (std::find(named->tags.begin(), named->tags.end(), t) == named->tags.end())
        named->tags.push_back(t);
    }
    if (std::find(bound.begin(), bound.end(), s->label) == bound.end()) bound.push_back(s->label);
  }
  list = std::move(kept);
  std::sort(named->tags.begin(), named->tags.end());
  mergeSurfaces(list, {named});
  return bound;
}

void mergeSurfaces(std::vector<std::shared_ptr<Surface>>& into,
                   const std::vector<std::shared_ptr<Surface>>& from)
{
  for (const auto& surface : from) {
    bool found = false;
    for (auto& s : into) {
      // By the surface, not the record: one infinite cylinder declared from
      // two heights is one surface, and two records of it made ownership, the
      // ledger and the declaration census each count it twice - lid10's channel
      // and entry slot both declare r = 79.8094, and came out as two surfaces,
      // one of them "unexplained". Measured neutral on geometry when first
      // tried (open item 17); it matters now because identity is per record.
      if (s == surface || (s && surface && s->sameAs(*surface))) {
        // The same surface declared by two originals is declared by both.
        if (surface) s = withDeclarersOf(s, *surface);
        found = true;
        break;
      }
    }
    if (!found) into.push_back(surface);
  }
}

std::shared_ptr<Surface> withDeclarersOf(const std::shared_ptr<Surface>& kept, const Surface& twin)
{
  if (kept == nullptr) return kept;
  // A twin nobody has stamped says nothing about who declared a record that has
  // been stamped: its role flag belongs to the moment before ids existed. This
  // is also what keeps a *fit* offered to the approximation's list from
  // replacing the declared record it duplicates.
  const bool kept_stamped = !kept->owners.empty() || !kept->witnesses.empty();
  const bool twin_stamped = !twin.owners.empty() || !twin.witnesses.empty();
  if (kept_stamped && !twin_stamped) return kept;
  auto unite = [](std::vector<int32_t> a, const std::vector<int32_t>& b) {
    a.insert(a.end(), b.begin(), b.end());
    std::sort(a.begin(), a.end());
    a.erase(std::unique(a.begin(), a.end()), a.end());
    return a;
  };
  std::vector<int32_t> owners = unite(kept->owners, twin.owners);
  std::vector<int32_t> witnesses = unite(kept->witnesses, twin.witnesses);
  std::vector<uint32_t> tags = kept->tags;
  tags.insert(tags.end(), twin.tags.begin(), twin.tags.end());
  std::sort(tags.begin(), tags.end());
  tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
  const bool evidence_only = kept->evidence_only && twin.evidence_only;
  if (owners == kept->owners && witnesses == kept->witnesses && evidence_only == kept->evidence_only &&
      tags == kept->tags) {
    return kept;
  }
  auto copy = kept->clone();
  copy->owners = std::move(owners);
  copy->witnesses = std::move(witnesses);
  copy->tags = std::move(tags);
  copy->evidence_only = evidence_only;
  return copy;
}

std::vector<std::shared_ptr<Surface>> stampOwners(const std::vector<std::shared_ptr<Surface>>& list,
                                                  const std::set<uint32_t>& ids)
{
  if (ids.empty()) return list;
  std::vector<int32_t> owners;
  for (const uint32_t id : ids) owners.push_back(static_cast<int32_t>(id));
  std::vector<std::shared_ptr<Surface>> out;
  out.reserve(list.size());
  for (const auto& surface : list) {
    if (surface == nullptr) continue;
    auto copy = surface->clone();
    if (surface->declaresFace()) {
      copy->owners = owners;
      copy->witnesses.clear();
    } else {
      copy->owners.clear();
      copy->witnesses = owners;
    }
    out.push_back(copy);
  }
  return out;
}

namespace {
std::atomic<uint32_t> next_tag{1};
std::mutex derived_mutex;
std::set<uint32_t>& derivedTags()
{
  static std::set<uint32_t> tags;
  return tags;
}
}  // namespace

uint32_t mintTag()
{
  // One counter for the process: a tag is compared only with tags, within one
  // evaluation, and never written anywhere.
  return next_tag++;
}

uint32_t mintDerivedTag()
{
  const uint32_t tag = next_tag++;
  const std::lock_guard<std::mutex> lock(derived_mutex);
  derivedTags().insert(tag);
  return tag;
}

bool isDerivedTag(uint32_t tag)
{
  const std::lock_guard<std::mutex> lock(derived_mutex);
  return derivedTags().count(tag) != 0;
}

std::vector<std::shared_ptr<Surface>> stampTags(const std::vector<std::shared_ptr<Surface>>& list)
{
  std::vector<std::shared_ptr<Surface>> out;
  out.reserve(list.size());
  for (const auto& surface : list) {
    if (surface == nullptr) continue;
    if (!surface->declaresFace()) {
      out.push_back(surface);
      continue;
    }
    auto copy = surface->clone();
    copy->tags.push_back(mintTag());
    out.push_back(copy);
  }
  return out;
}

PlaneSurface::PlaneSurface(Vector3d refpt, Vector3d normdir)
{
  this->refpt = refpt;
  this->normdir = normdir.normalized();
}

std::shared_ptr<Surface> PlaneSurface::clone() const
{
  return std::make_shared<PlaneSurface>(*this);
}

bool PlaneSurface::sameAs(const Surface& other) const
{
  // The plane, not the point on it: two records anchored a metre apart on one
  // plane are one surface. Parallel, and each one's point on the other.
  if (typeid(*this) != typeid(other)) return false;
  const auto& o = static_cast<const PlaneSurface&>(other);
  return recognition::samePlane(refpt, normdir, o.refpt, o.normdir);
}

int PlaneSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  return fabs(normdir.dot(pt - refpt)) > 1e-5 ? 0 : 1;
}

SphereSurface::SphereSurface(Vector3d refpt, Vector3d normdir, double r)
{
  this->refpt = refpt;
  this->normdir = normdir;
  this->r = r;
}

std::shared_ptr<Surface> SphereSurface::clone() const
{
  return std::make_shared<SphereSurface>(*this);
}

bool SphereSurface::transform(const Transform3d& mat, const Similarity& similarity)
{
  if (!Surface::transform(mat, similarity)) return false;
  this->r *= similarity.scale;
  return true;
}

bool SphereSurface::sameAs(const Surface& other) const
{
  // Four points of each, not in one plane, on the other.
  if (typeid(*this) != typeid(other)) return false;
  const auto& o = static_cast<const SphereSurface&>(other);
  auto points = [](const SphereSurface& s) {
    return std::vector<Vector3d>{s.refpt + Vector3d(s.r, 0, 0), s.refpt - Vector3d(s.r, 0, 0),
                                 s.refpt + Vector3d(0, s.r, 0), s.refpt + Vector3d(0, 0, s.r)};
  };
  return allOn(*this, points(o)) && allOn(o, points(*this));
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

void TorusSurface::reverse(void)
{
  this->normdir = -this->normdir;
}

std::shared_ptr<Surface> TorusSurface::clone() const
{
  return std::make_shared<TorusSurface>(*this);
}

bool TorusSurface::transform(const Transform3d& mat, const Similarity& similarity)
{
  if (!Surface::transform(mat, similarity)) return false;
  this->r_major *= similarity.scale;
  this->r_minor *= similarity.scale;
  return true;
}

bool TorusSurface::sameAs(const Surface& other) const
{
  // One axis line, and each one's outer, inner and top circles through a point
  // of the other.
  if (typeid(*this) != typeid(other)) return false;
  const auto& o = static_cast<const TorusSurface&>(other);
  if (!recognition::sameLine(refpt, normdir, o.refpt, o.normdir)) return false;
  auto points = [](const TorusSurface& s) {
    const Vector3d e = across(s.normdir), a = s.normdir.normalized();
    return std::vector<Vector3d>{s.refpt + e * (s.r_major + s.r_minor),
                                 s.refpt + e * (s.r_major - s.r_minor),
                                 s.refpt + e * s.r_major + a * s.r_minor};
  };
  return allOn(*this, points(o)) && allOn(o, points(*this));
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

/*! A rational Bezier curve - `weights` empty for a polynomial one - at `t`:
 * its point, its derivative, and the forward error of the point.
 *
 * de Casteljau is n levels of p (1 - t) + q t, each three roundings and 1 - t
 * once more, and each level a convex combination: so the numerator is off by
 * at most gamma_{3n+1} of its largest control point, one more for the
 * weighting, and the denominator by gamma_{3n+1} of its largest weight. The
 * quotient carries both, in proportion, and one rounding of its own. */
struct CurveAt {
  Vector3d point, tangent;
  double error = 0;
};

CurveAt rationalCurveAt(const std::vector<Vector3d>& ctrl, const std::vector<double>& weights, double t)
{
  const bool rational = weights.size() == ctrl.size();
  std::vector<Vector3d> num;
  std::vector<double> den;
  double largest_num = 0, largest_den = 0;
  for (std::size_t i = 0; i < ctrl.size(); i++) {
    const double w = rational ? weights[i] : 1.0;
    num.push_back(ctrl[i] * w);
    den.push_back(w);
    largest_num = std::max(largest_num, num.back().norm());
    largest_den = std::max(largest_den, std::fabs(w));
  }
  const int levels = 3 * (int(ctrl.size()) - 1) + 1;
  const Vector3d n = deCasteljau(num, t), n_d = deCasteljauDeriv(num, t);
  const double w = deCasteljau1d(den, t), w_d = deCasteljauDeriv1d(den, t);
  CurveAt out;
  out.point = n / w;
  out.tangent = (n_d - out.point * w_d) / w;
  const double err_num = recognition::forwardError(levels + (rational ? 1 : 0), largest_num);
  const double err_den = rational ? recognition::forwardError(levels, largest_den) : 0.0;
  out.error = (err_num + out.point.norm() * err_den) / std::fabs(w) +
              recognition::forwardError(rational ? 1 : 0, out.point.norm());
  return out;
}

/*! A patch at (u, v): its point, both derivatives, and the forward error of the
 * point - the curve's bound, with the two directions' levels one after the
 * other: the rows in v first, then the column of their values in u. */
struct PatchAt {
  Vector3d point, du, dv;
  double error = 0;
};

PatchAt patchAt(const BezierPatchSurface& patch, double u, double v)
{
  std::vector<Vector3d> rows, rows_dv;
  std::vector<double> rows_w, rows_w_dv;
  double largest_num = 0, largest_den = 0;
  for (int i = 0; i <= patch.degree_u; i++) {
    std::vector<Vector3d> row;
    std::vector<double> row_w;
    for (int j = 0; j <= patch.degree_v; j++) {
      row.push_back(patch.control(i, j) * patch.weight(i, j));
      row_w.push_back(patch.weight(i, j));
      largest_num = std::max(largest_num, row.back().norm());
      largest_den = std::max(largest_den, std::fabs(row_w.back()));
    }
    rows.push_back(deCasteljau(row, v));
    rows_dv.push_back(deCasteljauDeriv(row, v));
    rows_w.push_back(deCasteljau1d(row_w, v));
    rows_w_dv.push_back(deCasteljauDeriv1d(row_w, v));
  }
  const Vector3d n = deCasteljau(rows, u), n_du = deCasteljauDeriv(rows, u),
                 n_dv = deCasteljau(rows_dv, u);
  const double w = deCasteljau1d(rows_w, u), w_du = deCasteljauDeriv1d(rows_w, u),
               w_dv = deCasteljau1d(rows_w_dv, u);
  PatchAt out;
  out.point = n / w;
  out.du = (n_du - out.point * w_du) / w;
  out.dv = (n_dv - out.point * w_dv) / w;
  const bool rational = patch.isRational();
  const int levels = 3 * (patch.degree_u + patch.degree_v) + 2;
  const double err_num = recognition::forwardError(levels + (rational ? 1 : 0), largest_num);
  const double err_den = rational ? recognition::forwardError(levels, largest_den) : 0.0;
  out.error = (err_num + out.point.norm() * err_den) / std::fabs(w) +
              recognition::forwardError(rational ? 1 : 0, out.point.norm());
  return out;
}

/*! The distance from `p` to a point `at` on a curve or patch whose own error is
 * `error` and whose parameters were resolved to their last bit, moving along
 * it at `speed` per unit of parameter: the residual and its bound - the
 * point's error, the distance's nine roundings, and the last bit of the
 * parameters times how far that moves the point. */
recognition::Residual residualAt(const Vector3d& p, const Vector3d& at, double error, double speed,
                                 double sign)
{
  recognition::Residual r;
  r.value = sign * (p - at).norm();
  r.bound =
    error + recognition::forwardError(9, p.norm() + at.norm()) + recognition::forwardError(2, speed);
  return r;
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
  boxPieces();
}

std::shared_ptr<Surface> BezierPatchSurface::clone() const
{
  return std::make_shared<BezierPatchSurface>(*this);
}

bool BezierPatchSurface::transform(const Transform3d& mat, const Similarity&)
{
  // A Bezier is affine invariant: transforming the control points transforms
  // the surface exactly. Unlike a cylinder, this survives a non uniform scale
  // and a shear as well, so there is no similarity test to pass.
  for (auto& p : net) p = mat * p;
  refpt = net.empty() ? Vector3d::Zero() : net.front();
  boxPieces();
  return true;
}

bool BezierPatchSurface::sameAs(const Surface& other) const
{
  // A patch is its net: the same degrees, the same control points, the same
  // weights - two patches on one net with different weights are different
  // surfaces, a parabola and a circular arc through the same three points.
  if (typeid(*this) != typeid(other)) return false;
  const auto& o = static_cast<const BezierPatchSurface&>(other);
  return degree_u == o.degree_u && degree_v == o.degree_v && net == o.net && weights == o.weights;
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
    if (p != edge.front()) return false;
  }
  return true;
}

namespace {

/*! Newton from (cu, cv) towards the point of `patch` nearest `pt`, each step
 * taken only where it brings the point closer, until none does - the last bit
 * of the parameters. Leaves the parameters in (cu, cv). */
void descend(const BezierPatchSurface& patch, const Vector3d& pt, double& cu, double& cv)
{
  const int degree_u = patch.degree_u, degree_v = patch.degree_v;
  auto control = [&](int i, int j) -> const Vector3d& { return patch.control(i, j); };
  auto weight = [&](int i, int j) { return patch.weight(i, j); };
  auto evaluate = [&](double u, double v) { return patch.evaluate(u, v); };
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
    // Every step has to improve the distance, for the reason spelled out in
    // GridSurface::project: this matrix goes near singular where the two
    // parameter directions run nearly parallel, and the step it then asks
    // for lands somewhere else on the patch, where the iteration settles
    // quite happily. The twenty-five starts around this loop make that cost
    // a start rather than the answer, which is why it has never been a
    // defect here - but a start spent is a start not searching, and the
    // guard is two lines.
    const double here = r.squaredNorm();
    bool stepped = false;
    double shrink = 1.0;
    for (int back = 0; back < 6; back++, shrink *= 0.5) {
      const double nu = std::min(1.0, std::max(0.0, cu + step[0] * shrink));
      const double nv = std::min(1.0, std::max(0.0, cv + step[1] * shrink));
      if ((evaluate(nu, nv) - pt).squaredNorm() >= here) continue;
      // an improvement is a change: this runs until no step improves
      cu = nu;
      cv = nv;
      stepped = true;
      break;
    }
    if (!stepped) break;
  }
}

}  // namespace

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
      descend(*this, pt, cu, cv);
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

namespace {

/*! A patch's net in homogeneous form - (w P, w) per control point - rows in u,
 * columns in v. */
using HomogeneousNet = std::vector<std::vector<Vector4d>>;

/*! One half of the net, by de Casteljau at 1/2 in u or in v: each level an
 * average, which rounds once - the sum - and halves exactly. */
HomogeneousNet halve(const HomogeneousNet& net, bool along_u, bool upper)
{
  HomogeneousNet out = net;
  const std::size_t rows = net.size(), cols = net.front().size();
  auto split = [&](std::vector<Vector4d> line) {
    const std::size_t n = line.size();
    std::vector<Vector4d> lower(n), higher(n);
    for (std::size_t level = 0; level < n; level++) {
      lower[level] = line[0];
      higher[n - 1 - level] = line[n - 1 - level];
      for (std::size_t i = 0; i + 1 < n - level; i++) line[i] = (line[i] + line[i + 1]) * 0.5;
    }
    return upper ? higher : lower;
  };
  if (along_u) {
    for (std::size_t j = 0; j < cols; j++) {
      std::vector<Vector4d> column(rows);
      for (std::size_t i = 0; i < rows; i++) column[i] = net[i][j];
      const std::vector<Vector4d> half = split(column);
      for (std::size_t i = 0; i < rows; i++) out[i][j] = half[i];
    }
  } else {
    for (std::size_t i = 0; i < rows; i++) out[i] = split(net[i]);
  }
  return out;
}

/*! The boxes of the pieces `levels` halvings in each direction make of the
 * patch `net` states - each the box of its control points, which the piece
 * lies within, every weight positive - widened by `pad`. */
void collectPieces(const HomogeneousNet& net, double pad, int levels, std::vector<BoundingBox>& out)
{
  if (levels == 0) {
    BoundingBox box;
    for (const auto& row : net) {
      for (const Vector4d& h : row) box.extend(Vector3d(h.head<3>() / h[3]));
    }
    box.min().array() -= pad;
    box.max().array() += pad;
    out.push_back(box);
    return;
  }
  for (const bool upper_u : {false, true}) {
    const HomogeneousNet half_u = halve(net, true, upper_u);
    for (const bool upper_v : {false, true})
      collectPieces(halve(half_u, false, upper_v), pad, levels - 1, out);
  }
}

}  // namespace

void BezierPatchSurface::boxPieces()
{
  hull_ = BoundingBox();
  pieces_.clear();
  BoundingBox box;
  double largest_wp = 0, largest_p = 0, largest_w = 0, least_w = 0;
  bool first = true;
  for (int i = 0; i <= degree_u; i++) {
    for (int j = 0; j <= degree_v; j++) {
      const Vector3d& p = control(i, j);
      const double w = weight(i, j);
      if (!(w > 0)) return;  // no convex hull to box
      box.extend(p);
      largest_wp = std::max(largest_wp, (p * w).norm());
      largest_p = std::max(largest_p, p.norm());
      largest_w = first ? w : std::max(largest_w, w);
      least_w = first ? w : std::min(least_w, w);
      first = false;
    }
  }
  if (first) return;
  const int levels = 3 * (degree_u + degree_v) + 2;
  const double pad = (recognition::forwardError(levels + 1, largest_wp) +
                      largest_p * recognition::forwardError(levels, largest_w)) /
                       least_w +
                     recognition::forwardError(1, largest_p);
  box.min().array() -= pad;
  box.max().array() += pad;
  hull_ = box;
  // and the pieces: one more rounding a halving level, per degree, over the
  // largest weighted control point, over the least weight
  const int halvings = 3;
  const double piece_pad =
    pad + recognition::forwardError(halvings * (degree_u + degree_v) + 1, largest_wp) / least_w;
  HomogeneousNet hnet(degree_u + 1, std::vector<Vector4d>(degree_v + 1));
  for (int i = 0; i <= degree_u; i++) {
    for (int j = 0; j <= degree_v; j++) hnet[i][j] << control(i, j) * weight(i, j), weight(i, j);
  }
  collectPieces(hnet, piece_pad, halvings, pieces_);
}

BoundingBox BezierPatchSurface::hull() const
{
  return hull_;
}

recognition::Residual BezierPatchSurface::signedResidual(const Vector3d& p) const
{
  // Off the patch by more than any rounding, proved without projecting: outside
  // the hull's box, or outside every piece's - the distance to them is a lower
  // bound on the distance to the patch, which is all "not on" needs.
  if (!hull_.isEmpty()) {
    double at_least =
      hull_.contains(p) ? std::numeric_limits<double>::infinity() : hull_.exteriorDistance(p);
    if (hull_.contains(p)) {
      for (const BoundingBox& piece : pieces_) {
        at_least = std::min(at_least, piece.contains(p) ? 0.0 : piece.exteriorDistance(p));
        if (at_least == 0) break;
      }
    }
    if (at_least > 0) {
      recognition::Residual off;
      off.value = at_least;
      off.bound = 0;
      return off;
    }
  }
  // From the nearest of project()'s starts first: a point on the patch
  // converges from it, and is then on it whatever the other starts would find.
  // Only a point not on it here needs every start for its distance.
  double u = 0, v = 0, nearest = std::numeric_limits<double>::infinity();
  for (int gi = 0; gi <= 4; gi++) {
    for (int gj = 0; gj <= 4; gj++) {
      const double d = (evaluate(gi / 4.0, gj / 4.0) - p).norm();
      if (d < nearest) {
        nearest = d;
        u = gi / 4.0;
        v = gj / 4.0;
      }
    }
  }
  descend(*this, p, u, v);
  {
    const PatchAt at = patchAt(*this, u, v);
    const double sign = (p - at.point).dot(at.du.cross(at.dv)) < 0 ? -1.0 : 1.0;
    const recognition::Residual first =
      residualAt(p, at.point, at.error, at.du.norm() + at.dv.norm(), sign);
    if (first.exact()) return first;
  }
  if (!project(p, u, v)) return {};
  const PatchAt at = patchAt(*this, u, v);
  const Vector3d n = at.du.cross(at.dv);
  const double sign = (p - at.point).dot(n) < 0 ? -1.0 : 1.0;
  return residualAt(p, at.point, at.error, at.du.norm() + at.dv.norm(), sign);
}

bool BezierPatchSurface::closestPoint(const Vector3d& p, Vector3d& out) const
{
  double u = 0, v = 0;
  if (!project(p, u, v)) return false;
  out = evaluate(u, v);
  return true;
}

bool BezierPatchSurface::normalAt(const Vector3d& p, Vector3d& out) const
{
  double u = 0, v = 0;
  if (!project(p, u, v)) return false;
  const PatchAt at = patchAt(*this, u, v);
  const Vector3d n = at.du.cross(at.dv);
  if (!(n.norm() > 0)) return false;  // a degenerate edge: no normal there
  out = n.normalized();
  return true;
}

recognition::Residual BezierPatchSurface::boundaryResidual(bool along_u, bool far,
                                                           const Vector3d& p) const
{
  const std::vector<Vector3d> ctrl = boundary(along_u, far);
  const std::vector<double> weights = boundaryWeights(along_u, far);
  // Sampled, then halved around the nearest sample until a step no longer
  // moves the parameter - its last bit, not a chosen floor.
  double best = -1, bt = 0;
  const int samples = 64;
  for (int i = 0; i <= samples; i++) {
    const double t = double(i) / samples;
    const double d = (rationalCurveAt(ctrl, weights, t).point - p).norm();
    if (best < 0 || d < best) {
      best = d;
      bt = t;
    }
  }
  for (double step = 1.0 / samples; bt + step != bt || bt - step != bt; step *= 0.5) {
    for (const double t : {bt - step, bt + step}) {
      const double c = std::min(1.0, std::max(0.0, t));
      const double d = (rationalCurveAt(ctrl, weights, c).point - p).norm();
      if (d < best) {
        best = d;
        bt = c;
      }
    }
    if (!(step > 0)) break;
  }
  const CurveAt at = rationalCurveAt(ctrl, weights, bt);
  return residualAt(p, at.point, at.error, at.tangent.norm(), 1.0);
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

void CylinderSurface::reverse(void)
{
  this->normdir = -this->normdir;
}

std::shared_ptr<Surface> CylinderSurface::clone() const
{
  return std::make_shared<CylinderSurface>(*this);
}

bool CylinderSurface::transform(const Transform3d& mat, const Similarity& similarity)
{
  if (!Surface::transform(mat, similarity)) return false;
  this->r *= similarity.scale;
  return true;
}

bool CylinderSurface::sameAs(const Surface& other) const
{
  // One axis line - wherever along it either record was declared - and a point
  // of each wall on the other.
  if (typeid(*this) != typeid(other)) return false;
  const auto& o = static_cast<const CylinderSurface&>(other);
  if (!recognition::sameLine(refpt, normdir, o.refpt, o.normdir)) return false;
  return allOn(*this, {o.refpt + across(o.normdir) * o.r}) && allOn(o, {refpt + across(normdir) * r});
}

int CylinderSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  // check if on plane
  double dist = (pt - refpt).dot(normdir);

  if (fabs((pt - dist * normdir - refpt).norm() - r) > 1e-5) return 0;

  return 1;
}

ConeSurface::ConeSurface(Vector3d refpt, Vector3d normdir, double r, double slope)
{
  this->refpt = refpt;
  this->normdir = normdir;
  this->r = r;
  this->slope = slope;
}

void ConeSurface::reverse(void)
{
  // The apex is where it was; what changes is which way the radius grows.
  this->normdir = -this->normdir;
  this->slope = -this->slope;
}

std::shared_ptr<Surface> ConeSurface::clone() const
{
  return std::make_shared<ConeSurface>(*this);
}

bool ConeSurface::transform(const Transform3d& mat, const Similarity& similarity)
{
  if (!Surface::transform(mat, similarity)) return false;
  // Only the radius carries a length. The slope is a ratio of two of them, so
  // a similarity leaves the half angle alone - which is the whole reason for
  // storing the cone this way.
  this->r *= similarity.scale;
  return true;
}

double ConeSurface::radiusAt(const Vector3d& pt) const
{
  return r + slope * (pt - refpt).dot(normdir);
}

bool ConeSurface::sameAs(const Surface& other) const
{
  // One axis line, and two circles of each on the other: at its reference
  // level, and a step its own size towards its wider side. A cone is infinite
  // along its axis and traversed either way is one cone; its record says the
  // radius at one level, so two records of it agree nowhere in their fields.
  if (typeid(*this) != typeid(other)) return false;
  const auto& o = static_cast<const ConeSurface&>(other);
  if (!recognition::sameLine(refpt, normdir, o.refpt, o.normdir)) return false;
  auto points = [](const ConeSurface& s, std::vector<Vector3d>& out) {
    const double step = std::fabs(s.r) + s.refpt.norm();
    if (!(step > 0)) return false;  // an apex at the origin, and no size to step by
    const Vector3d e = across(s.normdir), a = s.normdir.normalized();
    const Vector3d level = s.refpt + a * (s.slope * a.dot(s.normdir) >= 0 ? step : -step);
    out = {s.refpt + e * s.r, level + e * s.radiusAt(level)};
    return true;
  };
  std::vector<Vector3d> of_other, of_this;
  if (!points(o, of_other) || !points(*this, of_this)) return false;
  return allOn(*this, of_other) && allOn(o, of_this);
}

int ConeSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  const double dist = (pt - refpt).dot(normdir);
  const double want = r + slope * dist;
  if (want < 0) return 0;  // past the apex, where this cone has no surface
  if (fabs((pt - dist * normdir - refpt).norm() - want) > 1e-5) return 0;
  return 1;
}

namespace {
/*! The bucket of width `width` a GridSurface indexes a point in. */
std::tuple<int64_t, int64_t, int64_t> gridKey(const Vector3d& pt, double width)
{
  return {(int64_t)llround(pt[0] / width), (int64_t)llround(pt[1] / width),
          (int64_t)llround(pt[2] / width)};
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

  // How far this surface can stand off the facets that approximate it, bounded
  // in closed form - see tessellationBound(), and tessellationBand() below for
  // the figure membership still asks.
  //
  // Membership is asked about facets, and a facet's middle sits somewhere
  // inside a cell, where the flat triangles covering it stand off the smooth
  // surface by more than a chord midpoint does - measured once at the cell
  // centre as 0.000271 where the middles were 0.000373 out, which refused every
  // facet of a twisted extrusion. It was then sampled, three points a cell each
  // way: a statistic, a lower bound on what it stood for.
  //
  // The surface is linear across the columns (evaluate), and so is a cell's
  // bilinear patch, so within a cell their difference is a blend of the two
  // column curves' differences from their chords; the triangles fall inside
  // that patch by half the cell's warp, exactly. And a column curve less its
  // chord is a cubic polynomial on every knot span - so on each piece of a
  // station interval between knots it has a Bezier form, recovered exactly
  // from four of its values, and by the convex hull property no point of the
  // piece is further from the chord than its furthest control point.
  bound = 0;
  const int segs = vspans();
  std::vector<double> deviation(std::size_t(m - 1) * std::size_t(cols), 0.0);
  for (int j = 0; j < cols; j++) {
    const double v =
      segs > 0 ? double(j) / segs : 0.0;  // column j exactly: evaluate's blend weight is 0
    for (int i = 0; i + 1 < m; i++) {
      std::vector<double> cuts{t[i]};
      for (const double k : uknots) {
        if (k > t[i] && k < t[i + 1] && k != cuts.back()) cuts.push_back(k);
      }
      cuts.push_back(t[i + 1]);
      double dev = 0;
      for (std::size_t c = 0; c + 1 < cuts.size(); c++) {
        Vector3d f[4];
        for (int k = 0; k < 4; k++) {
          const double u = cuts[c] + (cuts[c + 1] - cuts[c]) * k / 3.0;
          const double s = (u - t[i]) / (t[i + 1] - t[i]);
          f[k] = evaluate(u, v) - (at(i, j) * (1 - s) + at(i + 1, j) * s);
        }
        // the cubic through f at 0, 1/3, 2/3, 1, in Bezier form
        const Vector3d b1 = (-5 * f[0] + 18 * f[1] - 9 * f[2] + 2 * f[3]) / 6;
        const Vector3d b2 = (2 * f[0] - 9 * f[1] + 18 * f[2] - 5 * f[3]) / 6;
        dev = std::max({dev, f[0].norm(), b1.norm(), b2.norm(), f[3].norm()});
      }
      deviation[std::size_t(i) * std::size_t(cols) + std::size_t(j)] = dev;
    }
  }
  for (int i = 0; i + 1 < m; i++) {
    if (segs < 1) {
      bound = std::max(bound, deviation[std::size_t(i) * std::size_t(cols)]);
      continue;
    }
    for (int j = 0; j < segs; j++) {
      const int j2 = (j + 1) % cols;
      const Vector3d p00 = at(i, j), p10 = at(i + 1, j);
      const Vector3d p11 = at(i + 1, j2), p01 = at(i, j2);
      const double warp = (p00 - p10 + p11 - p01).norm() / 2;
      const double column = std::max(deviation[std::size_t(i) * std::size_t(cols) + std::size_t(j)],
                                     deviation[std::size_t(i) * std::size_t(cols) + std::size_t(j2)]);
      bound = std::max(bound, column + warp / 2);
    }
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
  // And then over the cells, because a chord along a column is not what the
  // mesh actually covers this surface with.
  //
  // Membership is asked about facets, and a facet's middle is neither a vertex
  // nor a chord midpoint: it sits somewhere inside a cell, and the flat
  // triangles covering that cell stand off the smooth surface by more than the
  // chord midpoint does. Measuring only at the cell centre said 0.000271 where
  // the middles were 0.000373 out, and on a twisted extrusion that gap refused
  // every facet the grid had just been recovered from.
  //
  // So the cell is sampled rather than probed at one point, against the
  // bilinear patch through its corners - which is what a flat tessellation of
  // it is - plus half the warp, the amount by which either triangulation of a
  // non-planar quad falls inside that patch.
  const double samples[3] = {1.0 / 6, 0.5, 5.0 / 6};
  for (int j = 0; j < segs; j++) {
    const int j2 = (j + 1) % cols;
    for (int i = 0; i + 1 < m; i++) {
      const Vector3d p00 = at(i, j), p10 = at(i + 1, j);
      const Vector3d p11 = at(i + 1, j2), p01 = at(i, j2);
      const double warp = (p00 - p10 + p11 - p01).norm() / 2;
      for (const double a : samples) {
        for (const double b : samples) {
          const double u = t[i] + a * (t[i + 1] - t[i]);
          const double v = (double(j) + b) / segs;
          const Vector3d flat =
            p00 * (1 - a) * (1 - b) + p10 * a * (1 - b) + p11 * a * b + p01 * (1 - a) * b;
          band = std::max(band, (evaluate(u, v) - flat).norm() + warp / 2);
        }
      }
    }
  }
}

std::vector<Vector3d> GridSurface::withClosingColumn(const std::vector<Vector3d>& src,
                                                     int seam_column) const
{
  if (!closed_v || cols < 1) return src;
  const int start = ((seam_column % cols) + cols) % cols;
  // As many rows as the net handed in has - the grid's own, or a rendering of
  // a closed form with rows of its own.
  const int n = int(src.size()) / cols;
  std::vector<Vector3d> out;
  out.reserve(src.size() + std::size_t(n));
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < cols; j++) {
      out.push_back(src[std::size_t(i) * cols + (start + j) % cols]);
    }
    out.push_back(src[std::size_t(i) * cols + start]);
  }
  return out;
}

double GridSurface::seamShift(int seam_column) const
{
  const int spans = vspans();
  if (!closed_v || spans <= 0) return 0.0;
  return double(((seam_column % cols) + cols) % cols) / double(spans);
}

bool GridSurface::onSeam(double u, double v) const
{
  if (!closed_v) return false;
  // Asked in space and not of v, because v is where a projection left it: a
  // corner on the seam is v = 0 and v = 1 at once, and `project` returns either,
  // or a few ulps off one, depending on which span's descent finished first.
  // Two evaluations, each off by at most evaluationBound(), and the norm of
  // their difference: at machine precision, the same point.
  const Vector3d at = evaluate(u, v);
  const double bound = 2 * evaluationBound() + recognition::forwardError(9, 2 * at.norm());
  return std::min((at - evaluate(u, 0.0)).norm(), (at - evaluate(u, 1.0)).norm()) <= bound;
}

bool GridSurface::closestPoint(const Vector3d& p, Vector3d& out) const
{
  double u = 0, v = 0;
  if (!project(p, u, v)) return false;
  const Vector3d q = evaluate(u, v);
  if (!((q - p).norm() <= tessellationBound() + evaluationBound())) return false;
  out = q;
  return true;
}

bool GridSurface::envelope(const std::vector<Vector3d>&, BoundingBox& box) const
{
  // The flat tessellation of the net lies within the net's box, and the
  // surface within tessellationBound() of that tessellation - a sound bound in
  // closed form - and evaluationBound() of what is computed for it. Where the
  // grid is not interpolated the surface is the tessellation itself.
  if (net.empty()) return false;
  box.setEmpty();
  for (const Vector3d& p : net) box.extend(p);
  const double grow = tessellationBound() + evaluationBound();
  if (!std::isfinite(grow)) return false;
  box.min() -= Vector3d::Constant(grow);
  box.max() += Vector3d::Constant(grow);
  return true;
}

double GridSurface::evaluationBound() const
{
  // `evaluate` sums positive weights of one - the cubic basis in u, which is a
  // partition of unity, and the linear blend in v - so its result is off by the
  // rounding of the weights and the sums times the largest point combined. The
  // basis: three levels of Cox-de Boor, 7 operations each (21); four products
  // summed (7); the blend in v (4) - 32. The bilinear fallback is fewer.
  double largest = 0;
  for (const auto& p : poles) largest = std::max(largest, p.norm());
  for (const auto& p : net) largest = std::max(largest, p.norm());
  return recognition::forwardError(32, largest);
}

bool GridSurface::splineForm(int& degree_u, int& degree_v, int& rows_out, int& cols_out,
                             std::vector<Vector3d>& ctrl, std::vector<double>& knots_u,
                             std::vector<int>& mults_u, std::vector<double>& knots_v,
                             std::vector<int>& mults_v, int seam_column) const
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
    ctrl = withClosingColumn(net, seam_column);
    return true;
  }

  degree_u = GRID_DEGREE;
  ctrl = withClosingColumn(poles, seam_column);
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
  const int nspans = std::max(1, vspans());
  // One start per profile span, not one overall. The profile is a polyline, so
  // the surface is creased between spans and each is a *separate smooth piece*:
  // a descent inside one cannot walk into another, and the best sample overall
  // is only ever in one of them. Keeping a start per span is the same thing
  // BezierPatchSurface::project does for the same reason, and here the spans
  // come from the declaration rather than from a guess about the shape.
  //
  // Measured on the band family's own ridge before this: `project` could not
  // find a point `evaluate` had just produced on the closing strip - the flat
  // back of the ridge - missing it by 0.78 mm, because the best coarse sample
  // lay on a flank and was already a local minimum there. Damping the descent
  // changed the answer in not one digit; it was never the descent.
  std::vector<double> best_per(nspans, std::numeric_limits<double>::infinity());
  std::vector<double> bu_per(nspans, 0.0), bv_per(nspans, 0.0);
  const int usamples = std::max(rows * 2, 8);
  // Four samples inside each span, at its quarter points, and never on a span
  // boundary. A profile is a polyline, so every boundary is a corner where the
  // v derivative does not exist; sampling one and starting the descent there
  // gives a finite difference straddling the corner, which points nowhere
  // useful, and the iteration stops on the spot. Landing inside a span instead
  // means the piece being descended is smooth, which is what Gauss-Newton
  // needs.
  const int vsamples = std::max(vspans() * 4, 4);
  for (int i = 0; i <= usamples; i++) {
    const double su = double(i) / usamples;
    for (int j = 0; j < vsamples; j++) {
      const double sv = (double(j) + 0.5) / vsamples;
      const double d = (evaluate(su, sv) - pt).squaredNorm();
      const int span = std::min(nspans - 1, int(sv * nspans));
      if (d < best_per[span]) {
        best_per[span] = d;
        bu_per[span] = su;
        bv_per[span] = sv;
      }
    }
  }
  // Then Gauss-Newton on the squared distance, by finite differences: the
  // surface is piecewise polynomial and this only has to converge locally.
  //
  // Every step has to *improve* the distance, and that is not a refinement of
  // the method but the whole of its correctness here. Gauss-Newton on a swept
  // surface is solving a normal equation whose matrix goes near singular
  // wherever the two parameter directions run nearly parallel - along a crest,
  // at a profile corner - and the step it then asks for is enormous. Taken on
  // trust it lands somewhere else entirely on the sweep, and the iteration
  // settles there quite happily.
  //
  // Measured on step-band-family at $fn 96 before this guard: of 44959
  // projections, 1038 came out *worse* than the coarse sample they started
  // from, the worst going from 3.90 mm to 11.90 mm on a ridge 2 mm deep. Those
  // are the points a boolean made along the wall, and they are exactly the ones
  // a declared sweep needs to recognise - so half of the sweep was left
  // faceted, on a surface it lies on to the width of a line.
  // Damped rather than plain, and that is not a refinement either. Where the
  // profile collapses - the run-out of a ridge, where the crest returns to the
  // root and the two parameter directions stop spanning a plane - `jtj` is
  // singular, and giving up there left the *coarse sample* as the answer.
  // Measured on the band family's own ridge: `project` could not find a point
  // `evaluate` had just produced, missing it by 0.78 mm at u = 0.95, which is
  // half a station and exactly the resolution of that sample grid.
  //
  // Levenberg-Marquardt asks the same question with a damping term that makes
  // the system solvable whatever the Jacobian does: lambda up when a step does
  // not help, down when it does. At lambda near zero it *is* Gauss-Newton, so
  // the well-conditioned interior behaves as before; at large lambda the step
  // becomes a short one down the gradient, which is defined everywhere.
  const double h = 1e-6;
  double answer = std::numeric_limits<double>::infinity();
  double au = 0, av = 0;
  for (int span = 0; span < nspans; span++) {
    if (!std::isfinite(best_per[span])) continue;
    double bu = bu_per[span], bv = bv_per[span];
    double best_d = best_per[span];
    double lambda = 1e-9;
    for (int iter = 0; iter < 64; iter++) {
      const Vector3d r = evaluate(bu, bv) - pt;
      const Vector3d du =
        (evaluate(std::min(1.0, bu + h), bv) - evaluate(std::max(0.0, bu - h), bv)) / (2 * h);
      // Differenced *within* the span, not across it. At a span boundary the
      // profile has a corner and the v derivative does not exist; a central
      // difference straddling one measures the average of two different slopes
      // and points nowhere useful. Held inside the piece being descended it
      // measures that piece. Worth 7.8e-07 down to rounding on the band family's
      // ridge, whose worst case sits exactly on a boundary.
      const double vlo = double(span) / nspans, vhi = double(span + 1) / nspans;
      const Vector3d dv = (evaluate(bu, std::min(vhi, bv + h)) - evaluate(bu, std::max(vlo, bv - h))) /
                          (std::min(vhi, bv + h) - std::max(vlo, bv - h));
      Eigen::Matrix2d jtj;
      jtj << du.dot(du), du.dot(dv), du.dot(dv), dv.dot(dv);
      const Eigen::Vector2d rhs(-r.dot(du), -r.dot(dv));
      // Every step still has to *improve* the distance, which is the guard this
      // has always carried: Gauss-Newton on a swept surface asks for enormous
      // steps wherever the two parameter directions run nearly parallel, and one
      // taken on trust lands somewhere else entirely on the sweep.
      bool moved = false;
      for (int attempt = 0; attempt < 16; attempt++) {
        Eigen::Matrix2d damped = jtj;
        damped(0, 0) += lambda * std::max(jtj(0, 0), 1e-30);
        damped(1, 1) += lambda * std::max(jtj(1, 1), 1e-30);
        if (!(fabs(damped.determinant()) > 0)) {
          lambda *= 10;
          continue;
        }
        const Eigen::Vector2d step = damped.inverse() * rhs;
        if (!step.allFinite()) {
          lambda *= 10;
          continue;
        }
        const double nu = std::clamp(bu + step[0], 0.0, 1.0);
        const double nv = std::clamp(bv + step[1], 0.0, 1.0);
        const double nd = (evaluate(nu, nv) - pt).squaredNorm();
        if (nd < best_d) {
          const bool settled = fabs(nu - bu) < 1e-14 && fabs(nv - bv) < 1e-14;
          bu = nu;
          bv = nv;
          best_d = nd;
          lambda = std::max(lambda * 0.1, 1e-12);
          moved = !settled;
          break;
        }
        lambda *= 10;
      }
      if (!moved) break;
    }
    if (best_d < answer) {
      answer = best_d;
      au = bu;
      av = bv;
    }
  }
  if (!std::isfinite(answer)) return false;
  u = au;
  v = av;
  return true;
}

bool GridSurface::onSurface(const Vector3d& pt, double tol) const
{
  double u = 0, v = 0;
  if (!project(pt, u, v)) return false;
  return (evaluate(u, v) - pt).norm() <= tol;
}

int GridSurface::netIndex(const Vector3d& pt) const
{
  if (!(bucket > 0)) return -1;
  const auto key = gridKey(pt, bucket);
  for (int64_t dx = -1; dx <= 1; dx++) {
    for (int64_t dy = -1; dy <= 1; dy++) {
      for (int64_t dz = -1; dz <= 1; dz++) {
        const auto it =
          lookup.find({std::get<0>(key) + dx, std::get<1>(key) + dy, std::get<2>(key) + dz});
        if (it != lookup.end() && recognition::samePoint(net[std::size_t(it->second)], pt)) {
          return it->second;
        }
      }
    }
  }
  return -1;
}

std::shared_ptr<GridSurface> GridSurface::fromRows(const std::vector<std::vector<Vector3d>>& rows,
                                                   bool closed_v, std::string& why)
{
  why.clear();
  if (rows.size() < 2) {
    why = "declare_grid: needs at least two rows of points";
    return nullptr;
  }
  const std::size_t cols = rows[0].size();
  for (std::size_t i = 0; i < rows.size(); i++) {
    if (rows[i].size() != cols) {
      // A ragged grid has no ordering to speak of, which is the one thing this
      // record exists to carry.
      why = "declare_grid: row " + std::to_string(i) + " has " + std::to_string(rows[i].size()) +
            " points, the first row has " + std::to_string(cols) +
            " - every row must be the same length";
      return nullptr;
    }
  }
  if (cols < 2) {
    why = "declare_grid: needs at least two points per row";
    return nullptr;
  }
  std::vector<Vector3d> net;
  net.reserve(rows.size() * cols);
  for (const auto& row : rows) {
    for (const auto& p : row) net.push_back(p);
  }
  return std::make_shared<GridSurface>(int(rows.size()), int(cols), std::move(net), closed_v);
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
  // As wide as two points one at machine precision can be apart at the net's
  // size (recognition::samePoint); a point at the far end of the net is at
  // most that from its net point in each coordinate, so a neighbour's bucket.
  double largest = 0;
  for (const auto& p : net) largest = std::max(largest, p.norm());
  bucket = recognition::forwardError(9, 2 * largest);
  if (!(bucket > 0)) return;
  for (std::size_t i = 0; i < net.size(); i++) lookup.emplace(gridKey(net[i], bucket), int(i));
}

int GridSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  // On the *surface*, which is what every other Surface means by this and what
  // the exporter needs. A declared point is on it by construction and is
  // checked first, because that is a lookup rather than a projection - but a
  // point a boolean created is on it too, and refusing those was the whole
  // shortfall of declaring the grid alone.
  if (isDeclaredPoint(pt)) return 1;
  return onSurface(pt, membershipTolerance()) ? 1 : 0;
}

std::shared_ptr<Surface> GridSurface::clone() const
{
  return std::make_shared<GridSurface>(*this);
}

bool GridSurface::transform(const Transform3d& mat, const Similarity&)
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
  return o->rows == rows && o->cols == cols && o->closed_v == closed_v && o->net == net;
}

// ---------------------------------------------------------------------------
// SweepSurface: a profile carried along a helix, declared as the shape itself.
// ---------------------------------------------------------------------------

namespace {

/*! The stations a sweep is rendered at, for the consumers that want a grid.
 *
 * Only the *written* surface depends on this. Membership does not, which is the
 * whole point of the class, so this number is a rendering choice and not a
 * tolerance anybody has to trust. */
int sweepStations(double turns, int asked)
{
  const int least = std::max(8, int(std::ceil(fabs(turns) * 8)));
  return std::max(least, asked);
}

/*! The sampled net, so the inherited GridSurface has something to interpolate. */
std::vector<Vector3d> sweepNet(const Vector3d& origin, const Vector3d& axis, const Vector3d& ref,
                               const SweepLaws& laws, double pitch, double turns, int stations)
{
  const Vector3d other = axis.cross(ref);
  std::vector<Vector3d> net;
  net.reserve(std::size_t(stations) * laws.points());
  for (int i = 0; i < stations; i++) {
    const double t = stations > 1 ? double(i) / (stations - 1) : 0.0;
    const double theta = 2 * M_PI * turns * t;
    const double z = pitch * turns * t;
    const Vector3d radial = ref * cos(theta) + other * sin(theta);
    const double r = laws.radiusAt(t);
    for (std::size_t k = 0; k < laws.points(); k++) {
      const Vector2d p = laws.pointAt(k, t);
      net.push_back(origin + radial * (r + p[0]) + axis * (z + p[1]));
    }
  }
  return net;
}

/*! Where `t` falls in a table of keys: the key at or below it, and how far on
 * to the next - clamped, so past either end the law is its end value. */
void lawSpan(const std::vector<double>& at, double t, std::size_t& k, double& f)
{
  if (at.size() < 2 || t <= at.front()) {
    k = 0;
    f = 0.0;
    return;
  }
  if (t >= at.back()) {
    k = at.size() - 1;
    f = 0.0;
    return;
  }
  k = std::size_t(std::upper_bound(at.begin(), at.end(), t) - at.begin()) - 1;
  f = (t - at[k]) / (at[k + 1] - at[k]);
}

/*! The reference direction, squared up against the axis. */
Vector3d sweepRef(const Vector3d& axis, const Vector3d& ref)
{
  const Vector3d a = axis.normalized();
  return (ref - a * ref.dot(a)).normalized();
}

}  // namespace

SweepLaws SweepLaws::constant(double r, std::vector<Vector2d> p)
{
  SweepLaws laws;
  laws.radius_at = {0.0};
  laws.radius = {r};
  laws.profile_at = {0.0};
  laws.profile = {std::move(p)};
  return laws;
}

double SweepLaws::radiusAt(double t) const
{
  std::size_t k = 0;
  double f = 0;
  lawSpan(radius_at, t, k, f);
  return f == 0.0 ? radius[k] : radius[k] + (radius[k + 1] - radius[k]) * f;
}

Vector2d SweepLaws::pointAt(std::size_t i, double t) const
{
  std::size_t k = 0;
  double f = 0;
  lawSpan(profile_at, t, k, f);
  return f == 0.0 ? profile[k][i] : Vector2d(profile[k][i] + (profile[k + 1][i] - profile[k][i]) * f);
}

std::vector<double> SweepLaws::corners() const
{
  std::vector<double> out;
  for (const auto *at : {&radius_at, &profile_at}) {
    for (const double t : *at) {
      if (t > 0.0 && t < 1.0) out.push_back(t);
    }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

double SweepLaws::largestRadius() const
{
  double r = 0.0;
  for (const double x : radius) r = std::max(r, fabs(x));
  return r;
}

std::string SweepLaws::invalid() const
{
  if (radius.empty() || radius.size() != radius_at.size()) return "the radius has no value";
  if (profile.empty() || profile.size() != profile_at.size()) return "the profile has no points";
  for (const auto *at : {&radius_at, &profile_at}) {
    for (std::size_t k = 0; k < at->size(); k++) {
      if (!std::isfinite((*at)[k])) return "a key of a law is not a finite number";
      if (k > 0 && !((*at)[k] > (*at)[k - 1])) return "the keys of a law have to increase";
    }
  }
  for (const double r : radius) {
    if (!std::isfinite(r)) return "a radius is not a finite number";
  }
  const std::size_t n = profile.front().size();
  if (n < 3) return "a closed profile needs at least three points, got " + std::to_string(n);
  for (std::size_t k = 0; k < profile.size(); k++) {
    if (profile[k].size() != n) {
      return "every profile of the law needs the same number of points - " + std::to_string(n) +
             " at the first, " + std::to_string(profile[k].size()) + " at key " + std::to_string(k);
    }
    for (std::size_t i = 0; i < n; i++) {
      if (!std::isfinite(profile[k][i][0]) || !std::isfinite(profile[k][i][1])) {
        return "profile point " + std::to_string(i) + " is not two finite numbers";
      }
      // A repeated point leaves a zero length segment, which has no direction,
      // and the distance to the profile would be ill defined at that corner.
      if ((profile[k][i] - profile[k][(i + 1) % n]).norm() < 1e-12) {
        return "profile points " + std::to_string(i) + " and " + std::to_string((i + 1) % n) +
               " are the same point";
      }
    }
  }
  return {};
}

SweepLaws SweepLaws::scaled(double s) const
{
  SweepLaws out = *this;
  for (double& r : out.radius) r *= s;
  for (auto& p : out.profile) {
    for (auto& q : p) q *= s;
  }
  return out;
}

bool SweepLaws::sameAs(const SweepLaws& o) const
{
  return radius_at == o.radius_at && radius == o.radius && profile_at == o.profile_at &&
         profile == o.profile;
}

SweepSurface::SweepSurface(const Vector3d& origin, const Vector3d& axis, const Vector3d& ref_in,
                           SweepLaws laws_in, double pitch_in, double turns_in, int stations_in)
  : GridSurface(sweepStations(turns_in, stations_in), int(laws_in.points()),
                sweepNet(origin, axis.normalized(), sweepRef(axis, ref_in), laws_in, pitch_in, turns_in,
                         sweepStations(turns_in, stations_in)),
                true),
    laws(std::move(laws_in)),
    pitch(pitch_in),
    turns(turns_in),
    stations(sweepStations(turns_in, stations_in))
{
  refpt = origin;
  normdir = axis.normalized();
  ref = sweepRef(axis, ref_in);
}

std::shared_ptr<SweepSurface> SweepSurface::make(const Vector3d& origin, const Vector3d& axis,
                                                 const Vector3d& ref_in, const SweepLaws& laws,
                                                 double pitch, double turns, int stations_in,
                                                 std::string& why)
{
  if (axis.norm() < 1e-12) {
    why = "declare_sweep: the axis has no direction";
    return nullptr;
  }
  const Vector3d a = axis.normalized();
  const Vector3d r = ref_in - a * ref_in.dot(a);
  if (r.norm() < 1e-9) {
    why =
      "declare_sweep: the reference direction is parallel to the axis, so there is no angle "
      "for the profile to be measured from";
    return nullptr;
  }
  const std::string bad = laws.invalid();
  if (!bad.empty()) {
    why = "declare_sweep: " + bad;
    return nullptr;
  }
  if (!std::isfinite(pitch) || !std::isfinite(turns) || turns == 0) {
    why = "declare_sweep: pitch and turns have to be finite, and turns cannot be zero";
    return nullptr;
  }
  return std::make_shared<SweepSurface>(origin, a, r, laws, pitch, turns, stations_in);
}

void SweepSurface::localCoords(const Vector3d& pt, double& t, double& dr, double& dz) const
{
  const Vector3d rel = pt - refpt;
  const double along = rel.dot(normdir);
  const Vector3d radial = rel - normdir * along;
  const Vector3d other = normdir.cross(ref);
  const double theta = atan2(radial.dot(other), radial.dot(ref));  // (-pi, pi]
  const double frac = theta / (2 * M_PI);
  // Which turn the point belongs to. A helix passes over the same angle once
  // per turn, so the angle alone does not say; the station whose height is
  // nearest does, and rounding picks it in one step rather than by searching.
  const double k = pitch != 0 ? std::round(along / pitch - frac) : 0.0;
  const double turn = frac + k;
  t = turns != 0 ? turn / turns : 0.0;
  dr = radial.norm() - laws.radiusAt(t);
  dz = along - turn * pitch;
}

double SweepSurface::profileDistance(double t, double dr, double dz, double *at) const
{
  const std::size_t n = laws.points();
  double best = std::numeric_limits<double>::infinity();
  double best_at = 0;
  const Vector2d q(dr, dz);
  for (std::size_t i = 0; i < n; i++) {
    const Vector2d a = laws.pointAt(i, t);
    const Vector2d b = laws.pointAt((i + 1) % n, t);
    const Vector2d e = b - a;
    const double len2 = e.squaredNorm();
    double s = len2 > 0 ? (q - a).dot(e) / len2 : 0.0;
    s = std::clamp(s, 0.0, 1.0);
    const double d = (a + e * s - q).norm();
    if (d < best) {
      best = d;
      best_at = (double(i) + s) / double(n);
    }
  }
  if (at != nullptr) *at = best_at;
  return best;
}

Vector3d SweepSurface::evaluate(double u, double v) const
{
  u = std::clamp(u, 0.0, 1.0);
  v = std::clamp(v, 0.0, 1.0);
  const std::size_t n = laws.points();
  const double s = v * double(n);
  std::size_t i = std::size_t(s);
  if (i >= n) i = n - 1;
  const double f = s - double(i);
  const Vector2d a = laws.pointAt(i, u);
  const Vector2d p = a + (laws.pointAt((i + 1) % n, u) - a) * f;
  const double theta = 2 * M_PI * turns * u;
  const double z = pitch * turns * u;
  const Vector3d other = normdir.cross(ref);
  const Vector3d radial = ref * cos(theta) + other * sin(theta);
  return refpt + radial * (laws.radiusAt(u) + p[0]) + normdir * (z + p[1]);
}

bool SweepSurface::project(const Vector3d& pt, double& u, double& v) const
{
  // An inversion, not a search: no coarse sample, no descent, nothing to
  // converge and no basin to land in the wrong one of.
  double t = 0, dr = 0, dz = 0;
  localCoords(pt, t, dr, dz);
  double at = 0;
  profileDistance(t, dr, dz, &at);
  u = std::clamp(t, 0.0, 1.0);
  v = at;
  return true;
}

bool SweepSurface::onSurface(const Vector3d& pt, double tol) const
{
  double t = 0, dr = 0, dz = 0;
  localCoords(pt, t, dr, dz);
  // Past either end of the sweep the surface is simply not there. The allowance
  // is stated in the sweep's own parameter, so it means the same distance
  // whatever the sweep's length.
  const double per_turn = std::max(1e-9, hypot(2 * M_PI * laws.largestRadius(), pitch));
  const double slack = turns != 0 ? tol / (fabs(turns) * per_turn) : 0.0;
  if (t < -slack || t > 1 + slack) return false;
  return profileDistance(t, dr, dz) <= tol;
}

int SweepSurface::pointMember(std::vector<Vector3d>& vertices, Vector3d pt)
{
  // No lookup of declared points and no tessellation band. The closed form
  // answers this outright, and the only tolerance is the one the caller is
  // entitled to state about its own mesh.
  return onSurface(pt, membershipTolerance()) ? 1 : 0;
}

bool SweepSurface::renderAlongU() const
{
  if (rendered_) return render_ok_;
  rendered_ = true;
  render_ok_ = false;
  const int n_cols = cols;
  const int spans_v = vspans();
  if (n_cols < 1 || spans_v < 1) return false;
  // Where the laws turn a corner the surface is only C0 along the sweep, and
  // each stretch between corners is rendered on its own.
  std::vector<double> ends{0.0};
  for (const double c : laws.corners()) ends.push_back(c);
  ends.push_back(1.0);

  std::vector<Vector3d> rows_out;  // accumulated control rows, n_cols each
  std::vector<double> knots{0.0};
  std::vector<int> mults{GRID_DEGREE + 1};
  for (std::size_t s = 0; s + 1 < ends.size(); s++) {
    const double a = ends[s], b = ends[s + 1];
    // Enough stations to start with that a cubic has something to fit, and
    // then as many more as the closed form needs.
    int n = std::max(GRID_DEGREE + 1, int(std::ceil(8.0 * fabs(turns) * (b - a))) + 1);
    std::vector<double> full;
    std::vector<Vector3d> poles_seg;
    for (;;) {
      std::vector<double> t(n);
      for (int i = 0; i < n; i++) t[i] = a + (b - a) * double(i) / double(n - 1);
      t.back() = b;
      // The knots: clamped at the ends of the stretch, averaged inside it.
      full.assign(std::size_t(n) + GRID_DEGREE + 1, a);
      for (int i = 0; i <= GRID_DEGREE; i++) full[std::size_t(n) + i] = b;
      for (int j = 1; j <= n - GRID_DEGREE - 1; j++) {
        double acc = 0.0;
        for (int i = j; i <= j + GRID_DEGREE - 1; i++) acc += t[i];
        full[std::size_t(j) + GRID_DEGREE] = acc / GRID_DEGREE;
      }
      // Banded - four basis functions a row - so solved sparse: the cost grows
      // with the stations, not with their square.
      std::vector<Eigen::Triplet<double>> entries;
      entries.reserve(std::size_t(n) * (GRID_DEGREE + 1));
      for (int i = 0; i < n; i++) {
        const int span = findSpan(full, n, t[i]);
        double basis[GRID_DEGREE + 1];
        basisFuns(full, span, t[i], basis);
        for (int k = 0; k <= GRID_DEGREE; k++) {
          if (basis[k] != 0.0) entries.emplace_back(i, span - GRID_DEGREE + k, basis[k]);
        }
      }
      Eigen::SparseMatrix<double> A(n, n);
      A.setFromTriplets(entries.begin(), entries.end());
      Eigen::SparseLU<Eigen::SparseMatrix<double>> lu;
      lu.compute(A);
      if (lu.info() != Eigen::Success) return false;
      poles_seg.assign(std::size_t(n) * n_cols, Vector3d::Zero());
      Eigen::MatrixXd rhs(n, 3);
      for (int j = 0; j < n_cols; j++) {
        const double v = double(j) / spans_v;
        for (int i = 0; i < n; i++) rhs.row(i) = evaluate(t[i], v).transpose();
        const Eigen::MatrixXd sol = lu.solve(rhs);
        if (!sol.allFinite()) return false;
        for (int i = 0; i < n; i++) poles_seg[std::size_t(i) * n_cols + j] = sol.row(i).transpose();
        // The ends are the closed form's own points at the corners, so the
        // stretches either side meet exactly.
        poles_seg[std::size_t(j)] = evaluate(t.front(), v);
        poles_seg[std::size_t(n - 1) * n_cols + j] = evaluate(t.back(), v);
      }
      // How far the rendering strays between its stations, against the form.
      double worst = 0.0;
      for (int i = 0; i + 1 < n; i++) {
        const double mid = 0.5 * (t[i] + t[i + 1]);
        const int span = findSpan(full, n, mid);
        double basis[GRID_DEGREE + 1];
        basisFuns(full, span, mid, basis);
        for (int j = 0; j < n_cols; j++) {
          Vector3d p = Vector3d::Zero();
          for (int k = 0; k <= GRID_DEGREE; k++)
            p += poles_seg[std::size_t(span - GRID_DEGREE + k) * n_cols + j] * basis[k];
          worst = std::max(worst, (p - evaluate(mid, double(j) / spans_v)).norm());
        }
      }
      if (worst <= kRenderTolerance) break;
      // A safety bound, not a tolerance. Between the laws' corners the form is
      // analytic - linear laws on a helix - so a cubic converges like h^4 and
      // comes nowhere near it; only a corner the laws did not name could, and
      // then the sweep is left to the inherited grid.
      if (n > 1 << 16) return false;
      n = 2 * n - 1;  // every station kept, one more between each two
    }
    const std::size_t skip = s == 0 ? 0 : 1;  // the corner row the stretch before ended on
    rows_out.insert(rows_out.end(), poles_seg.begin() + long(skip * n_cols), poles_seg.end());
    for (int k = GRID_DEGREE + 1; k < n; k++) {
      knots.push_back(full[std::size_t(k)]);
      mults.push_back(1);
    }
    knots.push_back(b);
    mults.push_back(s + 2 == ends.size() ? GRID_DEGREE + 1 : GRID_DEGREE);
  }
  render_rows_ = int(rows_out.size()) / n_cols;
  render_ctrl_ = std::move(rows_out);
  render_knots_ = std::move(knots);
  render_mults_ = std::move(mults);
  render_ok_ = true;
  return true;
}

bool SweepSurface::splineForm(int& degree_u, int& degree_v, int& rows_out, int& cols_out,
                              std::vector<Vector3d>& ctrl, std::vector<double>& knots_u,
                              std::vector<int>& mults_u, std::vector<double>& knots_v,
                              std::vector<int>& mults_v, int seam_column) const
{
  if (!renderAlongU()) {
    return GridSurface::splineForm(degree_u, degree_v, rows_out, cols_out, ctrl, knots_u, mults_u,
                                   knots_v, mults_v, seam_column);
  }
  // Across the profile, the grid's own form: ruled, degree 1, the closing
  // column repeated for a closed profile, and the seam where the caller puts it.
  if (!GridSurface::splineForm(degree_u, degree_v, rows_out, cols_out, ctrl, knots_u, mults_u, knots_v,
                               mults_v, seam_column)) {
    return false;
  }
  degree_u = GRID_DEGREE;
  rows_out = render_rows_;
  knots_u = render_knots_;
  mults_u = render_mults_;
  ctrl = withClosingColumn(render_ctrl_, seam_column);
  return true;
}

std::shared_ptr<Surface> SweepSurface::clone() const
{
  return std::make_shared<SweepSurface>(refpt, normdir, ref, laws, pitch, turns, stations);
}

bool SweepSurface::transform(const Transform3d& mat, const Similarity& similarity)
{
  // A sweep survives a rigid motion with a uniform scale and nothing else. Any
  // other map turns its circular path into an ellipse, which this form cannot
  // state, and refusing is what the caller needs - see Surface::transform.
  if (!similarity.stated) return false;
  const Eigen::Matrix3d m = mat.linear();
  // A reflection is not one of those, though it is a similarity: the maker
  // states it (Similarity::mirrors), and this record cannot absorb one.
  //
  // `evaluate` builds the second radial direction as `normdir x ref`, and a
  // cross product is not preserved by a reflection - it comes out negated. So a
  // mirrored record turns its helix the other way while the mesh beside it is
  // mirrored correctly, and the two describe different solids. Measured: a
  // radius-10 sweep mirrored in x puts the declaration **20.0 mm** from the
  // mesh at a quarter turn, which is the point on the far side of the axis.
  //
  // Negating `turns` almost absorbs it and does not quite: `turns` also carries
  // the axial rise through `z = pitch * turns * u`, so flipping it reverses a
  // height the mirror did not touch. Refusing loses the declaration and leaves
  // the mesh faceted, which is this file's rule for anything it cannot state.
  if (similarity.mirrors) return false;
  const double scale = similarity.scale;
  *this = SweepSurface(mat * refpt, (m * normdir).normalized(), (m * ref).normalized(),
                       laws.scaled(scale), pitch * scale, turns, stations);
  return true;
}

bool SweepSurface::sameAs(const Surface& other) const
{
  const auto *o = dynamic_cast<const SweepSurface *>(&other);
  if (o == nullptr) return false;
  return refpt == o->refpt && normdir == o->normdir && ref == o->ref && pitch == o->pitch &&
         turns == o->turns && laws.sameAs(o->laws);
}

// ---------------------------------------------------------------------------
// What each surface says about a point - see Surface.h. The bodies are the
// branches of the switches that used to ask this in recognition/Exact.h,
// AnalyticFeatures.cc and export_step.cc, moved here unchanged, operation
// counts included.
// ---------------------------------------------------------------------------

using recognition::forwardError;
using recognition::Residual;

Residual Surface::signedResidual(const Vector3d&) const
{
  return Residual{};
}
Residual Surface::residual(const Vector3d& p) const
{
  Residual out = signedResidual(p);
  out.value = std::fabs(out.value);
  return out;
}
bool Surface::closestPoint(const Vector3d&, Vector3d&) const
{
  return false;
}
bool Surface::normalAt(const Vector3d&, Vector3d&) const
{
  return false;
}
bool Surface::implicitAt(const Vector3d&, double&, Vector3d&) const
{
  return false;
}

Residual CylinderSurface::signedResidual(const Vector3d& p) const
{
  // axis: 3 mul, 2 add, sqrt, 3 div (9); rel: 3 (12); rel.axis: 3 mul, 2 add
  // (17); axis * t: 3 (20); rel - axis t: 3 (23); norm: 3 mul, 2 add, sqrt
  // (29); minus r (30).
  const Vector3d rel = p - refpt;
  const Vector3d axis = normdir.normalized();
  Residual out;
  out.value = (rel - axis * rel.dot(axis)).norm() - r;
  out.bound = forwardError(30, p.norm() + refpt.norm() + fabs(r));
  return out;
}

Residual ConeSurface::signedResidual(const Vector3d& p) const
{
  // The cylinder's 29 to the radial distance; radiusAt: its own axis (9),
  // rel (3), dot (5), times slope (1), plus r (1) = 19; the difference (1).
  const Vector3d rel = p - refpt;
  const Vector3d axis = normdir.normalized();
  const double want = radiusAt(p);
  Residual out;
  out.value = (rel - axis * rel.dot(axis)).norm() - want;
  out.bound = forwardError(49, p.norm() + refpt.norm() + fabs(r) + fabs(want));
  return out;
}

Residual SphereSurface::signedResidual(const Vector3d& p) const
{
  // rel: 3; norm: 6; minus r: 1.
  Residual out;
  out.value = (p - refpt).norm() - r;
  out.bound = forwardError(10, p.norm() + refpt.norm() + fabs(r));
  return out;
}

Residual PlaneSurface::signedResidual(const Vector3d& p) const
{
  return recognition::planeResidual(refpt, normdir, p);
}

Residual TorusSurface::signedResidual(const Vector3d& p) const
{
  // axis 9, rel 3, along 5, radial: 3 + 3 + 6 (26); radial - R: 1; hypot: 2
  // mul, 1 add, sqrt (4); minus r: 1 - 32 in all.
  const Vector3d rel = p - refpt;
  const Vector3d axis = normdir.normalized();
  const double along = rel.dot(axis);
  const double radial = (rel - axis * along).norm();
  Residual out;
  out.value = hypot(radial - r_major, along) - r_minor;
  out.bound = forwardError(32, p.norm() + refpt.norm() + fabs(r_major) + fabs(r_minor));
  return out;
}

bool CylinderSurface::closestPoint(const Vector3d& p, Vector3d& out) const
{
  const Vector3d axis = normdir.normalized();
  const Vector3d rel = p - refpt;
  const Vector3d radial = rel - axis * rel.dot(axis);
  if (radial.norm() < 1e-12) return false;
  out = refpt + axis * rel.dot(axis) + radial.normalized() * r;
  return true;
}

bool ConeSurface::closestPoint(const Vector3d& p, Vector3d& out) const
{
  const Vector3d axis = normdir.normalized();
  const Vector3d rel = p - refpt;
  const double h = rel.dot(axis);
  const Vector3d radial = rel - axis * h;
  if (radial.norm() < 1e-12) return false;
  const double want = r + h * slope;
  if (want <= 0) return false;
  out = refpt + axis * h + radial.normalized() * want;
  return true;
}

bool PlaneSurface::closestPoint(const Vector3d& p, Vector3d& out) const
{
  const Vector3d n = normdir.normalized();
  out = p - n * n.dot(p - refpt);
  return true;
}

bool SphereSurface::closestPoint(const Vector3d& p, Vector3d& out) const
{
  const Vector3d rel = p - refpt;
  if (rel.norm() < 1e-12) return false;
  out = refpt + rel.normalized() * r;
  return true;
}

bool CylinderSurface::normalAt(const Vector3d& p, Vector3d& out) const
{
  const Vector3d axis = normdir.normalized();
  const Vector3d rel = p - refpt;
  const Vector3d radial = rel - axis * rel.dot(axis);
  if (radial.norm() < 1e-12) return false;
  out = radial.normalized();
  return true;
}

bool ConeSurface::normalAt(const Vector3d& p, Vector3d& out) const
{
  const Vector3d axis = normdir.normalized();
  const Vector3d rel = p - refpt;
  const Vector3d radial = rel - axis * rel.dot(axis);
  if (radial.norm() < 1e-12) return false;
  // grad of |radial| - (r + slope * h)
  out = (radial.normalized() - axis * slope).normalized();
  return true;
}

bool SphereSurface::normalAt(const Vector3d& p, Vector3d& out) const
{
  const Vector3d rel = p - refpt;
  if (rel.norm() < 1e-12) return false;
  out = rel.normalized();
  return true;
}

bool TorusSurface::normalAt(const Vector3d& p, Vector3d& out) const
{
  const Vector3d axis = normdir.normalized();
  const Vector3d rel = p - refpt;
  const Vector3d radial = rel - axis * rel.dot(axis);
  if (radial.norm() < 1e-12) return false;
  const Vector3d ring = refpt + radial.normalized() * r_major;
  if ((p - ring).norm() < 1e-12) return false;
  out = (p - ring).normalized();
  return true;
}

bool SphereSurface::implicitAt(const Vector3d& p, double& f, Vector3d& grad) const
{
  const Vector3d rel = p - refpt;
  f = rel.squaredNorm() - r * r;
  grad = 2 * rel;
  return true;
}

namespace {

/*! |perp|^2 - want^2 about an axis, the radius `want` growing by `slope`: the
 * implicit form a cylinder (slope 0) and a cone share. */
void axialImplicit(const Surface& s, double r, double slope, const Vector3d& p, double& f,
                   Vector3d& grad)
{
  const Vector3d axis = s.normdir.normalized();
  const Vector3d rel = p - s.refpt;
  const double along = rel.dot(axis);
  const Vector3d perp = rel - axis * along;
  const double want = r + slope * along;
  f = perp.squaredNorm() - want * want;
  grad = 2 * perp - 2 * want * slope * axis;
}

}  // namespace

bool CylinderSurface::implicitAt(const Vector3d& p, double& f, Vector3d& grad) const
{
  axialImplicit(*this, r, 0.0, p, f, grad);
  return true;
}

bool ConeSurface::implicitAt(const Vector3d& p, double& f, Vector3d& grad) const
{
  axialImplicit(*this, r, slope, p, f, grad);
  return true;
}

bool PlaneSurface::implicitAt(const Vector3d& p, double& f, Vector3d& grad) const
{
  const Vector3d n = normdir.normalized();
  if (!(n.norm() > 0)) return false;
  f = n.dot(p - refpt);
  grad = n;
  return true;
}

// ---------------------------------------------------------------------------
// The axial family - see AxialSurface in Surface.h.
// ---------------------------------------------------------------------------

void AxialSurface::frame(Vector3d& ref, Vector3d& ref2) const
{
  // AnalyticFeatures::perpendicular, which every consumer measured from before
  // the family answered for itself; the same formula, so the same angles.
  const Vector3d a = axis();
  const Vector3d other = fabs(a[0]) < 0.9 ? Vector3d(1, 0, 0) : Vector3d(0, 1, 0);
  ref = a.cross(other).normalized();
  ref2 = a.cross(ref);
}

double AxialSurface::angle(const Vector3d& p) const
{
  Vector3d ref, ref2;
  frame(ref, ref2);
  const Vector3d rel = p - refpt;
  double t = atan2(rel.dot(ref2), rel.dot(ref));
  if (t < 0) t += 2 * M_PI;
  return t;
}

namespace {

double distanceFromAxis(const Vector3d& p, const Vector3d& base, const Vector3d& axis)
{
  const Vector3d rel = p - base;
  return (rel - axis * axis.dot(rel)).norm();
}

}  // namespace

double CylinderSurface::offset(const Vector3d& p) const
{
  return fabs(distanceFromAxis(p, refpt, axis()) - r);
}

double ConeSurface::offset(const Vector3d& p) const
{
  const double want = radiusAt(p);
  if (want < 0) return std::numeric_limits<double>::max();  // past the apex
  return fabs(distanceFromAxis(p, refpt, axis()) - want) / sqrt(1 + slope * slope);
}

// ---------------------------------------------------------------------------
// Moving a record with its geometry - see Surface::transformed.
// ---------------------------------------------------------------------------

std::shared_ptr<Surface> Surface::transformed(const Transform3d& mat, const Similarity& similarity) const
{
  auto copy = clone();
  return copy->transform(mat, similarity) ? copy : nullptr;
}

namespace {

/*! `into` carrying the declarers `from` had: owners, witnesses, and the role. */
std::shared_ptr<Surface> withDeclarersOfRecord(std::shared_ptr<Surface> into, const Surface& from)
{
  into->owners = from.owners;
  into->witnesses = from.witnesses;
  into->tags = from.tags;
  into->evidence_only = from.evidence_only;
  return into;
}

}  // namespace

std::shared_ptr<Surface> CylinderSurface::transformed(const Transform3d& mat,
                                                      const Similarity& similarity) const
{
  if (auto moved = Surface::transformed(mat, similarity)) return moved;  // a similarity: still circular
  // Any other affine map takes the circle across the axis to an ellipse, and the
  // cylinder to the elliptic one through it: the images of the axis and of two
  // perpendicular radii are its sweep and two conjugate semi-diameters.
  Vector3d ref, ref2;
  frame(ref, ref2);
  const Matrix3d L = mat.linear();
  auto moved =
    std::make_shared<EllipticCylinderSurface>(mat * refpt, L * axis(), L * (ref * r), L * (ref2 * r));
  if (!moved->usable()) return nullptr;
  return withDeclarersOfRecord(moved, *this);
}

std::shared_ptr<Surface> PlaneSurface::transformed(const Transform3d& mat, const Similarity&) const
{
  // A plane is a plane under any affine map: its points move by the map and its
  // normal by the inverse transpose of the linear part. Only a singular map,
  // which flattens space, has no plane to give.
  const Matrix3d L = mat.linear();
  if (!(fabs(L.determinant()) > 0)) return nullptr;
  const Vector3d n = L.inverse().transpose() * normdir;
  if (!(n.norm() > 0)) return nullptr;
  auto moved = std::make_shared<PlaneSurface>(mat * refpt, n.normalized());
  return withDeclarersOfRecord(moved, *this);
}

std::vector<std::shared_ptr<Surface>> transformedSurfaces(
  const std::vector<std::shared_ptr<Surface>>& list, const Transform3d& mat,
  const Similarity& similarity)
{
  std::vector<std::shared_ptr<Surface>> out;
  out.reserve(list.size());
  for (const auto& surface : list) {
    if (auto moved = surface->transformed(mat, similarity)) out.push_back(std::move(moved));
  }
  return out;
}

bool CylinderSurface::quadricForm(Matrix3d& M, Vector3d& origin, double& constant) const
{
  const Vector3d ax = axis();
  M = Matrix3d::Identity() - ax * ax.transpose();
  origin = refpt;
  constant = r * r;
  return true;
}

bool ConeSurface::quadricForm(Matrix3d& M, Vector3d& origin, double& constant) const
{
  if (fabs(slope) < 1e-12) return false;
  const Vector3d ax = axis();
  const double cos_a = 1.0 / sqrt(1.0 + slope * slope);
  M = ax * ax.transpose() - cos_a * cos_a * Matrix3d::Identity();
  origin = refpt + ax * (-r / slope);  // the apex
  constant = 0;
  return true;
}

// ---------------------------------------------------------------------------
// The elliptic cylinder - see Surface.h.
// ---------------------------------------------------------------------------

EllipticCylinderSurface::EllipticCylinderSurface(const Vector3d& centre, const Vector3d& direction,
                                                 const Vector3d& u, const Vector3d& v)
{
  refpt = centre;
  normdir = direction.normalized();
  // Slide the section along the generators onto the plane across the sweep:
  // every point keeps its line, so the surface is unchanged, and the semi-
  // diameters stay conjugate.
  const Vector3d d = normdir;
  const Vector3d uu = u - d * u.dot(d), vv = v - d * v.dot(d);
  // The principal semi-axes of centre + uu cos t + vv sin t are the singular
  // vectors of [uu vv], scaled by the singular values.
  Eigen::Matrix<double, 3, 2> m;
  m.col(0) = uu;
  m.col(1) = vv;
  const Eigen::JacobiSVD<Eigen::Matrix<double, 3, 2>> svd(m, Eigen::ComputeThinU);
  const Eigen::Vector2d sigma = svd.singularValues();
  major = svd.matrixU().col(0) * sigma(0);
  Vector3d across = d.cross(svd.matrixU().col(0));
  minor = across.normalized() * sigma(1);
}

std::shared_ptr<Surface> EllipticCylinderSurface::clone() const
{
  return std::make_shared<EllipticCylinderSurface>(*this);
}

bool EllipticCylinderSurface::usable() const
{
  return std::isfinite(a()) && std::isfinite(b()) && b() > 0 && normdir.norm() > 0;
}

bool EllipticCylinderSurface::transform(const Transform3d& mat, const Similarity&)
{
  // Any affine map keeps it one: the images of the axis and of the two principal
  // semi-axes are the sweep and two conjugate semi-diameters of a section.
  const Matrix3d L = mat.linear();
  const EllipticCylinderSurface moved(mat * refpt, L * normdir, L * major, L * minor);
  if (!moved.usable()) return false;
  refpt = moved.refpt;
  normdir = moved.normdir;
  major = moved.major;
  minor = moved.minor;
  return true;
}

bool EllipticCylinderSurface::sameAs(const Surface& other) const
{
  // One axis line, and three points of each section on the other - an ellipse
  // about a known centre is fixed by three.
  const auto *o = dynamic_cast<const EllipticCylinderSurface *>(&other);
  if (o == nullptr) return false;
  if (!recognition::sameLine(refpt, normdir, o->refpt, o->normdir)) return false;
  auto points = [](const EllipticCylinderSurface& s) {
    return std::vector<Vector3d>{s.refpt + s.major, s.refpt + s.minor,
                                 s.refpt + (s.major + s.minor) * M_SQRT1_2};
  };
  return allOn(*this, points(*o)) && allOn(*o, points(*this));
}

void EllipticCylinderSurface::frame(Vector3d& ref, Vector3d& ref2) const
{
  ref = major.normalized();
  ref2 = minor.normalized();
}

void EllipticCylinderSurface::section(const Vector3d& p, double& x, double& y) const
{
  const Vector3d q = p - refpt;
  const Vector3d across = q - normdir * q.dot(normdir);
  x = across.dot(major.normalized());
  y = across.dot(minor.normalized());
}

double EllipticCylinderSurface::angle(const Vector3d& p) const
{
  // The eccentric anomaly: the ellipse's own parameter, in which equal steps are
  // what the affine image of a regular polygon takes.
  double x = 0, y = 0;
  section(p, x, y);
  double t = atan2(y / b(), x / a());
  if (t < 0) t += 2 * M_PI;
  return t;
}

double EllipticCylinderSurface::signedOffset(const Vector3d& p) const
{
  // F / |grad F| for F = (x/a)^2 + (y/b)^2 - 1: the distance to first order,
  // and for a circle (|q|^2 - r^2) / 2|q|, the radius difference to first order;
  // negative inside.
  double x = 0, y = 0;
  section(p, x, y);
  const double A = a(), B = b();
  const double F = (x / A) * (x / A) + (y / B) * (y / B) - 1;
  const double g = 2 * hypot(x / (A * A), y / (B * B));
  // On the axis the gradient vanishes: as far as a double goes, and inside.
  if (!(g > 0)) return F < 0 ? -std::numeric_limits<double>::max() : std::numeric_limits<double>::max();
  return F / g;
}

double EllipticCylinderSurface::offset(const Vector3d& p) const
{
  return fabs(signedOffset(p));
}

recognition::Residual EllipticCylinderSurface::signedResidual(const Vector3d& p) const
{
  // The distance above, and its operations: the unit sweep (9), q (3), along it
  // (5), off it (6), the two unit semi-axes (18) and the coordinates on them
  // (10), each over its semi-axis and squared (8, with the two norms, 12), the
  // sum less one (2), the gradient's length (8) and the quotient (1) - 82. The
  // coordinates are divided by the semi-axes, so the error in what is measured
  // is magnified by up to a / b before the gradient scales it back: the bound
  // carries that conditioning, which is the ellipse's and not a choice.
  recognition::Residual out;
  out.value = signedOffset(p);
  out.bound = recognition::forwardError(82, (p.norm() + refpt.norm() + a()) * a() / b());
  return out;
}

bool EllipticCylinderSurface::normalAt(const Vector3d& p, Vector3d& out) const
{
  double f = 0;
  Vector3d grad;
  if (!implicitAt(p, f, grad) || !(grad.norm() > 0)) return false;
  out = grad.normalized();
  return true;
}

bool EllipticCylinderSurface::implicitAt(const Vector3d& p, double& f, Vector3d& grad) const
{
  double x = 0, y = 0;
  section(p, x, y);
  const double A = a(), B = b();
  f = (x / A) * (x / A) + (y / B) * (y / B) - 1;
  grad = major.normalized() * (2 * x / (A * A)) + minor.normalized() * (2 * y / (B * B));
  return true;
}

bool EllipticCylinderSurface::quadricForm(Matrix3d& M, Vector3d& origin, double& constant) const
{
  const Vector3d e1 = major.normalized(), e2 = minor.normalized();
  M = e1 * e1.transpose() / (a() * a()) + e2 * e2.transpose() / (b() * b());
  origin = refpt;
  constant = 1;
  return true;
}

// ---------------------------------------------------------------------------
// Envelopes - see Surface::envelope. Each kind says where the part of it a
// facet stands for lies, in closed form, or says nothing.
// ---------------------------------------------------------------------------

namespace {

/*! The box of a finite piece of a cylinder about the line through `base`
 * along the unit `axis`, of radius `r`, between axial positions t0 and t1: the
 * two end circles' boxes joined, each circle reaching r sqrt(1 - axis_k^2)
 * either side of its centre along coordinate k. */
BoundingBox cylinderBox(const Vector3d& base, const Vector3d& axis, double r, double t0, double t1)
{
  BoundingBox box;
  for (const double t : {t0, t1}) {
    const Vector3d c = base + axis * t;
    Vector3d reach;
    for (int k = 0; k < 3; k++) reach[k] = r * std::sqrt(std::max(0.0, 1 - axis[k] * axis[k]));
    box.extend(c - reach);
    box.extend(c + reach);
  }
  return box;
}

}  // namespace

bool Surface::envelope(const std::vector<Vector3d>&, BoundingBox&) const
{
  return false;
}

bool PlaneSurface::envelope(const std::vector<Vector3d>& corners, BoundingBox& box) const
{
  // A facet on a plane is itself.
  box.setEmpty();
  for (const auto& p : corners) box.extend(p);
  return !corners.empty();
}

bool CylinderSurface::envelope(const std::vector<Vector3d>& corners, BoundingBox& box) const
{
  // Its image lies where the facet's corners lie along the axis - the
  // cylinder's closest point keeps a point's axial position - and on the
  // cylinder, so within the finite cylinder over that axial range.
  if (corners.empty()) return false;
  const Vector3d a = normdir.normalized();
  double t0 = std::numeric_limits<double>::infinity(), t1 = -t0;
  for (const auto& p : corners) {
    const double t = (p - refpt).dot(a);
    t0 = std::min(t0, t);
    t1 = std::max(t1, t);
  }
  box = cylinderBox(refpt, a, r, t0, t1);
  return true;
}

bool ConeSurface::envelope(const std::vector<Vector3d>& corners, BoundingBox& box) const
{
  // The part of the cone a facet stands for lies between its corners' levels
  // along the axis, and the frustum between two levels lies within the box of
  // its two end circles - convex, and the apex, where it passes one, on the
  // axis between their centres. The radius at a level is |r + slope t|.
  if (corners.empty()) return false;
  const Vector3d a = normdir.normalized();
  double t0 = std::numeric_limits<double>::infinity(), t1 = -t0;
  for (const auto& p : corners) {
    const double t = (p - refpt).dot(a);
    t0 = std::min(t0, t);
    t1 = std::max(t1, t);
  }
  box = cylinderBox(refpt, a, std::fabs(radiusAt(refpt + a * t0)), t0, t0);
  box.extend(cylinderBox(refpt, a, std::fabs(radiusAt(refpt + a * t1)), t1, t1));
  return true;
}

bool TorusSurface::envelope(const std::vector<Vector3d>&, BoundingBox& box) const
{
  // The whole ring: a point is c + (R + r cos f) u + r sin f a, u a unit
  // vector across the axis a, so along coordinate k it reaches at most
  // R sqrt(1 - a_k^2) + r from the centre.
  const Vector3d a = normdir.normalized();
  Vector3d reach;
  for (int k = 0; k < 3; k++)
    reach[k] = std::fabs(r_major) * std::sqrt(std::max(0.0, 1 - a[k] * a[k])) + std::fabs(r_minor);
  box = BoundingBox(refpt - reach, refpt + reach);
  return true;
}

bool EllipticCylinderSurface::envelope(const std::vector<Vector3d>& corners, BoundingBox& box) const
{
  // Between the corners' levels along the sweep, the sections are the ellipse
  // centre + major cos t + minor sin t moved along it, which along coordinate k
  // reaches sqrt(major_k^2 + minor_k^2) from its centre.
  if (corners.empty()) return false;
  const Vector3d a = normdir.normalized();
  double t0 = std::numeric_limits<double>::infinity(), t1 = -t0;
  for (const auto& p : corners) {
    const double t = (p - refpt).dot(a);
    t0 = std::min(t0, t);
    t1 = std::max(t1, t);
  }
  Vector3d reach;
  for (int k = 0; k < 3; k++) reach[k] = std::sqrt(major[k] * major[k] + minor[k] * minor[k]);
  box.setEmpty();
  for (const double t : {t0, t1}) {
    const Vector3d c = refpt + a * t;
    box.extend(c - reach);
    box.extend(c + reach);
  }
  return true;
}

bool BezierPatchSurface::envelope(const std::vector<Vector3d>&, BoundingBox& box) const
{
  // A Bezier patch lies within the convex hull of its control net, a rational
  // one too where every weight is positive.
  for (const double w : weights) {
    if (!(w > 0)) return false;
  }
  if (net.empty()) return false;
  box.setEmpty();
  for (const auto& p : net) box.extend(p);
  return true;
}

bool SphereSurface::envelope(const std::vector<Vector3d>&, BoundingBox& box) const
{
  box = BoundingBox(refpt - Vector3d(r, r, r), refpt + Vector3d(r, r, r));
  return true;
}

bool SweepSurface::envelope(const std::vector<Vector3d>&, BoundingBox& box) const
{
  // The whole sweep, from its laws. A point of it is refpt + radial (R(u) +
  // dr) + axis (pitch turns u + dz), with R and the profile's (dr, dz) linear
  // between the laws' keys and a profile point between two linear in theirs -
  // so its distance from the axis and its axial position take their extremes
  // at a key and a profile point, and every point lies within the finite
  // cylinder those extremes span.
  const std::size_t n = laws.points();
  if (n == 0) return false;
  std::vector<double> keys = laws.corners();
  keys.push_back(0);
  keys.push_back(1);
  double reach = 0, lo = std::numeric_limits<double>::infinity(), hi = -lo;
  for (const double u : keys) {
    for (std::size_t i = 0; i < n; i++) {
      const Vector2d p = laws.pointAt(i, u);
      reach = std::max(reach, std::fabs(laws.radiusAt(u) + p[0]));
      const double along = pitch * turns * u + p[1];
      lo = std::min(lo, along);
      hi = std::max(hi, along);
    }
  }
  box = cylinderBox(refpt, normdir.normalized(), reach, lo, hi);
  return true;
}
