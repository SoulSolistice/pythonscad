/*! The corner placement's placeOnOwnQuadric - see BodyBuild.h. */
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

void StepKernel::BodyBuild::placeOnOwnQuadric()
{
  if (approximate) {
    std::size_t onto_own = 0;
    double worst_own = 0;
    for (const auto& patch : quadric_faces) {
      const Surface *surf = patch.surface.get();
      if (surf == nullptr) continue;
      for (const auto& run : patch.runs) {
        for (const int v : run.verts) {
          if (v < 0 || std::size_t(v) >= vertices.size()) continue;
          if (moves.count(v) != 0) continue;
          Vector3d q;
          if (!AnalyticFeatures::closestOnSurface(surf, vertices[v], q)) continue;
          const double off = (q - vertices[v]).norm();
          if (off <= 1e-9) continue;
          bool held = false;
          for (const auto& other : surfaces) {
            if (other.get() == surf) continue;
            if (dynamic_cast<const GridSurface *>(other.get()) != nullptr) continue;
            Vector3d qo;
            if (AnalyticFeatures::closestOnSurface(other.get(), vertices[v], qo) &&
                (qo - vertices[v]).norm() <= 1e-9) {
              held = true;
              break;
            }
          }
          if (held) continue;
          moves.emplace(v, q);
          worst_own = std::max(worst_own, off);
          onto_own++;
        }
      }
    }
    on_own = onto_own;
    if (onto_own > 0) {
      LOG(
        "STEP export: %1$d corners of a trimmed quadric that nothing else holds are placed on the "
        "surface their own face is written on, moving at most %2$.4f",
        int(onto_own), worst_own);
    }
  }
}
