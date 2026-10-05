/*! The corner placement's keepExactSurfaces - see BodyBuild.h. */
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

void StepKernel::BodyBuild::keepExactSurfaces()
{
  if (approximate && !moves.empty() && !ownerSplit.empty()) {
    std::map<int, std::vector<std::size_t>> faces_at;
    for (std::size_t i = 0; i < loops.size(); i++) {
      if (!loop_valid[i] || consumed[i] || loops[i].size() < 3) continue;
      for (const int v : loops[i]) faces_at[v].push_back(i);
    }
    std::size_t rerouted = 0, dropped = 0;
    double worst_reroute = 0;
    for (const auto& sp : ownerSplit) {
      const int v = sp.first;
      if (moves.count(v) == 0) continue;
      if (v < 0 || std::size_t(v) >= vertices.size()) continue;
      if (sp.second.first >= surfaces.size() || sp.second.second >= surfaces.size()) continue;
      const auto at = faces_at.find(v);
      if (at == faces_at.end()) continue;
      // The planes at this corner that are faces of the model rather than
      // tessellation: those a declaration vouches for, by name - a facet named
      // for a declared plane (namedFor), the corner on that plane. A facet
      // named for a curved surface is a chord of it, and one named for nothing
      // vouches for nothing.
      //
      // It was told by angle until 2026-09-30 - a plane's normal nearer
      // parallel to the surface's than perpendicular, a chord - which vouched
      // for any plane cutting across the surface whether anything declared it
      // or not, and disagreed with the triple-point pass's cos(pi/6) on every
      // plane between the two (open item 39, R4). And it asked whether the
      // corner is on the plane within the model's tolerance, and whether the
      // move leaves it within 1e-9: both at machine precision now, against the
      // record itself.
      std::vector<const PlaneSurface *> vouched;
      for (const std::size_t i : at->second) {
        const auto *record = dynamic_cast<const PlaneSurface *>(namedFor(i));
        if (record == nullptr) continue;
        if (!recognition::onPlane(record->refpt, record->normdir, vertices[v])) continue;
        if (std::find(vouched.begin(), vouched.end(), record) == vouched.end())
          vouched.push_back(record);
      }
      if (vouched.empty()) continue;  // nothing exact is being given up
      // Already in every one of them? Then the move keeps them and stands.
      bool leaves = false;
      for (const auto *w : vouched) {
        if (!recognition::onPlane(w->refpt, w->normdir, moves[v])) leaves = true;
      }
      if (!leaves) continue;
      // Two vouched planes and the exact owner is one constraint too many; a
      // corner on two planes is a triple point already, and it stays.
      if (vouched.size() > 1) {
        moves.erase(v);
        dropped++;
        continue;
      }
      // Place it where the exact owner crosses the plane, nearest to where the
      // mesh put it. Both are exact, so this is the answer rather than a
      // compromise between one and a fit.
      const Surface *own = surfaces[sp.second.first].get();
      const Vector3d pn = vouched[0]->normdir.normalized();
      const double pd = pn.dot(vouched[0]->refpt);
      Vector3d p = vertices[v], q;
      bool ok = true;
      for (int iter = 0; iter < 64; iter++) {
        if (!AnalyticFeatures::closestOnSurface(own, p, q)) {
          ok = false;
          break;
        }
        const Vector3d next = q - pn * (pn.dot(q) - pd);
        if ((next - p).norm() < 1e-12) {
          p = next;
          break;
        }
        p = next;
      }
      if (!ok || !AnalyticFeatures::closestOnSurface(own, p, q) || (q - p).norm() > 1e-6 ||
          fabs(pn.dot(p) - pd) > 1e-9) {
        moves.erase(v);
        dropped++;
        continue;
      }
      // And the fit only has to agree to within what it declared.
      const auto *fit = dynamic_cast<const GridSurface *>(surfaces[sp.second.second].get());
      if (fit != nullptr && AnalyticFeatures::closestOnSurface(fit, p, q) &&
          (q - p).norm() > fit->membershipTolerance()) {
        moves.erase(v);
        dropped++;
        continue;
      }
      worst_reroute = std::max(worst_reroute, (p - vertices[v]).norm());
      moves[v] = p;
      rerouted++;
    }
    if (rerouted > 0 || dropped > 0) {
      LOG(
        "STEP export: %1$d corners are placed on a plane a declaration vouches for crossed with "
        "their exact owner rather than on the fit, moving at most %2$.4f; %3$d stay where the "
        "mesh put them",
        int(rerouted), worst_reroute, int(dropped));
    }
  }
}
