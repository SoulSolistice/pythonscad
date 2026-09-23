/*! The Bezier recogniser: declared fillet patches, proposed and formed, and which
 * of them are exactly quadrics.
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

/*! The four edges of the parameter square, as arguments to
 * BezierPatchSurface::boundary: u=0, u=1, v=0, v=1. */
constexpr bool EDGE_ALONG_U[4] = {false, false, true, true};
constexpr bool EDGE_FAR[4] = {false, true, false, true};

/*! Distance from a point to one boundary curve of a patch, by sampling then
 * refining. The curve is degree 2 at most and only boundary vertices are ever
 * tested, so this does not need to be clever. */
double distanceToBoundary(const BezierPatchSurface& patch, int e, const Vector3d& pt)
{
  const std::vector<Vector3d> cp = patch.boundary(EDGE_ALONG_U[e], EDGE_FAR[e]);
  const std::vector<double> cw = patch.boundaryWeights(EDGE_ALONG_U[e], EDGE_FAR[e]);
  const bool rational = cw.size() == cp.size();
  auto at = [&](double t) {
    // The boundary of a rational patch is a rational curve, so de Casteljau has
    // to run on (w*P, w) and divide at the end. Evaluating it as a polynomial
    // curve measures the distance to the parabola through the same control
    // points, which is 6% of the radius away from the circular arc the mesh
    // actually sits on - against a tolerance of 1e-7 of the model size. Every
    // boundary segment of every fillet patch then belongs to no edge, and the
    // whole fillet is written faceted while nothing anywhere reports an error.
    std::vector<Vector3d> w = cp;
    std::vector<double> ww(cp.size(), 1.0);
    if (rational) {
      for (std::size_t i = 0; i < cp.size(); i++) {
        w[i] = cp[i] * cw[i];
        ww[i] = cw[i];
      }
    }
    for (std::size_t k = w.size(); k > 1; k--) {
      for (std::size_t i = 0; i + 1 < k; i++) {
        w[i] = w[i] * (1 - t) + w[i + 1] * t;
        ww[i] = ww[i] * (1 - t) + ww[i + 1] * t;
      }
    }
    return ww[0] == 0.0 ? w[0] : Vector3d(w[0] / ww[0]);
  };
  double best = -1, bt = 0;
  for (int i = 0; i <= 64; i++) {
    const double t = i / 64.0;
    const double d = (at(t) - pt).norm();
    if (best < 0 || d < best) {
      best = d;
      bt = t;
    }
  }
  for (double step = 1.0 / 64; step > 1e-13; step *= 0.5) {
    for (const double t : {bt - step, bt + step}) {
      const double c = std::min(1.0, std::max(0.0, t));
      const double d = (at(c) - pt).norm();
      if (d < best) {
        best = d;
        bt = c;
      }
    }
  }
  return best;
}

/*! Which boundary curves of the patch a vertex lies on.
 *
 * By distance to the curve rather than by its parameters, because the
 * parameters cannot answer it at a corner of the square: a corner is on two
 * edges at once, and a corner fillet's apex - the whole of its collapsed `u = 1`
 * edge - is the far end of *both* rails. Classifying by parameter put the apex
 * inside one rail's run, so that run spanned two different curves and the edge
 * from the apex to the other rail was about to be replaced by the wrong one. */
unsigned boundarySet(const BezierPatchSurface& patch, const Vector3d& pt, double tol)
{
  unsigned mask = 0;
  for (int e = 0; e < 4; e++) {
    if (patch.degenerateAt(EDGE_ALONG_U[e], EDGE_FAR[e])) continue;
    if (distanceToBoundary(patch, e, pt) <= tol) mask |= 1u << e;
  }
  return mask;
}

}  // namespace

