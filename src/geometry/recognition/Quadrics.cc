/*! The quadric recogniser: trimmed regions of declared or fitted cylinders and
 * cones - OnQuadric, the one definition of "on this surface", and its propose
 * and form.
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

namespace {

/*! Does one facet lie on one quadric?
 *
 * The exporter's single definition of *on this surface*, so that the pass which
 * writes such facets as a trimmed patch and the band pass, which has to know
 * whether the region a surface covers is larger than the ring its own form can
 * write, ask the same question. Two definitions would let a band keep a partial
 * claim because its own test disagreed with the test that then took the
 * remainder - and a region truncated to fit a representation is the defect this
 * is here to make visible, not a detail of it. See doc/step-rungs.md, R0.
 *
 * Three independent tests, each with a fixture behind it:
 *
 *   lean      a facet across the axis is not on the surface however close its
 *             corners fall - a cap over a bore has every vertex on the rim
 *   corners   every vertex within `max_off`, and within what the mesh itself
 *             concedes about where its surface lies
 *   interior  the centroid and the edge midpoints within the sagitta the
 *             facet's own angular span implies - what separates a facet *of*
 *             the surface from one spanning a hole in it
 */
class OnQuadric
{
public:
  OnQuadric(const Mesh& mesh, const std::map<EdgeKey, std::vector<std::size_t>>& edge_loops,
            const Surface *surface, double smooth_angle)
    : mesh_(mesh),
      edge_loops_(edge_loops),
      smooth_angle_(smooth_angle),
      cyl_(dynamic_cast<const CylinderSurface *>(surface)),
      cone_(dynamic_cast<const ConeSurface *>(surface))
  {
    if (!valid()) return;
    axis_ = (cyl_ != nullptr ? cyl_->normdir : cone_->normdir).normalized();
    base_ = cyl_ != nullptr ? cyl_->refpt : cone_->refpt;
    // A cone's normal is not perpendicular to its axis - it leans by the half
    // angle - so what a facet has to agree with is that lean, not zero.
    //
    // fabs: the facet lean below is an absolute value, so the threshold has to
    // be one too. A cone whose radius falls with height - which is what
    // `cylinder(r1, r2 = 0)` draws - has a negative slope, and comparing an
    // absolute lean against a negative threshold rejected every facet of it.
    //
    // Latent as it stands, and deliberately left that way. Nothing declares a
    // ConeSurface of that orientation today - primitives.cc declares the two
    // rims of a frustum and leaves the cone between them to be recognised - so
    // no facet reaches this test with a negative slope, and fixing it moves no
    // face of any export in the suite. It is fixed anyway, because an absolute
    // value compared against a signed threshold is wrong however it is reached.
    // The next person to follow *declare first, recognise second* and declare
    // that cone will need it, and should know what else they will meet: with
    // both in place the claim on step-bored-cone widens from 32 facets to 52,
    // the trimmed-quadric path bounds them wrongly, and the solid comes out
    // 2.75% under its derived volume - 5234.1 against 5382.2 +/- 12, with a
    // total surface area below the mesh's own, which a fitted cone cannot have.
    // The declaration is right and the bounding is not. That fixture's
    // VOLUME-APPROX is the assertion that says so; the surface census does not,
    // it improves while the solid gets worse.
    lean_ = cyl_ != nullptr ? 0.0 : fabs(cone_->slope) / sqrt(1 + cone_->slope * cone_->slope);
    ref_ = perpendicular(axis_);
    ref2_ = axis_.cross(ref_);
  }

  /*! A quadric this can be asked about at all: a cylinder of positive radius,
   * or a cone. */
  [[nodiscard]] bool valid() const { return (cyl_ != nullptr && cyl_->r > 0) || cone_ != nullptr; }

