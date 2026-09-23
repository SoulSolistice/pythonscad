/*! What every recogniser shares about a set of facets: its connected pieces,
 * its boundary cycles, the patch they make, and how far a facet reaches.
 *
 * Part of the recognition module - see doc/step-recognition-design.md, *The
 * recognition module, as built*. Moved out of AnalyticFeatures.cc as it stood.
 */
#include "geometry/AnalyticFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

#include "geometry/Surface.h"
#include "geometry/recognition/Internal.h"
#include "geometry/recognition/Ledger.h"

namespace AnalyticFeatures {

/*! Length of the facet across an edge: how far the facet reaches from that edge.
 *
 * This is the `c` in the sagitta, and it has to be the span the surface curves
 * over rather than the edge's own length - a long thin facet curves over its
 * width, not its length. */
double reachAcross(const std::vector<Vector3d>& verts, const std::vector<int>& loop, int a, int b)
{
  const Vector3d& pa = verts[a];
  const Vector3d& pb = verts[b];
  Vector3d along = pb - pa;
  const double len = along.norm();
  if (len < 1e-12) return 0.0;
  along /= len;
  double reach = 0.0;
  for (const int v : loop) {
    const Vector3d rel = verts[v] - pa;
    reach = std::max(reach, (rel - along * rel.dot(along)).norm());
  }
  return reach;
}

/*! The boundary of a region of facets, as directed cycles.
 *
 * Directed in the sense the region's own facets traverse it. That is what makes
 * a collapsed face agree with the mesh it replaces, and it is what forces the
 * neighbour on the far side of every boundary segment to traverse it the other
 * way round - the invariant that keeps the shell closed. An undirected walk
 * gives an arbitrary direction, and half the faces then share an edge in the
 * same direction as their neighbour.
 *
 * There can be more than one. A fillet patch is a disc and has exactly one, but
 * a declared sweep closed around its profile is a tube: trim it against a wall
 * and what is left is an annulus, whose boundary is two cycles and neither of
 * them closes over the other. Walking only the first reported that the boundary
 * did not close, which was true of the walk rather than of the region.
 *
 * Returns null on success, or why the region has no such boundary - which is
 * always a reason to leave it faceted. */
const char *boundaryCycles(const std::vector<std::vector<int>>& loops,
                           const std::vector<std::size_t>& facets, std::vector<std::vector<int>>& cycles)
{
  // Edges used by one of the region's facets rather than two. Anything else
  // means the region is not a simple sheet.
  std::map<EdgeKey, int> uses;
  for (const std::size_t f : facets) {
    const std::vector<int>& loop = loops[f];
    for (std::size_t i = 0; i < loop.size(); i++) {
      uses[edgeKey(loop[i], loop[(i + 1) % loop.size()])]++;
    }
  }
  std::map<int, int> next;  // boundary vertex -> the next one along
  std::size_t boundary_edges = 0;
  for (const std::size_t f : facets) {
    const std::vector<int>& loop = loops[f];
    for (std::size_t i = 0; i < loop.size(); i++) {
      const int a = loop[i], b = loop[(i + 1) % loop.size()];
      if (uses[edgeKey(a, b)] != 1) continue;
      next[a] = b;
      boundary_edges++;
    }
  }
  if (boundary_edges == 0 || next.size() != boundary_edges) {
    return "the facets on this patch do not form a simple sheet";
  }

  cycles.clear();
  std::set<int> walked;
  std::size_t seen = 0;
  for (const auto& entry : next) {
    if (walked.count(entry.first)) continue;
    std::vector<int> cycle;
    int cur = entry.first;
    do {
      cycle.push_back(cur);
      walked.insert(cur);
      cur = next[cur];
    } while (cur != entry.first && !walked.count(cur) && cycle.size() <= boundary_edges);
    if (cur != entry.first) return "the patch boundary does not close";
    seen += cycle.size();
    cycles.push_back(std::move(cycle));
  }
  if (seen != boundary_edges) return "the patch boundary does not close";
  return nullptr;
}

/*! The facets of a claim, split into the pieces that actually touch.
 *
 * A claim is a set of facets agreeing with one surface, and the booleans have
 * no reason to leave it in one piece: a helical ridge cut by a wall comes back
 * as two strips of the same sweep, the same size, somewhere else on it.
 *
 * Written as one face those become one ADVANCED_FACE with two outer loops,
 * which is not a face - and since the outer bound is then chosen by area in
 * parameter space, the second region is labelled a hole in the first. A face's
 * inner bounds have to lie inside its outer one, and two separate patches of a
 * surface are not a face with a hole.
 *
 * OpenCASCADE splits such a face on read and says nothing, which is why its
 * census reported more faces than the file ever contained and why no round trip
 * flagged it. SOLIDWORKS does not split, and builds what it can of one face
 * with two disjoint outer loops; a thread came back shredded that way.
 */
std::vector<std::vector<std::size_t>> connectedComponents(
  const std::vector<std::vector<int>>& loops,
  const std::map<EdgeKey, std::vector<std::size_t>>& edge_loops, const std::vector<std::size_t>& facets)
{
  std::map<std::size_t, std::size_t> slot;
  for (std::size_t i = 0; i < facets.size(); i++) slot[facets[i]] = i;

  std::vector<std::size_t> parent(facets.size());
  for (std::size_t i = 0; i < parent.size(); i++) parent[i] = i;
  std::function<std::size_t(std::size_t)> find = [&](std::size_t x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };

  for (std::size_t i = 0; i < facets.size(); i++) {
    const std::vector<int>& loop = loops[facets[i]];
    for (std::size_t k = 0; k < loop.size(); k++) {
      const auto it = edge_loops.find(edgeKey(loop[k], loop[(k + 1) % loop.size()]));
      if (it == edge_loops.end()) continue;
      for (const std::size_t other : it->second) {
        const auto jt = slot.find(other);
        if (jt == slot.end()) continue;
        const std::size_t a = find(i), b = find(jt->second);
        if (a != b) parent[a] = b;
      }
    }
  }

  std::map<std::size_t, std::vector<std::size_t>> grouped;
  for (std::size_t i = 0; i < facets.size(); i++) grouped[find(i)].push_back(facets[i]);
  std::vector<std::vector<std::size_t>> out;
  out.reserve(grouped.size());
  for (auto& entry : grouped) out.push_back(std::move(entry.second));
  return out;
}

Patch patchFromFacets(const std::vector<std::vector<int>>& loops,
                      const std::map<EdgeKey, std::vector<std::size_t>>& edge_loops,
                      const std::shared_ptr<const Surface>& surface,
                      const std::vector<std::size_t>& facets)
{
  Patch patch;
  patch.surface = surface;
  patch.facets = facets;

  std::vector<std::vector<int>> cycles;
  if (const char *why = boundaryCycles(loops, patch.facets, cycles)) {
    patch.alive = false;
    patch.dropped = why;
    return patch;
  }

  // Where a Bezier patch and a declared grid part company.
  //
  // A Bezier covers its whole parameter square, so every boundary segment
  // lies on one of the square's four edges and the split into runs follows
  // the geometry: this stretch is the curve u=0, that one is v=1. A declared
  // grid is trimmed wherever the boolean cut it, and its boundary therefore
  // lies nowhere in particular - which is the whole reason the facets had to
  // be claimed by projection rather than by position.
  //
  // So the split follows the *topology* instead: a run is a maximal stretch
  // of consecutive boundary segments with the same thing on the far side.
  // That is the property a run actually needs - every segment of it is
  // replaced in one neighbouring face, by one curve, or the shell opens -
  // and it is the one the parameter square was standing in for all along.
  const std::set<std::size_t> mine(patch.facets.begin(), patch.facets.end());
  bool ok = true;
  for (std::size_t ci = 0; ci < cycles.size() && ok; ci++) {
    const std::vector<int>& cycle = cycles[ci];
    const std::size_t n = cycle.size();
    std::vector<std::size_t> across(n, std::size_t(-1));
    for (std::size_t i = 0; i < n; i++) {
      const auto it = edge_loops.find(edgeKey(cycle[i], cycle[(i + 1) % n]));
      if (it == edge_loops.end() || it->second.size() != 2) {
        ok = false;
        break;
      }
      across[i] = mine.count(it->second[0]) ? it->second[1] : it->second[0];
    }
    if (!ok) break;

    std::size_t begin = 0;
    while (begin < n && across[begin] == across[(begin + n - 1) % n]) begin++;
    if (begin == n) begin = 0;  // one neighbour all the way round
    for (std::size_t i = 0; i < n;) {
      const std::size_t neighbour = across[(begin + i) % n];
      Patch::Run run;
      // A grid's run lies on no edge of a parameter square and is straight
      // only by accident, so neither field means anything here; the emitter
      // reads `kind`, `verts` and `bound`.
      run.bound = ci;
      // Which face is across the run. The split was made on it, so it is known
      // here for nothing, and the exporter needs it to hand that face a curve
      // when one replaces this run - a plane section of a quadric, say, whose
      // plane is the neighbour's own and is not otherwise recoverable. `kind`
      // stays UNRESOLVED: this says which face, not where in it, and a consumer
      // that needs the position still has to ask.
      run.loop = neighbour;
      std::size_t j = i;
      run.verts.push_back(cycle[(begin + j) % n]);
      while (j < n && across[(begin + j) % n] == neighbour) {
        j++;
        run.verts.push_back(cycle[(begin + j) % n]);
      }
      patch.runs.push_back(std::move(run));
      i = j;
    }
  }
  if (!ok) {
    patch.alive = false;
    patch.dropped = "a boundary segment is not shared with exactly one other face";
  }
  return patch;
}

}  // namespace AnalyticFeatures
