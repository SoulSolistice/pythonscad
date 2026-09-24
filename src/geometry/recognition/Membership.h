#pragma once

/*! The D0 region of every declared surface: which facets belong to it, decided
 * by declarations alone and by nothing any recogniser claimed.
 *
 * doc/step-recognition-design.md, *R0 is region first, and the region is
 * sequence-agnostic*. A facet belongs to a declared surface at D0 when
 *
 *   - its maker owns the surface: declared it as a face and still has a whole
 *     facet on it (Provenance::owns - identity plus presence);
 *   - every corner is on the surface at machine precision (recognition::Exact);
 *   - it does not lie wholly on a declared plane - a cap has every corner on
 *     the rim, and it is a facet of its plane, not of the curved surface.
 *
 * Nothing here is measured against the tessellation and nothing reads a claim,
 * so the regions are the same whatever order anything runs in. A region is one
 * connected component of those facets across mesh edges. Planes are not
 * covered: a plane's region is a different question, answered by the writer's
 * own planar faces.
 *
 * Not yet covered, and named so it is not mistaken for complete: a facet a
 * boolean *cut*, whose cut corner lies on no surface exactly. It belongs to its
 * maker's surface all the same, and where the cut corner belongs is the corner
 * ladder's question - so until that is answered at D0 a region stops at the
 * cut, and a band on the whole facets beside a patch on the cut ones does not
 * read as split here. doc/step-export-wip.md, open item 34.
 */

#include <cstddef>
#include <memory>
#include <vector>

#include "geometry/recognition/FacetGraph.h"
#include "geometry/recognition/Provenance.h"

class Surface;

namespace recognition {

/*! One connected component of the D0 members of one declared surface. */
struct Region {
  std::size_t surface_index = 0;
  std::vector<std::size_t> facets;  // ascending
};

/*! Per facet: 1 where every corner lies on one declared plane at machine
 * precision. `surfaces[0, declared)` are the model's own. */
std::vector<char> onDeclaredPlane(const FacetGraph& graph,
                                  const std::vector<std::shared_ptr<Surface>>& surfaces,
                                  std::size_t declared);

/*! The D0 regions of every declared cylinder, cone, sphere and torus - see the
 * file comment. Pure: a function of the graph, the declarations and
 * provenance. Ordered by surface, then by lowest facet. */
std::vector<Region> d0Regions(const FacetGraph& graph,
                              const std::vector<std::shared_ptr<Surface>>& surfaces,
                              std::size_t declared, const Provenance& provenance);

}  // namespace recognition
