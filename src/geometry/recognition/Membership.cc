/*! The D0 region of every declared surface - see Membership.h. */
#include "geometry/recognition/Membership.h"

#include <algorithm>
#include <map>

#include "geometry/Surface.h"
#include "geometry/recognition/Exact.h"

namespace recognition {

std::vector<char> onDeclaredPlane(const FacetGraph& graph,
                                  const std::vector<std::shared_ptr<Surface>>& surfaces,
                                  std::size_t declared)
{
  const Mesh& mesh = graph.mesh();
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  std::vector<char> out(loops.size(), 0);
  for (std::size_t j = 0; j < surfaces.size() && j < declared; j++) {
    const Surface *plane = surfaces[j].get();
    if (dynamic_cast<const PlaneSurface *>(plane) == nullptr) continue;
    for (std::size_t f = 0; f < loops.size(); f++) {
      if (out[f] || loops[f].size() < 3) continue;
      bool all = true;
      for (const int v : loops[f]) {
        if (!onExactly(plane, vertices[v])) {
          all = false;
          break;
        }
      }
      if (all) out[f] = 1;
    }
  }
  return out;
}

std::vector<std::size_t> d0Conflicts(const std::vector<Region>& regions)
{
  std::map<std::size_t, std::size_t> first_surface;
  std::vector<std::size_t> out;
  for (const Region& region : regions) {
    for (const std::size_t f : region.facets) {
      const auto it = first_surface.emplace(f, region.surface_index).first;
      if (it->second != region.surface_index) out.push_back(f);
    }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<Region> d0Regions(const FacetGraph& graph,
                              const std::vector<std::shared_ptr<Surface>>& surfaces,
                              std::size_t declared, const Provenance& provenance)
{
  const Mesh& mesh = graph.mesh();
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;
  const std::vector<char> planar = onDeclaredPlane(graph, surfaces, declared);

  std::vector<Region> out;
  for (std::size_t j = 0; j < surfaces.size() && j < declared; j++) {
    const Surface *s = surfaces[j].get();
    if (dynamic_cast<const AxialSurface *>(s) == nullptr &&
        dynamic_cast<const SphereSurface *>(s) == nullptr &&
        dynamic_cast<const TorusSurface *>(s) == nullptr) {
      continue;
    }
    std::vector<char> member(loops.size(), 0);
    for (std::size_t f = 0; f < loops.size(); f++) {
      if (!valid[f] || is_hole[f] || loops[f].size() < 3 || planar[f]) continue;
      if (!provenance.owns(f, j)) continue;
      bool exact = true;
      for (const int v : loops[f]) {
        if (!onExactly(s, vertices[v])) {
          exact = false;
          break;
        }
      }
      if (exact) member[f] = 1;
    }
    // Connected components across mesh edges, lowest facet first.
    std::vector<char> seen(loops.size(), 0);
    for (std::size_t seed = 0; seed < loops.size(); seed++) {
      if (!member[seed] || seen[seed]) continue;
      Region region;
      region.surface_index = j;
      std::vector<std::size_t> stack{seed};
      seen[seed] = 1;
      while (!stack.empty()) {
        const std::size_t f = stack.back();
        stack.pop_back();
        region.facets.push_back(f);
        const std::vector<int>& loop = loops[f];
        for (std::size_t i = 0; i < loop.size(); i++) {
          const auto it = graph.edgeLoops().find(edgeKey(loop[i], loop[(i + 1) % loop.size()]));
          if (it == graph.edgeLoops().end()) continue;
          for (const std::size_t g : it->second) {
            if (g < member.size() && member[g] && !seen[g]) {
              seen[g] = 1;
              stack.push_back(g);
            }
          }
        }
      }
      std::sort(region.facets.begin(), region.facets.end());
      out.push_back(std::move(region));
    }
  }
  return out;
}

}  // namespace recognition
