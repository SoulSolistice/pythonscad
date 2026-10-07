#pragma once

/*! The model's own vertices and curve points, from its leaves' declarations.
 *
 * The boundary of a boolean of declared primitives is pieces of their surfaces,
 * meeting along their intersection curves at their triple points. Those are the
 * model's exact edges and vertices, and each is a closed form of two or three
 * declarations - nothing measured. Two questions read them:
 *
 *  - how far the solid reaches along n (Support.h): its farthest point is a
 *    stationary point of n . x on some piece - a surface's own (a ball's two),
 *    a curve's where n is normal to it (an ellipse's two), or a vertex;
 *  - where a corner of the mesh belongs, and whether it is there.
 *
 * In closed form where every pair of surfaces meets in lines or conics - planes
 * with each other and with any quadric, and coaxial quadrics with each other -
 * and nowhere else: two cylinders on different axes meet in a quartic, and the
 * enumeration then says it is incomplete rather than leave a point out. So
 * does a tangency, whose roots are not well conditioned.
 */

#include <vector>

#include "geometry/SolidTree.h"
#include "geometry/linalg.h"

class Surface;

namespace recognition {

/*! A point of the arrangement, with the surfaces it lies on by construction. */
struct ArrangementPoint {
  Vector3d at;
  std::vector<const Surface *> on;
};

/*! Every distinct bound of `tree`'s declared leaves, in `out`; false where a
 * leaf states no bounds - opaque, a region, a hull. */
bool leafSurfaces(const SolidTree& tree, std::vector<const Surface *>& out);

/*! Where three of `surfaces` meet in isolated points: the model's candidate
 * vertices. False where a triple is not solvable in closed form. */
bool triplePoints(const std::vector<const Surface *>& surfaces, std::vector<ArrangementPoint>& out);

/*! Where n . x is stationary on a surface or on the curve two of them meet in:
 * a sphere's two points along n, a conic's two. False where a pair's curve is
 * not a line or a conic. */
bool criticalPoints(const std::vector<const Surface *>& surfaces, const Vector3d& n,
                    std::vector<ArrangementPoint>& out);

}  // namespace recognition
