/*! The corner placement's placeOnMeshPlanes - see BodyBuild.h. */
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

using namespace brep;
using AnalyticFeatures::declaredBand;
using AnalyticFeatures::isQuadric;
using AnalyticFeatures::projectOntoBoth;
using AnalyticFeatures::projectOntoSurfaceAndPlanes;
using AnalyticFeatures::surfaceImplicit;

void brep::BodyBuild::placeOnMeshPlanes()
{
  if (approximate && !singleOwner.empty()) {
    // The facets still to be written as planes at each corner, and every face
    // meeting it - those claimed by a surface's region too - whose names say
    // what the corner is on.
    std::map<int, std::vector<std::size_t>> faces_at, named_at;
    for (std::size_t i = 0; i < loops.size(); i++) {
      if (!loop_valid[i] || loops[i].size() < 3) continue;
      for (const int v : loops[i]) {
        named_at[v].push_back(i);
        if (!consumed[i]) faces_at[v].push_back(i);
      }
    }
    std::size_t placed = 0, triple = 0, placed_triple = 0, plane_gave_up = 0;
    std::size_t held_on_other = 0, two_records = 0;
    // Why the surface-against-line placement gives up, kept apart: these are
    // five unrelated reasons and one number for all of them says nothing about
    // which to work on. `t_unnamed` is a facet at the corner that carries no
    // name, so no one knows whether it is a face, `t_faces` is the corner not
    // having exactly two faces to cross - not a solver failure at all -
    // `t_parallel` is those two being parallel, and `t_solve` is the only one
    // the projection owns.
    std::size_t t_unnamed = 0, t_faces = 0, t_parallel = 0, t_solve = 0;
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
        } else if (!recognition::samePlane(n * d, n, pn * pd, pn)) {
          // One plane at machine precision, where it was normals and offsets
          // within a chosen 1e-6 (open item 39, R5).
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
        // Which is which is the facets' names (namedFor): one named for a
        // declared plane is a face of the model, and constrains the corner by
        // that record's own point and normal; one named for a curved surface -
        // this one or another - is a chord of it, and constrains nothing. It
        // was told by angle until 2026-09-30 - a chord's normal near the
        // surface's, within cos(pi/6) - which also took a chord of a *different*
        // curved surface for a face, and was, as it said of itself, an inference
        // standing in for something the model knew (open item 39, R4). A facet
        // named for nothing is not known to be either, and the corner stays.
        std::vector<std::pair<Vector3d, double>> faces_here;
        std::vector<const Surface *> records_here;
        double reach_all = 0;
        bool unnamed_here = false;
        {
          for (const std::size_t i : at->second) {
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
            const Surface *named = namedFor(i);
            if (named == nullptr) {
              unnamed_here = true;
              continue;
            }
            const auto *record = dynamic_cast<const PlaneSurface *>(named);
            if (record == nullptr) continue;  // a chord of a curved surface
            if (std::find(records_here.begin(), records_here.end(), named) != records_here.end())
              continue;
            records_here.push_back(named);
            const Vector3d rn = record->normdir.normalized();
            faces_here.emplace_back(rn, rn.dot(record->refpt));
          }
        }
        if (unnamed_here) {
          triple++;
          t_unnamed++;
          continue;
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
        // On the surface and on both planes at machine precision, where it was
        // within 1e-6 of the one and 1e-9 of the others (open item 39, R5).
        const bool on3 = got3 && recognition::onExactly(own3, p3);
        const bool planes3 =
          recognition::onPlane(n1 * d1, n1, p3) && recognition::onPlane(n2 * d2, n2, p3);
        if (!got3) {
          ts_noconv++;
          if (std::isfinite(near3)) near_misses.push_back(near3);
        } else if (!on3) ts_offsurf++;
        else if (!planes3) ts_plane++;
        else if ((p3 - vertices[v]).norm() > reach_all) {
          ts_far++;
          (void)reach_all;
        }
        if (!got3 || !on3 || !planes3 || (p3 - vertices[v]).norm() > reach_all) {
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
      // What the corner is on is what the faces meeting it are named for
      // (checklist E9; open item 39, R6) - not what it lies near, which is what
      // deserts_exact asked until 2026-10-05: on another declared quadric within
      // an absolute 1e-9, which pinned a corner that merely touched one and left
      // a corner on a maker's plane unprotected. So a facet named for a declared
      // plane cuts the owner in that record's plane, stated exactly, where the
      // mesh's plane through the facet's corners is a position; one named for
      // nothing is the plane its corners make, which is the plane it is written
      // on. A face named for a curved surface other than the owner puts the
      // corner on that surface as well, which this rung does not solve - beside
      // the corner such a facet is a chord of it, not a cutting plane (R13) - so
      // the corner is left to the rungs that do; and two plane records on one
      // facet plane are two statements this rung cannot choose between.
      const Surface *plane_record = nullptr;
      bool other_curved = false, two_planes = false;
      for (const std::size_t i : named_at[v]) {
        const Surface *named = namedFor(i);
        if (named == nullptr || named == own || named->sameAs(*own)) continue;
        if (dynamic_cast<const PlaneSurface *>(named) == nullptr) {
          other_curved = true;
          continue;
        }
        if (consumed[i]) continue;
        if (plane_record == nullptr) plane_record = named;
        else if (!named->sameAs(*plane_record)) two_planes = true;
      }
      if (other_curved) {
        held_on_other++;
        continue;
      }
      if (two_planes) {
        two_records++;
        continue;
      }
      if (plane_record != nullptr) {
        pn = plane_record->normdir.normalized();
        pd = pn.dot(plane_record->refpt);
      }
      Vector3d p = vertices[v], qa;
      bool ok = true;
      // Alternating projection, until the point is on both at machine
      // precision or stops moving - where it stopped at a step under 1e-12
      // (open item 39, R5).
      for (int iter = 0; iter < 64; iter++) {
        if (!AnalyticFeatures::closestOnSurface(own, p, qa)) {
          ok = false;
          break;
        }
        const Vector3d qb = qa - pn * (pn.dot(qa) - pd);
        const bool still = qb == p;
        p = qb;
        if (still || (recognition::onExactly(own, p) && recognition::onPlane(pn * pd, pn, p))) break;
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
      if (!recognition::onExactly(own, p) || !recognition::onPlane(pn * pd, pn, p)) {
        plane_gave_up++;
        continue;
      }
      const double travel = (p - vertices[v]).norm();
      if (travel > reach_here) {
        plane_gave_up++;
        continue;
      }
      worst_plane = std::max(worst_plane, travel);
      moves.emplace(v, p);
      move_rung[v] = CornerRung::MeshConic;
      placed++;
    }
    // Whenever a corner was asked, placed or not: why none was is the report
    // that matters most where none was.
    if (placed_triple + triple > 0) {
      LOG(
        "STEP export: %1$d corners where a declared surface meets two faces of the model are "
        "placed where all three cross, moving at most %2$.4f; %3$d are not - %4$d beside a facet "
        "named for nothing, %5$d without two faces to cross, %6$d on parallel faces, %7$d the solve "
        "did not reach",
        int(placed_triple), worst_triple, int(triple), int(t_unnamed), int(t_faces), int(t_parallel),
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
    if (held_on_other > 0 || two_records > 0) {
      LOG(
        "STEP export: %1$d corners provenance gives one owner bound a face named for another curved "
        "surface as well, and %2$d a facet named for two planes, and are left to the rungs that "
        "solve every face there",
        int(held_on_other), int(two_records));
    }
  }
}
