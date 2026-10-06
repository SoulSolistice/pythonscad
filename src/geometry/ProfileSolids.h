#pragma once

/*! The solids an extruder's profile bounds: turned about an axis
 * (rotate_extrude), or carried along a line (linear_extrude).
 *
 * Each is the profile's exact region (ProfileRegion) swept, and states it the
 * way the model built it: a point is taken back into the sweep's own
 * coordinates - radius and height about the axis, or the profile's plane and
 * the station along the line - and asked there. The map back is the inverse
 * of the frame the sweep was placed by, and every transform the geometry goes
 * through afterwards is composed into that frame rather than re-expressed: a
 * point is classified by carrying it back, so no transform with an inverse
 * makes the leaf opaque.
 *
 * Answered against the forward error of carrying the point back and of the
 * region's own arithmetic, never a tolerance. On, each names the record its
 * sweep declared for the edge or end the point is on (SweptFaces), so that
 * existence can match it with the record a facet was made on.
 */

#include <memory>
#include <vector>

#include "geometry/ProfileRegion.h"
#include "geometry/SolidTree.h"
#include "geometry/linalg.h"

class Surface;
struct SweptFaces;

/*! What a swept region keeps of its sweep's records: per profile edge, the
 * record its wall was made on (or null), and the two end records (or null). */
struct SweptRecords {
  std::vector<std::vector<std::shared_ptr<const Surface>>> walls;  // as ProfileRegion::profile().loops
  std::shared_ptr<const Surface> near_end, far_end;

  [[nodiscard]] SweptRecords transformed(const Transform3d& mat, const Similarity& similarity) const;
};

/*! What a swept solid's region names when a point is on it: per edge of
 * `profile`, the record its wall was made on (SweptFaces::walls, by the edge's
 * name), and the two end records - each a copy of the record in `surfaces`
 * from `first_record` on, or null where the sweep made none. */
SweptRecords sweptRecords(const Profile2d& profile, const SweptFaces& faces,
                          const std::vector<std::shared_ptr<Surface>>& surfaces,
                          std::size_t first_record);

/*! A region placed by an affine frame: what both swept solids share. */
class FramedRegion : public SolidRegion
{
public:
  bool extent(BoundingBox& out) const override;

protected:
  FramedRegion(std::shared_ptr<const ProfileRegion> profile, SweptRecords records,
               const Transform3d& frame);
  /*! `p` in the sweep's own coordinates, and how far that may be from exact. */
  [[nodiscard]] Vector3d local(const Vector3d& p, double& err) const;
  /*! The answer for a point on wall (loop, edge), or on an end. */
  [[nodiscard]] Answer onWall(const ProfileRegion::Answer& a) const;
  [[nodiscard]] Answer onEnd(const ProfileRegion::Answer& a, bool far) const;
  /*! The local box the solid lies within; extent() places its corners. */
  virtual void localBox(BoundingBox& box) const = 0;

  std::shared_ptr<const ProfileRegion> profile_;
  SweptRecords records_;
  Transform3d frame_, to_local_;
};

/*! A profile turned about z from +x, through `angle` degrees from `start` - a
 * full turn closing on itself, a partial one ending in the two half-planes. */
class RevolvedRegion : public FramedRegion
{
public:
  RevolvedRegion(std::shared_ptr<const ProfileRegion> profile, double start, double angle,
                 SweptRecords records, const Transform3d& frame = Transform3d::Identity());
  [[nodiscard]] Answer classify(const Vector3d& p) const override;
  [[nodiscard]] std::shared_ptr<const SolidRegion> transformed(
    const Transform3d& mat, const Similarity& similarity) const override;

private:
  void localBox(BoundingBox& box) const override;
  double start_, angle_;
};

/*! A profile in its own plane carried along a line: at station t in [0, 1]
 * the profile scaled by (1 + (sx - 1) t, 1 + (sy - 1) t) about its origin -
 * local coordinates (u, v, t) with (u, v) the scaled profile's. */
class ExtrudedRegion : public FramedRegion
{
public:
  ExtrudedRegion(std::shared_ptr<const ProfileRegion> profile, double scale_x, double scale_y,
                 SweptRecords records, const Transform3d& frame);
  [[nodiscard]] Answer classify(const Vector3d& p) const override;
  [[nodiscard]] std::shared_ptr<const SolidRegion> transformed(
    const Transform3d& mat, const Similarity& similarity) const override;

private:
  void localBox(BoundingBox& box) const override;
  double scale_x_, scale_y_;
};
