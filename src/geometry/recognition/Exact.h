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
 * and presence) and by the band pass's sphere and torus zones. The first export
 * it was not neutral on was step-rounded-profile, whose four tori became 32
 * cones: offset()'s points came off Clipper's 2^-27 grid, half a cell off the
 * arcs they were computed on, and the relative 1e-7 had hidden it. Fixed at the
 * origin - Polygon2d::placeOnArcs, after the offset and after every 2D boolean -
 * and asserted by step-rounded-profile-notched.
 *
 * Not yet asked here: the band pass still compares an axis and a radius it
 * measured from the mesh against the declaration, at a relative 1e-7, where the
 * D0 question is the generator's points against the declaration itself; a
 * declared grid still matches its net through a 1e-9 quantisation; and a Bezier
 * patch, D1, keeps its projection's tolerance.
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

}  // namespace recognition
