#pragma once

/*! The exact 2D region a profile's makers declared.
 *
 * A profile's outlines are polygons, but an edge its maker named a chord of a
 * recorded circle or ellipse stands for that curve (Profile2d.h), so the region
 * the profile declares is the polygon with each such chord replaced by its minor
 * arc. Between a chord and its arc lies a segment - the disc cut by the chord's
 * line, on the side away from the centre - and the region is the polygon with
 * every segment added where the arc bulges outwards and taken away where it
 * bulges in: a point is in it where it is in the polygon XOR in an odd number of
 * segments. That needs no curve crossed by a ray, and only one fact per edge.
 *
 * A chord is no boundary. A point within its error of one is decided by which
 * way that arc bulges - the polygon's interior on the centre's side means the
 * arc bulges out, and the chord lies inside - so the polygon's parity and the
 * segment's cannot disagree there by rounding.
 *
 * Answered exactly, against the forward error the caller states for the point
 * and what the region's own arithmetic adds; a Bezier chord, whose region needs
 * its polynomial, is not answered (exact() is false).
 */

#include <vector>

#include "geometry/Profile2d.h"
#include "geometry/linalg.h"

class ProfileRegion
{
public:
  enum class Where { In, Out, On, Unknown };
  struct Answer {
    Where where = Where::Unknown;
    /*! Where On: the edge of the profile the point is on, as an index into
     * `profile.loops` and that loop; -1 otherwise. */
    int loop = -1, edge = -1;
  };

  explicit ProfileRegion(Profile2d profile);

  /*! Whether every edge is a line, or a chord of a recorded circle or ellipse:
   * the region is then the one this answers for. */
  [[nodiscard]] bool exact() const { return exact_; }
  /*! Where `q` is, `err` being how far the caller's `q` may be from the point
   * it stands for. */
  [[nodiscard]] Answer classify(const Vector2d& q, double err) const;
  [[nodiscard]] const Profile2d& profile() const { return profile_; }
  /*! How far from the origin the profile reaches - its vertices and its
   * curves: what the region's own rounding is measured against. */
  [[nodiscard]] double reach() const { return reach_; }

private:
  Profile2d profile_;
  bool exact_ = true;
  // +1 where each outline has the region on its left, -1 on its right: the
  // sign of the region's area, summed over every outline.
  double side_ = 1;
  double reach_ = 0;
};