namespace {

/*! The circular arc a rational quadratic Bezier draws, from its control points
 * alone.
 *
 * A quadratic Bezier through (a, b, c) with the middle weight at cos(theta/2)
 * is exactly a circular arc - see BezierWeight() in core/FilletNode.cc, which
 * is where every patch this file sees comes from. The arc leaves `a` along
 * `b - a` and arrives at `c` along `c - b`, so its centre is the point on the
 * perpendicular to the first tangent at `a` which is also equidistant from `c`:
 * with u the in-plane unit normal to that tangent and d = a - c, the centre is
 * a + s*u where |d + s*u|^2 = s^2, so s = -|d|^2 / (2 u.d).
 *
 * Nothing here checks the *weight*. It cannot: three points and a weight that
 * is not cos(theta/2) draw some other conic through the same points, and this
 * returns the circle they would have drawn. The caller is required to measure
 * the patch against the result, which is what turns a guess into a fit. */
bool arcCircle(const Vector3d& a, const Vector3d& b, const Vector3d& c, Vector3d& centre,
               Vector3d& normal, double& radius)
{
  const Vector3d t0 = b - a, t1 = c - b;
  const Vector3d n = t0.cross(t1);
  if (n.norm() < 1e-12) return false;  // collinear: a straight rail, not an arc
  normal = n.normalized();
  const Vector3d u = normal.cross(t0).normalized();
  const Vector3d d = a - c;
  const double denom = 2.0 * u.dot(d);
  if (fabs(denom) < 1e-12) return false;
  const double s = -d.squaredNorm() / denom;
  centre = a + u * s;
  radius = fabs(s);
  return radius > 1e-9;
}

/*! How far `pt` is off the quadric, for the two kinds this file recovers. */
double deviationFrom(const Surface& surface, const Vector3d& pt)
{
  if (const auto *cyl = dynamic_cast<const CylinderSurface *>(&surface)) {
    return fabs(distanceToAxis(pt, cyl->refpt, cyl->normdir.normalized()) - cyl->r);
  }
  if (const auto *sph = dynamic_cast<const SphereSurface *>(&surface)) {
    return fabs((pt - sph->refpt).norm() - sph->r);
  }
  return std::numeric_limits<double>::infinity();
}

}  // namespace

std::shared_ptr<Surface> quadricOfPatch(const BezierPatchSurface& bez, double tol, const char **why)
{
  auto refuse = [&](const char *reason) -> std::shared_ptr<Surface> {
    if (why != nullptr) *why = reason;
    return nullptr;
  };
  // Only the two shapes FilletNode draws. A patch of any other degree was
  // declared by something else and has no reason to be a quadric.
  const bool strip = bez.degree_u == 2 && bez.degree_v == 1;
  const bool corner = bez.degree_u == 2 && bez.degree_v == 2;
  if (!strip && !corner) return refuse("it is not a shape fillet() draws");
  // A polynomial patch is a parabola, not an arc.
  if (!bez.isRational()) return refuse("it is polynomial, so its arcs are parabolas");

  std::shared_ptr<Surface> candidate;

  if (strip) {
    // The two rails are the columns of the net. Where the edge is straight and
    // the radius constant they are equal circles on a common axis, and the
    // ruled surface between them is exactly that cylinder.
    //
    // Two rails of *different* radii on a common axis would be a cone, and
    // there is no cone among the declarable surface types - a band expresses
    // one as a frustum, which needs two rims this patch does not have. Such a
    // strip stays a B-spline, which is exact and describes it correctly.
    Vector3d c0, n0, c1, n1;
    double r0 = 0, r1 = 0;
    if (!arcCircle(bez.control(0, 0), bez.control(1, 0), bez.control(2, 0), c0, n0, r0)) {
      return refuse("one rail is not a circular arc");
    }
    if (!arcCircle(bez.control(0, 1), bez.control(1, 1), bez.control(2, 1), c1, n1, r1)) {
      return refuse("the other rail is not a circular arc");
    }
    if (fabs(r0 - r1) > tol) return refuse("the two rails have different radii, so it is a cone");
    Vector3d axis = c1 - c0;
    if (axis.norm() < tol) return refuse("the two rails coincide");
    axis.normalize();
    // Both rails perpendicular to the line joining their centres is what makes
    // the pair coaxial rather than merely parallel.
    if (fabs(fabs(axis.dot(n0)) - 1.0) > 1e-9 || fabs(fabs(axis.dot(n1)) - 1.0) > 1e-9) {
      return refuse("the rails are parallel but not coaxial");
    }
    // Orient the axis so the rail sweeps counter clockwise about it. The face
    // is then a region of the surface's own parameterisation running forwards
    // from the reference direction rather than one wrapping through the seam.
    if ((bez.control(0, 0) - c0).cross(bez.control(2, 0) - c0).dot(axis) < 0) axis = -axis;
    candidate = std::make_shared<CylinderSurface>(c0, axis, r0);
  } else {
    // A corner. Its apex row is a single point, and its first row and first
    // column are two arcs meeting there - concentric and of one radius exactly
    // when the patch is an octant of that sphere.
    const Vector3d apex = bez.control(2, 0);
    if ((bez.control(2, 1) - apex).norm() > tol || (bez.control(2, 2) - apex).norm() > tol) {
      return refuse("its apex row is not a single point");
    }
    Vector3d cu, nu, cv, nv;
    double ru = 0, rv = 0;
    if (!arcCircle(bez.control(0, 0), bez.control(0, 1), bez.control(0, 2), cv, nv, rv) ||
        !arcCircle(bez.control(0, 0), bez.control(1, 0), bez.control(2, 0), cu, nu, ru)) {
      return refuse("one of its meridians is not a circular arc");
    }
    if (fabs(ru - rv) > tol) return refuse("its two meridians have different radii");
    if ((cu - cv).norm() > tol) return refuse("its two meridians are not concentric");
    // The apex is where the two meridians meet, so it is the pole of the
    // parameterisation that keeps this face inside one (theta, phi) rectangle:
    // the row at u = 0 is then the equator arc and the columns are meridians
    // climbing to it. A sphere looks the same from every direction, so this
    // choice costs nothing geometrically and buys a face with no seam in it.
    const Vector3d polar = apex - cu;
    if (polar.norm() < tol) return refuse("its apex sits on its own centre");
    candidate = std::make_shared<SphereSurface>(cu, polar.normalized(), ru);
  }

  // The geometry gate. Everything above reads the *boundary* of the net, and a
  // patch whose two rails are concentric arcs can still bulge off the quadric
  // in between - a rail rotated relative to its partner rules a hyperboloid
  // through the same two circles. Only measuring the surface itself settles it,
  // and the weights are what the evaluation uses, so a patch whose middle
  // weight is not cos(theta/2) fails here rather than being written as the
  // circle it is not.
  constexpr int STEPS = 6;
  for (int i = 0; i <= STEPS; i++) {
    for (int j = 0; j <= STEPS; j++) {
      const Vector3d pt = bez.evaluate(double(i) / STEPS, double(j) / STEPS);
      if (deviationFrom(*candidate, pt) > tol) {
        return refuse("its boundary fits but its middle bulges off the quadric");
      }
    }
  }
  return candidate;
}

