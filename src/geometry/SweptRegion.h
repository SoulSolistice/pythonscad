#pragma once

/*! The solid a declared sweep bounds.
 *
 * A sweep of a closed profile is closed all the way round, and its two ends
 * are the profile at t = 0 and t = 1 - each a closed curve in its own
 * meridional plane, bounding a flat region there. The surface and those two
 * regions are a closed boundary, so the declaration states the volume it
 * encloses with nothing added: the points whose station t, read off their
 * angle and turn, lies in [0, 1] and whose offset (dr, dz) in the meridional
 * plane there lies inside the profile at t. That is what this answers, exactly
 * - against the forward error of computing it, never a tolerance.
 *
 * doc/step-recognition-design.md, "Existence is a D0 question too";
 * doc/step-export-wip.md, item 42.
 */

#include <memory>

#include "geometry/SolidTree.h"
#include "geometry/Surface.h"
#include "geometry/linalg.h"

class SweptRegion : public SolidRegion
{
public:
  explicit SweptRegion(std::shared_ptr<const SweepSurface> sweep);

  [[nodiscard]] Answer classify(const Vector3d& p) const override;
  [[nodiscard]] std::shared_ptr<const SolidRegion> transformed(
    const Transform3d& mat, const Similarity& similarity) const override;
  bool extent(BoundingBox& out) const override;

  /*! The plane an end of the sweep lies in: the meridional plane at t = 0, or
   * at t = 1 - where the profile there bounds the solid. */
  [[nodiscard]] const PlaneSurface& end(bool last) const { return last ? *end1_ : *end0_; }
  [[nodiscard]] const SweepSurface& sweep() const { return *sweep_; }

private:
  /*! Where (dr, dz) is against the profile at `t`, within `err` of an edge
   * being On. */
  [[nodiscard]] Where inProfile(double t, const Vector2d& q, double err) const;

  std::shared_ptr<const SweepSurface> sweep_;
  std::shared_ptr<const PlaneSurface> end0_, end1_;
  // How fast the laws move with t - the largest change of a profile point's
  // and of the radius over a key span, per unit t - and how far a profile
  // point reaches: what an error in t, and in computing a point, is worth.
  double profile_slope_ = 0, radius_slope_ = 0, profile_reach_ = 0;
};
