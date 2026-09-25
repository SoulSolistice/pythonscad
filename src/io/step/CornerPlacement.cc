/*! The corner placement - see BodyBuild.h. */
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
#include "geometry/recognition/Reports.h"
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

void StepKernel::BodyBuild::placeCorners()
{
  // Put the junction corners on the curve their two owners cross along - but
  // only once it is known which faces are analytic, and only if no face that
  // keeps a PLANE would be bent by it.
  //
  // A corner two declared surfaces made belongs on their intersection, and
  // reportOwnership has computed that point all along. Where it may be applied
  // is the whole difficulty, and two earlier places are ruled out by
  // measurement, both recorded in doc/step-export-development.md, *Corner
  // placement*: before mergeTriangles it keeps the neighbours planar and
  // destroys the merge, at twelve to sixty-two times the faces; after the merge
  // but before recognition it bends the faceted neighbours by 0.0331 where they
  // are polygons.
  //
  // Here is the place that costs neither. Recognition has already run on the
  // untouched, fully merged mesh, so the claims are what they were; and an
  // analytic face does not care whether its corners are coplanar, only a PLANE
  // does. On step-bored-cone 52 faces use a movable corner and every one of
  // them is written analytic, so nothing is bent and nothing is split.
  //
  // All of them or none, and per export rather than per corner. A boundary half
  // moved is worse than one not moved at all - doc/step-export-development.md,
  // *Half a fix is worse than none*, measured that at 83 faulty faces against 9
  // - so where a polygon that keeps its plane would be bent, this declines the
  // lot and the export is exactly what it was.
  // The other half of the placement: a corner one declared surface shares with
  // a plane the mesh carries and nothing declared.
  //
  // Here rather than beside the two-owner case in reportOwnership, because
  // *which* plane is only answerable here. Asked of raw triangles it cannot be
  // done: a frustum cap shares an original with its wall, and no distance
  // threshold separates a cut facet near the rim from a wall facet, or a thin
  // base sliver from either - three attempts, all in
  // doc/step-export-development.md. mergeTriangles has since answered it
  // exactly, because a merged face that keeps a PLANE *is* a plane: the planes
  // at a corner are the distinct ones among the faces using it.
  //
  // One plane, and the corner goes on the conic where it meets the declared
  // surface. Two, and it is a triple point - the cut and the sliver of base a
  // tilted cut leaves behind - which belongs on neither conic and stays.
  std::map<int, Vector3d> moves = cornerMoves;
  // Which rung placed each move, so the arbiter below can weigh it against the
  // others. See kCornerRungs.
  std::map<int, CornerRung> move_rung;
  for (const auto& m : cornerMoves) move_rung[m.first] = CornerRung::Provenance;
  // what each path added
  std::size_t on_conic = 0, on_triple = 0, on_own = 0, on_faces = 0, on_plane_face = 0;

  // Whether placing corner `v` at `target` would take it off an exact surface the
  // model declared and the corner is already on - one other than `own`, which is
  // where it is being placed.
  //
  // The placement onto a conic below rests on provenance naming *one* declared
  // owner, and provenance is per original solid, decided by whole facets: an
  // original none of whose facets a boolean left whole owns nothing. Its surface
  // is then invisible to the vote, and a corner on it is handed to whatever the
  // other original at that junction owns. Measured on step-band-family at $fn
  // 24, the ridge cuts all but 2 of the bore's facets - three are needed - so
  // its 48 rim corners, generated on the r = 20 bore to 1e-14, were given the
  // r = 23 outer wall as sole owner, slid 3.0 along the cap onto it, and left the
  // bore face they bound 3.0 off its own surface. lid10 does the same to 120
  // corners of two chamfers, 0.65 and 2.25 along its z = 95 face. On both, only
  // the plane veto kept it out of the file - and it does not need the flagship
  // coupons: a bore with a frustum subtracted through its whole length, three
  // primitives, writes the bore 3.0 off itself with the veto silent.
  //
  // So the question is asked of the declarations rather than of the vote: a
  // corner on a declared quadric stays on it. That is the rule the pass placing
  // corners onto their own quadric already keeps, and the one the reroute below
  // is named for. A surface that answers by projecting is not asked - a grid
  // states a band, not a position, and a corner off it is its fit's business.
  //
  // Asked here and not of the triple-point placement, which rests on the same
  // single owner: across every fixture and flagship coupon it moved no corner
  // off a quadric, and a guard with nothing to refuse has regressions nothing
  // would see. The check before the veto below covers it, loudly.
  auto deserts_exact = [&](int v, const Surface *own, const Vector3d& target) {
    for (const auto& other : surfaces) {
      if (other.get() == own || !AnalyticFeatures::isQuadric(other.get())) continue;
      Vector3d on_now, on_then;
      if (!AnalyticFeatures::closestOnSurface(other.get(), vertices[v], on_now) ||
          (on_now - vertices[v]).norm() > 1e-9) {
        continue;
      }
      if (!AnalyticFeatures::closestOnSurface(other.get(), target, on_then) ||
          (on_then - target).norm() > 1e-9) {
        return true;
      }
    }
    return false;
  };
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

  // Ordered after the two paths above, and that order is the doctrine rather
  // than a convenience: where the model declared what a corner is on, the
  // declaration decides, and this general path applies only to what the
  // declarations left. Every path checks `moves.count(v)` first, so first refusal
  // is all the ordering does. Measured on step-exact-trim, where 32 corners are
  // reachable both ways: run first, this claimed all 32 and the declared rung
  // reported none.
  // A corner belongs where the surfaces of the analytic faces it bounds cross.
  //
  // This is the general statement of what the two placements above do for
  // special cases, and it is the one the *file* can be held to: whatever
  // provenance says about which original made a vertex, the faces written on it
  // assert that the corner lies on their surfaces, and where two of them meet it
  // asserts both. Provenance cannot answer this on its own - it is per original
  // solid, so it names a cylinder for a corner sitting on that solid's cap
  // (open item 18), and it names two coaxial cylinders 3.000 mm apart, which
  // never meet, for the corners where the band family's sweep crosses its bore.
  // Both are cases where the owners are not the surfaces the file will assert.
  //
  // Measured on `step-band-family` at $fn 32: 47 of its 1234 candidate edges
  // fall back to chords, and they are exactly the edges whose endpoints are off
  // the sweep - by 2.2e-02 to 1.0e-01, the 32-gon's own sagitta
  // 20(1 - cos(pi/32)) = 0.0963, so those ends sit on chord planes and were
  // never placed at all. A Bezier interpolates its endpoints, so no degree and
  // no better tracing can rescue an edge whose ends are not on the surface:
  // placing them is what turns those chords into the curve the two surfaces
  // actually cross along. See open items 3 and 9.
  //
  // Held to the ladder's own rules. The crossing is solved to an absolute
  // 1e-11, accepted only where the result is on *both* surfaces to 1e-9, and a
  // corner may travel no further than its nearest neighbouring vertex - which is
  // section 4's bound, and not the longest edge at it, which is the trap that
  // licensed a 1.9976 move on step-partial-cylinder.
  {
    const recognition::ReportInputs report_inputs{
      features,
      surfaces,
      recognition::Mesh{&vertices, &loops, &loop_valid, &loop_is_hole, &loop_normals},
      faceOrigin,
      owned,
      &rec.d0_regions,
      &rec.d0_representations};
    recognition::dumpLedger(report_inputs);
    recognition::reportArbitration(report_inputs);
    recognition::reportRegions(report_inputs);
    recognition::reportRepresentations(report_inputs);
  }
  if (approximate) {
    // The distinct surfaces of the analytic faces at each corner. Bands are
    // included by the surface their face is written from, which is the same
    // arithmetic the writer does further down - a band states its axis, its base
    // and the radius at each rim, and that is the surface.
    std::map<int, std::vector<std::shared_ptr<const Surface>>> faces_at;
    auto note = [&](int v, const std::shared_ptr<const Surface>& surface) {
      if (surface == nullptr || v < 0 || std::size_t(v) >= vertices.size()) return;
      auto& list = faces_at[v];
      for (const auto& have : list) {
        if (have.get() == surface.get() || have->sameAs(*surface)) return;
      }
      list.push_back(surface);
    };
    auto note_patches = [&](const std::vector<AnalyticFeatures::Patch>& patches) {
      for (const auto& patch : patches) {
        if (!patch.alive) continue;
        for (const auto& run : patch.runs) {
          for (const int v : run.verts) note(v, patch.surface);
        }
      }
    };
    note_patches(quadric_faces);
    note_patches(grid_faces);
    note_patches(bezier_patches);
    for (const auto& band : bands) {
      if (!band.alive) continue;
      const Vector3d axis = band.axis.normalized();
      std::shared_ptr<const Surface> surface;
      if (fabs(band.r_top - band.r_bottom) <= 1e-9 * std::max(1.0, band.r_bottom)) {
        surface = std::make_shared<CylinderSurface>(band.base, axis, band.r_bottom);
      } else if (fabs(band.height) > 0) {
        surface = std::make_shared<ConeSurface>(band.base, axis, band.r_bottom,
                                                (band.r_top - band.r_bottom) / band.height);
      }
      if (surface == nullptr) continue;
      for (const std::size_t w : band.walls) {
        if (w >= loops.size()) continue;
        for (const int v : loops[w]) note(v, surface);
      }
      // ...and the rims, which are corners of the band's face exactly as much
      // as the walls' are. Leaving them out is what left four of lid10's
      // corners 0.05 to 0.08 inside the cone whose face is written on them:
      // where a band meets the cap beside it, every vertex of that meeting is a
      // rim vertex and none of them is in a wall loop.
      for (const int v : band.bottom_set) note(v, surface);
      for (const int v : band.top_set) note(v, surface);
    }
    // ...and the planes, which are faces of the file exactly as much as the
    // quadrics are. A face written as a PLANE asserts that its corners lie in
    // that plane, and where such a face meets a cone or a cylinder the curve
    // between them is a conic - the very curve the plane-section pass writes as
    // an ELLIPSE. Leaving the planes out of this is what left those corners
    // where the mesh put them, a sagitta inside the quadric, and an ELLIPSE
    // through a run whose own endpoints are off it by up to 1.6196 on lid10 -
    // SOLIDWORKS fault code 7, "vertex does not lie on edge's curve". See open
    // item 20.
    //
    // A merged face that keeps a PLANE *is* a plane, so this is a lookup and
    // not a fit. What it is not is every plane at the corner: some of those are
    // the tessellation's own chords of the quadric being placed on, and a chord
    // is not a constraint - crossing it would place the corner on the chord
    // rather than on the face. The chord test is the ladder's own, section 4's
    // chord-vs-cap rule: a facet of an n-gon lies within pi/n of the surface
    // normal at its corners, nothing coarser than a hexagon is a tessellation,
    // and so cos(pi/6) admits every chord.
    // How far a corner may travel: to its nearest neighbour and no further.
    std::map<int, double> nearest;
    for (std::size_t i = 0; i < loops.size(); i++) {
      if (!loop_valid[i]) continue;
      const std::vector<int>& loop = loops[i];
      for (std::size_t k = 0; k < loop.size(); k++) {
        const int a = loop[k], b = loop[(k + 1) % loop.size()];
        const double d = (vertices[a] - vertices[b]).norm();
        if (!(d > 0)) continue;
        auto ia = nearest.find(a);
        if (ia == nearest.end() || d < ia->second) nearest[a] = d;
        auto ib = nearest.find(b);
        if (ib == nearest.end() || d < ib->second) nearest[b] = d;
      }
    }
    // Which mesh facets each corner belongs to, for the fold test below.
    std::map<int, std::vector<std::size_t>> mesh_facets_at;
    for (std::size_t i = 0; i < loops.size(); i++) {
      if (!loop_valid[i] || loops[i].size() < 3) continue;
      for (const int v : loops[i]) mesh_facets_at[v].push_back(i);
    }
    // Whether moving `v` to `target` would fold a facet it belongs to.
    //
    // This is what the travel bound is a proxy for, asked directly. A corner is
    // refused "further than its nearest neighbour" because a move that passes a
    // neighbour drags its facet inside out - but distance is not direction, and
    // the two part company exactly where the answer is furthest. lid10's corner
    // at the taper-to-bore crease has its nearest neighbour 0.2356 away
    // *perpendicular* to the move, while the first vertex along the move is
    // 4.64 away: the scalar bound refuses 1.338 mm of travel that has 4.6 mm of
    // room. The move is a sixth of one station of the sweep - in the surface's
    // own parameters it is tiny, and it is long in millimetres only because the
    // thread crosses the crease at 1.6 degrees.
    //
    // The condition itself is exact and states no tolerance, and it asks only
    // about what the file will contain. Two things can go wrong there: an edge
    // at the corner turning round, which disorders a boundary run, and a facet
    // *written as itself* folding. A facet an analytic face absorbed is not in
    // the file at all - the surface is - so it is not asked. Asking it was the
    // first version's mistake: every corner it refused on lid10 and the band
    // family, eight of them, was refused on a consumed facet, and on the band
    // family that cost code 17 38 -> 46 where the right test takes it to 8.
    //
    // For a facet that is written: the corner has to stay on the inner side of
    // each edge it is not an end of, and the facet has to keep its normal. A
    // facet that was already degenerate there is not made a reason to refuse.
    auto folds_a_facet = [&](int v, const Vector3d& target) {
      const auto at = mesh_facets_at.find(v);
      if (at == mesh_facets_at.end()) return false;
      // No edge at the corner may turn round: the corner may not pass the vertex
      // at its other end, wherever that vertex has itself been placed. This is
      // what keeps a boundary run in order when its corners slide along the
      // curve two surfaces cross in, and it holds for every edge, written or not.
      for (const std::size_t i : at->second) {
        const std::vector<int>& lp = loops[i];
        for (std::size_t k = 0; k < lp.size(); k++) {
          if (lp[k] != v) continue;
          for (const int w : {lp[(k + 1) % lp.size()], lp[(k + lp.size() - 1) % lp.size()]}) {
            const auto mw = moves.find(w);
            const Vector3d& wp = mw != moves.end() ? mw->second : vertices[w];
            if ((target - wp).dot(vertices[v] - vertices[w]) <= 0) return true;
          }
        }
      }
      for (const std::size_t i : at->second) {
        // A facet an analytic face has absorbed is never written - the surface
        // is - so it cannot fold in the file however far its corner moves.
        // Asking it anyway is what refused every corner this test refused on
        // lid10 and the band family: all eight were on consumed facets.
        if (consumed[i]) continue;
        const std::vector<int>& lp = loops[i];
        const Vector3d n = loop_normals[i];
        Vector3d after = Vector3d::Zero();  // Newell's normal with the corner moved
        for (std::size_t k = 0; k < lp.size(); k++) {
          const Vector3d& a = lp[k] == v ? target : vertices[lp[k]];
          const Vector3d& b = lp[(k + 1) % lp.size()] == v ? target : vertices[lp[(k + 1) % lp.size()]];
          after += a.cross(b);
        }
        bool folds = after.dot(n) <= 0;
        for (std::size_t k = 0; k < lp.size() && !folds; k++) {
          const int a = lp[k], b = lp[(k + 1) % lp.size()];
          if (a == v || b == v) continue;
          const Vector3d e = vertices[b] - vertices[a];
          const double was = e.cross(vertices[v] - vertices[a]).dot(n);
          const double now = e.cross(target - vertices[a]).dot(n);
          if (was > 0 && now <= 0) folds = true;
        }
        if (folds) return true;
      }
      return false;
    };
    std::size_t onto_faces = 0, onto_plane = 0, onto_corner = 0, onto_triple_faces = 0;
    double worst_faces = 0, worst_plane = 0, worst_corner = 0;
    // Corners this pass leaves off a surface whose face is written on them,
    // counted by the exit they took. A corner is a vertex of every face that
    // uses it, and each of those faces asserts it lies on that face's surface;
    // one that does not is what a reader calls fault code 16. The count is kept
    // per reason because the reasons are unrelated and one number for all of
    // them says nothing about which to work on.
    std::size_t left_solve = 0, left_far = 0;
    double left_worst = 0;
    // The corners the arbiter could not seat, by *identity*. Counting them says
    // how many; the claim revision needs to know which, and a coordinate cannot
    // answer that once the point has moved - see PointOrigin.
    std::set<int> left_solve_ids, left_far_ids;
    auto off_its_faces = [&](int v, const Vector3d& at) {
      double worst = 0;
      const auto have = faces_at.find(v);
      if (have == faces_at.end()) return worst;
      for (const auto& sf : have->second) {
        Vector3d q;
        if (!AnalyticFeatures::closestOnSurface(sf.get(), at, q)) continue;
        // Against the surface as written, not against the band the declaration
        // publishes. A grid's band is its tessellation's sagitta - 0.2527 on
        // lid10's thread - and the file asserts the corner is on the *surface*,
        // not within a sagitta of it. Measuring against the band is what let two
        // corners 0.0367 and 0.0215 off the sweep go unreported while the rung
        // that could have placed them was never asked.
        const double owed = 1e-9 * std::max(1.0, at.norm());
        const double d = (q - at).norm();
        if (d > owed) worst = std::max(worst, d);
      }
      return worst;
    };
    auto note_left = [&](std::size_t& bucket, int v, const Vector3d& at) {
      const double d = off_its_faces(v, at);
      if (d <= 0) return;
      bucket++;
      // By identity, and only once it is established that the corner really is
      // off a face it bounds - the early return above is the whole test.
      (&bucket == &left_solve ? left_solve_ids : left_far_ids).insert(v);
      left_worst = std::max(left_worst, d);
    };
    // The arbiter. For each corner of an analytic face: every constraint on it,
    // every candidate position any rung proposed or the constraints themselves
    // determine, and the candidate that satisfies the most precise constraints
    // wins. Two tables decide it, both at the top of this file: kCornerTiers
    // ranks the constraints, kCornerRungs breaks the ties between rungs.
    std::size_t overruled = 0;  // corners an earlier, less precise rung had held
    std::map<CornerRung, std::size_t> won_by;
    auto is_declared = [&](const Surface *sf) {
      for (const auto& d : surfaces) {
        if (d.get() == sf || d->sameAs(*sf)) return true;
      }
      return false;
    };
    for (const auto& entry : faces_at) {
      const int v = entry.first;
      const auto reach = nearest.find(v);
      const double travel_bound = reach != nearest.end() ? reach->second : 0.0;

      struct Constraint {
        const Surface *surface;
        ConstraintTier tier;
      };
      std::vector<Constraint> constraints;
      std::vector<std::shared_ptr<const Surface>> keep_alive;
      auto add = [&](const Surface *sf, ConstraintTier tier) {
        if (!tierUsed(tier)) return;
        for (auto& c : constraints) {
          if (c.surface == sf || c.surface->sameAs(*sf)) {
            if (tierRank(tier) < tierRank(c.tier)) c.tier = tier;
            return;
          }
        }
        constraints.push_back({sf, tier});
      };
      auto interpolant = [](const Surface *sf) {
        if (dynamic_cast<const SweepSurface *>(sf) != nullptr) return false;  // closed form
        return dynamic_cast<const GridSurface *>(sf) != nullptr ||
               dynamic_cast<const BezierPatchSurface *>(sf) != nullptr;
      };
      for (const auto& sf : entry.second) {
        add(sf.get(), !is_declared(sf.get())  ? ConstraintTier::FittedFace
                      : interpolant(sf.get()) ? ConstraintTier::DeclaredInterpolant
                                              : ConstraintTier::DeclaredFace);
      }
      const double scale = std::max(1.0, vertices[v].norm());
      for (const auto& sf : entry.second) {
        const auto sp = section_planes.find(sf.get());
        if (sp == section_planes.end()) continue;
        for (const auto& cp : sp->second) {
          if (!cp.declared) continue;
          if (fabs((vertices[v] - cp.on_plane).dot(cp.normal)) > 1e-6 * scale) continue;
          keep_alive.push_back(std::make_shared<PlaneSurface>(cp.on_plane, cp.normal));
          add(keep_alive.back().get(), ConstraintTier::DeclaredFace);
        }
      }
      const auto owners = cornerOwners.find(v);
      if (owners != cornerOwners.end()) {
        for (const std::size_t j : owners->second) {
          if (j < surfaces.size()) add(surfaces[j].get(), ConstraintTier::ProvenanceOwner);
        }
      }
      {
        const auto at = mesh_facets_at.find(v);
        if (at != mesh_facets_at.end()) {
          for (const std::size_t i : at->second) {
            if (consumed[i]) continue;
            const Vector3d n = loop_normals[i].normalized();
            if (!(n.norm() > 0)) continue;
            // A written face on a plane the model declared is a declaration, not
            // the tessellation's chord. Classing every written plane as a chord
            // let lid10's z = 95 cap - declared by the cylinder whose top it is -
            // go soft, and the corners of its hole were placed 0.65 below it on
            // the outer wall, leaving an annulus of no width no fan could cut.
            const Vector3d on = vertices[loops[i][0]];
            const PlaneSurface *declared = nullptr;
            for (const auto& d : surfaces) {
              const auto *pl = dynamic_cast<const PlaneSurface *>(d.get());
              if (pl == nullptr) continue;
              const Vector3d dn = pl->normdir.normalized();
              if (fabs(fabs(dn.dot(n)) - 1) > 1e-9) continue;
              if (fabs(dn.dot(on - pl->refpt)) > 1e-9 * std::max(1.0, on.norm())) continue;
              declared = pl;
              break;
            }
            if (declared != nullptr) {
              add(declared, ConstraintTier::DeclaredFace);
              continue;
            }
            keep_alive.push_back(std::make_shared<PlaneSurface>(on, n));
            add(keep_alive.back().get(), ConstraintTier::MeshPlane);
          }
        }
      }
      const std::size_t n_all = constraints.size();

      // Which constraints a position satisfies, counted per tier, to the ladder's
      // exactness floor. Compared from the most precise tier down.
      using Score = std::array<std::size_t, kConstraintTierCount>;
      auto score_of = [&](const Vector3d& p) {
        Score s{};
        for (const auto& c : constraints) {
          Vector3d q;
          if (AnalyticFeatures::closestOnSurface(c.surface, p, q) &&
              (q - p).norm() <= 1e-9 * std::max(1.0, p.norm())) {
            s[tierRank(c.tier)]++;
          }
        }
        return s;
      };

      struct Candidate {
        Vector3d p;
        CornerRung rung;
      };
      std::vector<Candidate> candidates;
      candidates.push_back({vertices[v], CornerRung::Mesh});
      const auto earlier = moves.find(v);
      if (earlier != moves.end()) {
        const auto r = move_rung.find(v);
        candidates.push_back(
          {earlier->second, r != move_rung.end() ? r->second : CornerRung::Provenance});
      }
      // Solved from the constraints: every pair, which meet in a curve and give
      // its nearest point, and every triple, which meet in a point. The score
      // decides which of them is the corner.
      for (std::size_t a = 0; a < n_all; a++) {
        for (std::size_t b = a + 1; b < n_all; b++) {
          Vector3d p = vertices[v];
          AnalyticFeatures::projectOntoBoth(constraints[a].surface, constraints[b].surface, p, 1e-11);
          if (p.allFinite()) candidates.push_back({p, CornerRung::FileFaces});
          for (std::size_t c = b + 1; c < n_all; c++) {
            const Surface *tri[3] = {constraints[a].surface, constraints[b].surface,
                                     constraints[c].surface};
            // Two planes and a quadric: the planes meet in a line and a line meets
            // a quadric twice, both roots exact and only one of them the corner.
            // A free Newton lands on either - on lid10 the far one, 1.87 away,
            // which then blocked its neighbour. Along the line the quadric's
            // implicit is exactly a quadratic in the line's parameter, so three
            // evaluations fix it and both roots come out in closed form; both are
            // offered, and least travel breaks the tie between equally exact ones.
            const PlaneSurface *pl[3];
            int np = 0, quad = -1;
            for (int k = 0; k < 3; k++) {
              pl[k] = dynamic_cast<const PlaneSurface *>(tri[k]);
              if (pl[k] != nullptr) np++;
              else if (AnalyticFeatures::isQuadric(tri[k])) quad = k;
            }
            if (np == 2 && quad >= 0) {
              const PlaneSurface *p1 =
                pl[(quad + 1) % 3] != nullptr ? pl[(quad + 1) % 3] : pl[(quad + 2) % 3];
              const PlaneSurface *p2 = pl[(quad + 2) % 3] != nullptr && pl[(quad + 2) % 3] != p1
                                         ? pl[(quad + 2) % 3]
                                         : pl[(quad + 1) % 3];
              const Vector3d n1 = p1->normdir.normalized(), n2 = p2->normdir.normalized();
              const double d1 = n1.dot(p1->refpt), d2 = n2.dot(p2->refpt);
              const Vector3d dir = n1.cross(n2);
              const double cc = n1.dot(n2), det = 1 - cc * cc;
              if (dir.norm() > 1e-12 && fabs(det) > 1e-12) {
                const Vector3d dh = dir.normalized();
                const Vector3d base = n1 * ((d1 - d2 * cc) / det) + n2 * ((d2 - d1 * cc) / det);
                const double t0 = dh.dot(vertices[v] - base);
                double f[3];
                Vector3d g;
                bool ok = true;
                for (int k = 0; k < 3 && ok; k++) {
                  ok = AnalyticFeatures::surfaceImplicit(tri[quad], base + dh * (t0 + k - 1), f[k], g,
                                                         nullptr);
                }
                if (ok) {
                  // f(t0 + s) = A s^2 + B s + C through s = -1, 0, 1.
                  const double A = 0.5 * (f[0] + f[2]) - f[1], B = 0.5 * (f[2] - f[0]), C = f[1];
                  std::vector<double> roots;
                  if (fabs(A) > 1e-15 * std::max({fabs(B), fabs(C), 1.0})) {
                    const double disc = B * B - 4 * A * C;
                    if (disc >= 0) {
                      const double sq = std::sqrt(disc);
                      // The numerically stable pair, not the textbook one.
                      const double qq = -0.5 * (B + (B >= 0 ? sq : -sq));
                      if (qq != 0) roots.push_back(C / qq);
                      roots.push_back(qq / A);
                    }
                  } else if (B != 0) {
                    roots.push_back(-C / B);
                  }
                  for (const double s : roots) {
                    const Vector3d p = base + dh * (t0 + s);
                    if (p.allFinite()) candidates.push_back({p, CornerRung::FileFaces});
                  }
                }
              }
              continue;
            }
            Vector3d p3 = vertices[v];
            if (AnalyticFeatures::projectOntoThree(tri[0], tri[1], tri[2], p3, 1e-11, 0)) {
              candidates.push_back({p3, CornerRung::FileFaces});
            }
          }
        }
      }
      (void)travel_bound;

      // Weigh them: the tiers from the most precise down, then the rung table,
      // then least travel. A candidate that moves the corner must not fold the file.
      const Candidate *best = nullptr;
      Score best_s{};
      bool full_but_folds = false;
      Score all{};
      for (const auto& c : constraints) all[tierRank(c.tier)]++;
      // Which constraints the corner is on where the mesh put it, for `hard`.
      std::vector<char> on_at_mesh(constraints.size(), 0);
      for (std::size_t k = 0; k < constraints.size(); k++) {
        Vector3d q;
        on_at_mesh[k] = AnalyticFeatures::closestOnSurface(constraints[k].surface, vertices[v], q) &&
                        (q - vertices[v]).norm() <= 1e-9 * std::max(1.0, vertices[v].norm());
      }
      // Ordered, then walked: score from the most precise tier down, then the
      // rung table, then least travel. Within one score only the first - the
      // nearest of the equally exact - is eligible. Two planes and a quadric
      // have two roots that score the same, and the far one is a true solution
      // that is the wrong answer: taking it when the near one would fold moved
      // a corner of f04 2.27 and an edge off its faces by as much. So if the
      // nearest at a score folds or leaves a hard constraint, that whole score
      // is given up and the walk drops to the next lower one.
      struct Weighed {
        const Candidate *c;
        Score s;
        double travel;
      };
      std::vector<Weighed> weighed;
      for (const auto& cand : candidates)
        weighed.push_back({&cand, score_of(cand.p), (cand.p - vertices[v]).norm()});
      std::stable_sort(weighed.begin(), weighed.end(), [](const Weighed& x, const Weighed& y) {
        if (x.s != y.s) return x.s > y.s;
        const int rx = cornerRungRank(x.c->rung), ry = cornerRungRank(y.c->rung);
        if (rx != ry) return rx < ry;
        return x.travel < y.travel;
      });
      bool have_level = false;
      Score level{};
      for (const auto& w : weighed) {
        if (have_level && w.s == level) continue;  // this score was given up, or already decided
        have_level = true;
        level = w.s;
        const Candidate& cand = *w.c;
        const bool moved = w.travel > 0;
        bool leaves_hard = false;
        for (std::size_t k = 0; moved && k < constraints.size(); k++) {
          if (!tierHard(constraints[k].tier) || !on_at_mesh[k]) continue;
          Vector3d q;
          if (!AnalyticFeatures::closestOnSurface(constraints[k].surface, cand.p, q) ||
              (q - cand.p).norm() > 1e-9 * std::max(1.0, cand.p.norm())) {
            leaves_hard = true;
          }
        }
        if (leaves_hard) continue;
        if (moved && folds_a_facet(v, cand.p)) {
          if (w.s == all) full_but_folds = true;
          continue;
        }
        best = &cand;
        best_s = w.s;
        break;
      }
      // Where the mesh put it is always a valid answer, and the floor of the walk.
      if (best == nullptr) {
        best = &candidates.front();
        best_s = score_of(best->p);
      }
      const std::size_t best_n = [&] {
        std::size_t n = 0;
        for (const auto k : best_s) n += k;
        return n;
      }();
      const std::size_t n_surf = entry.second.size();
      (void)n_surf;
      if (best == nullptr) continue;  // the mesh position is always a candidate
      won_by[best->rung]++;
      if (earlier != moves.end() && best->rung != move_rung[v] &&
          (best->p - earlier->second).norm() > 0) {
        overruled++;
      }
      if (best->rung == CornerRung::Mesh) {
        moves.erase(v);
        move_rung.erase(v);
      } else {
        moves[v] = best->p;
        move_rung[v] = best->rung;
      }
      if (best->rung == CornerRung::FileFaces) {
        const double travel = (best->p - vertices[v]).norm();
        if (n_all >= 3) {
          worst_corner = std::max(worst_corner, travel);
          onto_triple_faces++;
        } else if (std::any_of(constraints.begin(), constraints.end(), [](const Constraint& c) {
                     return dynamic_cast<const PlaneSurface *>(c.surface) != nullptr;
                   })) {
          worst_plane = std::max(worst_plane, travel);
          onto_plane++;
        } else {
          worst_faces = std::max(worst_faces, travel);
          onto_faces++;
        }
      }
      if (best_n < n_all) note_left(full_but_folds ? left_far : left_solve, v, best->p);
    }
    // Every corner's status, now that the arbiter has spoken. Derived here and
    // not cached anywhere: a status that outlives the move it describes is a
    // second thing that can be silently stale, which is the disease this is
    // meant to cure. See AnalyticFeatures::PointOrigin.
    {
      std::map<AnalyticFeatures::PointOrigin, std::size_t> tally;
      for (const auto& entry : faces_at) {
        const int v = entry.first;
        AnalyticFeatures::PointOrigin origin = AnalyticFeatures::PointOrigin::Tessellation;
        if (moves.count(v) != 0) {
          origin = AnalyticFeatures::PointOrigin::Placed;
        } else {
          // A point the generator emitted, asked of the declarations rather than
          // measured: isDeclaredPoint is exact, by position, with no tolerance
          // to choose.
          for (const auto& d : surfaces) {
            const auto *g = dynamic_cast<const GridSurface *>(d.get());
            if (g != nullptr && g->isDeclaredPoint(vertices[v])) {
              origin = AnalyticFeatures::PointOrigin::Declared;
              break;
            }
          }
        }
        tally[origin]++;
      }
      std::string line;
      for (const auto& kv : tally) {
        line += (line.empty() ? "" : ", ") + std::to_string(kv.second) + " " +
                AnalyticFeatures::pointOriginName(kv.first);
      }
      if (!line.empty()) {
        LOG("STEP export: corners of analytic faces by where their coordinate comes from: %1$s", line);
      }
    }
    const std::size_t left_total = left_solve + left_far;
    if (left_total > 0) {
      LOG(message_group::Export_Warning,
          "STEP export: %1$d corners are left off a surface the file writes a face of them on, "
          "by %2$.4f at worst - %3$d because no candidate satisfies every face there, %4$d because "
          "the one that does would fold a face or turn an edge round",
          int(left_total), left_worst, int(left_solve), int(left_far));
    }
    {
      std::string tally;
      for (const auto& spec : kCornerRungs) {
        const auto w = won_by.find(spec.rung);
        if (w == won_by.end()) continue;
        tally += (tally.empty() ? "" : ", ") + std::to_string(w->second) + " " + spec.name;
      }
      if (!tally.empty()) {
        LOG(
          "STEP export: corners of analytic faces, by the placement that satisfies most of their faces: "
          "%1$s; "
          "%2$d of them overruled a less precise rung that had placed them first",
          tally, int(overruled));
      }
    }
    on_faces = onto_faces;
    on_plane_face = onto_plane + onto_corner;
    if (onto_triple_faces > 0) {
      LOG(
        "STEP export: %1$d corners are placed where the three analytic faces they bound meet - a "
        "point, where two faces leave a curve",
        int(onto_triple_faces));
    }
    if (onto_faces > 0) {
      LOG(
        "STEP export: %1$d corners are placed where the two analytic faces they bound cross, "
        "moving at most %2$.4f - the surfaces the file asserts them to be on",
        int(onto_faces), worst_faces);
    }
    if (onto_plane > 0) {
      LOG(
        "STEP export: %1$d corners are placed on the conic where the analytic face they bound "
        "meets the plane of the face beside it, moving at most %2$.4f - which is the curve that "
        "boundary is written as",
        int(onto_plane), worst_plane);
    }
    if (onto_corner > 0) {
      LOG(
        "STEP export: %1$d corners are placed where an analytic face and the two planes beside it "
        "meet, moving at most %2$.4f",
        int(onto_corner), worst_corner);
    }
  }

  // A corner of a trimmed quadric that nothing else holds goes on that quadric.
  //
  // The placement above reaches a corner two surfaces made. It does not reach a
  // corner the *mesh* made in the middle of a facet, which a trim then left on
  // the boundary of the face written on that quadric: nothing declared it, so
  // provenance names no owners for it and no crossing curve applies. It is
  // nonetheless a corner of an analytic face, sitting at the inradius of the
  // polygon the surface was tessellated as - 20(1 - cos(pi/32)) = 0.0963 inside
  // the cylinder its own face is written on.
  //
  // On step-band-family that is the last 39 corners, at the height where the
  // ridge tapers to nothing and the trim runs along the bore's own facets
  // instead of across them. They are moved onto the surface their face names,
  // and only where no other exact declared surface already holds them - a
  // corner on two of them belongs where both agree, which is the crossing curve
  // the paths above compute, not on either one alone.
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

  // A welded vertex stays where it is.
  //
  // Those are the insertions that keep the shell conformal: a vertex one face
  // has on an edge another face spans in one line, added to both so they meet
  // edge for edge. It therefore sits on the *boundary* of faces whose loops do
  // not contain it, and the bend gate below - which reads each face's own loop -
  // cannot see what moving it does to them. On step-declare-grid-two-bands one
  // such vertex was moved 1.73e-07 off the flat bottom it lies on the rim of,
  // and every loop in the file was still within 1e-9 of its own plane: the face
  // asserted a plane a corner of it was off, and nothing internal could tell.
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
  // Keep every exact thing the corner is already on.
  //
  // A corner owned by a declared quadric and a declared sweep was being put
  // where the two cross - but the two are not worth the same. The quadric
  // states where its surface is; the sweep was interpolated through the model's
  // stations and publishes a tessellation band saying how well. Aiming at their
  // crossing curve spends an exact surface to satisfy a fitted one.
  //
  // It shows up as a plane. The mesh's flat bottom on step-declare-grid.py is a
  // 95-corner face, and one of its corners is owned by the wall cylinder and
  // the ridge's sweep; placed on their crossing curve it leaves z = 0 by
  // 0.0423, and a face that big cannot be fanned without turning the part's
  // flat bottom into 93 triangles, three of them off level and one by 21
  // degrees. The corner was exactly on that plane, 0.0103 off the cylinder -
  // 19.2(1 - cos(pi/96)), the wall's own sagitta - and 0.0378 off the sweep,
  // which had declared a band of 0.1290. Nothing was wrong with it that the
  // sweep was entitled to complain about.
  //
  // So: a plane the mesh carries *and a declaration vouches for* is kept, and
  // the corner is placed on that plane crossed with its exact owner instead.
  // Measured over the fixture set, 1533 merged planes are bent by a move and
  // exactly 2 are vouched for - the flat bottoms of step-declare-grid.py and
  // -strip, matching a declaration's own plane to 0.00e+00 - so this reaches
  // the case it is for and no other.
  //
  // The direction is what makes it safe: a declaration only ever *confirms* a
  // plane the mesh already has, and never invents one. refpt is not reliably a
  // rim - declare_cylinder takes the caller's centre, and a fitted cylinder's
  // refpt is wherever the fit landed - so a plane the mesh does not carry must
  // not be conjured out of one.
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

  // No move may take a corner off the surface of an analytic face it bounds.
  //
  // The veto below asks that of a face that keeps a PLANE, and nothing asked it
  // of a curved one: the corner is written as a vertex of that face, and a face
  // bounded by a point off its own surface is describing a different surface.
  // Every path above is meant to satisfy this by construction - the crossing
  // puts a corner on both its owners, the conic and triple placements on their
  // owner, the pass onto a quadric on the face's own - and one of them did not.
  // The placement onto a conic trusted provenance's single owner, and where a
  // boolean cut every facet of a bore, that owner was the wall across the cap:
  // band-fn024's 48 bore corners were slid 3.0 off the face they bound, lid10's
  // 120 chamfer corners 0.65 and 2.25, hidden only by the plane veto. That path
  // now keeps a corner on the quadric it is already on (see deserts_exact), and
  // this is measured to be reached by no fixture and no flagship coupon, with
  // the veto and without it.
  //
  // So it is an error, not a report: reaching it means a placement has a defect
  // the file would carry. The move is held, so the file stays right, and the
  // flagship check fails on the message, so the defect cannot come back quietly.
  //
  // Against each face's own declaration: a quadric states where it is, a grid
  // or patch publishes its band - AnalyticFeatures::declaredBand - and a corner
  // already that far off is judged only on whether the move takes it further.
  // A band is not a Surface record, so its cone or cylinder is read off its
  // axis and rims. A projection that does not answer judges nothing.
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
  // Under the approximation flag only. The exact tier asserts nothing the mesh
  // does not already state, and moving a vertex is such an assertion - without
  // this gate step-bored-cone's analytic export comes out with a PLANE
  // disagreeing with the winding of its own bound.
  if (approximate && !moves.empty()) {
    auto at_moved = [&](int v) -> const Vector3d& {
      const auto m = moves.find(v);
      return m == moves.end() ? vertices[v] : m->second;
    };
    // Refuse the corners a bent face implicates, and settle - rather than
    // refusing every move in the model because one face somewhere would bend.
    //
    // All-or-nothing was right while the export could not survive a partial
    // move, and that is no longer true: root causes A and B of open item 11 -
    // an edge's geometry decided twice, and a vertex keyed by mesh index -
    // were what broke under it, and both are closed at the root. Measured
    // here, on a build where they are:
    //
    //   lid10   795 corners refused for 11 bent faces  ->  20 dropped, 775 kept
    //   f04    1268 corners refused for  7 bent faces  ->  14 dropped, 1254 kept
    //
    // and the whole of open item 1 rides on it: a corner that moves is a corner
    // on both its surfaces, an edge between two such corners is written as the
    // curve they cross along rather than as a chord, and SOLIDWORKS faults the
    // chords. With f04's corners freed this way its boundary goes from chords
    // to 1223 crossing curves and it reads `faults=0` where the same binary
    // with the all-or-nothing refusal reads `faults=2 codes=17` on the swept
    // B-spline and the bore - one model, one session, one variable.
    //
    // It settles because refusals only grow, which is the argument the section
    // settle loop already uses; on all six flagship coupons it reaches its
    // fixpoint in one round.
    std::size_t analytic_faces = 0, bent = 0, turned = 0;
    std::set<int> bent_verts;
    std::set<int> held_by_veto;  // corners the veto took a placement away from
    std::map<std::size_t, std::vector<std::array<int, 3>>> splittable;
    std::size_t settle_rounds = 0, settle_dropped = 0;
    std::size_t why_turned = 0, why_triangle = 0, why_notri = 0, why_inverted = 0, why_rings = 0;
    double why_worst_tri = -std::numeric_limits<double>::max();
    for (;;) {
      analytic_faces = 0;
      bent = 0;
      turned = 0;
      bent_verts.clear();
      splittable.clear();
      // Whose moves the refusal actually costs, as against how many faces caused
      // it. The two numbers reported were the count of faces and the count of
      // *all* moves, and neither said how local the damage was.
      // Which corners a bent face implicates: every corner it is bounded by, its
      // holes included, for the same reason the test above reads them. Taking the
      // outer loop alone names corners that are not the ones bending it - on
      // lid10 three faces bend through their holes and the outer loops name no
      // moved corner at all, so a refusal keyed on them drops nothing and gives
      // up on the whole export.
      auto implicated = [&](std::size_t i) {
        for (const int w : loops[i]) bent_verts.insert(w);
        for (std::size_t j = 0; j < loops.size(); j++) {
          if (!loop_valid[j] || consumed[j] || parents[j] != int(i)) continue;
          for (const int w : loops[j]) bent_verts.insert(w);
        }
      };
      for (std::size_t i = 0; i < loops.size(); i++) {
        if (!loop_valid[i]) continue;
        // Whether a move touches this face at all - over every bound it has, not
        // only its outer one. The measurement below already reads the holes,
        // because a face asserts one plane and its inner bounds are as much a part
        // of it as its rim; this test did not, so a face bent purely through a
        // hole was never examined. Measured on lid10, that is a bottom face whose
        // hole ends 1.8 off its own plane and a z = 75 ring whose hole ends 5.7
        // off - which OpenCASCADE cannot read as one planar face, so it splits
        // them into a full disc and a loose loop, seals the cavity between them
        // and reads the solid 28% too large. The all-or-nothing veto is what kept
        // that out of today's files, on these two coupons and by luck rather than
        // by design: the proposal is made in the normal build too.
        bool uses = false;
        for (const int v : loops[i]) {
          if (moves.count(v) != 0) uses = true;
        }
        for (std::size_t j = 0; !uses && j < loops.size(); j++) {
          if (!loop_valid[j] || consumed[j] || parents[j] != int(i)) continue;
          for (const int w : loops[j]) {
            if (moves.count(w) != 0) uses = true;
          }
        }
        if (!uses) continue;
        if (consumed[i]) {
          analytic_faces++;
          continue;
        }
        // A triangle stays planar wherever its corners are and can still turn
        // over: where a corner crosses the line of the opposite edge the winding
        // reverses, and the face then contradicts the normal the shell was
        // oriented by. Splitting cannot rescue one - every triangle of a fan
        // turns with it - so a face that would turn stops the move outright.
        Vector3d after(0, 0, 0);
        for (std::size_t k = 0; k < loops[i].size(); k++) {
          after += at_moved(loops[i][k]).cross(at_moved(loops[i][(k + 1) % loops[i].size()]));
        }
        if (after.dot(loop_normals[i]) <= 0) {
          turned++;
          why_turned++;  // counted where it happens: it used to count the faces that did *not* turn
          implicated(i);
          continue;
        }
        // Whether it is actually taken out of its plane, rather than whether it
        // has more than three corners. A corner cut by a plane moves *along* that
        // plane - the conic it lands on lies in it - so its face is not bent at
        // all, and assuming otherwise declined the whole of step-cut-cone for a
        // move that could not have touched it.
        // Against the plane the *face* will assert, which is the outer loop's -
        // its point and its normal, exactly as written below - and over every
        // corner that face carries, its holes included.
        //
        // Measuring each loop against its own plane is not the same thing and
        // lets this through: two loops coplanar in the mesh drift apart by a
        // fraction of the move, and a hole's far corner is then off the outer
        // loop's plane by that drift times the face's extent. On
        // step-declare-grid-two-bands that put a corner 1.73e-07 off a disc of
        // radius 19.2 - outside OpenCASCADE's own Precision::Confusion, so the
        // face asserted a plane its corner was not on, while every loop was
        // within 1e-9 of its own.
        const Vector3d& p0 = at_moved(loops[i][0]);
        double out_of_plane = 0;
        for (const int w : loops[i]) {
          out_of_plane = std::max(out_of_plane, fabs((at_moved(w) - p0).dot(loop_normals[i])));
        }
        for (std::size_t j = 0; j < loops.size(); j++) {
          if (!loop_valid[j] || consumed[j] || parents[j] != int(i)) continue;
          for (const int w : loops[j]) {
            out_of_plane = std::max(out_of_plane, fabs((at_moved(w) - p0).dot(loop_normals[i])));
          }
        }
        if (out_of_plane <= 1e-9) continue;
        // Bent, and a bent polygon is written as triangles instead of being left
        // alone: a triangle is planar wherever its corners are.
        //
        // A hole used to end it here - "a fan from one corner triangulates a
        // simple loop and an inner bound would have to be threaded into it" -
        // and threading it in is exactly what `triangulateWithHoles` does, by
        // bridging each hole into the outer ring before clipping ears. On lid10
        // that is what held five corners 0.0109 to 0.1089 inside the quadric
        // their own face is written on, which is a reader's fault code 16.
        //
        // A triangle with no hole is not bent and never was. Three points are
        // coplanar wherever they go, so what this test measured for one was its
        // tilt away from the normal the *mesh* gave it - which is nothing to do
        // with whether the face can be written. It can: on the plane its own
        // three corners are on, which is what the writer now asserts. Measured
        // on lid10, that is the whole of the veto's remaining refusals - five
        // triangles, holding five corners 0.0109 to 0.1089 off their quadric.
        bool has_hole = false;
        for (std::size_t j = 0; j < loops.size(); j++) {
          if (loop_valid[j] && !consumed[j] && parents[j] == int(i)) has_hole = true;
        }
        if (loops[i].size() < 4 && !has_hole) continue;
        if (loops[i].size() < 4) {
          bent++;
          why_triangle++;
          implicated(i);
          continue;
        }
        // The triangulation is done on the corners *as moved*, because that is
        // where they will be written, and it is judged by the same rule the fan
        // was: every triangle has to wind the way the polygon does. One that
        // does not lies outside the loop bounding it, and the face written for
        // it would contradict that loop. The move is what makes this bite -
        // before it these polygons are convex and any apex would do; after it,
        // three corners of a bore facet sit nearly in a line on the trim the
        // ridge left, and an ear cut from the wrong one comes out at -4.0e-05.
        const Vector3d nhat = loop_normals[i].normalized();
        std::vector<std::vector<int>> rings;
        rings.push_back(loops[i]);
        for (std::size_t j = 0; j < loops.size(); j++) {
          if (loop_valid[j] && !consumed[j] && parents[j] == int(i)) rings.push_back(loops[j]);
        }
        const std::vector<std::array<int, 3>> tris =
          triangulateWithHoles(rings, at_moved, loop_normals[i]);
        double worst_tri = tris.empty() ? -1 : std::numeric_limits<double>::max();
        for (const auto& t : tris) {
          const Vector3d q0 = at_moved(t[0]), q1 = at_moved(t[1]), q2 = at_moved(t[2]);
          worst_tri = std::min(worst_tri, 0.5 * (q1 - q0).cross(q2 - q0).dot(nhat));
        }
        if (worst_tri <= 0) {
          bent++;
          if (tris.empty()) why_notri++;
          else why_inverted++;
          if (worst_tri > why_worst_tri) why_worst_tri = worst_tri;
          why_rings = std::max(why_rings, rings.size());
          implicated(i);
        } else {
          splittable.emplace(i, tris);
        }
      }
      // A face that turns over cannot be rescued by splitting - every triangle of
      // the fan turns with it - so it stops the move outright, and so does a
      // polygon that cannot be split. Otherwise the fans go ahead: all of the
      // corners move or none of them do.
      bent += turned;
      if (bent == 0 || bent_verts.empty()) break;
      std::size_t dropped_here = 0;
      for (const int v : bent_verts) {
        if (moves.count(v) != 0) held_by_veto.insert(v);
        dropped_here += moves.erase(v);
      }
      settle_rounds++;
      settle_dropped += dropped_here;
      if (dropped_here == 0 || moves.empty()) break;
    }
    if (settle_dropped > 0) {
      // Still a boundary given up, and still worth saying so - but the corners
      // it costs are the ones that actually touch a face which would bend,
      // rather than every corner in the model. `export-step-flagship-strict`
      // asserts this never happens and is registered WILL_FAIL: what is left
      // to close is the faces that bend and cannot be fanned into triangles,
      // which is a polygon with a hole needing a real triangulation.
      LOG(message_group::Export_Warning,
          "STEP export: %1$d corners are held where the mesh put them, each touching a face that "
          "keeps a plane and would bend if it moved; %2$d corners were placed, and the refusal "
          "settled in %3$d round%4$s",
          int(settle_dropped), int(moves.size()), int(settle_rounds), settle_rounds == 1 ? "" : "s");
      LOG(message_group::Export_Warning,
          "STEP export:    why: %1$d faces would turn over, %2$d are triangles, %3$d could not be "
          "triangulated, %4$d triangulate with a piece wound the wrong way (worst %5$.3e, up to "
          "%6$d rings)",
          int(why_turned), int(why_triangle), int(why_notri), int(why_inverted),
          why_worst_tri == -std::numeric_limits<double>::max() ? 0.0 : why_worst_tri, int(why_rings));
      // ...and what that costs, which the line above does not say: a corner held
      // here is a corner of an analytic face too, and holding it leaves that
      // face bounded by a point off its own surface. That is the other half of
      // the trade the veto makes - a plane kept flat, a quadric left unmet - and
      // it is what a reader calls fault code 16, "vertex does not lie on face's
      // surface". Measured against the surfaces of the faces that use the
      // corner, which is what the file will assert about it.
      std::map<int, std::vector<const Surface *>> at_held;
      auto hold_note = [&](int v, const Surface *sf) {
        if (sf == nullptr || held_by_veto.count(v) == 0) return;
        auto& list = at_held[v];
        for (const Surface *have : list) {
          if (have == sf || have->sameAs(*sf)) return;
        }
        list.push_back(sf);
      };
      auto hold_patches = [&](const std::vector<AnalyticFeatures::Patch>& patches) {
        for (const auto& patch : patches) {
          if (!patch.alive) continue;
          for (const auto& run : patch.runs) {
            for (const int v : run.verts) hold_note(v, patch.surface.get());
          }
        }
      };
      hold_patches(quadric_faces);
      hold_patches(grid_faces);
      hold_patches(bezier_patches);
      std::size_t off_faces = 0;
      double off_worst = 0;
      for (const auto& entry : at_held) {
        double worst = 0;
        for (const Surface *sf : entry.second) {
          Vector3d q;
          if (!AnalyticFeatures::closestOnSurface(sf, vertices[entry.first], q)) continue;
          const double owed =
            std::max(1e-9 * std::max(1.0, vertices[entry.first].norm()), declaredBand(sf));
          const double d = (q - vertices[entry.first]).norm();
          if (d > owed) worst = std::max(worst, d);
        }
        if (worst > 0) {
          off_faces++;
          off_worst = std::max(off_worst, worst);
        }
      }
      if (off_faces > 0) {
        LOG(message_group::Export_Warning,
            "STEP export:    %1$d of them are left off the surface of an analytic face they "
            "bound, by %2$.4f at worst - the plane keeps its shape and the quadric does not get "
            "its corner",
            int(off_faces), off_worst);
      }
    }
    std::size_t touched = 0;
    for (const auto& m : moves) {
      if (bent_verts.count(m.first) != 0) touched++;
    }
    if (bent == 0) split_for_corners = splittable;
    if (bent > 0) {
      // Reached only where a face bends and implicates no corner the settle
      // loop can drop, which the loop above cannot resolve by dropping more.
      // Measured unreachable on all six flagship coupons and all 45 fixtures;
      // if it is ever reached the moves are given up wholesale, which is what
      // this exporter did for every bend until 2026-09-16.
      LOG(message_group::Export_Warning,
          "STEP export: every corner is held where the mesh put them: %1$d face%2$s that keeps a "
          "plane would bend and names no corner to refuse, so no corner moves at all",
          int(bent), bent == 1 ? "" : "s");
    } else if (analytic_faces > 0) {
      double worst = 0;
      for (const auto& m : moves) {
        if (m.first < 0 || std::size_t(m.first) >= vertices.size()) continue;
        worst = std::max(worst, (m.second - vertices[m.first]).norm());
        vertices[m.first] = m.second;
      }
      // Three populations, not one. `moves` starts as the corners two declared
      // owners cross at and then takes the conic and triple-point placements
      // above, so calling the total "their two declared owners" was false
      // wherever the other paths fired: on step-exact-trim provenance names two
      // owners for *zero* vertices and this line still claimed 40 of them.
      // Each is named now, and they sum to the total by construction - every
      // path checks `moves.count(v)` before inserting.
      LOG(
        "STEP export: %1$d corners moved, by at most %2$.4f - %3$d where two declared owners "
        "cross, %4$d onto a conic with one plane, %5$d where three exact things meet, %6$d where "
        "the two analytic faces they bound cross, %7$d where an analytic face meets the plane of "
        "the face beside it, %8$d onto the quadric their own face is written "
        "on; %9$d analytic faces now bound themselves on their own surface",
        int(moves.size()), worst, int(cornerMoves.size()), int(on_conic), int(on_triple), int(on_faces),
        int(on_plane_face), int(on_own), int(analytic_faces));
      if (!split_for_corners.empty()) {
        std::size_t added = 0;
        for (const auto& sc : split_for_corners) added += sc.second.size() - 1;
        LOG(
          "STEP export: %1$d face%2$s that keep%3$s a plane fanned into triangles to follow them, "
          "adding %4$d face%5$s",
          int(split_for_corners.size()), split_for_corners.size() == 1 ? "" : "s",
          split_for_corners.size() == 1 ? "s" : "", int(added), added == 1 ? "" : "s");
      }
    }
  }
}