  /*! How far a point is off the quadric, as a distance rather than as a radius
   * difference - on a cone those differ by the cosine of the half angle, and it
   * is the perpendicular distance the tessellation band is a bound on. */
  [[nodiscard]] double offsetAt(const Vector3d& p) const
  {
    if (cyl_ != nullptr) return fabs(distanceToAxis(p, cyl_->refpt, axis_) - cyl_->r);
    const double want = cone_->radiusAt(p);
    if (want < 0) return std::numeric_limits<double>::max();  // past the apex
    return fabs(distanceToAxis(p, cone_->refpt, axis_) - want) / sqrt(1 + cone_->slope * cone_->slope);
  }

  /*! How far a facet reaches around the axis. The widest gap between
   * consecutive angles is the part the facet does *not* cover, so the span is
   * the turn less that gap - which is what tells a facet straddling the seam
   * from one wrapped right round. */
  [[nodiscard]] double spanOf(std::size_t f) const
  {
    const std::vector<Vector3d>& vertices = *mesh_.vertices;
    std::vector<double> t;
    for (const int v : (*mesh_.loops)[f]) {
      const Vector3d rel = vertices[v] - base_;
      double a = atan2(rel.dot(ref2_), rel.dot(ref_));
      if (a < 0) a += 2 * M_PI;
      t.push_back(a);
    }
    std::sort(t.begin(), t.end());
    double gap = t.front() + 2 * M_PI - t.back();
    for (std::size_t i = 0; i + 1 < t.size(); i++) gap = std::max(gap, t[i + 1] - t[i]);
    return std::max(0.0, 2 * M_PI - gap);
  }

  /*! How far off the surface a facet's own middle stands when its corners are
   * on it: R(1 - cos(theta/2)) over its angular span. Computable from the
   * surface and the facet, needing no neighbour, and unchanged by anything done
   * to the corners - which is what the interior test needs, because moving a
   * corner onto the curve where its owners cross changes every dihedral around
   * it. */
  [[nodiscard]] double sagittaOf(std::size_t f) const
  {
    const std::vector<Vector3d>& vertices = *mesh_.vertices;
    double r = 0;
    for (const int v : (*mesh_.loops)[f]) {
      r = std::max(r, distanceToAxis(vertices[v], base_, axis_));
    }
    return r * (1 - cos(std::min(M_PI, spanOf(f)) / 2));
  }

  /*! The band a single facet leaves open, by the same sagitta the approximation
   * pass measures: over every edge, (reach/2)*tan(dihedral/4) against whatever
   * lies across it. A single facet has no interior edge of its own, so this is
   * that sagitta asked of the one facet rather than of a region. */
  [[nodiscard]] double bandOf(std::size_t f) const
  {
    const std::vector<Vector3d>& vertices = *mesh_.vertices;
    const std::vector<std::vector<int>>& loops = *mesh_.loops;
    const std::vector<Vector3d>& normals = *mesh_.normals;
    const std::vector<int>& loop = loops[f];
    double band = 0;
    for (std::size_t i = 0; i < loop.size(); i++) {
      const int a = loop[i], b = loop[(i + 1) % loop.size()];
      const auto it = edge_loops_.find(edgeKey(a, b));
      if (it == edge_loops_.end()) continue;
      for (const std::size_t g : it->second) {
        if (g == f) continue;
        const double dot = std::clamp(normals[f].normalized().dot(normals[g].normalized()), -1.0, 1.0);
        const double dihedral = acos(dot);
        if (dihedral > smooth_angle_) continue;
        const double reach =
          std::max(reachAcross(vertices, loops[f], a, b), reachAcross(vertices, loops[g], a, b));
        band = std::max(band, (reach / 2.0) * tan(dihedral / 4.0));
      }
    }
    return band;
  }

