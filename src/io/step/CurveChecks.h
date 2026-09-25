#pragma once

/*! Checks of our own file against itself, before a reader makes them. */

#include "geometry/linalg.h"
#include "io/StepKernel.h"

namespace step {

/*! How far a vertex is from the curve its own edge is written as.
 *
 * This is SOLIDWORKS fault code 7 - "vertex does not lie on edge's curve" -
 * asked of our own file before anyone else asks it. Nothing here measured that
 * relation until 2026-09-16: the corner-stray family measures a corner against
 * the *surface* its face is written on, which is code 16, and code 16 has never
 * fired on a file of ours. See doc/step-export-development.md, *Fault codes*.
 *
 * Every substitution in this exporter replaces a run of chords with one curve,
 * and every one of them owes the same thing: the curve has to pass through the
 * run's own endpoints. A plane section that misses them by 1.6 mm is not a
 * near miss on a 940 mm ellipse, it is an edge two faces disagree about.
 *
 * The distance is exact for a line and a circle, and for a B-spline written
 * clamped - the endpoints are its first and last control points. An ellipse is
 * sampled and then refined, which is an upper bound on the true distance, and
 * an upper bound is the safe direction for a check that has to be believed.
 */
double vertexOffCurve(const StepKernel::RoundType *curve, const Vector3d& p);

}  // namespace step
