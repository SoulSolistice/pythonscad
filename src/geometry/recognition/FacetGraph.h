#pragma once

/*! The mesh recognition reads, and its adjacency, built once.
 *
 * Every recogniser used to build the same edge -> loops map for itself - four
 * copies of one derivation, each free to drift. The graph is the one read-only
 * input every proposer shares; nothing in recognition mutates it.
 */

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

#include "geometry/linalg.h"

namespace recognition {

/*! The mesh the recogniser reads, as the caller already has it.
 *
 * `loops` are cleaned polygons - no repeated vertices, indices canonical, so
 * two loops naming the same corner name it with the same index. The four
 * vectors are parallel and one entry long per loop. */
struct Mesh {
  const std::vector<Vector3d> *vertices = nullptr;
  const std::vector<std::vector<int>> *loops = nullptr;
  const std::vector<char> *valid = nullptr;    // 0 for a loop to ignore
  const std::vector<char> *is_hole = nullptr;  // 0 for an outer bound
  const std::vector<Vector3d> *normals = nullptr;
};

/*! An edge of a facet, as an unordered vertex pair. */
using EdgeKey = std::pair<int, int>;
inline EdgeKey edgeKey(int a, int b)
{
  return a < b ? EdgeKey(a, b) : EdgeKey(b, a);
}

/*! Which valid loops use each mesh edge - the adjacency every recogniser walks.
 * Built once per export from `Mesh`, over valid loops only, holes included. */
class FacetGraph
{
public:
  explicit FacetGraph(const Mesh& mesh);
  [[nodiscard]] const Mesh& mesh() const { return mesh_; }
  [[nodiscard]] const std::map<EdgeKey, std::vector<std::size_t>>& edgeLoops() const
  {
    return edge_loops_;
  }

private:
  Mesh mesh_;
  std::map<EdgeKey, std::vector<std::size_t>> edge_loops_;
};

}  // namespace recognition
