/*! The corner placement's placeOnFileFaces - see BodyBuild.h. */
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
#include "io/step/Meridian.h"
#include "io/step/PlaneSections.h"
#include "utils/printutils.h"

using namespace step;
using AnalyticFeatures::declaredBand;
using AnalyticFeatures::isQuadric;
using AnalyticFeatures::projectOntoBoth;
using AnalyticFeatures::projectOntoSurfaceAndPlanes;
using AnalyticFeatures::surfaceImplicit;

void StepKernel::BodyBuild::placeOnFileFaces()
{
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
          // A declared sweep and a coaxial surface of revolution cross in closed
          // form, and the corner keeps its station on it - see Meridian::place.
          step::Meridian meridian;
          Vector3d exact;
          if (step::Meridian::of(constraints[a].surface, constraints[b].surface, meridian) &&
              meridian.place(p, exact)) {
            p = exact;
          } else {
            AnalyticFeatures::projectOntoBoth(constraints[a].surface, constraints[b].surface, p, 1e-11);
          }
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
}
