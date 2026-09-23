#pragma once

/*! Helpers the recognisers share, for the recognition module's own files only.
 * Nothing outside src/geometry/recognition and AnalyticFeatures.cc includes
 * this; the public surface is AnalyticFeatures.h and the module's headers. */

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "geometry/AnalyticFeatures.h"
#include "geometry/linalg.h"
#include "geometry/recognition/FacetGraph.h"

class Surface;

namespace AnalyticFeatures {

using recognition::EdgeKey;
using recognition::edgeKey;

/*! printf into a std::string, for the report lines recognisers return. */
std::string format(const char *fmt, ...);

/*! The plane through the given vertices, when they lie on one within `tol`.
 * AnalyticFeatures.cc. */
bool fitPlane(const std::vector<Vector3d>& vertices, const std::vector<int>& ids, double tol,
              Vector3d& normal);

/*! How far a facet reaches from edge (a, b): the `c` in the sagitta. Regions.cc. */
double reachAcross(const std::vector<Vector3d>& verts, const std::vector<int>& loop, int a, int b);

/*! The boundary of a region of facets as directed cycles, or why it has none.
 * Regions.cc. */
const char *boundaryCycles(const std::vector<std::vector<int>>& loops,
                           const std::vector<std::size_t>& facets,
                           std::vector<std::vector<int>>& cycles);

/*! The connected pieces of a set of facets, across shared edges. Regions.cc. */
std::vector<std::vector<std::size_t>> connectedComponents(
  const std::vector<std::vector<int>>& loops,
  const std::map<EdgeKey, std::vector<std::size_t>>& edge_loops, const std::vector<std::size_t>& facets);

/*! One patch from a set of facets: its boundary cycles and the runs they split
 * into. Regions.cc. */
Patch patchFromFacets(const std::vector<std::vector<int>>& loops,
                      const std::map<EdgeKey, std::vector<std::size_t>>& edge_loops,
                      const std::shared_ptr<const Surface>& surface,
                      const std::vector<std::size_t>& facets);

}  // namespace AnalyticFeatures
