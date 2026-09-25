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
      // tessellation, told apart by the declared surface itself.
      //
      // A chord facet of a surface is spanned by two directions tangent to it,
      // so its plane's normal is parallel to the surface's own normal there. A
      // plane that cuts *across* the surface - a cylinder's cap - has a normal
      // perpendicular to it. So the question is only whether the plane's normal
      // is closer to parallel or to perpendicular, which is a midpoint rather
      // than a tuned constant, and the two cases are nowhere near it: measured
      // over the fixtures, cutting planes read 0.000000 to 0.000003 and chords
      // read 0.885 to 1.000.
      //
      // This asks the declaration and not the mesh, which is the point. An
      // earlier version compared the plane against the one a declared surface's
      // refpt is anchored at; that works only where the anchor happens to be
      // the rim, so it vouched for a cylinder's bottom cap and not its top -
      // primitives.cc pushes one record for r1 == r2 - and never for a sweep's
      // end caps. This needs no anchor and no second declaration.
      const Surface *exact = surfaces[sp.second.first].get();
      Vector3d q_at;
      if (!AnalyticFeatures::closestOnSurface(exact, vertices[v], q_at)) continue;
      const double off_at = (vertices[v] - q_at).norm();
      std::vector<std::pair<Vector3d, double>> vouched;
      for (const std::size_t i : at->second) {
        const double nn = loop_normals[i].norm();
        if (nn < 1e-12) continue;
        const Vector3d nhat = loop_normals[i] / nn;
        const double d = nhat.dot(vertices[loops[i][0]]);
        if (fabs(nhat.dot(vertices[v]) - d) > tol) continue;  // the corner is not on it
        // How much of a step along the plane's normal the surface takes back,
        // which is |n . nS| without needing a normal from the Surface API.
        const double delta = 1e-4;
        Vector3d q_off;
        if (!AnalyticFeatures::closestOnSurface(exact, vertices[v] + nhat * delta, q_off)) continue;
        const double along = fabs(((vertices[v] + nhat * delta) - q_off).norm() - off_at) / delta;
        if (along >= 0.5) continue;  // parallel to the surface's normal: a chord
        bool have = false;
        for (const auto& w : vouched) {
          if ((w.first - nhat).norm() < 1e-6 && fabs(w.second - d) < tol) have = true;
        }
        if (!have) vouched.emplace_back(nhat, d);
      }
      if (vouched.empty()) continue;  // nothing exact is being given up
      // Already in every one of them? Then the move keeps them and stands.
      bool leaves = false;
      for (const auto& w : vouched) {
        if (fabs(w.first.dot(moves[v]) - w.second) > 1e-9) leaves = true;
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
      const Surface *own = exact;
      const Vector3d pn = vouched[0].first;
      const double pd = vouched[0].second;
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
