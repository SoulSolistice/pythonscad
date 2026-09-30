/*! One solid's worth of STEP - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <limits>
#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Exact.h"
#include "geometry/recognition/Seam.h"
#include "geometry/recognition/Pipeline.h"
#include "geometry/recognition/Reports.h"
#include "io/step/BoundaryCycles.h"
#include "io/step/CornerLadder.h"
#include "io/step/CrossingArc.h"
#include "io/step/CurveChecks.h"
#include "io/step/LoopGeometry.h"
#include "io/step/PlaneSections.h"
#include "utils/printutils.h"
#include <algorithm>  // std::reverse
#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include <map>
#include <utility>
#include <array>
#include <set>
#include <functional>
#include <iomanip>  // put_time

using namespace step;

namespace {

// The implicit-surface machinery these passes solve with - `surfaceImplicit`,
// `projectOntoBoth`, `isQuadric`, `declaredBand` - lives in AnalyticFeatures,
// beside `closestOnSurface`, because it is geometry rather than STEP and
// `export_step.cc` needs the same Newton for its corner placement.
using AnalyticFeatures::declaredBand;
using AnalyticFeatures::isQuadric;
using AnalyticFeatures::projectOntoBoth;
using AnalyticFeatures::surfaceImplicit;

using AnalyticFeatures::projectOntoSurfaceAndPlanes;

}  // namespace

void StepKernel::BodyBuild::run()
{
  reportDeclaredSurfaces();

  buildLoops();

  recognise();

  placeCorners();

  emitBands();

  emitBezierFaces();

  redecideSections();

  decideCrossingRuns();

  emitSweepFaces();

  emitQuadricFaces();

  writeCrossingPcurves();

  emitPlanarFaces();

  emitCornerFans();

  assembleFaces();

  promoteCurves();

  checkBrep();

  checkAndReport();

  writeShells();
}

int32_t StepKernel::BodyBuild::surfaceIndexOf(const Surface *record) const
{
  for (std::size_t i = 0; i < surfaces.size(); i++) {
    if (surfaces[i].get() == record) return int32_t(i);
  }
  for (std::size_t i = 0; i < surfaces.size() && record != nullptr; i++) {
    if (surfaces[i] != nullptr && surfaces[i]->sameAs(*record)) return int32_t(i);
  }
  return -1;
}

void StepKernel::BodyBuild::registerFace(const Face *face, std::shared_ptr<const Surface> record,
                                         std::vector<std::size_t> facets,
                                         std::optional<recognition::EvidenceClass> evidence,
                                         bool plane_from_corners)
{
  using recognition::EvidenceClass;
  // The weakest class its facets' honoured claims state: a patch of D0 members
  // and D4 additions is D4. A facet no honoured claim holds leaves the face
  // unstated, which the B-rep's check names.
  EvidenceClass weakest = EvidenceClass::D0;
  if (!evidence) {
    for (const std::size_t f : facets) {
      EvidenceClass here = EvidenceClass::Unstated;
      for (const auto *claim : features.ledger.claimants(f)) {
        if (!claim->honoured()) continue;
        here = claim->evidence;
        break;
      }
      if (here == EvidenceClass::Unstated) {
        weakest = EvidenceClass::Unstated;
        break;
      }
      weakest = std::max(weakest, here);
    }
  }
  // And what the record itself is, which is a different fact: a maker's record
  // is D0 and one every name on which is derived D1 (mintDerivedTag); a fit, a
  // record the model never stated, D3; a plane from corners D4 until the B-rep
  // finds its corners determine it (brep::stateClasses).
  EvidenceClass record_class = EvidenceClass::D3;
  if (plane_from_corners) {
    record_class = EvidenceClass::D4;
  } else if (surfaceIndexOf(record.get()) >= 0) {
    bool all_derived = !record->tags.empty();
    for (const uint32_t tag : record->tags) all_derived = all_derived && isDerivedTag(tag);
    record_class = all_derived ? EvidenceClass::D1 : EvidenceClass::D0;
  }
  face_source[face] = FaceSource{std::move(record), std::move(facets), evidence ? *evidence : weakest,
                                 plane_from_corners, record_class};
}

StepKernel::Vertex *StepKernel::BodyBuild::get_vertex(int ind)
{
  if (step_verts[ind] == nullptr) {
    const auto key = std::make_tuple(vertices[ind][0], vertices[ind][1], vertices[ind][2]);
    const auto it = vertex_at.find(key);
    if (it != vertex_at.end()) {
      step_verts[ind] = it->second;
    } else {
      auto point = new Point(entities, vertices[ind]);
      step_verts[ind] = new Vertex(entities, point);
      vertex_at.emplace(key, step_verts[ind]);
    }
  }
  return step_verts[ind];
}