  /*! Is this facet on the surface, spending no more than `max_off` of what the
   * mesh concedes? `off_out` takes the worst corner offset, which a caller
   * judging a whole region needs. */
  [[nodiscard]] bool covers(std::size_t f, double max_off, double *off_out = nullptr) const
  {
    const std::vector<Vector3d>& vertices = *mesh_.vertices;
    const std::vector<std::vector<int>>& loops = *mesh_.loops;
    const std::vector<Vector3d>& normals = *mesh_.normals;
    if (!(*mesh_.valid)[f] || (*mesh_.is_hole)[f]) return false;
    if (loops[f].size() < 3) return false;

    // A facet across the axis is not on the surface however close its corners
    // fall - a cap over a bore has every vertex on the rim. On a cylinder that
    // is exact: every facet of a prism is parallel to the axis. On a cone it is
    // not, and demanding it be was what kept the fitted cones from ever
    // reaching a face: a facet of a polygonal frustum is a chord plane, so it
    // leans by a little more than the true cone does, by a factor of cos(pi/n),
    // and the rest is left to the distances below.
    const double lean_of_f = fabs(normals[f].normalized().dot(axis_));
    if (cyl_ != nullptr ? lean_of_f > 1e-6 : lean_of_f > std::min(0.99, lean_ * 1.05 + 1e-6)) {
      return false;
    }
    double off = 0;
    for (const int v : loops[f]) off = std::max(off, offsetAt(vertices[v]));
    // The band is what the mesh concedes about where its surface lies;
    // `max_off` is how much of that concession this tier is willing to spend.
    // At 1e-7 it spends none, and claims only facets the surface already runs
    // through.
    if (off > std::min(bandOf(f), max_off)) return false;

    // Corners on the surface are not enough, and the lean test above is not the
    // whole of the guard. A facet that spans a *hole* in the surface has every
    // corner on it and no interior on it at all. The bore through
    // step-bored-cone is walled by one quad per angular step running the full
    // 18mm length of the bore; all four of its corners lie on the two mouth
    // curves, and a mouth curve is a section of the cone, so all four are on
    // the cone to 1e-7. Its middle is 6.0 from that cone, which concedes 0.048.
    // At theta = 180 the bore's wall runs so nearly parallel to the cone it
    // pierces that the two normals agree to five degrees, so only the distance
    // separates them - and only if it is asked somewhere other than at the
    // corners. The quarter allows for a tessellation whose facets are not even
    // arcs.
    const double interior_allow = 1.25 * sagittaOf(f) + 1e-12;
    Vector3d centre(0, 0, 0);
    for (const int v : loops[f]) centre += vertices[v];
    centre /= double(loops[f].size());
    double interior = offsetAt(centre);
    for (std::size_t i = 0; i < loops[f].size() && interior <= interior_allow; i++) {
      const Vector3d mid = (vertices[loops[f][i]] + vertices[loops[f][(i + 1) % loops[f].size()]]) / 2;
      interior = std::max(interior, offsetAt(mid));
    }
    if (interior > interior_allow) return false;
    if (off_out != nullptr) *off_out = off;
    return true;
  }

private:
  const Mesh& mesh_;
  const std::map<EdgeKey, std::vector<std::size_t>>& edge_loops_;
  double smooth_angle_ = 0;
  const CylinderSurface *cyl_ = nullptr;
  const ConeSurface *cone_ = nullptr;
  Vector3d axis_{0, 0, 1}, base_{0, 0, 0}, ref_{1, 0, 0}, ref2_{0, 1, 0};
  double lean_ = 0;
};

}  // namespace

/*! Propose: every facet each declared or fitted quadric covers, whoever holds
 * it. Pure - it reads the graph, the surfaces and provenance, and nothing any
 * pass has claimed, so the order recognisers run in cannot change it.
 *
 * Membership is OnQuadric::covers, after provenance's gate. Evidence class D4:
 * see recogniseQuadricPatches. */
