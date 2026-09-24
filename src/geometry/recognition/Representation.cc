/*! How each D0 region would be written - see Representation.h. */
#include "geometry/recognition/Representation.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "geometry/AnalyticFeatures.h"
#include "geometry/Surface.h"
#include "geometry/recognition/Exact.h"
#include "geometry/recognition/Internal.h"

namespace recognition {

namespace {

/*! A vertex's place on a surface of revolution: its level along the declared
 * axis and its radius about it, each with the forward error of computing it. */
struct AxialPlace {
  double t = 0, rho = 0;
  double t_err = 0, rho_err = 0;
};

AxialPlace placeOf(const Surface *s, const Vector3d& p)
{
  AxialPlace out;
  // axis: 9 roundings; p - refpt: 3; the dot: 5 - 17 for the level. The radius
  // adds axis * t (3), the difference (3) and a norm (6) - 29.
  const Vector3d axis = s->normdir.normalized();
  const Vector3d rel = p - s->refpt;
  out.t = rel.dot(axis);
  out.rho = (rel - axis * out.t).norm();
  const double scale = p.norm() + s->refpt.norm();
  out.t_err = forwardError(17, scale);
  out.rho_err = forwardError(29, scale);
  return out;
}

/*! Group values that are equal at machine precision: sorted, a value within the
 * two values' combined error bounds of the group's first joins it. Returns, per
 * input, its group's index. */
std::vector<std::size_t> groupEqual(const std::vector<double>& value, const std::vector<double>& err)
{
  std::vector<std::size_t> order(value.size());
  for (std::size_t i = 0; i < order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(),
            [&](std::size_t a, std::size_t b) { return value[a] < value[b]; });
  std::vector<std::size_t> group(value.size(), 0);
  std::size_t g = 0, first = 0;
  for (std::size_t k = 0; k < order.size(); k++) {
    const std::size_t i = order[k];
    if (k == 0) {
      first = i;
    } else if (value[i] - value[first] > err[i] + err[first]) {
      g++;
      first = i;
    }
    group[i] = g;
  }
  return group;
}

}  // namespace

std::vector<RegionRepresentation> representRegions(const FacetGraph& graph,
                                                   const std::vector<std::shared_ptr<Surface>>& surfaces,
                                                   const std::vector<Region>& regions)
{
  const Mesh& mesh = graph.mesh();
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  std::vector<RegionRepresentation> out;
  for (std::size_t r = 0; r < regions.size(); r++) {
    const Region& region = regions[r];
    RegionRepresentation rep;
    rep.region = r;
    const Surface *s =
      region.surface_index < surfaces.size() ? surfaces[region.surface_index].get() : nullptr;
    const bool revolved_patchable = dynamic_cast<const CylinderSurface *>(s) != nullptr ||
                                    dynamic_cast<const ConeSurface *>(s) != nullptr;
    const bool sphere = dynamic_cast<const SphereSurface *>(s) != nullptr;

    // The rims: every vertex of the region, keyed by level then radius, each
    // grouped at machine precision.
    std::vector<int> verts;
    for (const std::size_t f : region.facets)
      verts.insert(verts.end(), loops[f].begin(), loops[f].end());
    std::sort(verts.begin(), verts.end());
    verts.erase(std::unique(verts.begin(), verts.end()), verts.end());
    std::vector<double> t, t_err, rho, rho_err;
    for (const int v : verts) {
      const AxialPlace place = placeOf(s, vertices[v]);
      t.push_back(place.t);
      t_err.push_back(place.t_err);
      rho.push_back(place.rho);
      rho_err.push_back(place.rho_err);
    }
    const std::vector<std::size_t> level = groupEqual(t, t_err);
    // Within each level, by radius.
    std::map<int, std::size_t> rim_of;
    {
      std::map<std::size_t, std::vector<std::size_t>> at_level;
      for (std::size_t i = 0; i < verts.size(); i++) at_level[level[i]].push_back(i);
      std::size_t next = 0;
      for (const auto& kv : at_level) {
        std::vector<double> rr, re;
        for (const std::size_t i : kv.second) {
          rr.push_back(rho[i]);
          re.push_back(rho_err[i]);
        }
        const std::vector<std::size_t> g = groupEqual(rr, re);
        std::size_t most = 0;
        for (std::size_t k = 0; k < kv.second.size(); k++) {
          rim_of[verts[kv.second[k]]] = next + g[k];
          most = std::max(most, g[k]);
        }
        next += most + 1;
      }
    }

    // Each facet: a wall between exactly two rims, or a cap on one.
    std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t>> by_rims;
    std::set<std::size_t> cap_rims;
    for (const std::size_t f : region.facets) {
      std::set<std::size_t> on;
      for (const int v : loops[f]) on.insert(rim_of[v]);
      if (on.size() == 2) {
        by_rims[{*on.begin(), *on.rbegin()}].push_back(f);
      } else if (on.size() == 1) {
        rep.caps.push_back(f);
        cap_rims.insert(*on.begin());
      } else if (rep.not_rings == nullptr) {
        rep.not_rings = "a facet spans more than two rims of the surface";
      }
    }
    if (rep.not_rings == nullptr && !rep.caps.empty() && !sphere) {
      rep.not_rings = "a facet lies on one rim only, and only a sphere closes at a pole";
    }
    if (rep.not_rings == nullptr) {
      for (auto& kv : by_rims) {
        // Closed: as many facets as vertices on either rim; a part turn has one
        // vertex more on each rim than it has facets.
        std::set<int> low, high;
        for (const std::size_t f : kv.second) {
          for (const int v : loops[f]) (rim_of[v] == kv.first.first ? low : high).insert(v);
        }
        const bool closed = low.size() == kv.second.size() && high.size() == kv.second.size();
        const bool part = low.size() == kv.second.size() + 1 && high.size() == kv.second.size() + 1;
        if (!closed && !part) {
          rep.not_rings = "a ring's rims do not have one vertex per wall, or one more";
          break;
        }
        rep.rings.push_back(kv.second);
        rep.ring_closed.push_back(closed ? 1 : 0);
      }
    }
    if (rep.not_rings == nullptr) {
      rep.kind = RegionRepresentation::Rings;
    } else {
      rep.rings.clear();
      rep.ring_closed.clear();
      rep.caps.clear();
      if (revolved_patchable) {
        const AnalyticFeatures::Patch patch = AnalyticFeatures::patchFromFacets(
          loops, graph.edgeLoops(), surfaces[region.surface_index], region.facets);
        rep.patch_formable = patch.alive;
        rep.not_patch = patch.alive ? nullptr : patch.dropped;
        rep.kind = patch.alive ? RegionRepresentation::Patch : RegionRepresentation::None;
      } else {
        rep.not_patch = "no trimmed face of this kind of surface is written";
        rep.kind = RegionRepresentation::None;
      }
    }
    out.push_back(std::move(rep));
  }
  return out;
}

}  // namespace recognition
