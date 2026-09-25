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

  emitSweepFaces();

  emitQuadricFaces();

  writeCrossingPcurves();

  emitPlanarFaces();

  emitCornerFans();

  assembleFaces();

  promoteCurves();

  checkAndReport();

  writeShells();
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