std::vector<Vector3d> runControlPoints(const Patch& patch, const Patch::Run& run,
                                       const std::vector<Vector3d>& vertices,
                                       std::vector<double> *weights_out)
{
  if (weights_out != nullptr) weights_out->clear();
  const auto *bez = dynamic_cast<const BezierPatchSurface *>(patch.surface.get());
  if (bez == nullptr || run.edge < 0 || run.edge > 3 || run.verts.empty()) return {};
  const bool along_u = EDGE_ALONG_U[run.edge], far = EDGE_FAR[run.edge];
  std::vector<Vector3d> cp = bez->boundary(along_u, far);
  std::vector<double> cw = bez->boundaryWeights(along_u, far);
  if (cp.empty()) return cp;
  // A Bezier interpolates its end control points, so which end the curve starts
  // at is decided by comparing the first of them with the run's first vertex.
  // The weights are reversed with the points, off the same decision rather than
  // a second one made from the same coordinates.
  const Vector3d& first = vertices[run.verts.front()];
  if ((cp.front() - first).norm() > (cp.back() - first).norm()) {
    std::reverse(cp.begin(), cp.end());
    std::reverse(cw.begin(), cw.end());
  }
  if (weights_out != nullptr) *weights_out = std::move(cw);
  return cp;
}

bool runCircle(const Patch& patch, const Patch::Run& run, const std::vector<Vector3d>& vertices,
               Vector3d& centre, Vector3d& normal, double& radius)
{
  std::vector<double> cw;
  const std::vector<Vector3d> cp = runControlPoints(patch, run, vertices, &cw);
  if (cp.size() != 3) return false;  // only a quadratic boundary is an arc
  // runControlPoints orders the net so the curve starts at run.verts.front(),
  // and arcCircle's normal is the one the sweep from the first control point to
  // the last turns counter clockwise about - which is the direction a STEP
  // CIRCLE is always written in, so the caller can use it unchanged.
  if (!arcCircle(cp[0], cp[1], cp[2], centre, normal, radius)) return false;

  // arcCircle reads the control points only, so it answers "the circle these
  // three points would draw" for a curve which may be some other conic - a
  // glyph outline is full of them. The weight is what decides, and rather than
  // compare it against cos(theta/2) the curve is measured: at the midpoint the
  // two differ by the whole sagitta, so one sample separates them.
  const double w = cw.size() == 3 ? cw[1] : 1.0;
  const double b0 = 0.25, b1 = 0.5 * w, b2 = 0.25;
  const Vector3d mid = (cp[0] * b0 + cp[1] * b1 + cp[2] * b2) / (b0 + b1 + b2);
  return fabs((mid - centre).norm() - radius) <= 1e-9 * std::max(1.0, radius);
}

