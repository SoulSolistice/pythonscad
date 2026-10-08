#pragma once

/*! Where a point is with respect to the model's solid, answered by the model.
 *
 * The mesh boolean ran on chords, and its answer is exact only where every
 * operand's tessellation error is smaller than the model's features. This asks
 * the model instead: the boolean it built by (SolidTree), over the solids its
 * generators declared, each bound's residual against the forward error of
 * computing it (MachinePrecision.h). No tolerance anywhere; where the model
 * does not say, the answer is Unknown and never a guess.
 *
 * Up the tree it is Kleene logic, which is what lets a model decide what one
 * opaque operand cannot: a difference whose subtracted operand holds the point
 * is Out whatever the rest says. On is exact only where one operand is on at
 * the point; two operands on at the same point - two faces coinciding - would
 * need the half-space each keeps there, and is Unknown until that is built.
 *
 * Pure, STEP-agnostic, and reads no claim (checklist A1). See
 * doc/step-recognition-design.md, "Existence is a D0 question too", step 2.
 */

#include <vector>

#include "geometry/SolidTree.h"
#include "geometry/linalg.h"

class Surface;

namespace recognition {

enum class Where { In, Out, On, Unknown };

struct Classification {
  Where where = Where::Unknown;
  /*! Where On: the declared bounds the point lies on, at machine precision. */
  std::vector<const Surface *> on;
  /*! The leaf behind the answer: the one that holds the point, rules it out,
   * or carries its face - or, for Unknown, the operand that stated nothing and
   * left it open. Null where no one operand is the reason: a point on two
   * surfaces at once. What the ledger names (SolidTree::what). */
  const SolidTree *because = nullptr;
  /*! Where On, the side the leaf keeps there, where it states one - a
   * region's (SolidRegion::Answer::sides), a hull's supporting plane - and
   * whether the solid near the point is in all those half-spaces or in any.
   * Empty where it states none; a declared leaf's are its bounds' own. */
  std::vector<SolidRegion::Side> sides;
  bool all = true;
};

/*! Where `p` is with respect to the solid `tree` describes. See the file comment. */
[[nodiscard]] Classification classify(const SolidTree& tree, const Vector3d& p);

/*! The same, for a point that lies on each of `on` - bounds of the tree's own
 * leaves - by construction: a vertex or a curve point computed from them,
 * which is on them whatever its rounding says, and asked so. */
[[nodiscard]] Classification classifyAt(const SolidTree& tree, const Vector3d& p,
                                        const std::vector<const Surface *>& on);

/*! At a point on two surfaces crossing there - a point of the curve they meet
 * in, computed from them - which of the four quadrants of directions go into
 * the solid: inside[i][j], i the side of `s`'s own normal there (0 against, 1
 * along), j of `t`'s (normalOf: a plane's, a curved surface's at the point).
 * The half-space rule's cells, asked of two named surfaces: whether the curve
 * is an edge of the solid, and on which side of it each surface's face lies,
 * facing which way. False where a leaf on the point states no side, a tangent
 * plane through it is neither surface's, or the two are tangent there. */
[[nodiscard]] bool quadrants(const SolidTree& tree, const Vector3d& p, const Surface *s,
                             const Surface *t, bool inside[2][2]);

}  // namespace recognition
