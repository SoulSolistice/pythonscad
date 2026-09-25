#pragma once

/*! The arc of the curve where two declared surfaces cross, fitted between two
 * points already on it. */

#include <limits>
#include <vector>

#include "geometry/Surface.h"
#include "geometry/linalg.h"

namespace step {

/*! Why a crossing arc came out as it did.
 *
 * `intersectionArc` knows how close it got and used to throw the number away,
 * so a boundary that fell back to chords reported a count and nothing else -
 * and a count cannot tell a fit that missed by 3.4e-05 from one that missed by
 * a tenth of a millimetre. These are what turn that into a distribution.
 */
struct ArcOutcome {
  int degree = 0;           //!< the degree that answered, or the highest tried
  int sampling_failed = 0;  //!< degrees abandoned because a sample would not project
  double best_error = std::numeric_limits<double>::infinity();  //!< nearest any degree came
  double on_quadric = 0;      //!< of that, how much was against a surface stated algebraically
  double on_fitted = 0;       //!< and how much against one that answers by projecting
  int straddles_crease = -1;  //!< 1 if the arc's ends sit in different profile spans, 0 if not
};

/*! The arc of the curve where two declared quadrics cross, between two points
 *  already on it, as the control points of a Bezier.
 *
 * The curve two quadrics meet along is a quartic in general - two cylinders of
 * unequal radius crossing is the everyday case - and ISO 10303 has no entity for
 * it. OpenCASCADE's own export of such a solid writes a degree-7 B-spline of
 * some thirty control points, so approximating is not a shortcut here: it is
 * what the format offers. What matters is that the approximation is of the
 * *true* curve rather than of the mesh, that it is held to a stated tolerance,
 * and that both faces derive it from the same two declarations and so agree on
 * it exactly - which is what keeps the shell closed without either face knowing
 * what the other did.
 *
 * The degree is raised until the fit is inside `tol` rather than fixed, because
 * how much of the curve one mesh edge spans is not something this can know: a
 * cubic is enough over most of a bore's opening and nowhere near it where the
 * two surfaces run nearly tangent and the curve turns hard. Measured on
 * step-bored-cylinder, a cubic leaves the surfaces by 3.4e-05 - three hundred
 * times better than the chords it replaces, and still not exact.
 */
bool intersectionArc(const Surface *a, const Surface *b, const Vector3d& p0, const Vector3d& p1,
                     double tol, std::vector<Vector3d>& ctrl, ArcOutcome *why = nullptr);

/*! How far a fitted arc leaves the two surfaces it is supposed to run along; and,
 * where asked, how much of that is against a surface stated algebraically
 * (`on_quadric`) and how much against one that answers by projecting
 * (`on_fitted`). */
double intersectionArcError(const Surface *a, const Surface *b, const std::vector<Vector3d>& ctrl,
                            double *on_quadric = nullptr, double *on_fitted = nullptr);

}  // namespace step
