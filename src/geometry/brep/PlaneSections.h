#pragma once

/*! Where a plane meets a quadric about an axis: the conic it cuts, the tilt past
 * which that conic no longer closes, and the stretches of a boundary cycle that
 * lie in one plane - plus the one question that tells a plane cutting a curved
 * surface from a facet lying along it. */

#include <memory>
#include <utility>
#include <vector>

#include "geometry/Surface.h"
#include "geometry/linalg.h"

namespace brep {

/*! The ellipse a plane cuts from a cylinder or a cone.
 *
 * `a` is the semi-axis along `major`, and is the longer of the two - which is
 * the order ISO 10303-42 gives an ELLIPSE its two radii in.
 *
 * A cylinder is the easy half: the section's centre is where the plane meets the
 * axis, the short semi-axis is the cylinder's own radius, and the long one is
 * that divided by the cosine of the tilt.
 *
 * A cone is worked in its plane of symmetry - the one containing the axis and
 * perpendicular to the cutting plane - because that is where the section's two
 * extreme points are. The cone shows there as its two outermost generators and
 * the cutting plane as a line, so the two crossings are the ends of the major
 * axis and the centre and `a` follow from them. `b` is the half chord through
 * the centre at right angles to that, and the cone's own quadratic gives it in
 * one square root: the direction is perpendicular to the axis and to the major
 * axis both, so every linear term in it drops out.
 */
struct SectionEllipse {
  Vector3d centre, major;
  double a = 0, b = 0;
};

bool planeSectionEllipse(const Surface *surface, const Vector3d& normal, const Vector3d& on_plane,
                         SectionEllipse& out);

/*! The conic a plane cuts from a cylinder or a cone, whichever conic it is.
 *
 * `planeSectionEllipse` above answers only for the closed case and refuses the
 * rest, which is right for the pass that writes a *face's* boundary as one
 * closed curve. It is not right for a single edge: a plane through a cone
 * steeper than the cone's own half angle cuts a hyperbola, one at exactly that
 * angle cuts a parabola, and a chord written straight across either of them lies
 * off the cone. On lid10 that is 64 edges and up to 0.1068, because its draft
 * cone's half angle is about 1.15 degrees and almost any plane down the wall is
 * steeper than that.
 *
 * Both are ISO 10303-42 entities, so the exact curve can be written and there is
 * no reason to approximate one.
 *
 * The section is found algebraically rather than geometrically, because that is
 * the form that classifies itself. A cone about `a` through apex `A` with half
 * angle `alpha` is the quadric (X-A)' M (X-A) = 0 for M = a a' - cos^2(alpha) I,
 * and a cylinder of radius r about the same line is (X-O)' (I - a a') (X-O) = r^2.
 * Substituting X = P + u E1 + v E2 for an orthonormal basis of the plane leaves a
 * conic in (u, v); the sign of the determinant of its quadratic part names it,
 * its centre solves the linear part, and its eigenvectors are the axes.
 */
struct SectionConic {
  enum Kind { ELLIPSE, HYPERBOLA, PARABOLA } kind = ELLIPSE;
  Vector3d centre, major, normal;
  double a = 0, b = 0;  // semi-axes; for a parabola `a` is the focal distance
};

bool planeSectionConic(const Surface *surface, const Vector3d& normal_in, const Vector3d& on_plane,
                       SectionConic& out);

/*! The real planes two quadrics' crossing lies in, where it lies in planes -
 * empty where it does not.
 *
 * Two quadrics meet in a quartic curve, which in general no STEP entity
 * states; it splits into two conics exactly when a member of their pencil,
 * A - lambda B in homogeneous form, is a pair of real planes - two equal
 * cylinders whose axes cross, in the planes bisecting them; a sphere and a
 * coaxial cylinder, in the two planes across the axis. Each of those planes
 * cuts either quadric in the same conic, which the section pass writes. A
 * closed form from the two declarations (D1), where the crossing was fitted
 * and then handed back if the fit's control points came out flat within 1e-9
 * (open item 39, R7). Each plane as (a point on it, its unit normal). */
std::vector<std::pair<Vector3d, Vector3d>> pencilPlanes(const Surface *a, const Surface *b);

/*! Whether `a` and `b` are surfaces of revolution about one axis - cylinders,
 * cones and spheres whose sections across it are circles, the axes one line
 * at machine precision, a sphere's centre on it. Such a pair meets in circles
 * across the axis whatever its radii and slopes: by identity, with no plane to
 * compute. Their pencil is ill conditioned there - the member that splits is
 * the difference of two nearly equal forms - and on a cone 1e5 from its apex
 * its planes came out off the circles' own points (open item 39, R7). */
bool coaxialRevolution(const Surface *a, const Surface *b);

/*! How far off perpendicular a plane may tilt and still cut a closed section. */
double sectionTiltFloor(const Surface *surface);

/*! The axis of a surface about one, into `axis`; false for any other surface. */
bool surfaceAxis(const Surface *surface, Vector3d& axis);

/*! The maximal stretches of a boundary cycle that lie in one plane.
 *
 * A plane section of a cylinder is an ellipse, which is exact and writable as
 * one - the third way a boundary can be honest about a curved surface, beside
 * running along the axis and running round it at constant height. The mesh does
 * not hand it over: Patch::Run splits the boundary wherever the neighbouring
 * face changes, so a single ellipse arrives as thirty-three runs of one edge
 * each. The stretches have to be found by walking the cycle.
 *
 * A cycle has no first vertex of its own - the flattening picked one - so a
 * left-to-right scan reports whatever stretch straddles that arbitrary seam as
 * two shorter ones, and the neighbouring face, whose seam is elsewhere, then
 * disagrees about where the curve begins. Two faces that disagree cannot share
 * an edge and the shell comes apart. So the search is over the cycle as a
 * circle: grow a plane from every vertex, take the longest, consume its edges,
 * repeat. That answer does not depend on where the cycle was cut.
 *
 * A stretch is only worth having if it covers at least two edges: one edge is a
 * chord, and a chord is what this is trying to get rid of. Indices returned may
 * run past the end of the cycle and are to be read modulo its length.
 */
struct PlaneStretch {
  std::size_t start = 0, count = 0;  // count is vertices, so count - 1 edges
  Vector3d normal;
};

std::vector<PlaneStretch> coplanarStretches(const std::vector<int>& cycle,
                                            const std::vector<Vector3d>& vertices, const Vector3d& axis,
                                            double min_cos_tilt);

}  // namespace brep
