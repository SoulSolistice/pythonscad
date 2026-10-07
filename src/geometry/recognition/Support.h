#pragma once

/*! How far a solid reaches in a direction: its support value h(n), the most
 * n . x takes over it, and where it takes it.
 *
 * What a hull needs of its operands, and all it needs: the hull of solids is
 * the set of their convex combinations (Caratheodory), its support value in
 * every direction the largest of theirs, and the hull meets its supporting
 * plane in direction n in the convex hull of the points where its operands
 * reach that far. So a point on an operand's smooth boundary, outward normal n,
 * is on the hull's boundary exactly where no operand reaches further along n,
 * and inside it where one does; a point on a surface the hull itself makes -
 * the cone between two rims - is on it where it lies in that convex hull, and
 * out where a plane separates it. A union reaches as far as its farthest
 * operand; a hull too.
 *
 * Stated in closed form for a declared primitive - a sphere, a cylinder or cone
 * frustum between caps across its axis, a polytope of planes - and for a
 * boolean of them, whose farthest point is one of its arrangement's
 * (Arrangement.h); not otherwise. `err` is the forward error of the value.
 */

#include <vector>

#include "geometry/SolidTree.h"
#include "geometry/linalg.h"

namespace recognition {

/*! A solid's reach along n: the value, its forward error, and the points that
 * attain it. `finite` is false where they stand for more than themselves - a
 * face square to n, every point of which attains it - and their convex hull is
 * then not the whole set. */
struct Reach {
  double h = 0, err = 0;
  std::vector<Vector3d> at;
  bool finite = true;
};

/*! `tree`'s reach along `n` (any length); false where the tree does not state one. */
bool reachOf(const SolidTree& tree, const Vector3d& n, Reach& out);

/*! The support value alone. */
bool supportValue(const SolidTree& tree, const Vector3d& n, double& h, double& err);

}  // namespace recognition
