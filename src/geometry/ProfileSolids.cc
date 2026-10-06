/*! The solids an extruder's profile bounds - see ProfileSolids.h. */
#include "geometry/ProfileSolids.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "geometry/MachinePrecision.h"
#include "geometry/Surface.h"
#include "geometry/SweptSurfaces.h"

using recognition::forwardError;

namespace {

std::shared_ptr<const Surface> moved(const std::shared_ptr<const Surface>& s, const Transform3d& mat,
                                     const Similarity& similarity)
{
  if (s == nullptr) return nullptr;
  auto copy = s->clone();
  // A record the transform cannot carry is no record afterwards: the region
  // still answers where a point is, and names no surface for it.
  return copy->transform(mat, similarity) ? std::shared_ptr<const Surface>(copy) : nullptr;
}

SolidRegion::Where of(ProfileRegion::Where w)
{
  switch (w) {
  case ProfileRegion::Where::In:  return SolidRegion::Where::In;
  case ProfileRegion::Where::Out: return SolidRegion::Where::Out;
  case ProfileRegion::Where::On:  return SolidRegion::Where::On;
  default:                        return SolidRegion::Where::Unknown;
  }
}

}  // namespace

SweptRecords sweptRecords(const Profile2d& profile, const SweptFaces& faces,
                          const std::vector<std::shared_ptr<Surface>>& surfaces,
                          std::size_t first_record)
{
  auto record = [&](int index) -> std::shared_ptr<const Surface> {
    if (index < 0 || first_record + std::size_t(index) >= surfaces.size()) return nullptr;
    const auto& s = surfaces[first_record + std::size_t(index)];
    return s != nullptr ? std::shared_ptr<const Surface>(s->clone()) : nullptr;
  };
  SweptRecords out;
  for (const auto& loop : profile.loops) {
    out.walls.emplace_back();
    for (const ProfileEdge& e : loop) {
      const auto it = e.id != 0 ? faces.walls.find(e.id) : faces.walls.end();
      out.walls.back().push_back(it != faces.walls.end() ? record(it->second) : nullptr);
    }
  }
  out.near_end = record(faces.near_end);
  out.far_end = record(faces.far_end);
  return out;
}

SweptRecords SweptRecords::transformed(const Transform3d& mat, const Similarity& similarity) const
{
  SweptRecords out;
  out.walls.reserve(walls.size());
  for (const auto& loop : walls) {
    out.walls.emplace_back();
    for (const auto& s : loop) out.walls.back().push_back(moved(s, mat, similarity));
  }
  out.near_end = moved(near_end, mat, similarity);
  out.far_end = moved(far_end, mat, similarity);
  return out;
}

FramedRegion::FramedRegion(std::shared_ptr<const ProfileRegion> profile, SweptRecords records,
                           const Transform3d& frame)
  : profile_(std::move(profile)), records_(std::move(records)), frame_(frame), to_local_(frame.inverse())
{
}

Vector3d FramedRegion::local(const Vector3d& p, double& err) const
{
  // The inverse applied to p: each coordinate a dot product of a row with p,
  // and the translation - bounded by the rows' size times p's, and the map's
  // own rounding when it was inverted, which the same bound covers.
  const double reach = to_local_.linear().norm() * p.norm() + to_local_.translation().norm();
  err = forwardError(24, reach);
  return to_local_ * p;
}

SolidRegion::Answer FramedRegion::onWall(const ProfileRegion::Answer& a) const
{
  Answer out{of(a.where), {}};
  if (a.where != ProfileRegion::Where::On) return out;
  if (a.loop >= 0 && std::size_t(a.loop) < records_.walls.size() &&
      std::size_t(a.edge) < records_.walls[a.loop].size() && records_.walls[a.loop][a.edge] != nullptr) {
    out.on.push_back(records_.walls[a.loop][a.edge].get());
  }
  return out;
}

SolidRegion::Answer FramedRegion::onEnd(const ProfileRegion::Answer& a, bool far) const
{
  // On an end's plane: on the solid where the profile there holds the point.
  if (a.where == ProfileRegion::Where::Out) return Answer{Where::Out, {}};
  if (a.where == ProfileRegion::Where::Unknown) return Answer{};
  Answer out = onWall(a);
  out.where = Where::On;
  const auto& end = far ? records_.far_end : records_.near_end;
  if (end != nullptr) out.on.insert(out.on.begin(), end.get());
  return out;
}

bool FramedRegion::extent(BoundingBox& out) const
{
  BoundingBox box;
  localBox(box);
  if (box.isEmpty()) return false;
  out.setEmpty();
  for (int k = 0; k < 8; k++) {
    const Vector3d c((k & 1) ? box.max()[0] : box.min()[0], (k & 2) ? box.max()[1] : box.min()[1],
                     (k & 4) ? box.max()[2] : box.min()[2]);
    out.extend(frame_ * c);
  }
  return true;
}