/*! Propose: every facet on each declared Bezier patch, whoever holds it. Pure -
 * it reads the graph and the surfaces and no claim of any pass. Membership is
 * every corner on the patch; evidence D1. */
std::vector<BezierProposal> proposeBezierPatches(const recognition::FacetGraph& graph,
                                                 const std::vector<std::shared_ptr<Surface>>& surfaces)
{
  const Mesh& mesh = graph.mesh();
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;
  std::vector<BezierProposal> out;
  for (std::size_t surface_index = 0; surface_index < surfaces.size(); surface_index++) {
    const auto& surface = surfaces[surface_index];
    const auto *bez = dynamic_cast<const BezierPatchSurface *>(surface.get());
    if (bez == nullptr || bez->net.empty()) continue;
    BezierProposal proposal;
    proposal.surface_index = surface_index;
    // A Bezier lies inside the convex hull of its control net, so a box round
    // the net rejects almost every facet in the model without projecting
    // anything. That matters: projection is a Newton solve from a grid of
    // starts, and a filleted cube has thousands of facets and dozens of
    // patches.
    Vector3d lo = bez->net.front(), hi = bez->net.front();
    for (const auto& p : bez->net) {
      lo = lo.cwiseMin(p);
      hi = hi.cwiseMax(p);
    }
    const double slack = 1e-6 * std::max(1.0, (hi - lo).norm());
    lo.array() -= slack;
    hi.array() += slack;

    proposal.extent = (hi - lo).norm();
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
        std::vector<Vector3d> unused;
        if (!const_cast<BezierPatchSurface *>(bez)->pointMember(unused, vertices[v])) {
          on = false;
          break;
        }
      }
      if (!on) continue;
      proposal.facets.push_back(f);
    }
    out.push_back(std::move(proposal));
  }
  return out;
}

std::vector<Patch> recogniseBezierPatches(const recognition::FacetGraph& graph,
                                          const std::vector<std::shared_ptr<Surface>>& surfaces,
                                          const std::vector<char>& consumed,
                                          std::vector<std::string>& report, ClaimLedger *ledger,
                                          std::size_t declared_count)
{
  return formBezierPatches(graph, surfaces, consumed, report, ledger, declared_count,
                           proposeBezierPatches(graph, surfaces));
}

