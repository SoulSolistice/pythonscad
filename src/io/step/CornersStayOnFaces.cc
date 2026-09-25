/*! The corner placement's checkMovesStayOnFaces - see BodyBuild.h. */
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

void StepKernel::BodyBuild::checkMovesStayOnFaces()
{
  if (approximate && !moves.empty()) {
    std::map<int, std::vector<std::pair<const Surface *, std::size_t>>> bounding;
    const std::size_t NOT_A_BAND = std::size_t(-1);
    auto bound_by = [&](const std::vector<AnalyticFeatures::Patch>& patches) {
      for (const auto& patch : patches) {
        for (const auto& run : patch.runs) {
          for (const int v : run.verts) {
            if (moves.count(v) != 0) bounding[v].emplace_back(patch.surface.get(), NOT_A_BAND);
          }
        }
      }
    };
    bound_by(quadric_faces);
    bound_by(grid_faces);
    bound_by(bezier_patches);
    for (std::size_t b = 0; b < bands.size(); b++) {
      if (!bands[b].alive) continue;
      for (const std::size_t w : bands[b].walls) {
        for (const int v : loops[w]) {
          if (moves.count(v) != 0) bounding[v].emplace_back(bands[b].zone.get(), b);
        }
      }
    }
    // How far `p` is from the surface, and what that surface allows; negative
    // where it gives no answer.
    auto off_face = [&](const Surface *s, std::size_t band, const Vector3d& p, double& allowed) {
      allowed = 1e-9;
      if (band != NOT_A_BAND && s == nullptr) {
        const AnalyticFeatures::Band& bd = bands[band];
        const Vector3d axis = bd.axis.normalized();
        const Vector3d rel = p - bd.base;
        const double h = rel.dot(axis);
        const double slope = bd.height > 0 ? (bd.r_top - bd.r_bottom) / bd.height : 0;
        return fabs((rel - axis * h).norm() - (bd.r_bottom + slope * h)) / sqrt(1 + slope * slope);
      }
      Vector3d q;
      if (s == nullptr || !AnalyticFeatures::closestOnSurface(s, p, q)) return -1.0;
      allowed = std::max(AnalyticFeatures::declaredBand(s), 1e-9);
      return (q - p).norm();
    };
    std::size_t deserting = 0;
    double worst_desertion = 0;
    for (auto it = moves.begin(); it != moves.end();) {
      bool deserts = false;
      const auto faces = bounding.find(it->first);
      if (faces != bounding.end()) {
        for (const auto& face : faces->second) {
          double allowed = 0;
          const double before = off_face(face.first, face.second, vertices[it->first], allowed);
          const double after = off_face(face.first, face.second, it->second, allowed);
          if (before < 0 || after < 0) continue;
          if (after > allowed && after > before) {
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
