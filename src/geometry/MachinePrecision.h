#pragma once

/*! The arithmetic's own bound, which is the only allowance D0 admits.
 *
 * doc/step-rungs.md, D0: fixed by declarations alone - no fit, no tolerance, no
 * sample. A quantity computed in floating point is still off by its rounding,
 * and that is bounded by the forward error of the formula: n rounded operations
 * over inputs whose magnitudes sum to `scale` are off by at most
 * gamma_n = n eps / (1 - n eps) times `scale`. See recognition/Exact.h for how
 * membership asks it, and each Surface::residual for its own count.
 *
 * Here rather than in recognition/ because a surface states its own residual
 * (Surface.h), and a surface is below recognition. */

#include <cfloat>
#include <limits>

namespace recognition {

/*! A residual and the machine-precision bound on computing it. */
struct Residual {
  double value = std::numeric_limits<double>::infinity();
  double bound = 0;
  /*! On the surface, as exactly as the arithmetic can say. */
  [[nodiscard]] bool exact() const { return value <= bound; }
};

/*! gamma_n x scale: the forward error of n rounded operations. */
inline double forwardError(int n, double scale)
{
  const double ne = n * DBL_EPSILON;
  return ne / (1 - ne) * scale;
}

}  // namespace recognition
