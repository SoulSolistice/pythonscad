/*! The corner placement's placeOnMeshPlanes - see BodyBuild.h. */
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

void StepKernel::BodyBuild::placeOnMeshPlanes()
{
  if (approximate && !singleOwner.empty()) {
    std::map<int, std::vector<std::size_t>> faces_at;
    for (std::size_t i = 0; i < loops.size(); i++) {
      if (!loop_valid[i] || consumed[i] || loops[i].size() < 3) continue;
      for (const int v : loops[i]) faces_at[v].push_back(i);
    }
    std::size_t placed = 0, triple = 0, placed_triple = 0, plane_gave_up = 0, held_on_exact = 0;
    // Why the surface-against-line placement gives up, kept apart: these are
    // five unrelated reasons and one number for all of them says nothing about
    // which to work on. `t_offsurf` is the corner's own surface refusing to
    // answer, `t_faces` is the corner not having exactly two mesh planes to
    // cross - not a solver failure at all - `t_parallel` is those two planes
    // being parallel, and `t_solve` is the only one the projection owns.
    std::size_t t_offsurf = 0, t_faces = 0, t_parallel = 0, t_solve = 0;
    std::size_t ts_noconv = 0, ts_offsurf = 0, ts_plane = 0, ts_far = 0;
    double near_min = 0, near_med = 0, near_max = 0;
    std::vector<double> near_misses;
    double worst_plane = 0, worst_triple = 0;
    for (const auto& entry : singleOwner) {
      const int v = entry.first;
      if (v < 0 || std::size_t(v) >= vertices.size()) continue;
      if (moves.count(v) != 0 || entry.second >= surfaces.size()) continue;
      const auto at = faces_at.find(v);
      if (at == faces_at.end()) continue;
      Vector3d pn(0, 0, 0);
      double pd = 0, reach_here = 0;
      bool one = true, any = false;
      for (const std::size_t i : at->second) {
        const Vector3d n = loop_normals[i].normalized();
        const double d = n.dot(vertices[loops[i][0]]);
        for (const int w : loops[i]) {
          reach_here = std::max(reach_here, (vertices[w] - vertices[v]).norm());
        }
        if (!any) {
          pn = n;
          pd = d;
          any = true;
        } else if ((n - pn).norm() > 1e-6 || fabs(d - pd) > 1e-6) {
          one = false;
          break;
        }
      }
      if (!any) continue;
      if (!one) {
        // More than one plane, which is a corner where three exact things meet:
        // the declared surface and two faces of the model. That point is
        // computable, and until now it was declined outright - "belongs on
        // neither conic and stays" - which left 48 corners on step-exact-trim,
        // 6 on step-shared-arc and 2 on step-cut-cone where the mesh put them.
        //
        // The planes have to be sorted first, because not every plane at the
        // corner is a face: some are the tessellation's own chords of the very
        // surface being placed on, and a chord is not a constraint. On
        // step-exact-trim 16 of the 48 corners have three planes and one of
        // them is a chord; taking all three would over-determine the point and
        // decline it again.
        //
        // A chord's plane normal is parallel to the surface's normal there, a
        // face's is not. The bound is the tessellation's own angular half-step:
        // a facet of an n-gon lies within pi/n of the surface normal at any of
        // its corners, and nothing coarser than a hexagon is a tessellation, so
        // cos(pi/6) admits every chord. Measured, chords read 0.885 to 1.000
        // and faces 0.000 to 0.643 - the closest pair being step-band-family's
        // 0.885 against step-cut-cone's 0.643.
        //
        // That margin is the weakest thing here and it is an inference standing
        // in for something the model knew: a declared plane would make this a
        // lookup. See doc/step-export-development.md, *Corner placement*.
        std::vector<std::pair<Vector3d, double>> faces_here;
        double reach_all = 0;
        {
          const Surface *os = surfaces[entry.second].get();
          Vector3d q0;
          if (!AnalyticFeatures::closestOnSurface(os, vertices[v], q0)) {
            triple++;
            t_offsurf++;
            continue;
          }
          const double off0 = (vertices[v] - q0).norm();
          for (const std::size_t i : at->second) {
            const Vector3d n = loop_normals[i].normalized();
            const double d = n.dot(vertices[loops[i][0]]);
            // How far the corner may travel: to its nearest neighbour and no
            // further. The face's whole extent is far too loose - the line two
            // faces cross along can meet the surface twice, and on
            // step-shared-arc the far one is 2.0 away and takes a cone that was
            // exact to 1.41 out. A corner is correcting its own tessellation,
            // so it belongs nearer than the vertex next to it.
            for (const int w : loops[i]) {
              if (w == v) continue;
              const double e = (vertices[w] - vertices[v]).norm();
              if (e > 1e-12) reach_all = reach_all == 0 ? e : std::min(reach_all, e);
            }
            bool have = false;
            for (const auto& w : faces_here) {
              if ((w.first - n).norm() < 1e-6 && fabs(w.second - d) < 1e-6) have = true;
            }
            if (have) continue;
            Vector3d q1;
            if (!AnalyticFeatures::closestOnSurface(os, vertices[v] + n * 1e-4, q1)) continue;
            const double along = fabs(((vertices[v] + n * 1e-4) - q1).norm() - off0) / 1e-4;
            if (along >= cos(M_PI / 6)) continue;  // a chord of this surface
            faces_here.emplace_back(n, d);
          }
        }
        if (faces_here.size() != 2) {
          triple++;
          t_faces++;
          continue;
        }
        // Where the two faces cross is a line; where that line meets the
        // surface is the corner. Projecting onto the line is exact, so this
        // alternates between two closed forms rather than three.
        const Vector3d n1 = faces_here[0].first, n2 = faces_here[1].first;
        const double d1 = faces_here[0].second, d2 = faces_here[1].second;
        const Vector3d dir = n1.cross(n2);
        if (dir.norm() < 1e-9) {
          triple++;
          t_parallel++;
          continue;
        }
        const Surface *own3 = surfaces[entry.second].get();
        Vector3d p3 = vertices[v], q3;
        // Newton on all three at once - the surface and both planes. This used
        // to alternate between projecting onto the surface and onto the line,
        // which converges linearly at cos^2(theta) per cycle and so, under its
        // 64-iteration cap, only for lines meeting the surface above about 30
        // degrees. It gave up on 280 corners across the fixture set, 260 of
        // them on lid10, and none of them because the geometry was not there.
        const double solve_tol = 1e-12 * std::max(1.0, vertices[v].norm());
        double near3 = 0;
        const bool ok3 =
          projectOntoSurfaceAndPlanes(own3, n1, d1, n2, d2, p3, solve_tol, reach_all, &near3);
        const bool got3 = ok3 && AnalyticFeatures::closestOnSurface(own3, p3, q3);
        if (!got3) {
          ts_noconv++;
          if (std::isfinite(near3)) near_misses.push_back(near3);
        } else if ((q3 - p3).norm() > 1e-6) ts_offsurf++;
        else if (fabs(n1.dot(p3) - d1) > 1e-9 || fabs(n2.dot(p3) - d2) > 1e-9) ts_plane++;
        else if ((p3 - vertices[v]).norm() > reach_all) {
          ts_far++;
          (void)reach_all;
        }
        if (!got3 || (q3 - p3).norm() > 1e-6 || fabs(n1.dot(p3) - d1) > 1e-9 ||
            fabs(n2.dot(p3) - d2) > 1e-9 || (p3 - vertices[v]).norm() > reach_all) {
          triple++;
          t_solve++;
          continue;
        }
        worst_triple = std::max(worst_triple, (p3 - vertices[v]).norm());
        moves.emplace(v, p3);
        move_rung[v] = CornerRung::MeshTriple;
        placed_triple++;
        continue;
      }
      const Surface *own = surfaces[entry.second].get();
      Vector3d p = vertices[v], qa;
      bool ok = true;
      for (int iter = 0; iter < 64; iter++) {
        if (!AnalyticFeatures::closestOnSurface(own, p, qa)) {
          ok = false;
          break;
        }
        const Vector3d qb = qa - pn * (pn.dot(qa) - pd);
        if ((qb - p).norm() < 1e-12) {
          p = qb;
          break;
        }
        p = qb;
      }
      if (!ok || !AnalyticFeatures::closestOnSurface(own, p, qa)) {
        plane_gave_up++;
        continue;
      }
      // Tight, because the file will be read as though it were exact. Accepting
      // 1e-6 here put a corner 1.73e-07 off a plane it was placed *in*, which is
      // outside OpenCASCADE's own Precision::Confusion of 1e-7 - so the face
      // asserted a plane its corner was not on, by the kernel's own reckoning.
      // The alternating projection converges, so there is no reason to accept
      // less than it converges to.
      if ((qa - p).norm() > 1e-9 || fabs(pn.dot(p) - pd) > 1e-9) {
        plane_gave_up++;
        continue;
      }
      const double travel = (p - vertices[v]).norm();
      if (travel > reach_here) {
        plane_gave_up++;
        continue;
      }
      if (deserts_exact(v, own, p)) {
        held_on_exact++;
        continue;
      }
      worst_plane = std::max(worst_plane, travel);
      moves.emplace(v, p);
      move_rung[v] = CornerRung::MeshConic;
      placed++;
    }
    on_conic = placed;
    on_triple = placed_triple;
    if (placed_triple > 0) {
      LOG(
        "STEP export: %1$d corners where a declared surface meets two faces of the model are "
        "placed where all three cross, moving at most %2$.4f; %3$d are not - %4$d off their own "
        "surface, %5$d without two faces to cross, %6$d on parallel faces, %7$d the solve did not reach",
        int(placed_triple), worst_triple, int(triple), int(t_offsurf), int(t_faces), int(t_parallel),
        int(t_solve));
      if (!near_misses.empty()) {
        std::sort(near_misses.begin(), near_misses.end());
        near_min = near_misses.front();
        near_med = near_misses[near_misses.size() / 2];
        near_max = near_misses.back();
      }
      LOG(
        "STEP export:    of those %1$d: %2$d no answer, %3$d off the surface, %4$d off a plane, %5$d "
        "past the nearest neighbour; closest the line came to the surface inside the window "
        "%6$.3e/%7$.3e/%8$.3e min/med/max",
        int(t_solve), int(ts_noconv), int(ts_offsurf), int(ts_plane), int(ts_far), near_min, near_med,
        near_max);
    }
    if (placed > 0) {
      LOG(
        "STEP export: %1$d corners a declared surface shares with one plane of the mesh reach "
        "the conic where the two cross, moving at most %2$.4f; %3$d do not and stay where the "
        "mesh put them",
        int(placed), worst_plane, int(plane_gave_up));
    }
    if (held_on_exact > 0) {
      LOG(
        "STEP export: %1$d corners provenance gives one owner are already on another quadric the "
        "model declared, and stay on it rather than be placed off it",
        int(held_on_exact));
    }
  }
}