std::vector<QuadricProposal> proposeQuadrics(const recognition::FacetGraph& graph,
                                             const std::vector<std::shared_ptr<Surface>>& surfaces,
                                             const Provenance& provenance, double smooth_angle,
                                             double max_off)
{
  const Mesh& mesh = graph.mesh();
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;
  std::vector<QuadricProposal> out;
  for (std::size_t surface_index = 0; surface_index < surfaces.size(); surface_index++) {
    const auto& surface = surfaces[surface_index];
    const auto *cyl = dynamic_cast<const CylinderSurface *>(surface.get());
    const auto *cone = dynamic_cast<const ConeSurface *>(surface.get());
    if (cyl == nullptr && cone == nullptr) continue;
    if (cyl != nullptr && !(cyl->r > 0)) continue;
    // How far a facet may sit off the surface and still be on it is the
    // facet's *own* tessellation band, not a fraction of the radius. A
    // fraction cannot be right for both: on a bore of r=4 it has to admit the
    // 0.0188 a trim vertex strays, and on a wall of r=82 the same fraction is
    // four millimetres, which reaches clean across to the next declared
    // cylinder - the reference lid has four of them inside 1.7 of each other,
    // and claiming across them is how this first went wrong.
    //
    // The band is local and self calibrating: it is what the mesh already
    // concedes about where its own surface lies, so a facet inside it is one
    // this cylinder could be the surface of, and a facet outside it belongs to
    // something else. That, the lean and the interior test are OnQuadric, which
    // the band pass asks the same question of.
    const OnQuadric on_quadric(mesh, graph.edgeLoops(), surface.get(), smooth_angle);
    QuadricProposal proposal;
    proposal.surface_index = surface_index;
    for (std::size_t f = 0; f < loops.size(); f++) {
      if (!loop_valid[f] || is_hole[f]) continue;
      if (loops[f].size() < 3) continue;
      // Ask the model before measuring. See Provenance: where the generator
      // says this facet's maker does not own this surface, no distance test can
      // make it true, and every threshold below is spared the question.
      if (provenance.excludes(f, surface_index)) {
        proposal.gated++;
        continue;
      }
      // The allowance comes from the surface, not from the neighbours.
      //
      // It used to be twice `bandOf`, which is the dihedral to whatever lies
      // across each edge - a statement about the mesh's local flatness. That
      // holds only while the mesh stays as the generator emitted it. Move a
      // corner onto the curve where its two declared owners cross, which is
      // what a corner belongs on, and the dihedrals change: measured on
      // step-bored-cone, a facet still sitting a correct 0.048 off a cone of
      // radius 10 was allowed 0.0033 because its neighbours happened to come
      // out nearly coplanar. The claim shattered into fourteen pieces of one
      // and three facets.
      double off = 0;
      if (!on_quadric.covers(f, max_off, &off)) continue;
      proposal.facets.push_back(f);
      proposal.off.push_back(off);
      proposal.span.push_back(on_quadric.spanOf(f));
    }
    out.push_back(std::move(proposal));
  }
  return out;
}

/*! Regions lying on a declared or fitted quadric, as patches.
 *
 * The band recogniser already writes quadrics, and writes them better - a
 * cylinder between two circular rims goes out with CIRCLEs a kernel can offset
 * and pattern along. What it cannot do is write one whose rim is not a circle,
 * because a band *is* two rims at a constant height and every rule in it says
 * so. A cylinder bored across, or trimmed by anything that is not a plane
 * section, has no such rim: the trim is a quartic, and the whole face is lost
 * for want of a curve to bound it with.
 *
 * This is the other route to the same face. It claims facets by distance to the
 * axis rather than by walking rings, hands them to patchFromFacets, and lets
 * the boundary be whatever the booleans left - each stretch of it becoming one
 * curve against one neighbour, exactly as a trimmed sweep's does. It runs after
 * the band pass and over what that pass did not consume, so nothing it does can
 * cost a circle that would otherwise have been written.
 *
 * It is an approximation and is gated as one. The trim's vertices are not all
 * on the surface: where a ruling of the bore met a facet of the wall the vertex
 * is exactly on it, but where a facet met an *edge* it is not, and on the
 * reference bore 16 of 80 rim vertices are off by up to 0.0188 against that
 * region's tessellation band of 0.0193. Inside the band is the same licence the
 * rest of the approximation pass runs on, and a claim which cannot show that is
 * dropped rather than written.
 */
