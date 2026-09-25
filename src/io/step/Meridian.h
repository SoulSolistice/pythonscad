#pragma once

/*! A declared sweep and a cylinder or cone about the same axis, seen where both
 * are simple: in the sweep's meridian half-plane at one of its stations.
 *
 * There the sweep's profile is a polygon - each edge a segment, its vertices
 * moving linearly in t between the keys of the laws (SweepLaws) - and the other
 * surface is the line rho = r + slope h. So everything about where the two
 * cross is a line meeting a line: a point of the crossing at a station is one
 * linear equation in the edge's parameter, and where a profile vertex - a
 * crease of the sweep - reaches the other surface is one linear equation in t
 * per interval between keys. Exact to rounding, and conditioned by the angle
 * between the lines in that plane rather than between the surfaces, which is
 * what a sweep fading into the surface it grows from needs.
 *
 * The closed form doc/step-export-wip.md derives for lid10's lead-in, where the
 * author asked whether these curves shouldn't be algebraic: rational in t in
 * the meridian plane, the angle linear in t - transcendental in space, so
 * written as a fit, but a fit to this. */

#include <cstddef>
#include <vector>

#include "geometry/Surface.h"
#include "geometry/linalg.h"

namespace step {

struct Meridian {
  const SweepSurface *sweep = nullptr;
  double r0 = 0, slope = 0;  // the other surface: rho = r0 + slope h
  double h0 = 0, lift = 0;   // h of the sweep's origin, and dh per unit of its rise

  /*! The pair, if one of `a` and `b` is a declared sweep and the other a
   * cylinder or cone on the same axis - coaxial at machine precision. */
  static bool of(const Surface *a, const Surface *b, Meridian& out);

  /*! The profile edges `p` lies on at its own station: one, or the two meeting
   * at a vertex it is at; empty past the ends of the sweep. */
  [[nodiscard]] std::vector<std::size_t> edgesOf(const Vector3d& p) const;

  /*! Where profile edge `i` at station `t` meets the other surface. */
  bool crossingOnEdge(double t, std::size_t i, Vector3d& out) const;

  /*! The corners of the crossing strictly between `p0` and `p1` - where a
   * profile vertex reaches the other surface, and where a law turns - ordered
   * from `p0`. */
  [[nodiscard]] std::vector<Vector3d> corners(const Vector3d& p0, const Vector3d& p1) const;

  /*! The sweep's station of `p`. */
  [[nodiscard]] double station(const Vector3d& p) const;

private:
  /*! rho - r0 - slope h for profile point `q` at station `t`: zero on the other surface. */
  [[nodiscard]] double residual(double t, const Vector2d& q) const;
};

}  // namespace step
