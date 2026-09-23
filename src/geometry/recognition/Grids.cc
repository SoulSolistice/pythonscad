/*! The grid recogniser: declared sweeps, proposed and formed.
 *
 * Part of the recognition module - see doc/step-recognition-design.md, *The
 * recognition module, as built*. Moved out of AnalyticFeatures.cc as it stood.
 */
#include "geometry/AnalyticFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

#include "geometry/Surface.h"
#include "geometry/recognition/Internal.h"
#include "geometry/recognition/Ledger.h"

namespace AnalyticFeatures {

/*! One Patch out of a set of facets: its boundary cycles, and the runs those
 * split into.
 *
 * Shared rather than written twice because it depends on nothing about which
 * kind of surface claimed the facets. A Bezier covers its whole parameter
 * square, so its boundary could be split by geometry - this stretch is u=0,
 * that one is v=1. Nothing else can: a declared grid is trimmed wherever the
 * boolean cut it, and a quadric region is whatever the booleans left of it.
 * The split here follows the *topology* instead, which is what a run actually
 * needs to be - a maximal stretch of consecutive boundary segments with the
 * same face on the far side, because every segment of it has to be replaced
 * in that one neighbour by one curve or the shell opens. */
/*! Propose: every facet each declared sweep covers, whoever holds it. Pure -
 * it reads the graph and the surfaces and no claim of any pass. Membership is
 * every corner on the sweep and the centroid on it too; evidence D1 for a
 * declared sweep, D3 for one the approximation recovered. */
std::vector<GridProposal> proposeGrids(const recognition::FacetGraph& graph,
                                       const std::vector<std::shared_ptr<Surface>>& surfaces)
{
  const Mesh& mesh = graph.mesh();
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;
  std::vector<GridProposal> out;
  for (std::size_t surface_index = 0; surface_index < surfaces.size(); surface_index++) {
    const auto& surface = surfaces[surface_index];
    const auto *grid = dynamic_cast<const GridSurface *>(surface.get());
    if (grid == nullptr || grid->net.empty()) continue;
    GridProposal proposal;
    proposal.surface_index = surface_index;
    // The declared points bound the sweep, so a box round them rejects most of
    // the model without projecting anything - the same reason the Bezier path
    // takes a box round its control net first. The band is added because
    // membership itself is only accurate to the band.
    Vector3d lo = grid->net.front(), hi = grid->net.front();
    for (const auto& p : grid->net) {
      lo = lo.cwiseMin(p);
      hi = hi.cwiseMax(p);
    }
    const double slack = grid->tessellationBand() + 1e-6 * std::max(1.0, (hi - lo).norm());
    lo.array() -= slack;
    hi.array() += slack;
    for (std::size_t f = 0; f < loops.size(); f++) {
      if (!loop_valid[f] || is_hole[f]) continue;
      bool on = true;
      for (const int v : loops[f]) {
        const Vector3d& p = vertices[v];
        if ((p.array() < lo.array()).any() || (p.array() > hi.array()).any()) {
          on = false;
          break;
        }
      }
      if (!on) continue;
      for (const int v : loops[f]) {
        std::vector<Vector3d> unused;  // pointMember's scratch argument, as elsewhere
        if (!const_cast<GridSurface *>(grid)->pointMember(unused, vertices[v])) {
          on = false;
          break;
        }
      }
      if (!on) continue;
      // Corners are not enough. Every corner of the facet closing a declared
      // profile is a point the generator emitted, so position alone claims it
      // even when the declaration does not cover that strip - which is exactly
      // what a grid declared open does. The centroid is the cheapest point that
      // is not a declared point, and asking whether it lies on the surface asks
      // about the facet rather than about its corners.
      Vector3d centroid = Vector3d::Zero();
      for (const int v : loops[f]) centroid += vertices[v];
      centroid /= double(loops[f].size());
      double pu = 0, pv = 0;
      if (!grid->project(centroid, pu, pv)) {
        proposal.corners_only++;
        continue;
      }
      const double miss = (grid->evaluate(pu, pv) - centroid).norm();
      if (miss > grid->membershipTolerance()) {
        proposal.corners_only++;
        proposal.worst_miss = std::max(proposal.worst_miss, miss);
        // The widest, not the last seen: claim_band two lines down already
        // takes a max, and the two describing the same kind of quantity
        // differently is what made this one wrong.
        proposal.missed_against = std::max(proposal.missed_against, grid->membershipTolerance());
        continue;
      }
      // How far the facet's own corners are off the fit. These become the
      // corners of the written face wherever the facet is on its boundary, so
      // unlike the centroid above they are asserted in the file.
      double stray = 0;
      for (const int v : loops[f]) {
        double vu = 0, vv = 0;
        if (!grid->project(vertices[v], vu, vv)) {
          stray = std::numeric_limits<double>::max();
          break;
        }
        stray = std::max(stray, (grid->evaluate(vu, vv) - vertices[v]).norm());
      }
      proposal.facets.push_back(f);
      proposal.miss.push_back(miss);
      proposal.u.push_back(pu);
      proposal.v.push_back(pv);
      proposal.stray.push_back(stray);
    }
    out.push_back(std::move(proposal));
  }
  return out;
}

std::vector<Patch> recogniseGridPatches(const recognition::FacetGraph& graph,
                                        const std::vector<std::shared_ptr<Surface>>& surfaces,
                                        const std::vector<char>& consumed,
                                        std::vector<std::string>& report, ClaimLedger *ledger,
                                        std::size_t declared_count)
{
  return formGridPatches(graph, surfaces, consumed, report, ledger, declared_count,
                         proposeGrids(graph, surfaces));
}

std::vector<Patch> formGridPatches(const recognition::FacetGraph& graph,
                                   const std::vector<std::shared_ptr<Surface>>& surfaces,
                                   const std::vector<char>& consumed, std::vector<std::string>& report,
                                   ClaimLedger *ledger, std::size_t declared_count,
                                   const std::vector<GridProposal>& proposals)
{
  const Mesh& mesh = graph.mesh();
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;

  std::vector<Patch> patches;
  std::vector<char> taken(loops.size(), 0);
  std::size_t corners_only = 0;               // every corner on the sweep, the middle not
  double worst_claimed = 0.0;                 // the furthest a *claimed* facet's middle strays
  double claim_band = 0.0;                    // the allowance it was measured against
  double worst_at_u = 0.0;                    // and where along the sweep it happened
  std::size_t cut_into = 0;                   // faces a wrapping claim had to be cut into
  double worst_miss = 0, missed_against = 0;  // how far off, and what was allowed

  // Which faces sit on either side of every edge of the mesh. Needed before the
  // boundaries are split rather than after, because for a declared grid the
  // split *is* by neighbour - see below.
  // The adjacency every recogniser walks, built once - see recognition::FacetGraph.
  const std::map<EdgeKey, std::vector<std::size_t>>& edge_loops = graph.edgeLoops();

  for (std::size_t surface_index = 0; surface_index < surfaces.size(); surface_index++) {
    const auto& surface = surfaces[surface_index];
    const auto *grid = dynamic_cast<const GridSurface *>(surface.get());
    if (grid == nullptr || grid->net.empty()) continue;

    // Which facets lie on the sweep, and where along the profile each one sits.
    // The second answer is needed as early as the first: a region covering every
    // span of a closed profile closes around it, and no face on a surface
    // written as an open rectangle can be bounded across its own seam.
    const int segs = grid->closed_v ? grid->cols : grid->cols - 1;
    // A declared sweep is an interpolant through the model's stations and
    // publishes its band, which is D1; a sweep the approximation recovered from
    // the mesh is past `declared_count` and is D3 whatever it interpolates.
    auto record = [&](std::size_t f, ClaimFate fate, const char *why) {
      if (ledger == nullptr) return;
      recognition::recordClaim(*ledger, recognition::kDeclaredSweep, f, surface, surface_index,
                               declared_count, fate, why);
    };
    std::vector<std::size_t> claimed;
    std::vector<std::size_t> contested;  // on this sweep, but already held elsewhere
    std::vector<int> span_of;
    std::vector<double> stray_of;  // worst any corner of the facet is off the fit
    // What this sweep proposed - every facet it covers, whoever holds it -
    // granted by the policy in force today. See proposeGrids.
    const GridProposal *proposal = nullptr;
    for (const auto& p : proposals) {
      if (p.surface_index == surface_index) proposal = &p;
    }
    if (proposal == nullptr) continue;
    corners_only += proposal->corners_only;
    worst_miss = std::max(worst_miss, proposal->worst_miss);
    missed_against = std::max(missed_against, proposal->missed_against);
    for (std::size_t k = 0; k < proposal->facets.size(); k++) {
      const std::size_t f = proposal->facets[k];
      const double miss = proposal->miss[k], pu = proposal->u[k], pv = proposal->v[k];
      const double stray = proposal->stray[k];
      // A facet something else already holds is still *examined*, and if this
      // sweep would have covered it that is recorded rather than lost.
      //
      // Whoever runs first takes a facet and it becomes invisible to everyone
      // after, so the contest leaves no trace and "why is this facet a plane?"
      // has to be reconstructed with an instrumented build. Cataloguing the
      // claim costs one membership test per held facet and answers it outright.
      // See doc/step-export-development.md, *Catalogue first, arbitrate
      // second*: this is the record, not yet the arbitration - who holds the
      // facet is still decided by who ran first.
      const bool held = consumed[f] != 0 || taken[f] != 0;
      if (held) {
        // Covered and not held: recorded, never honoured. A claim that leaves
        // no trace is the whole defect this ledger exists for.
        contested.push_back(f);
        record(f, ClaimFate::Held, "covered, but already held when this pass looked");
        continue;
      }
      claimed.push_back(f);
      stray_of.push_back(stray);
      if (miss > worst_claimed) worst_at_u = pu;
      worst_claimed = std::max(worst_claimed, miss);
      claim_band = std::max(claim_band, grid->membershipTolerance());
      span_of.push_back(segs > 0 ? std::max(0, std::min(segs - 1, int(pv * segs))) : 0);
    }
    // What this sweep covers and does not hold. Reported per holder, because
    // "already taken" without a name is the answer that costs an afternoon.
    if (!contested.empty()) {
      std::map<std::string, std::size_t> by_holder;
      for (const std::size_t f : contested) {
        const Claim *first = ledger != nullptr ? ledger->firstClaimant(f) : nullptr;
        by_holder[first != nullptr && first->by != nullptr ? first->by : "another declared sweep"]++;
      }
      std::string who;
      for (const auto& kv : by_holder) {
        who += (who.empty() ? "" : ", ") + std::to_string(kv.second) + " by " + kv.first;
      }
      report.push_back(
        format("%d facets this declared sweep also covers are held by something that claimed them "
               "first: %s - who holds a facet is still decided by which pass ran first, and this "
               "is the record of what that cost",
               int(contested.size()), who.c_str()));
    }
    if (claimed.empty()) continue;

    // Refuse the facets where the fit gave up, judged against the rest of this
    // same claim rather than against a fixed figure.
    //
    // A cubic interpolates its stations exactly, so a corner the generator
    // declared is on the surface to 1e-14 and only a corner some boolean made
    // can be off it at all. Measured over the reference lid's thread, those cut
    // corners sit at a flat 0.0128 for 85% of the sweep and then jump to 0.1931
    // and 0.2184 in the first and last twentieth - the run-in and run-out, where
    // a cubic has least support and the model's uniform sampling is thinnest
    // against geometry changing fastest. On step-declare-grid-scad the same
    // measurement is 0.0379 from end to end, with a worst of 0.0387.
    //
    // So the two are told apart by the shape of the distribution and not by its
    // size: a fit that is uniformly a little off is a fit, and one that is clean
    // everywhere and wild in two places has *failed in those two places*. The
    // median of the non-trivial strays is what "a little off" means for this
    // claim, and four times it is the outlier test. On the lid that is 0.051,
    // which cuts the two ends and keeps the middle; on the fixture it is 0.152,
    // which cuts nothing.
    //
    // Those corners are why this matters: a corner 0.2 off the surface is a
    // vertex of the written face, and doc/step-export-development.md, *What the
    // CAD kernels do*, records what a strict reader does with one.
    {
      std::vector<double> ranked;
      for (const double d : stray_of) {
        if (d > 1e-9 && d < std::numeric_limits<double>::max()) ranked.push_back(d);
      }
      if (!ranked.empty()) {
        std::sort(ranked.begin(), ranked.end());
        const double typical = ranked[ranked.size() / 2];
        const double allow = 4 * typical;
        std::vector<std::size_t> kept;
        std::vector<int> kept_span;
        std::size_t refused = 0;
        for (std::size_t k = 0; k < claimed.size(); k++) {
          if (stray_of[k] > allow) {
            refused++;
            // A refusal by a statistic over the claim's own strays - the shape
            // open item 27 wants gone. Recorded so the census can say so.
            record(claimed[k], ClaimFate::Declined,
                   "a corner is further off the fit than four times this claim's median stray");
            continue;
          }
          kept.push_back(claimed[k]);
          kept_span.push_back(span_of[k]);
        }
        if (refused > 0) {
          report.push_back(
            format("%d facets of the sweep are left faceted: a corner of each is further off the fit "
                   "than four times the %.4f this claim is typically off, by up to %.4f",
                   int(refused), typical, *std::max_element(stray_of.begin(), stray_of.end())));
        }
        claimed = std::move(kept);
        span_of = std::move(kept_span);
      }
    }
    if (claimed.empty()) continue;

    // Build one Patch out of a set of facets: its boundary cycles, and the runs
    // those split into. Called once for the whole claim, or once per group when
    // the claim has to be cut - see below.
    //
    // One per connected component, because a claim is not necessarily in one
    // piece and a face cannot be in two - see connectedComponents above.
    auto buildPatches = [&](const std::vector<std::size_t>& fs) {
      std::vector<Patch> out;
      for (const auto& piece : connectedComponents(loops, edge_loops, fs)) {
        out.push_back(patchFromFacets(loops, edge_loops, surface, piece));
      }
      return out;
    };

    // Does the claim close around the profile? Only a closed profile can, and
    // only when every span of it carries facets.
    bool wraps = grid->closed_v && segs > 0;
    if (wraps) {
      std::vector<char> used(segs, 0);
      for (const int span : span_of) used[span] = 1;
      for (const char c : used) wraps = wraps && c != 0;
    }

    if (!wraps) {
      for (Patch& patch : buildPatches(claimed)) {
        if (patch.alive) {
          for (const std::size_t f : patch.facets) taken[f] = 1;
        }
        patches.push_back(std::move(patch));
      }
      continue;
    }

    // Cut it into arcs of spans, each of which stays inside the rectangle.
    //
    // The alternative is to write the surface as closed across v and carry a
    // seam edge round the loop, the way a full cylindrical band is written.
    // That works when the region is the whole tube; here it is whatever the
    // boolean left of one, with a trim boundary meeting the seam wherever it
    // happens to. Cutting is the operation that does not depend on the shape of
    // that boundary: each arc of spans is a sheet in its own right and its own
    // face, and the cut runs along mesh edges the two arcs already share, so
    // nothing is asked of the neighbours.
    //
    // Halves first, because two faces are better than four and a ridge whose
    // profile is a simple quadrilateral splits cleanly; one face per span if a
    // half is not a sheet.
    std::vector<std::vector<int>> attempts;
    if (segs >= 4) attempts.push_back({0, segs / 2});
    std::vector<int> each;
    for (int i = 0; i < segs; i++) each.push_back(i);
    attempts.push_back(each);

    for (std::size_t a = 0; a < attempts.size(); a++) {
      const std::vector<int>& starts = attempts[a];
      std::vector<Patch> built;
      bool all_alive = true;
      for (std::size_t g = 0; g < starts.size() && all_alive; g++) {
        const int from = starts[g];
        const int to = g + 1 < starts.size() ? starts[g + 1] : segs;
        std::vector<std::size_t> group;
        for (std::size_t i = 0; i < claimed.size(); i++) {
          if (span_of[i] >= from && span_of[i] < to) group.push_back(claimed[i]);
        }
        if (group.empty()) continue;
        for (Patch& patch : buildPatches(group)) {
          all_alive = all_alive && patch.alive;
          built.push_back(std::move(patch));
        }
      }
      // The last attempt stands whatever it produced, so a sweep that cannot be
      // cut into sheets still reports why rather than vanishing.
      if (!all_alive && a + 1 < attempts.size()) continue;
      cut_into += built.size();
      for (auto& patch : built) {
        if (patch.alive) {
          for (const std::size_t f : patch.facets) taken[f] = 1;
        }
        patches.push_back(std::move(patch));
      }
      break;
    }
  }

  // What each run borders. Grouping by neighbour has already answered which
  // face it is; what is left is where in that face the run sits, which is what
  // the emitter substitutes into.
  std::map<std::size_t, std::size_t> patch_of_loop;
  for (std::size_t pi = 0; pi < patches.size(); pi++) {
    if (!patches[pi].alive) continue;
    for (const std::size_t f : patches[pi].facets) patch_of_loop[f] = pi;
  }
  for (std::size_t pi = 0; pi < patches.size(); pi++) {
    if (!patches[pi].alive) continue;
    for (auto& run : patches[pi].runs) {
      if (run.verts.size() < 2) continue;  // stays UNRESOLVED
      const auto it = edge_loops.find(edgeKey(run.verts[0], run.verts[1]));
      if (it == edge_loops.end() || it->second.size() != 2) continue;
      const std::size_t f0 = it->second[0], f1 = it->second[1];
      const auto own = patch_of_loop.find(f0);
      const std::size_t other = (own != patch_of_loop.end() && own->second == pi) ? f1 : f0;
      const auto op = patch_of_loop.find(other);
      if (op != patch_of_loop.end()) {
        if (op->second == pi) continue;  // folded back on itself
        run.kind = Patch::Run::OTHER_PATCH;
        run.patch = op->second;
        continue;
      }
      const std::vector<int>& loop = loops[other];
      const std::size_t m = loop.size();
      run.count = run.verts.size() - 1;
      if (run.count > m) continue;
      for (std::size_t j = 0; j < m; j++) {
        bool fwd = true, rev = true;
        for (std::size_t c = 0; c < run.count; c++) {
          const int a = loop[(j + c) % m], b = loop[(j + c + 1) % m];
          fwd = fwd && a == run.verts[c] && b == run.verts[c + 1];
          rev = rev && a == run.verts[run.count - c] && b == run.verts[run.count - c - 1];
        }
        if (!fwd && !rev) continue;
        run.kind = run.count == m ? Patch::Run::WHOLE_LOOP : Patch::Run::LOOP_RUN;
        run.loop = other;
        run.start = j;
        run.reversed = !fwd;
        break;
      }
    }
  }

  std::size_t live = 0, facets = 0, runs = 0, stuck = 0, longest = 0, bounds = 0;
  for (const auto& p : patches) {
    if (!p.alive) {
      report.push_back(
        format("a declared sweep of %d facets was left faceted: %s", int(p.facets.size()), p.dropped));
      continue;
    }
    live++;
    facets += p.facets.size();
    runs += p.runs.size();
    for (const auto& run : p.runs) {
      if (run.kind == Patch::Run::UNRESOLVED) stuck++;
      longest = std::max(longest, run.verts.size() - 1);
      bounds = std::max(bounds, run.bound + 1);
    }
  }
  if (cut_into > 0) {
    // Said out loud, because it is a face count the model did not ask for and
    // the alternative to it is a seam.
    report.push_back(
      format("a sweep closing around its profile was cut into %d faces, so that "
             "no face crosses the surface's seam",
             int(cut_into)));
  }
  if (!patches.empty()) {
    // Where the fitted surface actually is, rather than where it is on average.
    // A cubic interpolated through stations passes through every one of them
    // exactly, so measuring at the data says nothing; what moves is the surface
    // *between* them, and it moves most at the ends of the sweep, where the
    // model's uniform sampling is thinnest against geometry that is changing
    // fastest. The facet centroid is the cheapest point that is not a declared
    // point, so this is that deviation over every facet the sweep claimed.
    report.push_back(
      format("the fitted sweep passes within %.4f of the middle of every facet it claims, against a "
             "tessellation band of %.4f - worst at %.0f%% along the sweep",
             worst_claimed, claim_band, worst_at_u * 100.0));
  }
  if (corners_only > 0) {
    // Worth a line of its own: it is the difference between the facets a
    // declaration claims and the facets that are actually on it, and the
    // biggest single contributor is a face closing the profile of a grid
    // declared open, every corner of which the generator emitted.
    report.push_back(
      format("%d facets have every corner on the sweep and their middle off it, "
             "by up to %.4f against an allowance of at most %.4f",
             int(corners_only), worst_miss, missed_against));
  }
  if (live > 0) {
    report.push_back(
      format("%d declared sweep%s %s %d facets over %d boundary cycle%s, split "
             "into %d runs of up to %d mesh edges, %d unresolved",
             int(live), live == 1 ? "" : "s", live == 1 ? "covers" : "cover", int(facets), int(bounds),
             bounds == 1 ? "" : "s", int(runs), int(longest), int(stuck)));
  }
  return patches;
}

}  // namespace AnalyticFeatures
