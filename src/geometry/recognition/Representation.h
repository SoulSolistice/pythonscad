#pragma once

/*! How each D0 region would be written: the representation chosen for the
 * region, second - never the region carved to fit a representation.
 *
 * doc/step-recognition-design.md, *R0 is region first, and the region is
 * sequence-agnostic*; doc/step-design-checklist.md, B1-B3. A region (see
 * Membership.h) is decided by provenance and exact membership alone. Here each
 * one is given the representation that fits it:
 *
 *   rings   every facet is a wall between two rims of the declared surface - a
 *           band, or a stack of bands sharing rims. On a sphere, a facet lying
 *           on one rim only is a polar cap and belongs to the zone.
 *   patch   a cylinder or cone region that is not rings: one trimmed face over
 *           the whole region, where one can be formed.
 *   none    a sphere or torus region that is not a zone - this writer has no
 *           trimmed sphere or torus face. A named gap, not a proof that no
 *           closed form exists.
 *
 * A rim is decided from the declaration at machine precision, never fitted: a
 * vertex's axial level and its radius about the declared axis, grouped only
 * within the forward error of computing them (recognition/Exact.h). The radius
 * matters as well as the level because a torus has two rims at every height.
 *
 * Pure: a function of the graph, the declarations and the regions. Nothing is
 * granted here yet - the representations are proposals, reported against what
 * holds each region today (recognition::reportRepresentations), which is the
 * before-measurement for granting them.
 */

#include <cstddef>
#include <memory>
#include <vector>

#include "geometry/recognition/FacetGraph.h"
#include "geometry/recognition/Membership.h"

class Surface;

namespace recognition {

struct RegionRepresentation {
  enum Kind { Rings, Patch, None };
  std::size_t region = 0;  // index into the regions it was computed from
  Kind kind = None;
  /*! For Rings: the facets of each ring - a wall between one pair of rims -
   * and whether it closes on itself; caps are listed apart. */
  std::vector<std::vector<std::size_t>> rings;
  std::vector<char> ring_closed;
  std::vector<std::size_t> caps;
  /*! Why not rings, where it is not - a string literal naming the rule. */
  const char *not_rings = nullptr;
  /*! For a cylinder or cone that is not rings: whether one patch over the whole
   * region can be formed, and why not where it cannot. */
  bool patch_formable = false;
  const char *not_patch = nullptr;
};

/*! One representation per region, in the regions' order. See the file
 * comment. */
std::vector<RegionRepresentation> representRegions(const FacetGraph& graph,
                                                   const std::vector<std::shared_ptr<Surface>>& surfaces,
                                                   const std::vector<Region>& regions);

}  // namespace recognition
