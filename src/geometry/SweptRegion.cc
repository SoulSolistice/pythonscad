/*! The solid a declared sweep bounds - see SweptRegion.h. */
#include "geometry/SweptRegion.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>
#include <vector>

#include "geometry/MachinePrecision.h"

using recognition::forwardError;

SweptRegion::SweptRegion(std::shared_ptr<const SweepSurface> sweep) : sweep_(std::move(sweep))
{
  const SweepSurface& s = *sweep_;
  const SweepLaws& laws = s.laws;
  // The ends: the meridional plane through the axis at t = 0, where theta is
  // 0, and at t = 1, a whole number of turns or not further round.
  const Vector3d other = s.normdir.cross(s.ref);
  const double last = 2 * M_PI * s.turns;
  end0_ = std::make_shared<PlaneSurface>(s.refpt, s.normdir.cross(s.ref));
  end1_ =
    std::make_shared<PlaneSurface>(s.refpt, s.normdir.cross(s.ref * cos(last) + other * sin(last)));
  for (std::size_t k = 0; k + 1 < laws.profile_at.size(); k++) {
    const double dt = laws.profile_at[k + 1] - laws.profile_at[k];
    for (std::size_t i = 0; i < laws.points(); i++) {
      profile_slope_ =
        std::max(profile_slope_, (laws.profile[k + 1][i] - laws.profile[k][i]).norm() / dt);
    }
  }
  for (std::size_t k = 0; k + 1 < laws.radius_at.size(); k++) {
    const double dt = laws.radius_at[k + 1] - laws.radius_at[k];
    radius_slope_ = std::max(radius_slope_, std::fabs(laws.radius[k + 1] - laws.radius[k]) / dt);
  }
  for (const auto& key : laws.profile) {
    for (const Vector2d& q : key) profile_reach_ = std::max(profile_reach_, q.norm());
  }
}

