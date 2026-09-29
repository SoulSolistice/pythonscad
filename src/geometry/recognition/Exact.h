#pragma once

/*! D0 membership of a point: is it on a declared surface, exactly?
 *
 * doc/step-rungs.md, D0: *fixed by declarations alone - no fit, no tolerance,
 * no sample*. So "on the surface" at D0 admits no chosen threshold; the only
 * allowance is what the arithmetic itself cannot avoid. A point the generator
 * emitted on a surface, asked of that surface in floating point, comes back
 * with a residual that is not zero - and is bounded, by the forward error of
 * the evaluation, which is a property of the formula and of `DBL_EPSILON`, not
 * of the model or the mesh.
 *
 * The bound used is the classical one: an expression of n rounded operations
 * over inputs whose magnitudes sum to `scale` is off by at most
 * gamma_n = n eps / (1 - n eps) times `scale`. n is counted per surface kind
 * below from the residual as written, every operation counted whether or not
 * it lies on the longest dependency chain, so it bounds rather than estimates.
 * `scale` is |p| + |refpt| + the surface's own radii - every magnitude the
 * residual subtracts.
 *
 * What the bound does not state, and why that is not a gap measured today: the
 * point and the declaration are themselves computed - r cos(theta) by the
 * generator, each through the same chain of transforms - and their rounding
 * arrives as further error of the same kind. Measured on 2026-09-24 over every
 * reference part and fixture that carries provenance, both tiers: every mesh
 * vertex within 1e-7 of a declared curved surface is within one eps x scale of
 * it (worst 2.8e-14 in absolute terms, on lid10 and the bayonet), and none lies
 * anywhere between that and 1e-7. The 1e-7 this replaces in the quadric pass was
 * a threshold standing where only machine precision belongs, and the entity
 * census of all 106 exports shows it decided nothing there.
 *
 * Asked here by the quadric pass, by export_step.cc's `runsThrough` (ownership
 * and presence), by the band pass's sphere and torus zones, and since
 * 2026-09-28 by the band pass itself: a band names a declared cylinder or cone
 * only by its seed's corners on it, walks only onto facets whose corners are on
 * it, and takes its ring from the record rather than from circles fitted to the
 * mesh. The first export
 * it was not neutral on was step-rounded-profile, whose four tori became 32
 * cones: offset()'s points came off Clipper's 2^-27 grid, half a cell off the
 * arcs they were computed on, and the relative 1e-7 had hidden it. Fixed at the
 * origin - Polygon2d::placeOnArcs, after the offset and after every 2D boolean -
 * and asserted by step-rounded-profile-notched.
 *
 * Not yet asked here: a declared grid still matches its net through a 1e-9
 * quantisation, and a Bezier patch, D1, keeps its projection's tolerance. The
 * band pass keeps a relative 1e-7 for the approximation's *fits* alone, which
 * are measurements of the mesh already and are labelled D3.
 *
 * Anything that fails this is not on the surface at D0, however close. Its
 * membership is then a D4 question - the tessellation band - and is labelled so.
 */

#include <cmath>
#include <vector>

#include "geometry/MachinePrecision.h"
#include "geometry/Surface.h"
#include "geometry/linalg.h"

