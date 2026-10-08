#pragma once

/*! The model's boundary, evaluated from its declared tree - faces, edges and
 * vertices the declarations decide, and nothing a mesh does.
 *
 * Every face of a boolean of declared solids is a piece of a declared surface,
 * every edge a piece of the curve two of them meet in, every vertex where three
 * do. So the boundary is the arrangement of the declarations, asked of the tree:
 *
 *  - **curves**: every pair of declared surfaces, in closed form - two planes a
 *    line, a plane and a cylinder a circle, an ellipse or two rulings;
 *  - **vertices**: where three meet (Arrangement.h's triple points), one vertex
 *    for each place however many triples find it;
 *  - **edges**: each piece of a curve between the vertices on it, asked at its
 *    middle which quadrants of directions go into the solid (quadrants()): it
 *    is an edge where both surfaces part an inside quadrant from an outside
 *    one, and that says too on which side of the curve each surface's face
 *    lies and which way it faces - no offset chosen, no sample;
 *  - **faces**: on each surface, the edges' uses chained into loops, each with
 *    its face on its left, and the loops grouped - on a plane, outer loops and
 *    the holes inside them by exact signed area and containment; on a cylinder,
 *    loops that go round the axis paired into bands by their order along it.
 *
 * What it states is the model's own, at machine precision: a writer writes it
 * (brep::fromModel), and the passes that ask the same questions of a mesh -
 * existence, membership, corner placement, crossing curves - can ask it
 * instead (doc/step-export-wip.md, item 42).
 *
 * Refused, with the reason, rather than guessed: a surface that is not a plane
 * or a cylinder; two cylinders not on one axis (a quartic); a plane tangent to
 * a cylinder; a point the tree states no side at; an edge four quadrants of
 * which alternate - two solids touching along a line; a face pinched at a
 * vertex; loops the grouping cannot place.
 */

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "geometry/SolidTree.h"
#include "geometry/linalg.h"

class Surface;

namespace model {

/*! A curve two declared surfaces meet in: a line origin + t u, or a conic
 * origin + u cos t + v sin t - u and v conjugate semi-diameters, a circle where
 * they are perpendicular and of one length, which `circle` states rather than
 * leaves to be measured. */
struct Curve {
  enum class Kind { Line, Conic };
  Kind kind = Kind::Line;
  Vector3d origin{0, 0, 0}, u{1, 0, 0}, v{0, 1, 0};
  bool circle = false;

  [[nodiscard]] Vector3d at(double t) const;
  [[nodiscard]] Vector3d tangent(double t) const;
  /*! The parameter of a point on the curve. */
  [[nodiscard]] double parameterOf(const Vector3d& p) const;
};

/*! A vertex: where it is, and the declared surfaces meeting there. */
struct Vertex {
  Vector3d at{0, 0, 0};
  std::vector<const Surface *> on;
};

/*! A piece of the curve two surfaces meet in, from parameter t0 at vertex
 * `from` to t1 > t0 at vertex `to` - a closed conic its whole turn, from and
 * to one vertex. */
struct Edge {
  const Surface *s = nullptr, *t = nullptr;
  Curve curve;
  double t0 = 0, t1 = 0;
  std::size_t from = 0, to = 0;
};

/*! A use of an edge by a face's loop: forward runs it from t0 to t1. */
struct EdgeUse {
  std::size_t edge = 0;
  bool forward = true;
};

/*! A closed chain of uses, the face on its left seen from outside. `outer`
 * marks a face's outer boundary; a band round a cylinder has none. */
struct Loop {
  std::vector<EdgeUse> uses;
  bool outer = true;
};

/*! A bounded piece of a declared surface: `along` whether it faces out along
 * the surface's own normal (a plane's normdir, a cylinder's radially out). */
struct Face {
  std::shared_ptr<const Surface> surface;
  bool along = true;
  std::vector<Loop> loops;
};

struct Boundary {
  std::vector<Vertex> vertices;
  std::vector<Edge> edges;
  std::vector<Face> faces;
};

/*! The boundary `tree` bounds, or nullopt with `why` filled. */
std::optional<Boundary> evaluate(const SolidTree& tree, std::string& why);

}  // namespace model
