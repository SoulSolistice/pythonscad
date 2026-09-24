#pragma once

/*! The rims of a set of facets on a declared surface of revolution.
 *
 * A rim is where a band ends: the vertices at one place on the surface - one
 * level along its declared axis and one radius about it. Decided from the
 * declaration at machine precision (recognition/Exact.h), never from circles
 * fitted to the mesh: two vertices share a rim when their levels, and then their
 * radii, are equal within the forward error of computing them. The radius as
 * well as the level, because a torus has two rims at every height.
 *
 * Data, read by whatever needs to know where a region's rims are: the
 * representation stage asks whether a region is rings (Representation.cc), and
 * a band built from a region takes its rims from here.
 */

#include <cstddef>
#include <map>
#include <vector>

#include "geometry/recognition/FacetGraph.h"

class Surface;

namespace recognition {

struct Rims {
  std::vector<int> vertices;       // every vertex of the facets, ascending
  std::vector<std::size_t> level;  // parallel: the vertex's level; groups ascend along the axis
  std::size_t levels = 0;          // how many levels there are
  std::map<int, std::size_t> rim;  // vertex -> its rim, a level and a radius

  [[nodiscard]] std::size_t rimOf(int v) const { return rim.at(v); }
};

/*! The rims of `facets` on the declared surface `s` - see the file comment. */
Rims rimsOf(const Mesh& mesh, const Surface *s, const std::vector<std::size_t>& facets);

}  // namespace recognition
