#include "geometry/recognition/FacetGraph.h"

namespace recognition {

FacetGraph::FacetGraph(const Mesh& mesh) : mesh_(mesh)
{
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& valid = *mesh.valid;
  for (std::size_t f = 0; f < loops.size(); f++) {
    if (!valid[f]) continue;
    const std::vector<int>& loop = loops[f];
    for (std::size_t i = 0; i < loop.size(); i++) {
      edge_loops_[edgeKey(loop[i], loop[(i + 1) % loop.size()])].push_back(f);
    }
  }
}

}  // namespace recognition
