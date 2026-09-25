#pragma once

/*! Where a face that goes all the way round its axis can be closed by a seam.
 *
 * A surface about an axis is periodic round it, so a face covering the whole
 * turn between two rims is bounded in its own parameters only by one loop that
 * runs along one rim, up a seam to the other, along that one and back down the
 * seam - the band writer's shape. The seam has to lie on the surface, and the
 * test for that is exact: a line meets a quadric in at most two points unless
 * it lies in it, so a segment whose two ends and middle are all on the surface
 * at machine precision is a ruling of it. And it has to stay in the face: a rim
 * that runs up and down round a notch can cross the ruling between its two
 * ends, and a seam through that crossing leaves the face and back, which a
 * reader rightly calls unorientable. In the surface's own angle and height the
 * seam is one angle between two heights, so the test is whether any other edge
 * of either rim crosses that angle strictly between them.
 *
 * Asked by the trimmed-quadric pass, which cuts a wrapping region in two only
 * where no seam can close it, and by the writer, which closes it - the same
 * question in both, so the pass never leaves whole a face the writer cannot
 * close. */

#include <cstddef>
#include <utility>
#include <vector>

#include "geometry/Surface.h"
#include "geometry/linalg.h"

namespace recognition {

/*! Whether the segment from `a` to `b` lies on `s` - see the file comment. */
bool isRuling(const Surface& s, const Vector3d& a, const Vector3d& b);

/*! Whether the closed `cycle` of vertices goes once round `s`'s axis: its
 * winding number, so an integer, asked by whether the turn exceeds half of one. */
bool windsRound(const AxialSurface& s, const std::vector<int>& cycle,
                const std::vector<Vector3d>& vertices);

/*! The pairs (i, j), `first[i]` and `second[j]`, that a ruling of `s` joins and
 * that no other edge of either rim crosses: the seams that can close a face
 * running round the axis between the two. */
std::vector<std::pair<std::size_t, std::size_t>> rulingSeams(const AxialSurface& s,
                                                             const std::vector<int>& first,
                                                             const std::vector<int>& second,
                                                             const std::vector<Vector3d>& vertices);

}  // namespace recognition