RevolvedRegion::RevolvedRegion(std::shared_ptr<const ProfileRegion> profile, double start, double angle,
                               SweptRecords records, const Transform3d& frame)
  : FramedRegion(std::move(profile), std::move(records), frame),
    // A turn the other way is the same solid from its other end.
    start_(angle < 0 ? start + angle : start),
    angle_(std::fabs(angle))
{
  if (angle < 0) std::swap(records_.near_end, records_.far_end);
}

SolidRegion::Answer RevolvedRegion::classify(const Vector3d& p) const
{
  double err = 0;
  const Vector3d l = local(p, err);
  const double rho = std::hypot(l[0], l[1]);
  // On the axis no angle says where the point is about it.
  if (rho <= err) return Answer{};
  const ProfileRegion::Answer a =
    profile_->classify(Vector2d(rho, l[2]), 2 * err + forwardError(4, rho + std::fabs(l[2])));
  if (a.where == ProfileRegion::Where::Unknown) return Answer{};
  if (angle_ >= 360) return onWall(a);
  // How far round from the start, and that angle's error.
  const double theta = std::atan2(l[1], l[0]) * 180 / M_PI;
  double d = std::fmod(theta - start_, 360.0);
  if (d < 0) d += 360;
  const double d_err =
    (err / rho + forwardError(8, 1.0)) * 180 / M_PI + forwardError(4, std::fabs(start_) + 360);
  const bool at_start = d <= d_err || d >= 360 - d_err, at_stop = std::fabs(d - angle_) <= d_err;
  if (at_start || at_stop) return onEnd(a, !at_start);
  if (d > angle_) return Answer{Where::Out, {}};
  return onWall(a);
}

std::shared_ptr<const SolidRegion> RevolvedRegion::transformed(const Transform3d& mat,
                                                               const Similarity& similarity) const
{
  if (mat.linear().determinant() == 0) return nullptr;
  // Built from its stated start and turn again: the records swap back only if
  // the turn was given the other way, and it is stored the positive way.
  return std::make_shared<RevolvedRegion>(profile_, start_, angle_,
                                          records_.transformed(mat, similarity), mat * frame_);
}

void RevolvedRegion::localBox(BoundingBox& box) const
{
  const double r = profile_->reach();
  box = BoundingBox(Vector3d(-r, -r, -r), Vector3d(r, r, r));
}

ExtrudedRegion::ExtrudedRegion(std::shared_ptr<const ProfileRegion> profile, double scale_x,
                               double scale_y, SweptRecords records, const Transform3d& frame)
  : FramedRegion(std::move(profile), std::move(records), frame), scale_x_(scale_x), scale_y_(scale_y)
{
}

SolidRegion::Answer ExtrudedRegion::classify(const Vector3d& p) const
{
  double err = 0;
  const Vector3d l = local(p, err);
  const double t = l[2];
  if (t < -err || t > 1 + err) return Answer{Where::Out, {}};
  const double tc = std::clamp(t, 0.0, 1.0);
  const double sx = 1 + (scale_x_ - 1) * tc, sy = 1 + (scale_y_ - 1) * tc;
  // Where the far end is scaled to a point the profile there has no extent to
  // ask in.
  const double s_err = (std::fabs(scale_x_ - 1) + std::fabs(scale_y_ - 1)) * err + forwardError(4, 2.0);
  if (std::fabs(sx) <= s_err || std::fabs(sy) <= s_err) return Answer{};
  const Vector2d q(l[0] / sx, l[1] / sy);
  const double q_err =
    (err + s_err * q.norm()) / std::min(std::fabs(sx), std::fabs(sy)) + forwardError(4, q.norm());
  const ProfileRegion::Answer a = profile_->classify(q, q_err);
  if (a.where == ProfileRegion::Where::Unknown) return Answer{};
  if (std::fabs(t) <= err) return onEnd(a, false);
  if (std::fabs(t - 1) <= err) return onEnd(a, true);
  return onWall(a);
}

std::shared_ptr<const SolidRegion> ExtrudedRegion::transformed(const Transform3d& mat,
                                                               const Similarity& similarity) const
{
  if (mat.linear().determinant() == 0) return nullptr;
  return std::make_shared<ExtrudedRegion>(profile_, scale_x_, scale_y_,
                                          records_.transformed(mat, similarity), mat * frame_);
}

void ExtrudedRegion::localBox(BoundingBox& box) const
{
  const double r = profile_->reach() * std::max({1.0, std::fabs(scale_x_), std::fabs(scale_y_)});
  box = BoundingBox(Vector3d(-r, -r, 0), Vector3d(r, r, 1));
}
