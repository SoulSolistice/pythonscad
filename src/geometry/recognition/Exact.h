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

/*! Whether two lines - each a point and a direction, as a declaration states
 * an axis - are one line, at machine precision: parallel (two normalisations,
 * 9 each, and a cross, 9 more) and the second's point on the first line (a
 * difference, a dot, a scale, a difference, a norm: 20). Asked of two
 * declarations, never of a measured axis: which declarations share an axis is
 * a fact of the model. */
inline bool sameLine(const Vector3d& origin_a, const Vector3d& dir_a, const Vector3d& origin_b,
                     const Vector3d& dir_b)
{
  const Vector3d a = dir_a.normalized(), b = dir_b.normalized();
  if (a.cross(b).norm() > forwardError(27, 2.0)) return false;
  const Vector3d d = origin_b - origin_a;
  const double scale = origin_a.norm() + origin_b.norm();
  return (d - a * d.dot(a)).norm() <= forwardError(20, scale);
}

}  // namespace recognition
