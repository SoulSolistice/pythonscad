/*! How each D0 region would be written - see Representation.h.
 *
 * One decision per function, data between them: the rims (Rims.h), then whether
 * the region is rings (ringsOf), then whether it is one ring cut by a declared
 * plane (tiltedRing), then whether one patch forms over it (patchOf).
 * representRegions only composes them.
 */
#include "geometry/recognition/Representation.h"

#include <map>
#include <set>
#include <utility>

#include "geometry/AnalyticFeatures.h"
#include "geometry/Surface.h"
#include "geometry/recognition/Exact.h"
#include "geometry/recognition/Internal.h"
#include "geometry/recognition/Rims.h"

namespace recognition {

namespace {

/*! Whether a region is rings: every facet a wall between exactly two rims, or -
 * where `pole` allows it - a cap on one. */
struct RingSplit {
  std::vector<std::vector<std::size_t>> rings;
  std::vector<char> closed;
  std::vector<std::size_t> caps;
  const char *not_rings = nullptr;  // the rule that says it is not, if one does
};

RingSplit ringsOf(const std::vector<std::vector<int>>& loops, const std::vector<std::size_t>& facets,
                  const Rims& rims, bool pole)
{
  RingSplit out;
  std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t>> by_rims;
  for (const std::size_t f : facets) {
    std::set<std::size_t> on;
    for (const int v : loops[f]) on.insert(rims.rimOf(v));
    if (on.size() == 2) {
      by_rims[{*on.begin(), *on.rbegin()}].push_back(f);
    } else if (on.size() == 1) {
      out.caps.push_back(f);
    } else if (out.not_rings == nullptr) {
      out.not_rings = "a facet spans more than two rims of the surface";
    }
  }
  if (out.not_rings == nullptr && !out.caps.empty() && !pole) {
    out.not_rings = "a facet lies on one rim only, and only a sphere closes at a pole";
  }
  if (out.not_rings != nullptr) return out;
  for (const auto& kv : by_rims) {
    // Closed: as many facets as vertices on either rim; a part turn has one
    // vertex more on each rim than it has facets.
    std::set<int> low, high;
    for (const std::size_t f : kv.second) {
      for (const int v : loops[f]) (rims.rimOf(v) == kv.first.first ? low : high).insert(v);
    }
    const bool closed = low.size() == kv.second.size() && high.size() == kv.second.size();
    const bool part = low.size() == kv.second.size() + 1 && high.size() == kv.second.size() + 1;
    if (!closed && !part) {
      out.not_rings = "a ring's rims do not have one vertex per wall, or one more";
      return out;
    }
    out.rings.push_back(kv.second);
    out.closed.push_back(closed ? 1 : 0);
  }
  return out;
}

/*! Whether a cylinder region is one full ring whose far rim is cut by a plane
 * at an angle: one flat rim, and every other vertex on one *declared* plane
 * that crosses the axis - which is what an oblique cut is, and what the model
 * states. The band pass takes this case by fitting a plane to the mesh at a
 * tolerance; here it is a declaration asked at machine precision. A full turn of
 * a cylinder only, as the band writes it: a cone cut off-axis has no
 * single-radius rim, and a part turn would need an elliptical arc. */
bool tiltedRing(const Mesh& mesh, const std::vector<std::shared_ptr<Surface>>& surfaces,
                const CylinderSurface& cyl, const std::vector<std::size_t>& facets, const Rims& rims)
{
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  if (rims.levels == 0) return false;
  const Vector3d axis = cyl.normdir.normalized();
  // the flat rim: the lowest level, or the highest
  for (const std::size_t flat : {std::size_t(0), rims.levels - 1}) {
    std::vector<int> on_flat, rest;
    for (std::size_t i = 0; i < rims.vertices.size(); i++) {
      (rims.level[i] == flat ? on_flat : rest).push_back(rims.vertices[i]);
    }
    if (on_flat.size() != facets.size() || rest.size() != facets.size()) continue;
    const Surface *cut = nullptr;
    for (const auto& candidate : surfaces) {
      const auto *plane = dynamic_cast<const PlaneSurface *>(candidate.get());
      if (plane == nullptr) continue;
      // crosses the axis: a plane containing the axis direction cuts a
      // cylinder in straight lines, and there is no rim there to write. At
      // machine precision: two normalisations (9 each) and a dot (5).
      if (fabs(plane->normdir.normalized().dot(axis)) <= forwardError(23, 2.0)) continue;
      bool all = true;
      for (const int v : rest) all = all && onExactly(plane, vertices[v]);
      if (all) {
        cut = plane;
        break;
      }
    }
    if (cut == nullptr) continue;
    const std::set<int> flat_set(on_flat.begin(), on_flat.end());
    bool walls = true;
    for (const std::size_t f : facets) {
      std::size_t n_flat = 0;
      for (const int v : loops[f]) n_flat += flat_set.count(v);
      walls = walls && n_flat == 2 && loops[f].size() == 4;
    }
    if (walls) return true;
  }
  return false;
}

/*! Whether one patch forms over the whole region - and why not where it does
 * not. Only a cylinder or a cone has a trimmed face in this writer. */
void patchOf(const FacetGraph& graph, const std::shared_ptr<Surface>& surface,
             const std::vector<std::size_t>& facets, RegionRepresentation& rep)
{
  const Surface *s = surface.get();
  if (dynamic_cast<const CylinderSurface *>(s) == nullptr &&
      dynamic_cast<const ConeSurface *>(s) == nullptr) {
    rep.not_patch = "no trimmed face of this kind of surface is written";
    rep.kind = RegionRepresentation::None;
    return;
  }
  const AnalyticFeatures::Patch patch =
    AnalyticFeatures::patchFromFacets(*graph.mesh().loops, graph.edgeLoops(), surface, facets);
  rep.patch_formable = patch.alive;
  rep.not_patch = patch.alive ? nullptr : patch.dropped;
  rep.kind = patch.alive ? RegionRepresentation::Patch : RegionRepresentation::None;
}

}  // namespace

std::vector<RegionRepresentation> representRegions(const FacetGraph& graph,
                                                   const std::vector<std::shared_ptr<Surface>>& surfaces,
                                                   const std::vector<Region>& regions)
{
  const Mesh& mesh = graph.mesh();
  std::vector<RegionRepresentation> out;
  for (std::size_t r = 0; r < regions.size(); r++) {
    const Region& region = regions[r];
    const std::shared_ptr<Surface>& surface = surfaces[region.surface_index];
    RegionRepresentation rep;
    rep.region = r;

    const Rims rims = rimsOf(mesh, surface.get(), region.facets);
    const bool pole = dynamic_cast<const SphereSurface *>(surface.get()) != nullptr;
    RingSplit split = ringsOf(*mesh.loops, region.facets, rims, pole);
    if (split.not_rings != nullptr) {
      const auto *cyl = dynamic_cast<const CylinderSurface *>(surface.get());
      if (cyl != nullptr && tiltedRing(mesh, surfaces, *cyl, region.facets, rims)) {
        split = RingSplit{{region.facets}, {1}, {}, nullptr};
      }
    }
    if (split.not_rings == nullptr) {
      rep.kind = RegionRepresentation::Rings;
      rep.rings = std::move(split.rings);
      rep.ring_closed = std::move(split.closed);
      rep.caps = std::move(split.caps);
    } else {
      rep.not_rings = split.not_rings;
      patchOf(graph, surface, region.facets, rep);
    }
    out.push_back(std::move(rep));
  }
  return out;
}

}  // namespace recognition
