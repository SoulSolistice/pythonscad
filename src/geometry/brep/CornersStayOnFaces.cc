/*! The corner placement's checkMovesStayOnFaces - see BodyBuild.h. */
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

void step::BodyBuild::checkMovesStayOnFaces()
{
  if (approximate && !moves.empty()) {
    std::map<int, std::vector<const Surface *>> bounding;
    auto bound_by = [&](const std::vector<AnalyticFeatures::Patch>& patches) {
      for (const auto& patch : patches) {
        for (const auto& run : patch.runs) {
          for (const int v : run.verts) {
            if (moves.count(v) != 0) bounding[v].push_back(patch.surface.get());
          }
        }
      }
    };
    bound_by(quadric_faces);
    bound_by(grid_faces);
    bound_by(bezier_patches);
    // A band's face by the surface it is written on, as the band states it
    // (Band::carrier) - read here off its axis and rims where it closed no
    // zone, a third copy of the writer's arithmetic (open item 43, W1).
    std::vector<std::shared_ptr<const Surface>> carriers;
    for (const auto& band : bands) {
      if (!band.alive) continue;
      carriers.push_back(band.carrier());
      if (carriers.back() == nullptr) continue;
      for (const std::size_t w : band.walls) {
        for (const int v : loops[w]) {
          if (moves.count(v) != 0) bounding[v].push_back(carriers.back().get());
        }
      }
    }
    // A move may not take a corner off the surface of a face it was on - by the
    // one predicate the arbiter scores with (AnalyticFeatures::onFace). It was
    // a corner anywhere within a grid's declared band of the surface, and a
    // move refused only where it ended further off than it began, so a corner
    // already past the band was never asked about at all (open item 39, R15).
    // A surface whose projection does not answer judges nothing.
    std::size_t deserting = 0;
    double worst_desertion = 0;
    for (auto it = moves.begin(); it != moves.end();) {
      bool deserts = false;
      const auto faces = bounding.find(it->first);
      if (faces != bounding.end()) {
        for (const auto& face : faces->second) {
          const double before = AnalyticFeatures::offFace(face, vertices[it->first]);
          const double after = AnalyticFeatures::offFace(face, it->second);
          if (before < 0 || after < 0) continue;
          if (before == 0 && after > 0) {
            deserts = true;
            worst_desertion = std::max(worst_desertion, after);
          }
        }
      }
      if (deserts) {
        it = moves.erase(it);
        deserting++;
      } else {
        ++it;
      }
    }
    if (deserting > 0) {
      LOG(message_group::Export_Error,
          "STEP export: %1$d corners would have been moved off the surface of an analytic face they "
          "bound, by up to %2$.4f, and are held where the mesh put them - a corner placement "
          "proposed a point its own face is not on",
          int(deserting), worst_desertion);
    }
  }
}
