#pragma once

/*! How far a solid reaches in a direction: its support value h(n), the most
 * n . x takes over it.
 *
 * What a hull needs of its operands, and all it needs: the hull of solids is
 * the set of their convex combinations (Caratheodory), its support value in
 * every direction the largest of theirs, and a point on an operand's smooth
 * boundary, with outward normal n there, is on the hull's boundary exactly
 * where no operand reaches further along n - h_H(n) = n . p - and inside it
 * where one does. A union reaches as far as its farthest operand; a hull too.
 *
 * Stated in closed form where the solid is a declared primitive - a sphere, a
 * cylinder or cone frustum between caps across its axis, or a polytope of
 * planes - and not otherwise: a difference or an intersection reaches no
 * further than its first operand, but by how much less, nothing here says.
 * `err` is the forward error of computing the value.
 */

#include "geometry/SolidTree.h"
#include "geometry/linalg.h"

namespace recognition {

/*! The support value of `tree` in direction `n` (any length), in `h`, and its
 * forward error in `err`; false where the tree does not state one. */
bool supportValue(const SolidTree& tree, const Vector3d& n, double& h, double& err);

}  // namespace recognition
