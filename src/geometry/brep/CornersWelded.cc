/*! The corner placement's holdWeldedVertices - see BodyBuild.h. */
#include "geometry/brep/BodyBuild.h"

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
#include "geometry/brep/BoundaryCycles.h"
#include "geometry/brep/CornerLadder.h"
#include "geometry/brep/LoopGeometry.h"
#include "geometry/brep/PlaneSections.h"
#include "utils/printutils.h"

using namespace step;
using AnalyticFeatures::declaredBand;
using AnalyticFeatures::isQuadric;
using AnalyticFeatures::projectOntoBoth;
using AnalyticFeatures::projectOntoSurfaceAndPlanes;
using AnalyticFeatures::surfaceImplicit;

void step::BodyBuild::holdWeldedVertices()
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
