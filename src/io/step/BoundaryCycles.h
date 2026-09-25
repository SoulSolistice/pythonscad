#pragma once

/*! A quadric patch's boundary, as cycles of vertices, with the plane sections
 * found on each - asked by three passes that must agree exactly. */

#include <cstddef>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "geometry/AnalyticFeatures.h"
#include "geometry/Surface.h"
#include "geometry/linalg.h"

namespace step {

/*! One run of a boundary cycle that lies in one plane section. */
struct BoundarySection {
  std::size_t start = 0, count = 0;  // count is vertices, so count - 1 edges
  Vector3d normal, on_plane;
  int loop = -1;          // the planar face across it, or -1 where another quadric is
  bool declared = false;  // the plane came from the model, not from the mesh
};

/*! A plane that cuts a quadric, taken from a face rather than fitted.
 *
 * `declared` says the model stated it, which is the difference that matters: a
 * declaration cannot drift, where a plane fitted to mesh vertices moves with the
 * tessellation, with whatever the boolean did, and with the tolerance the fit
 * was given. Everything derived from it inherits that - the section's centre and
 * its two semi-axes are all differences of the plane against the surface, and a
 * plane that is exact makes them exact too.
 */
struct CutPlane {
  Vector3d normal, on_plane;
  bool declared = false;
};

struct BoundaryCycle {
  std::vector<int> verts;
  std::vector<int> owner;  // per edge: the neighbouring planar loop, or -1
  std::vector<BoundarySection> sections;
  /*! One of the two rims of a face that goes all the way round its axis, rolled
   * to start where the seam that closes the face meets it. See sealWithSeam. */
  bool seam = false;
};

/*! What binding sections' ends to the surface cost over one export: the runs
 * shortened, the runs dropped, and the farthest an end was off the surface.
 *
 * Three process-wide globals until 2026-09-25, never reset, so a second export
 * in one session reported the first one's as well. */
struct SectionStats {
  std::size_t trimmed = 0, dropped = 0;
  double end_stray = 0;
};

/*! The plane sections of one boundary cycle, into `cycle.sections`: runs of
 * edges lying in one of `planes` with one neighbour across them, then, where no
 * plane is to be had, stretches fitted to the cycle itself. An edge in
 * `spoken_for` has a better curve and is offered none. With `bind_ends` a run is
 * shortened until both its ends are on the surface, and what that cost is added
 * to `stats`. */
void findSections(const Surface *surface, const std::vector<Vector3d>& vertices,
                  const std::vector<CutPlane>& planes, BoundaryCycle& cycle,
                  const std::set<std::pair<int, int>>& spoken_for, bool bind_ends,
                  SectionStats *stats = nullptr);

/*! A patch's boundary cycles and who is across each edge, with no curves decided
 *  yet - which is what the crossing-curve search needs, since that runs first. */
std::map<std::size_t, BoundaryCycle> rawBoundaryCycles(const AnalyticFeatures::Patch& patch,
                                                       const std::vector<std::vector<int>>& loops,
                                                       const std::vector<char>& loop_valid,
                                                       const std::vector<char>& taken);

/*! Close a face that goes all the way round its axis between two rims.
 *
 * The surface is periodic round the axis, so such a face is bounded in its own
 * parameters by one loop: along one rim, up a seam to the other, along that one
 * and back down the seam - the band writer's shape. The seam is a ruling that
 * joins a vertex of one rim to a vertex of the other (recognition/Seam.h), and
 * each rim is rolled to start there, so that a rim which is one whole plane
 * section is one closed curve from the seam back to it. A seam no section runs
 * across is the one taken; where there is none, false, and the face is written
 * as it always was. */
bool sealWithSeam(const AnalyticFeatures::Patch& patch, const std::vector<Vector3d>& vertices,
                  const std::vector<CutPlane>& planes, const std::set<std::pair<int, int>>& spoken_for,
                  bool bind_ends, SectionStats *stats, std::map<std::size_t, BoundaryCycle>& cycles);

/*! A quadric patch's boundary, as cycles, with the plane sections found on it.
 *
 * One place answers this because three ask it - whether a face may be written at
 * all, which sections its faces agree on, and the writer itself - and all three
 * have to agree exactly, because a curve one face writes and its neighbour does
 * not is a hole in the shell.
 *
 * A section's plane comes from one of two places, and which is not a detail.
 * Where the face across it is a *planar* one the plane is that face's own, known
 * exactly and known however short the shared stretch is - a single edge is
 * enough. Only where the neighbour is another quadric is there no plane to be
 * had from anywhere, and then it has to be fitted to three consecutive boundary
 * vertices and grown along the cycle. That fit is the weaker of the two and it
 * is used second: it needs two edges before it can see a plane at all, and a
 * cone cut by a slab meets that cut in a single edge on two of its four
 * regions - which is exactly the case that refused the whole surface while the
 * fit was the only way in.
 *
 * Cycles come back rolled so that no section straddles their seam, since the
 * writer reads a cycle left to right. Sections index into `verts` and never
 * wrap.
 */
std::map<std::size_t, BoundaryCycle> boundaryCycles(
  const AnalyticFeatures::Patch& patch, const std::vector<Vector3d>& vertices,
  const std::vector<std::vector<int>>& loops, const std::vector<char>& loop_valid,
  const std::vector<char>& taken, const std::vector<CutPlane>& planes,
  const std::set<std::pair<int, int>>& spoken_for, bool bind_ends = false,
  SectionStats *stats = nullptr);

}  // namespace step
