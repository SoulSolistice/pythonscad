#pragma once

/*! The solid a faceted generator's faces bound, as its declarations state them.
 *
 * `polyhedron()` states its solid by its faces: each is the model's own exact
 * plane, unless a declaration names it a piece of a recorded surface - a
 * declared grid, the interpolant through the stations a thread was generated
 * at. The solid is then the one those records and planes bound, and a point is
 * asked of them:
 *
 *  - far from every face - beyond the band a named face's record may lie from
 *    the face, the record's own closed-form tessellation bound - the faces'
 *    winding number decides, their solid angles summing to a whole number of
 *    turns;
 *  - on a record, within its evaluation bound of the point its projection
 *    finds, it is On that record;
 *  - otherwise near a named face, the side of that point decides: the
 *    projection's point is a stationary point of the distance, so the point
 *    lies along the surface's normal there, and the face's outward normal is
 *    within a right angle of it;
 *  - near a plane face, or near two faces of different kinds at once, it is On
 *    the plane or Unknown - never a guess.
 *
 * Stated only where every named face is named for a declared grid, and the
 * faces close (every edge used twice): see ManifoldUtils::facetedSolid.
 */

#include <memory>
#include <vector>

#include "geometry/SolidTree.h"
#include "geometry/linalg.h"

class GridSurface;

class FacetedRegion : public SolidRegion
{
public:
  struct Face {
    Vector3d a, b, c;
    /*! The record the face is a piece of, or null: its own plane. */
    std::shared_ptr<const GridSurface> record;
  };

  explicit FacetedRegion(std::vector<Face> faces);

  [[nodiscard]] Answer classify(const Vector3d& p) const override;
  /*! A point on a grid this solid is bounded by, by construction, is on it:
   * the grid between the solid's ends is its boundary there. */
  [[nodiscard]] Answer classifyOn(const Vector3d& p,
                                  const std::vector<const Surface *>& on) const override;
  [[nodiscard]] std::shared_ptr<const SolidRegion> transformed(
    const Transform3d& mat, const Similarity& similarity) const override;
  bool extent(BoundingBox& out) const override;
  /*! Whether the faces bound a solid: every edge used twice, once each way,
   * and a volume to have an inside. */
  [[nodiscard]] bool closed() const { return closed_; }

private:
  std::vector<Face> faces_;
  bool closed_ = false;
  double orientation_ = 1;  // the sign of the faces' signed volume
  double reach_ = 0;        // how far from the origin any vertex is
  BoundingBox box_;         // the faces' box, grown by the widest band: outside it, out
};