std::vector<Patch> formBezierPatches(const recognition::FacetGraph& graph,
                                     const std::vector<std::shared_ptr<Surface>>& surfaces,
                                     const std::vector<char>& consumed, std::vector<std::string>& report,
                                     ClaimLedger *ledger, std::size_t declared_count,
                                     const std::vector<BezierProposal>& proposals)
{
  const Mesh& mesh = graph.mesh();
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;

  std::vector<Patch> patches;
  std::vector<char> taken(loops.size(), 0);

  for (std::size_t surface_index = 0; surface_index < surfaces.size(); surface_index++) {
    const auto& surface = surfaces[surface_index];
    const auto *bez = dynamic_cast<const BezierPatchSurface *>(surface.get());
    if (bez == nullptr || bez->net.empty()) continue;

    // What this patch proposed - every facet on it, whoever holds it -
    // granted by the policy in force today. See proposeBezierPatches.
    const BezierProposal *proposal = nullptr;
    for (const auto& p : proposals) {
      if (p.surface_index == surface_index) proposal = &p;
    }
    if (proposal == nullptr) continue;

    Patch patch;
    patch.surface = surface;
    for (const std::size_t f : proposal->facets) {
      // A held facet is still examined. Held is not held by the right claimant -
      // not the most precise one, and not necessarily the right declaration even
      // when that is exact - so a patch that would have covered it says so in the
      // ledger instead of never having looked. It does not take it: `consumed`
      // still decides, and this only records.
      const bool held = consumed[f] != 0 || taken[f] != 0;
      if (held) {
        if (ledger != nullptr) {
          recognition::recordClaim(*ledger, recognition::kBezierPatch, f, surface, surface_index,
                                   declared_count, ClaimFate::Held,
                                   "covered, but already held when this pass looked");
        }
        continue;
      }
      patch.facets.push_back(f);
    }
    if (patch.facets.empty()) continue;

    std::vector<int> edge_of;
    std::vector<std::vector<int>> cycles;
    const char *why = boundaryCycles(loops, patch.facets, cycles);
    // A Bezier patch is a disc: one boundary, or it is not the sheet it claims
    // to be.
    if (why == nullptr && cycles.size() != 1) why = "the patch boundary does not close";
    if (why != nullptr) {
      patch.alive = false;
      patch.dropped = why;
      patches.push_back(std::move(patch));
      continue;
    }
    const std::vector<int>& cycle = cycles.front();
    // Each *segment* of the boundary is assigned a curve, not each vertex: a
    // segment lies on exactly one, while its endpoints may lie on two.
    const double curve_tol = 1e-7 * std::max(1.0, proposal->extent);
    std::vector<unsigned> on(cycle.size());
    for (std::size_t i = 0; i < cycle.size(); i++) {
      on[i] = boundarySet(*bez, vertices[cycle[i]], curve_tol);
    }
    bool classified = true;
    for (std::size_t i = 0; i < cycle.size(); i++) {
      const unsigned both = on[i] & on[(i + 1) % cycle.size()];
      if (both == 0) {
        classified = false;
        break;
      }
      // A segment whose ends share two curves is a whole edge of the square
      // seen end to end; take the lowest, consistently.
      int e = 0;
      while (((both >> e) & 1u) == 0) e++;
      edge_of.push_back(e);
    }
    if (!classified) {
      patch.alive = false;
      patch.dropped = "a boundary segment lies on none of the patch's edges";
      patches.push_back(std::move(patch));
      continue;
    }

    // Split the cycle into maximal runs sharing one edge of the parameter
    // square. A vertex sitting exactly on a corner reports whichever edge came
    // first, so let it join the run already in progress.
    // Maximal runs of consecutive segments on the same curve. `edge_of[i]` is
    // the segment from cycle[i] to cycle[i+1], so a run of segments is a run of
    // vertices one longer.
    const std::size_t n = cycle.size();
    std::size_t begin = 0;
    while (begin < n && edge_of[begin] == edge_of[(begin + n - 1) % n]) begin++;
    if (begin == n) begin = 0;  // the whole boundary is one curve
    for (std::size_t i = 0; i < n;) {
      const int id = edge_of[(begin + i) % n];
      Patch::Run run;
      run.edge = id;
      run.straight = id <= 1 ? bez->degree_v == 1 : bez->degree_u == 1;
      std::size_t j = i;
      run.verts.push_back(cycle[(begin + j) % n]);
      while (j < n && edge_of[(begin + j) % n] == id) {
        j++;
        run.verts.push_back(cycle[(begin + j) % n]);
      }
      patch.runs.push_back(std::move(run));
      i = j;
    }

    for (const std::size_t f : patch.facets) taken[f] = 1;
    patches.push_back(std::move(patch));
  }

  // Resolve what each boundary run borders. Only possible once every patch is
  // known, because a run may be shared with another one.
  {
    // Hole loops belong here. A patch's facets are always outer bounds - a hole
    // is not a sheet of anything - but what a patch *borders* may perfectly well
    // be one: the counter of a glyph is a hole in the letter's cap face, and
    // every patch around the inside of an O runs along it. Skipping holes left
    // those runs with one user instead of two and so unresolved, which drops the
    // patch; an O kept eight of its nineteen. The band path has always used every
    // valid loop for the same lookup, and the emitter already substitutes into a
    // hole loop and builds a FaceBound for it.
    // The adjacency every recogniser walks, built once - see recognition::FacetGraph.
    const std::map<EdgeKey, std::vector<std::size_t>>& edge_loops = graph.edgeLoops();
    std::map<std::size_t, std::size_t> patch_of_loop;
    for (std::size_t pi = 0; pi < patches.size(); pi++) {
      if (!patches[pi].alive) continue;
      for (const std::size_t f : patches[pi].facets) patch_of_loop[f] = pi;
    }

    for (std::size_t pi = 0; pi < patches.size(); pi++) {
      if (!patches[pi].alive) continue;
      for (auto& run : patches[pi].runs) {
        // Which face is on the other side of each segment. When that face
        // belongs to another patch the answer is the *patch*, not the face: a
        // run of eleven segments against a corner patch borders eleven
        // different triangles of it, so asking for one neighbouring loop
        // rejects every rail on a filleted solid.
        std::set<std::size_t> other_loops, other_patches;
        bool ok = run.verts.size() >= 2;
        for (std::size_t i = 0; ok && i + 1 < run.verts.size(); i++) {
          const auto it = edge_loops.find(edgeKey(run.verts[i], run.verts[i + 1]));
          if (it == edge_loops.end() || it->second.size() != 2) {
            ok = false;
            break;
          }
          const std::size_t a = it->second[0], b = it->second[1];
          const auto own = patch_of_loop.find(a);
          const std::size_t other = (own != patch_of_loop.end() && own->second == pi) ? b : a;
          const auto op = patch_of_loop.find(other);
          if (op != patch_of_loop.end()) other_patches.insert(op->second);
          else other_loops.insert(other);
        }
        if (!ok) continue;  // stays UNRESOLVED
        if (!other_patches.empty()) {
          if (other_patches.size() != 1 || !other_loops.empty()) continue;
          if (*other_patches.begin() == pi) continue;  // folded back on itself
          run.kind = Patch::Run::OTHER_PATCH;
          run.patch = *other_patches.begin();
          continue;
        }
        if (other_loops.size() != 1) continue;
        const std::size_t other = *other_loops.begin();
        // A stretch of one neighbouring face. Find where it starts, so the
        // caller can replace exactly those segments and leave the rest.
        const std::vector<int>& loop = loops[other];
        const std::size_t n = loop.size();
        run.count = run.verts.size() - 1;
        for (std::size_t j = 0; j < n; j++) {
          bool fwd = true, rev = true;
          for (std::size_t c = 0; c < run.count; c++) {
            const int a = loop[(j + c) % n], b = loop[(j + c + 1) % n];
            fwd = fwd && a == run.verts[c] && b == run.verts[c + 1];
            // the neighbour walking the same segments the other way round
            rev = rev && a == run.verts[run.count - c] && b == run.verts[run.count - c - 1];
          }
          if (!fwd && !rev) continue;
          run.kind = run.count == n ? Patch::Run::WHOLE_LOOP : Patch::Run::LOOP_RUN;
          run.loop = other;
          run.start = j;
          run.reversed = !fwd;
          break;
        }
      }
    }
  }

  // Two patches sharing a seam must agree on it exactly. They will share one
  // EdgeCurve, so a seam that is one run for one of them and two for the other
  // cannot be written at all.
  std::size_t seam_mismatch = 0, shared_seams = 0;
  for (std::size_t pi = 0; pi < patches.size(); pi++) {
    if (!patches[pi].alive) continue;
    for (auto& run : patches[pi].runs) {
      if (run.kind != Patch::Run::OTHER_PATCH) continue;
      shared_seams++;
      const Patch& other = patches[run.patch];
      run.partner = std::size_t(-1);
      for (std::size_t ri = 0; ri < other.runs.size(); ri++) {
        const std::vector<int>& theirs = other.runs[ri].verts;
        if (theirs.size() != run.verts.size()) continue;
        bool same = true, flipped = true;
        for (std::size_t k = 0; k < theirs.size(); k++) {
          same = same && theirs[k] == run.verts[k];
          flipped = flipped && theirs[k] == run.verts[theirs.size() - 1 - k];
        }
        if (same || flipped) {
          run.partner = ri;
          break;
        }
      }
      if (run.partner == std::size_t(-1)) {
        run.kind = Patch::Run::UNRESOLVED;
        seam_mismatch++;
      }
    }
  }

  std::size_t live = 0, facets = 0;
  for (const auto& p : patches) {
    if (!p.alive) {
      report.push_back(
        format("a Bezier patch of %d facets was left faceted: %s", int(p.facets.size()), p.dropped));
      continue;
    }
    live++;
    facets += p.facets.size();
  }
  if (live > 0) {
    report.push_back(
      format("%d Bezier patch%s cover %d facets", int(live), live == 1 ? "" : "es", int(facets)));
  }
  if (shared_seams > 0) {
    // Stated even when nothing is wrong. Reporting only the failures makes a
    // clean run and a binary built before the check look identical - both print
    // nothing - which is the same ambiguity the availability line had.
    report.push_back(format("%d of %d shared seams agree between the two patches meeting there",
                            int(shared_seams - seam_mismatch), int(shared_seams)));
  }
  return patches;
}

}  // namespace AnalyticFeatures