std::vector<Patch> recogniseQuadricPatches(const recognition::FacetGraph& graph,
                                           const std::vector<std::shared_ptr<Surface>>& surfaces,
                                           const std::vector<char>& consumed,
                                           const Provenance& provenance, double smooth_angle,
                                           double max_off, std::vector<std::string>& report,
                                           ClaimLedger *ledger, std::size_t declared_count)
{
  return formQuadricPatches(graph, surfaces, consumed, provenance, smooth_angle, max_off, report, ledger,
                            declared_count,
                            proposeQuadrics(graph, surfaces, provenance, smooth_angle, max_off));
}

std::vector<Patch> formQuadricPatches(const recognition::FacetGraph& graph,
                                      const std::vector<std::shared_ptr<Surface>>& surfaces,
                                      const std::vector<char>& consumed, const Provenance& provenance,
                                      double smooth_angle, double max_off,
                                      std::vector<std::string>& report, ClaimLedger *ledger,
                                      std::size_t declared_count,
                                      const std::vector<QuadricProposal>& proposals)
{
  const Mesh& mesh = graph.mesh();
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;
  const std::vector<Vector3d>& normals = *mesh.normals;

  std::vector<Patch> patches;
  std::size_t gated = 0;
  std::vector<char> taken(loops.size(), 0);

  // The adjacency every recogniser walks, built once - see recognition::FacetGraph.
  const std::map<EdgeKey, std::vector<std::size_t>>& edge_loops = graph.edgeLoops();

  // `proposals` came from proposeQuadrics, which reads what no pass holds. See
  // doc/step-recognition-design.md, *Three phases, and the first one is pure*.

  // The band a region of facets leaves open, by the same sagitta the
  // approximation pass measures: over every interior edge,
  // (reach/2)*tan(dihedral/4). The one-facet case belongs to OnQuadric, which
  // asks it of whatever lies across each of the facet's own edges.
  auto bandOf = [&](const std::vector<std::size_t>& facets) {
    const std::set<std::size_t> in(facets.begin(), facets.end());
    double band = 0;
    for (const std::size_t f : facets) {
      const std::vector<int>& loop = loops[f];
      for (std::size_t i = 0; i < loop.size(); i++) {
        const int a = loop[i], b = loop[(i + 1) % loop.size()];
        const auto it = edge_loops.find(edgeKey(a, b));
        if (it == edge_loops.end()) continue;
        for (const std::size_t g : it->second) {
          if (g == f || !in.count(g)) continue;
          const double dot = std::clamp(normals[f].normalized().dot(normals[g].normalized()), -1.0, 1.0);
          const double dihedral = acos(dot);
          if (dihedral > smooth_angle) continue;
          const double reach =
            std::max(reachAcross(vertices, loops[f], a, b), reachAcross(vertices, loops[g], a, b));
          band = std::max(band, (reach / 2.0) * tan(dihedral / 4.0));
        }
      }
    }
    return band;
  };

  for (std::size_t surface_index = 0; surface_index < surfaces.size(); surface_index++) {
    const auto& surface = surfaces[surface_index];
    const auto *cyl = dynamic_cast<const CylinderSurface *>(surface.get());
    const auto *cone = dynamic_cast<const ConeSurface *>(surface.get());
    if (cyl == nullptr && cone == nullptr) continue;
    if (cyl != nullptr && !(cyl->r > 0)) continue;
    const Vector3d axis = (cyl != nullptr ? cyl->normdir : cone->normdir).normalized();

    const Vector3d ref = perpendicular(axis);
    const Vector3d ref2 = axis.cross(ref);

    // Every facet this pass covers gets exactly one claim, recorded where its
    // fate is decided: here for the rules below, at the writer for a region it
    // hands on. Before 2026-09-23 only the held ones were recorded, so a facet
    // this pass covered and then dropped to its own span rule or its region
    // test left no trace - and those are exactly the facets that come out
    // planar on a declared surface.
    //
    // The evidence is D4 in both tiers. Membership is OnQuadric::covers, whose
    // corners are held to min(bandOf(f), max_off) and whose interior to
    // 1.25 x the facet's own sagitta - the tessellation deciding, even where
    // max_off is 1e-7. A declared surface in the claim does not make it D0; see
    // doc/step-rungs.md, *a rung is as exact as its evidence*.
    auto record = [&](std::size_t f, ClaimFate fate, const char *why) {
      if (ledger == nullptr) return;
      recognition::recordClaim(*ledger, recognition::kQuadricFace, f, surface, surface_index,
                               declared_count, fate, why);
    };
    std::vector<std::size_t> claimed;
    std::vector<std::size_t> contested;  // on this quadric, but already held elsewhere
    std::vector<double> span_of_claim;
    double worst = 0;
    // What this surface proposed - every facet it covers, whoever holds it -
    // granted by the policy in force today: whatever an earlier pass kept, or an
    // earlier surface of this pass took, is held. See proposeQuadrics.
    const QuadricProposal *proposal = nullptr;
    for (const auto& p : proposals) {
      if (p.surface_index == surface_index) proposal = &p;
    }
    if (proposal == nullptr) continue;
    gated += proposal->gated;
    for (std::size_t k = 0; k < proposal->facets.size(); k++) {
      const std::size_t f = proposal->facets[k];
      const double off = proposal->off[k];
      // Examined even when something already holds it - see the same record in
      // recogniseGridPatches. This pass runs *after* the sweeps, so it is the
      // one that loses facets, and the first pass reporting no contention is a
      // statement about the order the passes run in and not about the geometry.
      const bool held = consumed[f] != 0 || taken[f] != 0;
      if (held) {
        // What this pass covers and does not hold. Recorded as it is found, and
        // never honoured - such a facet is held by someone else, so it can
        // never be mistaken for a claim nothing acted on.
        contested.push_back(f);
        record(f, ClaimFate::Held, "covered, but already held when this pass looked");
        continue;
      }
      claimed.push_back(f);
      span_of_claim.push_back(proposal->span[k]);
      worst = std::max(worst, off);
    }
    if (claimed.size() < 3) {
      for (const std::size_t f : claimed) {
        record(f, ClaimFate::Declined, "fewer than three facets of the surface were free to claim");
      }
      continue;
    }

    // And refuse a facet that reaches further round the axis than a facet of
    // this surface's tessellation does.
    //
    // The sagitta above is honest and, taken alone, readmits exactly the defect
    // the interior test was written for. The bore through step-bored-cone is
    // walled by one quad per angular step running its whole length, and each
    // spans 134 degrees of the cone it pierces: the sagitta at that span is
    // 6.09, and the facet is 6.0 out, so it passes. What gives it away is the
    // span itself - no facet of a tessellation reaches 134 degrees when its
    // neighbours reach eleven. Judged against this claim's own median, as the
    // sweep's corners are, so that nothing here assumes a resolution.
    if (!contested.empty()) {
      std::map<std::string, std::size_t> by_holder;
      for (const std::size_t f : contested) {
        const Claim *first = ledger != nullptr ? ledger->firstClaimant(f) : nullptr;
        by_holder[first != nullptr && first->by != nullptr ? first->by : "an earlier pass"]++;
      }
      std::string who;
      for (const auto& kv : by_holder) {
        who += (who.empty() ? "" : ", ") + std::to_string(kv.second) + " by " + kv.first;
      }
      report.push_back(
        format("%d facets this quadric also covers are held by something that claimed them first: "
               "%s - who holds a facet is still decided by which pass ran first, and this is the "
               "record of what that cost",
               int(contested.size()), who.c_str()));
    }
    {
      std::vector<double> ranked = span_of_claim;
      std::sort(ranked.begin(), ranked.end());
      const double typical = ranked[ranked.size() / 2];
      if (typical > 0) {
        std::vector<std::size_t> kept;
        std::size_t reached = 0;
        for (std::size_t k = 0; k < claimed.size(); k++) {
          if (span_of_claim[k] > 4 * typical) {
            reached++;
            record(claimed[k], ClaimFate::Declined,
                   "reaches further round the axis than four times this claim's median span");
            continue;
          }
          kept.push_back(claimed[k]);
        }
        if (reached > 0) {
          report.push_back(
            format("%d facets reach further round the declared %s r=%g than four times the %.1f "
                   "degrees this claim's facets typically do, and are left faceted",
                   int(reached), cyl != nullptr ? "cylinder" : "cone", cyl != nullptr ? cyl->r : cone->r,
                   typical * 180 / M_PI));
        }
        claimed = std::move(kept);
      }
    }
    if (claimed.size() < 3) {
      for (const std::size_t f : claimed) {
        record(f, ClaimFate::Declined, "fewer than three facets were left after the span rule");
      }
      continue;
    }

    const double band = bandOf(claimed);
    if (worst > band) {
      for (const std::size_t f : claimed) {
        record(f, ClaimFate::Declined,
               "the region's worst corner strays further than the region's tessellation band");
      }
      report.push_back(
        format("a region of %d facets on the declared %s r=%g is not written: its boundary strays %.4f "
               "from the surface where the tessellation allows %.4f",
               int(claimed.size()), cyl != nullptr ? "cylinder" : "cone",
               cyl != nullptr ? cyl->r : cone->r, worst, band));
      continue;
    }

    // Does the claim close around the axis? A face that wraps needs a seam, and
    // the simpler answer - the one a declared sweep already takes when it
    // closes around its profile - is to cut it so that no face wraps at all.
    const Vector3d base = cyl != nullptr ? cyl->refpt : cone->refpt;
    std::vector<char> quadrant(4, 0);
    std::vector<double> theta(claimed.size());
    for (std::size_t i = 0; i < claimed.size(); i++) {
      Vector3d c(0, 0, 0);
      for (const int v : loops[claimed[i]]) c += vertices[v];
      c /= double(loops[claimed[i]].size());
      const Vector3d rel = c - base;
      double t = atan2(rel.dot(ref2), rel.dot(ref));
      if (t < 0) t += 2 * M_PI;
      theta[i] = t;
      quadrant[std::min(3, int(t / (M_PI / 2)))] = 1;
    }
    const bool wraps = quadrant[0] && quadrant[1] && quadrant[2] && quadrant[3];

    std::vector<std::vector<std::size_t>> groups;
    if (!wraps) {
      groups.push_back(claimed);
    } else {
      groups.resize(2);
      for (std::size_t i = 0; i < claimed.size(); i++) {
        groups[theta[i] < M_PI ? 0 : 1].push_back(claimed[i]);
      }
    }

    for (const auto& group : groups) {
      if (group.size() < 3) {
        for (const std::size_t f : group) {
          record(f, ClaimFate::Declined, "a piece of the seam split has fewer than three facets");
        }
        continue;
      }
      // The seam split above cuts a wrapping claim in two; this one cuts a
      // claim the booleans left in pieces. Both have to happen, and neither
      // implies the other.
      // No size test on a piece. The three facet floor above is about whether a
      // claim is worth making at all; once it has been made, a region of two
      // facets on that surface is as real as a region of two hundred, and
      // leaving it planar while its neighbours across the same surface are
      // written is the crease that "all of a surface or none" exists to avoid.
      for (const auto& piece : connectedComponents(loops, edge_loops, group)) {
        if (piece.empty()) continue;
        Patch patch = patchFromFacets(loops, edge_loops, surface, piece);
        if (patch.alive) {
          for (const std::size_t f : patch.facets) taken[f] = 1;
        }
        patches.push_back(std::move(patch));
      }
    }
  }
  if (gated > 0) {
    report.push_back(
      format("provenance spared %d facet-surface tests: the model says that facet's "
             "maker does not own that surface",
             int(gated)));
  }
  return patches;
}

}  // namespace AnalyticFeatures
