/*! The corner placement's holdWeldedVertices - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Exact.h"
#include "io/step/BoundaryCycles.h"
#include "io/step/CornerLadder.h"
#include "io/step/LoopGeometry.h"
#include "io/step/PlaneSections.h"
#include "utils/printutils.h"

using namespace step;
using AnalyticFeatures::declaredBand;
using AnalyticFeatures::isQuadric;
using AnalyticFeatures::projectOntoBoth;
using AnalyticFeatures::projectOntoSurfaceAndPlanes;
using AnalyticFeatures::surfaceImplicit;

void StepKernel::BodyBuild::holdWeldedVertices()
{
  std::size_t held = 0;
  for (auto it = moves.begin(); it != moves.end();) {
    if (welded_verts.count(it->first) != 0) {
      it = moves.erase(it);
      held++;
    } else {
      ++it;
    }
  }
  if (held > 0) {
    LOG(
      "STEP export: %1$d corners are left where the mesh put them: they were welded onto an "
      "edge to keep the shell conformal, and sit on faces whose loops do not name them",
      int(held));
  }
}
