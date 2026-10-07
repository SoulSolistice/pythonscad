#pragma once

/*! The solid a faceted generator's faces bound, as its declarations state them.
 *
 * `polyhedron()` states its solid by its faces: each is the model's own exact
 * plane, unless a declaration names it a piece of a recorded surface - a
 * declared grid, the interpolant through the stations a thread was generated
 * at, or a declared cylinder a channel's walls were swept round. The solid is
 * then the one those records and planes bound, and a point is asked of them:
 *
 *  - far from every face - beyond the band a named face's record may lie from
 *    the face, in closed form (Surface::facetBound; a grid's own tessellation
 *    bound) - the faces' winding number decides, their solid angles summing
 *    to a whole number of turns;
 *  - near a named face, the point of its record nearest it decides, and only
 *    where that point is in the piece of the record the faces stand for: the
 *    record's normal there meets a face named for it, within that face's band.
 *    On the record there, it is On; off it, the side decides - the nearest
 *    point is a stationary point of the distance, so the point lies along the
 *    record's normal there, and the face's outward normal is within a right
 *    angle of it - unless a plane face is as near as the band, which the
 *    record's side does not decide;
 *  - near a plane face alone it is On it; near faces of two records, or near
 *    a record beyond the piece its faces stand for, Unknown - never a guess.
 *
 * Stated only where every named face's band is stated, and the faces close
 * (every edge used twice): see ManifoldUtils::facetedSolid.
 */

#include <memory>
#include <vector>

#include "geometry/SolidTree.h"
#include "geometry/linalg.h"

class Surface;

class FacetedRegion : public SolidRegion
{
public:
  struct Face {
    Vector3d a, b, c;
    /*! The record the face is a piece of, or null: its own plane. */
    std::shared_ptr<const Surface> record;
  };

  explicit FacetedRegion(std::vector<Face> faces);

  [[nodiscard]] Answer classify(const Vector3d& p) const override;
  /*! A point on a record this solid is bounded by, by construction, is on it
   * where it is in the piece of the record the faces stand for - without
   * being taken onto the record again. */
  [[nodiscard]] Answer classifyOn(const Vector3d& p,
                                  const std::vector<const Surface *>& on) const override;
  [[nodiscard]] std::shared_ptr<const SolidRegion> transformed(
    const Transform3d& mat, const Similarity& similarity) const override;
  bool extent(BoundingBox& out) const override;
  /*! Whether the faces bound a solid: every edge used twice, once each way, a
   * volume to have an inside, and every named face's band stated. */
  [[nodiscard]] bool closed() const { return closed_; }

private:
  /*! Whether `q`, on `record`, is in the piece of it the faces named for it
   * stand for, as exactly as `err` lets the arithmetic say. */
  [[nodiscard]] bool inPiece(const Surface *record, const Vector3d& q, double err) const;

  std::vector<Face> faces_;
  std::vector<double> band_;  // how far each face's record may lie from it
  bool closed_ = false;
  double orientation_ = 1;  // the sign of the faces' signed volume
  double reach_ = 0;        // how far from the origin any vertex is
  BoundingBox box_;         // the faces' box, grown by the widest band: outside it, out
};
