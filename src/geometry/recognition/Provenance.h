#pragma once

/*! Provenance: which original made each facet, and which surfaces it declared.
 *
 * Identity, not measurement - see doc/step-recognition-design.md, *What
 * discriminates instead*, and doc/step-export-wip.md, open item 31.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace recognition {

/*! Who made each facet, so that a recogniser can be told rather than measure.
 *
 * The exporter knows this and has only ever reported it. A facet came from one
 * original solid, that solid declared certain surfaces, and a recogniser asking
 * "is this facet near that surface" is rediscovering by measurement what the
 * generator already said. Every tolerance in that measurement - the tessellation
 * band, the interior test, the outlier rules - exists to make the rediscovery
 * reliable, and none of them is needed for a question the model answers.
 *
 * It gates rather than replaces. Provenance is per original solid and not per
 * surface: a frustum's original owns its cone and both rim circles, and its cap
 * facets belong to that same original without being on the cone. So this says
 * which surfaces a facet *could* be on, and the geometry still says which one it
 * is - a much easier question than the one asked without it.
 *
 * `owned` is identity plus presence (export_step.cc, reportOwnership): an
 * original owns a surface it declared *as a face* (`Surface::owners`, carried
 * from where its id was minted) of which a whole facet of its own is still in
 * the mesh - every corner on the surface by the generator's own points, and not
 * wholly on a declared plane. No band, no count. Until 2026-09-24 it was a
 * measurement: three whole facets facing the surface within 60 degrees.
 *
 * Silent where it knows nothing. An original absent from `owned` declared no
 * surface that survives in its facets - a polyhedron, an import - and surfaces
 * past `declared` were fitted by the approximation pass and have no provenance
 * at all. Both are left to the measurement. */
struct Provenance {
  const std::vector<int32_t> *face_origin = nullptr;
  const std::map<int32_t, std::vector<std::size_t>> *owned = nullptr;
  std::size_t declared = 0;  // surfaces[0, declared) are the model's own

  /*! Whether the maker of `facet` is known not to own `surface`. */
  [[nodiscard]] bool excludes(std::size_t facet, std::size_t surface) const
  {
    if (face_origin == nullptr || owned == nullptr) return false;
    if (surface >= declared) return false;  // fitted: nothing declared it
    if (facet >= face_origin->size()) return false;
    const int32_t id = (*face_origin)[facet];
    if (id < 0) return false;
    const auto it = owned->find(id);
    if (it == owned->end()) return false;  // its maker owns nothing we can use
    return std::find(it->second.begin(), it->second.end(), surface) == it->second.end();
  }

  /*! Whether the maker of `facet` owns `surface`: declared it as a face, and a
   * whole facet of its own is still on it. The positive answer - identity, not
   * a measurement - which `excludes` could only give by its absence, and which
   * is silent (false) wherever `excludes` is. */
  [[nodiscard]] bool owns(std::size_t facet, std::size_t surface) const
  {
    if (face_origin == nullptr || owned == nullptr) return false;
    if (surface >= declared) return false;  // fitted: nothing declared it
    if (facet >= face_origin->size()) return false;
    const int32_t id = (*face_origin)[facet];
    if (id < 0) return false;
    const auto it = owned->find(id);
    if (it == owned->end()) return false;
    return std::find(it->second.begin(), it->second.end(), surface) != it->second.end();
  }
};

}  // namespace recognition
