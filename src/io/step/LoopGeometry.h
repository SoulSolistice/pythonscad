#pragma once

/*! The plane geometry of the mesh's own loops: their normals, their projection
 * onto a coordinate plane, containment, and triangulation with holes. Nothing
 * here knows about a declared surface or a STEP entity. */

#include <array>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "geometry/linalg.h"

namespace step {

// Newell's method. In contrast to the cross product of the first two edges it
// is stable for concave corners (where the cross product points the wrong way)
// and for polygons whose first three vertices happen to be collinear (where
// the cross product collapses to zero). The magnitude is twice the area.
Vector3d polygonNormal(const std::vector<Vector3d>& vertices, const std::vector<int>& poly);

// A face of no area is three or more collinear points, and the mesh uses it to
// stitch a vertex sitting in the interior of another face's edge back into the
// surface. Sort its points along their common line: the two extremes are the
// span some neighbouring face still crosses in one edge, and everything between
// them is a T-junction that neighbour has to be told about.
//
// Records nothing when the points are not collinear to within weld_eps, which
// is deliberately far tighter than model_tol - these points are collinear to
// within rounding (about 1e-13 on the lid), so anything looser would start
// moving vertices that were never on the edge at all.
void recordSliverSpan(const std::vector<Vector3d>& vertices, const std::vector<int>& loop,
                      std::map<std::pair<int, int>, std::vector<std::pair<double, int>>>& spans);

// The axis the normal points along most; dropping it projects the plane onto
// the remaining two coordinates without ever collapsing it.
int dominantAxis(const Vector3d& norm);

/*! `pt` with coordinate `drop` left out. */
std::array<double, 2> projectPoint(const Vector3d& pt, int drop);

/*! A loop's vertices, projected with coordinate `drop` left out. */
void projectLoop(const std::vector<Vector3d>& vertices, const std::vector<int>& loop, int drop,
                 std::vector<std::array<double, 2>>& out);

/*! The area of a projected loop, unsigned. */
double loopArea2d(const std::vector<std::array<double, 2>>& poly);

// Crossing number test. The half open comparison on the y coordinate makes a
// ray which passes exactly through a vertex count once instead of twice, which
// is what the 3D ray cast in mergeTriangles() gets wrong on concentric loops.
bool pointInLoop2d(const std::vector<std::array<double, 2>>& poly, const std::array<double, 2>& pt);

// Does `outer` hold every corner of `inner`, both projected along `drop`?
//
// One probe point says a loop *starts* inside a candidate; it does not say the
// loop is inside it. A hole whose corners fall outside the face carrying it is
// not a hole of that face, and OpenCASCADE says so - InvalidImbricationOfWires,
// on three faces of the bayonet lid, where the chosen parent held 1, 2 and 3 of
// the loop's corners rather than all of them. Every genuine hole on that model
// sits wholly inside its parent, so ask for all of them.
bool loopContains(const std::vector<Vector3d>& vertices, const std::vector<int>& outer,
                  const std::vector<int>& inner, int drop);

/*! Triangulate a polygon with holes, in the plane its face asserts.
 *
 * The corner placement bends a planar face whenever it moves one of its corners
 * off the plane the rest of them share, and a bent polygon is not a face any
 * more - but its triangles are, because three points are coplanar wherever they
 * are. Fanning from one corner does that for a simple loop and cannot do it for
 * one with a hole: an inner bound has to be threaded into the outer one first.
 * So a face with a hole used to refuse the move outright, and on lid10 that is
 * what held five corners 0.0109 to 0.1089 inside the quadric their own face is
 * written on.
 *
 * Bridged ear clipping, which is the textbook answer and needs no tolerance of
 * its own: each hole is spliced into the outer ring along a segment between two
 * mutually visible corners, and the simple ring that leaves is cut ear by ear.
 * A bridge is traversed once each way, so it ends up an interior diagonal shared
 * by exactly two triangles, exactly as a fan's diagonals are.
 *
 * Returns the triangles as vertex indices, wound the way the outer ring is, or
 * nothing at all if the rings are degenerate or an ear cannot be found. Nothing
 * is the honest answer there and the caller treats it as it always treated a
 * polygon it could not fan: the move is refused.
 */
std::vector<std::array<int, 3>> triangulateWithHoles(const std::vector<std::vector<int>>& rings,
                                                     const std::function<Vector3d(int)>& at,
                                                     const Vector3d& normal);

/*! The root of `x` in a union-find forest, halving the path on the way. */
int uf_find(std::vector<int>& parent, int x);

}  // namespace step
