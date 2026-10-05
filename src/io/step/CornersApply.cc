/*! The corner placement's applyCornerMoves - see BodyBuild.h. */
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

void StepKernel::BodyBuild::applyCornerMoves()
{
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
        // A hole is a bound of its face, and is judged with it: everything below
        // reads every hole of the face it asks about. Judged as a face of its
        // own, a hole a move bent was fanned into triangles of its own, which
        // covered the hole a second time against the face already bounding it -
        // every edge of the ring used twice the same way (open item 39, R24).
        if (!loop_valid[i] || loop_is_hole[i]) continue;
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
        //
        // Asked as the writer asks what it writes (flatWhereItStands): over
        // every corner the face carries, its holes included - measuring each
        // loop against its own plane let a hole drift 1.73e-07 off a disc of
        // radius 19.2 on step-declare-grid-two-bands - against the declared
        // plane the face was made on where there is one, and otherwise whether
        // the corners are on one plane at all. Until 2026-09-30 it was asked
        // against the mesh's normal through a moved corner, within 1e-9: that
        // called a face the moves had tilted whole and left flat bent, and
        // fanned it for a bend it did not have - 41 faces over the measured
        // exports - and could not see a face whose every corner moved alike off
        // its declared plane (open item 39, R2, R3).
        if (flatWhereItStands(i, at_moved)) continue;
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
          // A triangle that states no plane at machine precision winds neither
          // way, and the writer could not write it: as wrong as one turned over
          // (open item 39, R9) - asked here as there, so the fan the placement
          // accepts is the fan the writer writes.
          const double area =
            recognition::statesPlane(q0, q1, q2) ? 0.5 * (q1 - q0).cross(q2 - q0).dot(nhat) : 0.0;
          worst_tri = std::min(worst_tri, area);
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
        // By the one predicate (AnalyticFeatures::onFace), where a grid was
        // allowed its declared band - the mesh's distance from the surface, not
        // the file's (open item 39, R15).
        for (const Surface *sf : entry.second) {
          worst = std::max(worst, AnalyticFeatures::offFace(sf, vertices[entry.first]));
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
      // By the placement each move is finally the work of, so they sum to the
      // total by construction. Calling the total "their two declared owners"
      // was false wherever another path fired: on step-exact-trim provenance
      // names two owners for *zero* vertices and the line claimed 40 of them.
      // And counting what each path *proposed* was false once the arbiter
      // weighed them: it overrules and drops proposals, and the paths' counts
      // summed to 144 of 120 moves on a bore with a frustum through it (open
      // item 39, R14). A move no rung is recorded for is provenance's, as the
      // placement starts.
      std::map<CornerRung, std::size_t> by_rung;
      for (const auto& m : moves) {
        const auto r = move_rung.find(m.first);
        by_rung[r != move_rung.end() ? r->second : CornerRung::Provenance]++;
      }
      std::string tally;
      for (const auto& spec : kCornerRungs) {
        const auto n = by_rung.find(spec.rung);
        if (n == by_rung.end()) continue;
        tally += (tally.empty() ? "" : ", ") + std::to_string(n->second) + " " + spec.name;
      }
      LOG(
        "STEP export: %1$d corners moved, by at most %2$.4f - by the placement that put them "
        "there: %3$s; %4$d analytic faces now bound themselves on their own surface",
        int(moves.size()), worst, tally, int(analytic_faces));
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