namespace recognition {

/*! How far `p` is from a declared surface, and the bound the arithmetic puts on
 * that figure - each surface states its own (Surface::residual), with its own
 * count of operations. One with no closed-form residual answers infinity. */
inline Residual residualOf(const Surface *s, const Vector3d& p)
{
  return s == nullptr ? Residual{} : s->residual(p);
}

/*! Whether `p` is on `s` at D0 - see the file comment. */
inline bool onExactly(const Surface *s, const Vector3d& p)
{
  return residualOf(s, p).exact();
}

/* The classifications of directions, lines and planes - parallel,
 * perpendicular, on a line, on a plane, one line, one plane - each asked in one
 * place and at machine precision, so that a question has one answer wherever
 * it is asked. Each is a membership question in the end: a segment
 * perpendicular to a direction is its second end on the plane across that
 * direction through its first; one parallel to it is its second end on the
 * line. So the ends are asked, never the normalised difference, whose
 * direction is off by the rounding of both ends over the length between them. */

/*! Whether two directions are one, either sense, at machine precision: two
 * normalisations, 9 roundings each, and a cross product, 9 more, over unit
 * vectors. */
inline bool parallel(const Vector3d& dir_a, const Vector3d& dir_b)
{
  return dir_a.normalized().cross(dir_b.normalized()).norm() <= forwardError(27, 2.0);
}

/*! Whether two directions are at right angles, at machine precision: two
 * normalisations and a dot, over unit vectors. */
inline bool perpendicular(const Vector3d& dir_a, const Vector3d& dir_b)
{
  return fabs(dir_a.normalized().dot(dir_b.normalized())) <= forwardError(23, 2.0);
}

/*! How far `p` is from the plane through `origin` across `normal`, signed, and
 * the bound on that figure: a normalisation, 9; a difference, 3; a dot, 5 - 17
 * roundings over |p| + |origin|. The one plane residual; PlaneSurface states
 * its own through it. */
inline Residual planeResidual(const Vector3d& origin, const Vector3d& normal, const Vector3d& p)
{
  Residual out;
  out.value = (p - origin).dot(normal.normalized());
  out.bound = forwardError(17, p.norm() + origin.norm());
  return out;
}

/*! Whether `p` is on the plane through `origin` across `normal`, at machine
 * precision - and so whether two points are at one level along a direction. */
inline bool onPlane(const Vector3d& origin, const Vector3d& normal, const Vector3d& p)
{
  const Residual r = planeResidual(origin, normal, p);
  return fabs(r.value) <= r.bound;
}

/*! Whether every one of `ids` is on the plane through `origin` across
 * `normal`, at machine precision: which declared plane a mesh loop lies in is
 * this question, and not a comparison of the loop's normal with the plane's -
 * the vertices are what the generator put on the plane; the normal is measured
 * from them. */
inline bool onPlane(const Vector3d& origin, const Vector3d& normal,
                    const std::vector<Vector3d>& vertices, const std::vector<int>& ids)
{
  for (const int v : ids) {
    if (!onPlane(origin, normal, vertices[v])) return false;
  }
  return !ids.empty();
}

/*! Whether two points are one, at machine precision: a difference, 3; its
 * norm, 6 - over |p| + |q|. A generator's point and the mesh vertex made from
 * it are one point by this, whatever arithmetic carried each. */
inline bool samePoint(const Vector3d& p, const Vector3d& q)
{
  return (p - q).norm() <= forwardError(9, p.norm() + q.norm());
}

/*! Whether `p` is on the plane through `a`, `b` and `c`, at machine precision:
 * the orientation determinant (b - a) x (c - a) . (p - a) against its forward
 * error - each difference rounded relative to the points it subtracts (3 each),
 * a cross product and a dot (14) - bounded over the magnitudes each factor
 * multiplies. Three points on a line state no plane: false. */
inline bool coplanar(const Vector3d& a, const Vector3d& b, const Vector3d& c, const Vector3d& p)
{
  const Vector3d u = b - a, v = c - a, w = p - a;
  const double det = u.cross(v).dot(w);
  const double scale = (a.norm() + b.norm()) * v.norm() * w.norm() +
                       u.norm() * (a.norm() + c.norm()) * w.norm() +
                       u.norm() * v.norm() * (a.norm() + p.norm());
  if (!(u.cross(v).norm() >
        forwardError(9, (a.norm() + b.norm()) * v.norm() + u.norm() * (a.norm() + c.norm())))) {
    return false;
  }
  return fabs(det) <= forwardError(23, scale);
}

/*! Whether `p` is on the line through `origin` along `dir`, at machine
 * precision: a normalisation, 9; a difference, a dot, a scale, a difference, a
 * norm, 20 - over |p| + |origin|. */
inline bool onLine(const Vector3d& origin, const Vector3d& dir, const Vector3d& p)
{
  const Vector3d a = dir.normalized();
  const Vector3d d = p - origin;
  return (d - a * d.dot(a)).norm() <= forwardError(29, origin.norm() + p.norm());
}

/*! Whether two lines - each a point and a direction, as a declaration states
 * an axis - are one line, at machine precision: parallel, and the second's
 * point on the first line. Asked of two declarations, never of a measured
 * axis: which declarations share an axis is a fact of the model. */
inline bool sameLine(const Vector3d& origin_a, const Vector3d& dir_a, const Vector3d& origin_b,
                     const Vector3d& dir_b)
{
  return parallel(dir_a, dir_b) && onLine(origin_a, dir_a, origin_b);
}

/*! Whether two planes - each a point and a normal - are one plane, at machine
 * precision: parallel, and each one's point on the other. Not the point on it:
 * two records anchored a metre apart on one plane are one plane. */
inline bool samePlane(const Vector3d& origin_a, const Vector3d& normal_a, const Vector3d& origin_b,
                      const Vector3d& normal_b)
{
  return parallel(normal_a, normal_b) && onPlane(origin_a, normal_a, origin_b) &&
         onPlane(origin_b, normal_b, origin_a);
}

}  // namespace recognition
