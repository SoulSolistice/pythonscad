#pragma once

/*! The recognition pipeline - see Pipeline.cc for the stages and their inputs.
 */

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/FacetGraph.h"
#include "geometry/recognition/Membership.h"
#include "geometry/recognition/Representation.h"

class Surface;

namespace recognition {

/*! What recognition reads. Nothing here is written by recognition. */
struct Inputs {
  Mesh mesh;
  const std::vector<std::shared_ptr<Surface>> *surfaces = nullptr;     // the model's declarations
  const std::vector<int32_t> *face_origin = nullptr;                   // provenance: facet -> original
  const std::map<int32_t, std::vector<std::size_t>> *owned = nullptr;  // original -> surfaces
  double model_tol = 0;
  bool approximate = false;  // step-approximate-surfaces: spend the tessellation band
};

/*! The writer's half of "can this region be written".
 *
 * Recognition decides which regions to propose and what to do with a refusal;
 * whether a region's boundary can be written on its surface is the writer's
 * boundary machinery - sections, crossing curves - and it is asked, not
 * reimplemented. The two are circular (a section exists only while the faces
 * on both sides stand), which is why it is asked of a *standing set*. */
struct Writability {
  /*! Called once, with the surface list the passes walk, before any question. */
  std::function<void(const std::vector<std::shared_ptr<Surface>>&)> begin;
  /*! Per standing region, in order: 1 when its whole boundary lies on its
   * surface given the others standing. */
  std::function<std::vector<char>(const std::vector<const AnalyticFeatures::Patch *>&)>
    boundaryOnSurface;
};

/*! What recognition hands the writer. */
struct Recognition {
  AnalyticFeatures::Result features;  // bands, rims, consumed, the ledger
  std::vector<AnalyticFeatures::Patch> bezier_patches;
  std::vector<std::shared_ptr<Surface>> patch_quadric;  // parallel to bezier_patches
  std::set<std::set<int>> circular_runs;                // runs exactly a circular arc
  std::vector<AnalyticFeatures::Patch> grid_faces;      // declared sweeps written as faces
  std::vector<AnalyticFeatures::Patch> quadric_faces;   // trimmed quadrics written as faces
  std::vector<Region> d0_regions;  // each declared surface's D0 region - see Membership.h
  std::vector<RegionRepresentation> d0_representations;  // how each would be written - proposals
};

/*! Run every stage. `out` is filled as the stages run, so a Writability that
 * reads it - the writer's sections look at the sweep faces already decided -
 * sees each stage's result as soon as it exists. */
void runPipeline(const Inputs& in, Writability& writability, Recognition& out);

}  // namespace recognition
