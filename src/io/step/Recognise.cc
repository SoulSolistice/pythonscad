/*! Recognition, asked from the writer - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <memory>
#include <vector>
#include "geometry/recognition/Pipeline.h"
#include "geometry/recognition/Reports.h"

void StepKernel::BodyBuild::recognise()
{
  features.consumed.assign(face_cnt, 0);  // nothing collapsed unless it says so
  if (analytic) {
    recognition::Writability writability;
    writability.begin = [&](const std::vector<std::shared_ptr<Surface>>& passes) {
      for (std::size_t si = 0; si < passes.size(); si++) {
        const auto *pl = dynamic_cast<const PlaneSurface *>(passes[si].get());
        if (pl == nullptr) continue;
        declared_planes.emplace_back(pl->refpt, pl->normdir.normalized());
        declared_plane_index.push_back(si < surfaces.size() ? si : surfaces.size());
      }
    };
    writability.boundaryOnSurface = [&](const std::vector<const AnalyticFeatures::Patch *>& standing) {
      decideSections(standing);
      std::vector<char> ok;
      ok.reserve(standing.size());
      for (const auto *patch : standing)
        ok.push_back(boundaryLiesOnSurface(*patch, section_curves) ? 1 : 0);
      return ok;
    };
    recognition::Inputs inputs;
    inputs.mesh.vertices = &vertices;
    inputs.mesh.loops = &loops;
    inputs.mesh.valid = &loop_valid;
    inputs.mesh.is_hole = &loop_is_hole;
    inputs.mesh.normals = &loop_normals;
    inputs.surfaces = &surfaces;
    inputs.face_origin = &faceOrigin;
    inputs.face_surface = &faceSurface;
    inputs.owned = &owned;
    inputs.solid_tree = solidTree;
    inputs.model_tol = model_tol;
    inputs.approximate = approximate;
    recognition::runPipeline(inputs, writability, rec);
  }
}

void StepKernel::BodyBuild::reportRecognition()
{
  const recognition::ReportInputs report_inputs{
    features,
    surfaces,
    recognition::Mesh{&vertices, &loops, &loop_valid, &loop_is_hole, &loop_normals},
    faceOrigin,
    owned,
    &rec.d0_regions,
    &rec.d0_representations,
    &rec.existence};
  recognition::dumpLedger(report_inputs);
  recognition::reportArbitration(report_inputs);
  recognition::reportExistence(report_inputs);
  recognition::reportRegions(report_inputs);
  recognition::reportRepresentations(report_inputs);
}