SolidRegion::Where SweptRegion::inProfile(double t, const Vector2d& q, double err) const
{
  const SweepLaws& laws = sweep_->laws;
  const std::size_t n = laws.points();
  std::vector<Vector2d> at(n);
  for (std::size_t i = 0; i < n; i++) at[i] = laws.pointAt(i, t);
  // On an edge, as nearly as (dr, dz) and the profile are known.
  for (std::size_t i = 0; i < n; i++) {
    const Vector2d a = at[i], b = at[(i + 1) % n];
    const Vector2d e = b - a;
    const double len2 = e.squaredNorm();
    const double s = len2 > 0 ? std::clamp((q - a).dot(e) / len2, 0.0, 1.0) : 0.0;
    if ((a + e * s - q).norm() <= err + forwardError(8, q.norm() + a.norm() + b.norm()))
      return Where::On;
  }
  // Off every edge by more than either is known to, so the crossings along
  // +dr are counted the same however the arithmetic rounds them.
  bool inside = false;
  for (std::size_t i = 0; i < n; i++) {
    const Vector2d a = at[i], b = at[(i + 1) % n];
    if ((a[1] > q[1]) == (b[1] > q[1])) continue;
    const double x = a[0] + (q[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
    if (x > q[0]) inside = !inside;
  }
  return inside ? Where::In : Where::Out;
}

void SweptRegion::stateSides(double t, const Vector2d& q, double err, double t_err, bool at_start,
                             bool at_end, Answer& a) const
{
  const SweepSurface& s = *sweep_;
  const SweepLaws& laws = s.laws;
  // At a key of a law the sweep turns a corner along t: two pieces, whose
  // sides this does not state.
  for (const double key : laws.corners()) {
    if (std::fabs(t - key) <= t_err) return;
  }
  const std::size_t n = laws.points();
  std::vector<Vector2d> at(n);
  for (std::size_t i = 0; i < n; i++) at[i] = laws.pointAt(i, t);
  // The profile's orientation in (dr, dz): which side of an edge is out.
  double area = 0;
  for (std::size_t i = 0; i < n; i++) {
    const Vector2d& u = at[i];
    const Vector2d& w = at[(i + 1) % n];
    area += u[0] * w[1] - w[0] * u[1];
  }
  if (area == 0) return;
  const double turn_sign = area > 0 ? 1 : -1;
  // The edges the point is on, each with where along it.
  std::vector<std::pair<std::size_t, double>> edges;
  for (std::size_t i = 0; i < n; i++) {
    const Vector2d e = at[(i + 1) % n] - at[i];
    const double len2 = e.squaredNorm();
    const double f = len2 > 0 ? std::clamp((q - at[i]).dot(e) / len2, 0.0, 1.0) : 0.0;
    if ((at[i] + e * f - q).norm() <=
        err + forwardError(8, q.norm() + at[i].norm() + at[(i + 1) % n].norm()))
      edges.emplace_back(i, f);
  }
  const Vector3d other = s.normdir.cross(s.ref);
  const double theta = 2 * M_PI * s.turns * t;
  const Vector3d radial = s.ref * cos(theta) + other * sin(theta);
  const int side = at_end ? -1 : 1;  // the slopes of the span the sweep is on
  std::vector<Side> sides;
  for (const auto& [i, f] : edges) {
    Vector3d P, Pt, Ps;
    s.derivativesAt(t, i, f, side, P, Pt, Ps);
    Vector3d m = Pt.cross(Ps);
    if (!(m.norm() > 0)) return;
    // Outward: the edge's own normal in the meridional plane - (e_z, -e_r)
    // on a profile counter clockwise in (dr, dz) - which the normal's
    // meridional part, (r + q_r) theta' times it, cannot oppose off the axis.
    const Vector2d e = at[(i + 1) % n] - at[i];
    const Vector2d out2(e[1] * turn_sign, -e[0] * turn_sign);
    const Vector3d out3 = radial * out2[0] + s.normdir * out2[1];
    if (m.dot(out3) < 0) m = -m;
    sides.push_back(Side{m.normalized(), sweep_.get()});
  }
  bool convex = true;
  if (edges.size() == 2) {
    // A corner of the profile: convex where it turns the profile's way.
    const std::size_t i0 = edges[0].first, i1 = edges[1].first;
    const std::size_t into = (i0 + 1) % n == i1 ? i0 : i1, from = into == i0 ? i1 : i0;
    if ((into + 1) % n != from) return;  // two edges that do not meet: not one corner
    const Vector2d e1 = at[(into + 1) % n] - at[into], e2 = at[(from + 1) % n] - at[from];
    const double cross = (e1[0] * e2[1] - e1[1] * e2[0]) * turn_sign;
    if (std::fabs(cross) <= forwardError(8, e1.squaredNorm() + e2.squaredNorm())) return;
    convex = cross > 0;
  } else if (edges.size() > 2) {
    return;
  }
  if (at_start || at_end) {
    // An end cuts the sweep: its plane, outward away from the sweep, and in
    // every half-space at once - which a concave corner of the profile is not.
    if (!convex) return;
    const PlaneSurface& plane = end(at_end);
    const Vector3d along = s.normdir.cross(radial) * (s.turns > 0 ? 1 : -1);
    const Vector3d n = plane.normdir.dot(along) > 0 ? plane.normdir : Vector3d(-plane.normdir);
    sides.push_back(Side{(at_end ? n : Vector3d(-n)).normalized(), &plane});
  }
  if (sides.empty()) return;
  a.sides = std::move(sides);
  a.all = convex;
}

SolidRegion::Answer SweptRegion::classify(const Vector3d& p) const
{
  const SweepSurface& s = *sweep_;
  const SweepLaws& laws = s.laws;
  // As SweepSurface::localCoords reads a point, with what each step costs.
  const Vector3d rel = p - s.refpt;
  const double along = rel.dot(s.normdir);
  const Vector3d radial = rel - s.normdir * along;
  const double rho = radial.norm();
  const double pos_err = forwardError(16, p.norm() + s.refpt.norm());
  // On the axis no angle says which station the point is at.
  if (rho <= pos_err) return Answer{};
  const Vector3d other = s.normdir.cross(s.ref);
  const double frac = atan2(radial.dot(other), radial.dot(s.ref)) / (2 * M_PI);
  // atan2's own rounding, and its arguments' error seen across rho.
  const double frac_err = forwardError(8, 1.0) + pos_err / rho;
  const double turns_size = std::fabs(s.turns);

  // Every turn whose station could hold the point: a helix passes over the
  // same angle once a turn, and each pass is asked.
  const double first = std::floor(std::min(0.0, s.turns) - frac) - 1;
  const double final_k = std::ceil(std::max(0.0, s.turns) - frac) + 1;
  bool any_in = false;
  int ons = 0;
  std::vector<const Surface *> on;
  // The one pass a point on the boundary is on, and where on it.
  double on_t = 0, on_err = 0, on_t_err = 0;
  Vector2d on_q(0, 0);
  bool on_start = false, on_end = false;
  for (double k = first; k <= final_k; k++) {
    const double turn = frac + k;
    const double turn_err = frac_err + forwardError(2, std::fabs(turn));
    const double t_raw = turn / s.turns;
    const double t_err = (turn_err + forwardError(2, std::fabs(turn))) / turns_size;
    if (t_raw < -t_err || t_raw > 1 + t_err) continue;
    const double t = std::clamp(t_raw, 0.0, 1.0);
    const double r = laws.radiusAt(t);
    const Vector2d q(rho - r, along - turn * s.pitch);
    // How far (dr, dz) and the profile at t may each be from where they are
    // computed: the position's error, the angle's carried along the pitch and
    // through the laws' slopes, and each rounding on the way.
    const double err =
      2 * pos_err + std::fabs(s.pitch) * turn_err +
      forwardError(8, std::fabs(r) + rho + std::fabs(along) + std::fabs(turn * s.pitch)) +
      (profile_slope_ + radius_slope_) * t_err + forwardError(8, profile_reach_);
    const Where w = inProfile(t, q, err);
    // At an end, within the error of t, the point is on the end's plane: on
    // the solid's boundary where the profile there holds it, off it otherwise.
    const bool at_start = std::fabs(t_raw) <= t_err, at_end = std::fabs(t_raw - 1) <= t_err;
    if (at_start || at_end) {
      if (w == Where::Out) continue;
      ons++;
      on.push_back(at_start ? end0_.get() : end1_.get());
      if (w == Where::On) on.push_back(sweep_.get());
      std::tie(on_t, on_q, on_err, on_t_err, on_start, on_end) =
        std::tuple(t, q, err, t_err, at_start, at_end);
      continue;
    }
    if (w == Where::In) any_in = true;
    if (w == Where::On) {
      ons++;
      on.push_back(sweep_.get());
      std::tie(on_t, on_q, on_err, on_t_err, on_start, on_end) =
        std::tuple(t, q, err, t_err, false, false);
    }
  }
  if (any_in) return Answer{Where::In, {}};
  // On two passes at once - two turns touching - is a point on two surfaces,
  // which needs the side each keeps; not one this answers.
  if (ons > 1) return Answer{};
  if (ons == 1) {
    Answer a{Where::On, on};
    stateSides(on_t, on_q, on_err, on_t_err, on_start, on_end, a);
    return a;
  }
  return Answer{Where::Out, {}};
}

std::shared_ptr<const SolidRegion> SweptRegion::transformed(const Transform3d& mat,
                                                            const Similarity& similarity) const
{
  auto moved = std::dynamic_pointer_cast<SweepSurface>(sweep_->clone());
  if (moved == nullptr || !moved->transform(mat, similarity)) return nullptr;
  return std::make_shared<SweptRegion>(std::move(moved));
}

bool SweptRegion::extent(BoundingBox& out) const
{
  return sweep_->envelope({}, out);
}
