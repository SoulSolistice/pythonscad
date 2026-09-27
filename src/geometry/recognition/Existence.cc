/*! Whether the model's solid has a face where a facet was made - see Existence.h. */
#include "geometry/recognition/Existence.h"

#include "geometry/SolidTree.h"
#include "geometry/Surface.h"
#include "geometry/recognition/Classify.h"
#include "geometry/recognition/Provenance.h"

namespace recognition {

std::vector<Exists> existence(const FacetGraph& graph,
                              const std::vector<std::shared_ptr<Surface>>& surfaces,
                              const Provenance& provenance, const SolidTree *tree)
{
  if (tree == nullptr || provenance.face_surface == nullptr) return {};
  const Mesh& mesh = graph.mesh();
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  std::vector<Exists> out(loops.size(), Exists::NotAsked);
  for (std::size_t f = 0; f < loops.size(); f++) {
    if (f >= provenance.face_surface->size()) break;
    const int32_t made = (*provenance.face_surface)[f];
    if (made < 0 || std::size_t(made) >= surfaces.size() || loops[f].size() < 3) continue;
    const Surface *surface = surfaces[made].get();
    // The facet's own points - its corners and its centroid - each taken onto
    // the surface it was made on, where the question is asked.
    std::vector<Vector3d> points;
    Vector3d centroid(0, 0, 0);
    for (const int v : loops[f]) {
      points.push_back(vertices[v]);
      centroid += vertices[v];
    }
    points.push_back(centroid / double(loops[f].size()));
    bool decided_off = true, face = false;
    for (std::size_t k = 0; k < points.size(); k++) {
      Vector3d on;
      if (!surface->closestPoint(points[k], on)) {
        decided_off = false;
        continue;
      }
      const Classification c = classify(*tree, on);
      if (c.where == Where::On || c.where == Where::Unknown) decided_off = false;
      // A face of this surface at the centroid, the last point.
      if (k + 1 == points.size() && c.where == Where::On) {
        for (const Surface *bound : c.on) face = face || describesSameSurface(*bound, *surface);
      }
    }
    out[f] = decided_off ? Exists::Phantom : face ? Exists::Face : Exists::Undecided;
  }
  return out;
}

}  // namespace recognition
