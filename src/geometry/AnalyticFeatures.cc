#include "geometry/AnalyticFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <map>
#include <set>

#include "geometry/Surface.h"

namespace AnalyticFeatures {

Vector3d perpendicular(const Vector3d& norm)
{
  const Vector3d axis = fabs(norm[0]) < 0.9 ? Vector3d(1, 0, 0) : Vector3d(0, 1, 0);
  return norm.cross(axis).normalized();
}

bool fitCircleCentre(const std::vector<Vector3d>& vertices, const std::vector<int>& ids,
                     const Vector3d& axis, double level, Vector3d& centre)
{
  if (ids.size() < 3) return false;

  const Vector3d u = perpendicular(axis);
  const Vector3d w = axis.cross(u);
  const Vector3d origin = vertices[ids[0]];

  Matrix3d ata = Matrix3d::Zero();
  Vector3d atb = Vector3d::Zero();
  for (const int id : ids) {
    const Vector3d rel = vertices[id] - origin;
    const Vector3d row(2 * rel.dot(u), 2 * rel.dot(w), 1.0);
    const double val = rel.dot(u) * rel.dot(u) + rel.dot(w) * rel.dot(w);
    ata += row * row.transpose();
    atb += row * val;
  }

  Eigen::FullPivLU<Matrix3d> lu(ata);
  if (!lu.isInvertible()) return false;
  const Vector3d sol = lu.solve(atb);
  if (!sol.allFinite()) return false;

  centre = origin + sol[0] * u + sol[1] * w;
  centre -= axis * (axis.dot(centre) - level);
  return true;
}

double distanceToAxis(const Vector3d& pt, const Vector3d& base, const Vector3d& axis)
{
  const Vector3d rel = pt - base;
  return (rel - axis * axis.dot(rel)).norm();
}

bool Band::isCone() const
{
  return fabs(r_bottom - r_top) > 1e-9 * std::max(r_bottom, r_top);
}

namespace {

std::string format(const char *fmt, ...)
{
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  return buf;
}

}  // namespace

namespace {

/*! An edge of a facet, as an unordered vertex pair. */
using EdgeKey = std::pair<int, int>;
EdgeKey edgeKey(int a, int b)
{
  return a < b ? EdgeKey(a, b) : EdgeKey(b, a);
}

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
  auto at = [&](double t) {
    std::vector<Vector3d> w = cp;
    for (std::size_t k = w.size(); k > 1; k--) {
      for (std::size_t i = 0; i + 1 < k; i++) w[i] = w[i] * (1 - t) + w[i + 1] * t;
    }
    return w[0];
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

std::vector<Patch> recogniseBezierPatches(const Mesh& mesh,
                                          const std::vector<std::shared_ptr<Surface>>& surfaces,
                                          const std::vector<char>& consumed,
                                          std::vector<std::string>& report)
{
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;

  std::vector<Patch> patches;
  std::vector<char> taken(loops.size(), 0);

  for (const auto& surface : surfaces) {
    const auto *bez = dynamic_cast<const BezierPatchSurface *>(surface.get());
    if (bez == nullptr || bez->net.empty()) continue;

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

    Patch patch;
    patch.surface = surface;
    for (std::size_t f = 0; f < loops.size(); f++) {
      if (!loop_valid[f] || is_hole[f] || consumed[f] || taken[f]) continue;
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
      if (on) patch.facets.push_back(f);
    }
    if (patch.facets.empty()) continue;

    // The boundary of the region: edges used by one of its facets rather than
    // two. Anything else means the region is not a simple sheet.
    std::map<EdgeKey, int> uses;
    for (const std::size_t f : patch.facets) {
      const std::vector<int>& loop = loops[f];
      for (std::size_t i = 0; i < loop.size(); i++) {
        uses[edgeKey(loop[i], loop[(i + 1) % loop.size()])]++;
      }
    }
    std::map<int, std::vector<int>> next;  // boundary adjacency
    std::size_t boundary_edges = 0;
    for (const auto& [key, count] : uses) {
      if (count != 1) continue;
      next[key.first].push_back(key.second);
      next[key.second].push_back(key.first);
      boundary_edges++;
    }
    bool simple = boundary_edges > 0;
    for (const auto& [v, adj] : next) simple = simple && adj.size() == 2;
    if (!simple) {
      patch.alive = false;
      patch.dropped = "the facets on this patch do not form a simple sheet";
      patches.push_back(std::move(patch));
      continue;
    }

    // Walk the boundary once, recording where each vertex sits in the patch's
    // own parameters. That is what says which edge of the patch a boundary
    // segment belongs to, and so which segments have to become one curve.
    std::vector<int> cycle;
    std::vector<int> edge_of;
    {
      const int start = next.begin()->first;
      int prev = -1, cur = start;
      do {
        cycle.push_back(cur);
        const std::vector<int>& adj = next[cur];
        const int step = adj[0] == prev ? adj[1] : adj[0];
        prev = cur;
        cur = step;
      } while (cur != start && cycle.size() <= boundary_edges);
    }
    if (cycle.size() != boundary_edges) {
      patch.alive = false;
      patch.dropped = "the patch boundary does not close";
      patches.push_back(std::move(patch));
      continue;
    }
    // Each *segment* of the boundary is assigned a curve, not each vertex: a
    // segment lies on exactly one, while its endpoints may lie on two.
    const double curve_tol = 1e-7 * std::max(1.0, (hi - lo).norm());
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
  return patches;
}

Result recogniseSurfacesOfRevolution(const Mesh& mesh,
                                     const std::vector<std::shared_ptr<Surface>>& surfaces, double tol)
{
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& loop_is_hole = *mesh.is_hole;
  const std::vector<Vector3d>& loop_normals = *mesh.normals;
  const std::size_t face_cnt = loops.size();
  const double model_tol = tol > 0 ? tol : 1e-5;

  Result result;
  result.band_of_loop.assign(face_cnt, NO_BAND);
  result.consumed.assign(face_cnt, 0);
  if (surfaces.empty()) return result;

  std::vector<Band>& bands = result.bands;
  std::vector<char>& consumed = result.consumed;
  std::vector<std::size_t>& band_of_loop = result.band_of_loop;
  std::vector<std::pair<RimRef, RimRef>>& rims = result.rims;

  std::map<std::pair<int, int>, std::vector<std::size_t>> loop_edges_map;
  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i]) continue;
    const auto& loop = loops[i];
    for (std::size_t j = 0; j < loop.size(); j++) {
      const int a = loop[j], b = loop[(j + 1) % loop.size()];
      loop_edges_map[{std::min(a, b), std::max(a, b)}].push_back(i);
    }
  }

  auto edge_key = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };

  // Did the model declare a cylinder of this radius about this axis?
  auto declared_cylinder = [&](double radius, const Vector3d& axis, const Vector3d& base) {
    for (const auto& surface : surfaces) {
      const auto *cyl = dynamic_cast<const CylinderSurface *>(surface.get());
      if (cyl == nullptr) continue;
      if (fabs(cyl->r - radius) > 1e-7 * radius) continue;
      if (fabs(fabs(cyl->normdir.normalized().dot(axis)) - 1.0) > 1e-7) continue;
      if (distanceToAxis(cyl->refpt, base, axis) > 1e-7 * radius) continue;
      return true;
    }
    return false;
  };

  // Walk the strip of quads reached by crossing ruling edges.
  //
  // The previous version grew across edges parallel to the axis, which finds
  // a cylinder and never a frustum: a cone's rulings are tilted, each one
  // differently. Entering a quad through one ruling fixes which pair of its
  // edges are rulings, so the walk needs no axis and is unambiguous even on a
  // cylinder, where both pairs are parallel.
  // Does this facet lie on the surface the band started on? Passing nullptr
  // admits every quad, which is only used for the first, exploratory walk.
  using OnSurface = std::function<bool(std::size_t)>;

  auto walk_strip = [&](std::size_t seed, int entry_side, std::vector<std::size_t>& walls,
                        std::map<std::size_t, int>& entry, const OnSurface *on_surface) {
    walls.clear();
    entry.clear();
    std::vector<std::pair<std::size_t, int>> stack{{seed, entry_side}};
    while (!stack.empty()) {
      const auto cur = stack.back();
      stack.pop_back();
      if (entry.count(cur.first)) continue;
      entry.emplace(cur.first, cur.second);
      walls.push_back(cur.first);
      const auto& loop = loops[cur.first];
      for (const int side : {cur.second, (cur.second + 2) % 4}) {
        const int a = loop[side], b = loop[(side + 1) % 4];
        const auto it = loop_edges_map.find(edge_key(a, b));
        if (it == loop_edges_map.end()) continue;
        for (const std::size_t nb : it->second) {
          if (nb == cur.first || entry.count(nb) || consumed[nb]) continue;
          if (!loop_valid[nb] || loop_is_hole[nb] || loops[nb].size() != 4) continue;
          if (on_surface != nullptr && !(*on_surface)(nb)) continue;
          for (int j = 0; j < 4; j++) {
            if (edge_key(loops[nb][j], loops[nb][(j + 1) % 4]) == edge_key(a, b)) {
              stack.emplace_back(nb, j);
              break;
            }
          }
        }
      }
    }
  };

  // The chords of a set of facets: the edges which are not rulings. They all
  // lie in a plane perpendicular to the axis.
  auto chords_of = [&](const std::vector<std::size_t>& walls, const std::map<std::size_t, int>& entry) {
    std::vector<Vector3d> chords;
    for (const std::size_t f : walls) {
      const int r = entry.at(f);
      for (const int c : {(r + 1) % 4, (r + 3) % 4}) {
        const Vector3d dir = vertices[loops[f][(c + 1) % 4]] - vertices[loops[f][c]];
        if (dir.norm() > 1e-12) chords.push_back(dir.normalized());
      }
    }
    return chords;
  };

  // Two chords which are not parallel fix the axis exactly.
  auto axis_from = [](const std::vector<Vector3d>& chords) {
    for (std::size_t c = 1; c < chords.size(); c++) {
      const Vector3d n = chords[0].cross(chords[c]);
      if (n.norm() < 1e-9) continue;
      Vector3d axis = n.normalized();
      if (axis[2] < 0 || (axis[2] == 0 && axis[0] < 0)) axis = -axis;
      return axis;
    }
    return Vector3d(0, 0, 0);
  };

  auto perpendicular_to = [](const std::vector<Vector3d>& chords, const Vector3d& axis) {
    for (const Vector3d& c : chords) {
      if (fabs(c.dot(axis)) >= 1e-9) return false;
    }
    return true;
  };

  for (std::size_t seed = 0; seed < face_cnt; seed++) {
    if (!loop_valid[seed] || consumed[seed] || loop_is_hole[seed]) continue;
    if (loops[seed].size() != 4) continue;

    for (int side = 0; side < 4; side++) {
      std::vector<std::size_t> walls;
      std::map<std::size_t, int> entry;
      // First walk freely, only to pin down which surface the seed sits on.
      // Left unconstrained this runs off the wall wherever something flat is
      // attached to it - a rib welded to a tube has quads for side faces, so
      // the strip crosses through the rib and back into the next arc, and the
      // whole ring then fails the fit as one band that was never a band.
      walk_strip(seed, side, walls, entry, nullptr);
      if (walls.size() < 3) continue;

      // Take the axis from the seed and the facets joined to it across a
      // ruling, and from nothing else.
      //
      // Taking it from the whole free walk looks more robust and is not: where
      // that walk runs off the surface it brings foreign chords back with it,
      // the perpendicularity test below rejects them, and the candidate is
      // thrown away before the constrained walk ever gets the chance to clean
      // it up. That is how four lug chamfers of the bayonet container came to
      // be silently unrecognisable - the walk crossed the end of a five quad
      // strip into the lug's side face, turned through ninety degrees there
      // because entering a quad by a different edge redefines which pair of
      // its edges are rulings, and came back with four of fourteen chords
      // perpendicular to nothing. Two chords is all the axis needs, and the
      // seed's own neighbours are the two it can trust.
      std::vector<std::size_t> near{seed};
      for (const std::size_t f : walls) {
        if (f == seed) continue;
        bool joined = false;
        for (int j = 0; j < 4 && !joined; j++) {
          const auto key = edge_key(loops[f][j], loops[f][(j + 1) % 4]);
          for (const int s : {side, (side + 2) % 4}) {
            if (key == edge_key(loops[seed][s], loops[seed][(s + 1) % 4])) joined = true;
          }
        }
        if (joined) near.push_back(f);
      }
      const Vector3d axis = axis_from(chords_of(near, entry));
      if (axis.norm() < 0.5) continue;

      // Fit the surface from the seed and its first two neighbours - four
      // vertices on each rim, which is enough - and walk again, this time
      // admitting only facets which sit on it.
      {
        std::map<int, double> probe_along;
        for (std::size_t f = 0; f < 3 && f < walls.size(); f++) {
          for (const int v : loops[walls[f]]) probe_along[v] = axis.dot(vertices[v]);
        }
        double probe_lo = probe_along.begin()->second, probe_hi = probe_lo;
        for (const auto& kv : probe_along) {
          probe_lo = std::min(probe_lo, kv.second);
          probe_hi = std::max(probe_hi, kv.second);
        }
        std::vector<int> probe_bottom, probe_top;
        for (const auto& kv : probe_along) {
          if (fabs(kv.second - probe_lo) < model_tol) probe_bottom.push_back(kv.first);
          else if (fabs(kv.second - probe_hi) < model_tol) probe_top.push_back(kv.first);
        }
        Vector3d probe_base, probe_top_centre;
        if (!fitCircleCentre(vertices, probe_bottom, axis, probe_lo, probe_base)) continue;
        if (!fitCircleCentre(vertices, probe_top, axis, probe_hi, probe_top_centre)) continue;
        double probe_r0 = 0, probe_r1 = 0;
        for (const int v : probe_bottom) probe_r0 += distanceToAxis(vertices[v], probe_base, axis);
        for (const int v : probe_top) probe_r1 += distanceToAxis(vertices[v], probe_base, axis);
        probe_r0 /= double(probe_bottom.size());
        probe_r1 /= double(probe_top.size());
        const double probe_scale = std::max(probe_r0, probe_r1);
        if (probe_scale < model_tol) continue;

        const OnSurface on_surface = [&](std::size_t f) {
          for (const int v : loops[f]) {
            const double t = axis.dot(vertices[v]);
            const double want = fabs(t - probe_lo) < model_tol
                                  ? probe_r0
                                  : (fabs(t - probe_hi) < model_tol ? probe_r1 : -1.0);
            if (want < 0) return false;
            if (fabs(distanceToAxis(vertices[v], probe_base, axis) - want) > 1e-7 * probe_scale) {
              return false;
            }
          }
          return true;
        };
        walk_strip(seed, side, walls, entry, &on_surface);
        if (walls.size() < 3) continue;
      }

      // Now that the walk is confined to one surface, the whole band has to
      // agree with the axis the seed's neighbourhood gave. This is the test
      // that used to run against the free walk, moved to the only set of
      // facets it can be asked of meaningfully - a seed whose neighbourhood
      // happens to give a wrong axis is still rejected here, just later.
      if (!perpendicular_to(chords_of(walls, entry), axis)) continue;

      // every wall vertex has to sit on one of the two rims
      std::map<int, double> along;
      for (const std::size_t f : walls) {
        for (const int v : loops[f]) along[v] = axis.dot(vertices[v]);
      }
      double lo = along.begin()->second, hi = lo;
      for (const auto& kv : along) {
        lo = std::min(lo, kv.second);
        hi = std::max(hi, kv.second);
      }
      if (hi - lo < model_tol) continue;

      std::vector<int> bottom_set, top_set;
      bool split_ok = true;
      for (const auto& kv : along) {
        if (fabs(kv.second - lo) < model_tol) bottom_set.push_back(kv.first);
        else if (fabs(kv.second - hi) < model_tol) top_set.push_back(kv.first);
        else split_ok = false;
      }
      if (!split_ok) continue;

      // A band which closes on itself has one rim vertex per facet; one which
      // stops short of a full turn has one more, the far end of the last
      // facet. Anything else is not a band around a common axis.
      const bool full_turn = bottom_set.size() == walls.size() && top_set.size() == walls.size();
      const bool part_turn = bottom_set.size() == walls.size() + 1 && top_set.size() == walls.size() + 1;
      if (!full_turn && !part_turn) continue;

      // Fit each rim on its own: the centroid of a full rim lies on the axis
      // but the centroid of an arc sits inside its chord, and the two rims of
      // a frustum have different radii anyway.
      Vector3d base, top_centre;
      if (!fitCircleCentre(vertices, bottom_set, axis, lo, base)) continue;
      if (!fitCircleCentre(vertices, top_set, axis, hi, top_centre)) continue;
      if (distanceToAxis(top_centre, base, axis) > 1e-6) continue;  // coaxial

      double r_bottom = 0, r_top = 0;
      for (const int v : bottom_set) r_bottom += distanceToAxis(vertices[v], base, axis);
      for (const int v : top_set) r_top += distanceToAxis(vertices[v], base, axis);
      r_bottom /= double(bottom_set.size());
      r_top /= double(top_set.size());
      const double scale = std::max(r_bottom, r_top);
      if (scale < model_tol) continue;
      double dev = 0;
      for (const int v : bottom_set) {
        dev = std::max(dev, fabs(distanceToAxis(vertices[v], base, axis) - r_bottom));
      }
      for (const int v : top_set) {
        dev = std::max(dev, fabs(distanceToAxis(vertices[v], base, axis) - r_top));
      }
      if (dev > 1e-7 * scale) continue;

      // Intent. A cylinder needs its own record; a frustum has none, because
      // the shape that produces one - hull() of two coaxial cylinders, the
      // standard chamfer - declares the two cylinders rather than the cone
      // between them. Both of its rims matching a declared cylinder is the
      // same statement of intent, made by two primitives instead of one.
      const bool is_cone = fabs(r_bottom - r_top) > 1e-9 * scale;
      if (is_cone) {
        if (!declared_cylinder(r_bottom, axis, base)) continue;
        if (!declared_cylinder(r_top, axis, base)) continue;
      } else if (!declared_cylinder(r_bottom, axis, base)) {
        continue;
      }

      Band info;
      info.walls = walls;
      info.axis = axis;
      info.base = base;
      info.r_bottom = r_bottom;
      info.r_top = r_top;
      info.height = hi - lo;
      info.closed = full_turn;
      info.bottom_set = bottom_set;
      info.top_set = top_set;

      const Vector3d probe = vertices[loops[walls[0]][0]];
      const Vector3d radial = (probe - base) - axis * axis.dot(probe - base);
      info.outward = radial.normalized().dot(loop_normals[walls[0]]) > 0;

      for (const std::size_t f : walls) {
        consumed[f] = 1;
        band_of_loop[f] = bands.size();
      }
      bands.push_back(info);
      break;
    }
  }

  // ---- what each rim borders -------------------------------------------
  //
  // A band whose rim cannot be resolved is dropped, which can leave a
  // neighbour's shared rim unresolvable in turn, so this runs to a fixed
  // point. Dropping is monotone, so it terminates.
  rims.assign(bands.size(), {RimRef(), RimRef()});

  auto rim_edges = [&](std::size_t bi, bool bottom) {
    const Band& band = bands[bi];
    const std::vector<int>& level_v = bottom ? band.bottom_set : band.top_set;
    const std::set<int> level(level_v.begin(), level_v.end());
    std::set<std::pair<int, int>> out;
    for (const std::size_t f : band.walls) {
      const auto& loop = loops[f];
      for (std::size_t j = 0; j < loop.size(); j++) {
        const int a = loop[j], b = loop[(j + 1) % loop.size()];
        if (level.count(a) && level.count(b)) out.insert(edge_key(a, b));
      }
    }
    return out;
  };

  // The two ends of a rim that stops short of a full turn, counter clockwise
  // about the axis.
  //
  // They come out of the rim's own edges - the two vertices used by one of them
  // rather than two - and not out of whatever lies on the far side. That is
  // what lets a rim shared between two bands be handled at all, since there is
  // no neighbouring loop there to index into, and it keeps one code path for
  // both cases rather than two that can drift apart.
  //
  // Ordering them by angle would put the branch cut of atan2 in the way; the
  // sign of one cross product of two *adjacent* rim vertices does not, because
  // adjacent rim vertices are a whole facet apart.
  auto rim_ends = [&](std::size_t bi, bool bottom, int& ccw_start, int& ccw_end) {
    const Band& band = bands[bi];
    const auto edges = rim_edges(bi, bottom);
    std::map<int, std::vector<int>> adjacent;
    for (const auto& edge : edges) {
      adjacent[edge.first].push_back(edge.second);
      adjacent[edge.second].push_back(edge.first);
    }
    std::vector<int> ends;
    for (const auto& kv : adjacent) {
      if (kv.second.size() == 1) ends.push_back(kv.first);
    }
    if (ends.size() != 2) return false;

    const Vector3d centre = bottom ? band.base : band.base + band.axis * band.height;
    const Vector3d va = vertices[ends[0]] - centre;
    const Vector3d vb = vertices[adjacent[ends[0]].front()] - centre;
    const bool first_is_start = band.axis.dot(va.cross(vb)) > 0;
    ccw_start = ends[first_is_start ? 0 : 1];
    ccw_end = ends[first_is_start ? 1 : 0];
    return true;
  };

  // The direction the wall facets traverse a rim edge is the direction the
  // collapsed face has to traverse the whole rim: the face replaces those
  // facets, so its boundary is theirs.
  auto wall_runs_ccw = [&](std::size_t bi, const std::pair<int, int>& edge) {
    const Band& band = bands[bi];
    for (const std::size_t f : band.walls) {
      const auto& loop = loops[f];
      for (std::size_t j = 0; j < loop.size(); j++) {
        const int a = loop[j], b = loop[(j + 1) % loop.size()];
        if (edge_key(a, b) != edge) continue;
        const Vector3d va = vertices[a] - band.base;
        const Vector3d vb = vertices[b] - band.base;
        return band.axis.dot(va.cross(vb)) > 0;
      }
    }
    return true;
  };

  auto resolve_rim = [&](std::size_t bi, bool bottom, RimRef& out, const char **why) {
    const Band& band = bands[bi];
    const auto edges = rim_edges(bi, bottom);
    if (edges.empty()) {
      *why = "no rim edges";
      return false;
    }
    const std::set<std::size_t> in_band(band.walls.begin(), band.walls.end());

    std::set<std::size_t> others;
    for (const auto& edge : edges) {
      const auto it = loop_edges_map.find(edge);
      if (it == loop_edges_map.end()) {
        *why = "a rim edge belongs to no loop";
        return false;
      }
      std::size_t outside = face_cnt;
      int count = 0;
      for (const std::size_t user : it->second) {
        if (in_band.count(user)) continue;
        count++;
        outside = user;
      }
      if (count != 1) {
        *why = "a rim edge is used by more than two faces";
        return false;
      }
      others.insert(outside);
    }

    out.wall_ccw = wall_runs_ccw(bi, *edges.begin());

    if (others.size() == 1) {
      const std::size_t nb = *others.begin();
      if (band_of_loop[nb] != NO_BAND) {
        *why = "the rim borders a single facet of another band";
        return false;
      }  // a one facet band
      if (!loop_valid[nb] || consumed[nb]) {
        *why = "the neighbouring face was dropped";
        return false;
      }
      const std::vector<int>& nb_loop = loops[nb];
      const std::set<int> key(nb_loop.begin(), nb_loop.end());
      if (key.size() == nb_loop.size() && edges.size() == nb_loop.size()) {
        out.kind = RimRef::WHOLE_LOOP;
        out.loop = nb;
        return true;
      }
      // a run inside the loop, which an arc can replace only when its edges
      // are consecutive there
      const std::size_t n = nb_loop.size();
      std::vector<char> on_rim(n, 0);
      std::size_t cnt = 0;
      for (std::size_t j = 0; j < n; j++) {
        if (edges.count(edge_key(nb_loop[j], nb_loop[(j + 1) % n])) == 0) continue;
        on_rim[j] = 1;
        cnt++;
      }
      if (cnt != edges.size() || cnt >= n) {
        *why = "the rim is not a run of its neighbour's edges";
        return false;
      }
      std::size_t start = n;
      for (std::size_t j = 0; j < n; j++) {
        if (on_rim[j] == 0 || on_rim[(j + n - 1) % n] != 0) continue;
        if (start != n) {
          *why = "the rim is split across its neighbour's loop";
          return false;
        }
        start = j;
      }
      if (start == n) {
        *why = "the rim covers its neighbour's whole loop twice";
        return false;
      }
      out.kind = RimRef::LOOP_RUN;
      out.loop = nb;
      out.start = start;
      out.count = cnt;
      return true;
    }

    // shared with another band, which has to be collapsed too
    std::set<std::size_t> nb_bands;
    for (const std::size_t f : others) nb_bands.insert(band_of_loop[f]);
    if (nb_bands.size() != 1 || *nb_bands.begin() == NO_BAND) {
      *why = "the rim borders one face per facet";
      return false;
    }
    const std::size_t other = *nb_bands.begin();
    if (!bands[other].alive) {
      *why = "the band sharing this rim was dropped";
      return false;
    }
    if (band.closed != bands[other].closed) {
      *why = "a shared rim needs both bands to be the same shape";
      return false;
    }

    if (!band.closed) {
      // Two partial bands, so the shared rim is an arc rather than a circle -
      // a bayonet lug is a wall on a chamfer on a wall and none of the three
      // goes all the way round, so every joint in one is this case.
      //
      // It is the same substitution the closed case makes, and safe under the
      // same condition strengthened: the two bands have to meet along the
      // *whole* of the rim. If either had rim edges the other lacked, the arc
      // would have to be split on one side and not the other, and the two
      // faces could no longer share one edge.
      for (const bool other_bottom : {true, false}) {
        if (rim_edges(other, other_bottom) != edges) continue;
        out.kind = RimRef::OTHER_BAND_ARC;
        out.band = other;
        return true;
      }
      *why = "the two partial bands share only part of the rim";
      return false;
    }

    if (others.size() != bands[other].walls.size()) {
      *why = "the shared rim does not cover the whole neighbouring band";
      return false;
    }
    out.kind = RimRef::OTHER_BAND;
    out.band = other;
    return true;
  };

  // The end edges of a partial band have to be edges the mesh already has.
  //
  // The face closes by running along the bottom rim, up one end, back along
  // the top rim and down the other, so the two ends it needs are "where the
  // bottom traversal finishes to where the top traversal starts" and the
  // reverse. If those are not edges of the mesh the face would be closed with
  // a diagonal that is not an edge at all, which opens the shell against every
  // face that shares the real one.
  auto ends_line_up = [&](const RimRef& bottom, const RimRef& top) {
    return loop_edges_map.count(edge_key(bottom.traversalEnd(), top.traversalStart())) != 0 &&
           loop_edges_map.count(edge_key(top.traversalEnd(), bottom.traversalStart())) != 0;
  };

  // Two bands must not rewrite the same planar loop, or the same run of it.
  for (bool changed = true; changed;) {
    changed = false;
    std::set<std::size_t> whole_taken;
    std::map<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>> runs_taken;

    for (std::size_t i = 0; i < bands.size(); i++) {
      if (!bands[i].alive) continue;
      RimRef bottom, top;
      const char *why = "unresolved";
      if (!resolve_rim(i, true, bottom, &why) || !resolve_rim(i, false, top, &why)) {
        bands[i].alive = false;
        bands[i].dropped = why;
        changed = true;
        continue;
      }

      // A full turn collapses each rim into a closed circle, which can only
      // replace a whole loop or the matching rim of another closed band; a
      // partial band collapses each rim into an arc, which either replaces a
      // run of a loop or is shared with another partial band. Anything else
      // would put a closed circle in the middle of a loop, or an arc where a
      // whole bound was wanted.
      auto is_arc = [](const RimRef& rim) {
        return rim.kind == RimRef::LOOP_RUN || rim.kind == RimRef::OTHER_BAND_ARC;
      };
      const bool shapes_ok =
        bands[i].closed ? (!is_arc(bottom) && !is_arc(top)) : (is_arc(bottom) && is_arc(top));
      if (!shapes_ok) {
        bands[i].alive = false;
        bands[i].dropped = "a rim is an arc, but the band covers the full turn";
        changed = true;
        continue;
      }

      // Both ends of a partial band's rims, taken from the rims themselves.
      if (!bands[i].closed && !(rim_ends(i, true, bottom.ccw_start, bottom.ccw_end) &&
                                rim_ends(i, false, top.ccw_start, top.ccw_end))) {
        bands[i].alive = false;
        bands[i].dropped = "a rim of the band has no two ends";
        changed = true;
        continue;
      }

      // The two ends of a partial band are ordinary edges of the mesh. If the
      // runs do not line up - the vertex ending one rim's run sitting on the
      // same ruling as the vertex starting the other's - the face would be
      // closed with a diagonal that is not an edge at all, which opens the
      // shell against every face that shares the real one.
      if (!bands[i].closed && !ends_line_up(bottom, top)) {
        bands[i].alive = false;
        bands[i].dropped = "the two rims of the band do not end on the same rulings";
        changed = true;
        continue;
      }

      bool clash = false;
      for (const RimRef *rim : {&bottom, &top}) {
        if (rim->kind == RimRef::WHOLE_LOOP) {
          if (!whole_taken.insert(rim->loop).second) clash = true;
        } else if (rim->kind == RimRef::LOOP_RUN) {
          const std::size_t n = loops[rim->loop].size();
          for (const auto& taken : runs_taken[rim->loop]) {
            for (std::size_t a = 0; a < rim->count && !clash; a++) {
              for (std::size_t b = 0; b < taken.second; b++) {
                if ((rim->start + a) % n == (taken.first + b) % n) clash = true;
              }
            }
          }
          runs_taken[rim->loop].push_back({rim->start, rim->count});
        }
      }
      if (clash) {
        bands[i].alive = false;
        bands[i].dropped = "another band already rewrites the same loop";
        changed = true;
        continue;
      }
      rims[i] = {bottom, top};
    }
  }

  // A periodic face needs a seam, and the seam has to be a ruling: both of
  // its ends on the same radial direction, or the line would cut through the
  // surface instead of lying on it.
  //
  // Picking each end independently by angle does not work, however obvious it
  // looks. atan2 has its branch cut at pi, a polygon with an even number of
  // facets has a vertex sitting exactly there, and which side of the cut it
  // lands on is decided by the sign of a y coordinate which is zero to
  // fifteen digits. Two rims of one wall disagreed on that sign, their seams
  // came out on different rulings, and a cylinder that was otherwise perfect
  // was dropped.
  //
  // So only one end is chosen, and the other is *derived* from it. Where two
  // bands share a rim they have to use the same vertex - the circle between
  // them is one edge - so a band takes whichever of its rims is already
  // settled and derives the other; a single pass suffices, because a band
  // which finds neither settled settles both.
  std::map<std::set<int>, int> rim_seam;

  auto vertex_on_ruling = [&](int from, const std::vector<int>& level, const Vector3d& axis,
                              const Vector3d& centre) {
    const Vector3d a = vertices[from] - centre;
    const Vector3d ra = (a - axis * axis.dot(a)).normalized();
    for (const int v : level) {
      const Vector3d b = vertices[v] - centre;
      const Vector3d rb = (b - axis * axis.dot(b)).normalized();
      if ((ra - rb).norm() < 1e-6) return v;
    }
    return -1;
  };

  for (std::size_t i = 0; i < bands.size(); i++) {
    Band& band = bands[i];
    if (!band.alive || !band.closed) continue;
    const std::set<int> bottom_key(band.bottom_set.begin(), band.bottom_set.end());
    const std::set<int> top_key(band.top_set.begin(), band.top_set.end());
    const Vector3d top_centre = band.base + band.axis * band.height;

    const auto settled_bottom = rim_seam.find(bottom_key);
    const auto settled_top = rim_seam.find(top_key);
    if (settled_bottom != rim_seam.end()) {
      band.seam_bottom = settled_bottom->second;
      band.seam_top = vertex_on_ruling(band.seam_bottom, band.top_set, band.axis, band.base);
    } else if (settled_top != rim_seam.end()) {
      band.seam_top = settled_top->second;
      band.seam_bottom = vertex_on_ruling(band.seam_top, band.bottom_set, band.axis, top_centre);
    } else {
      band.seam_bottom = band.bottom_set.front();
      band.seam_top = vertex_on_ruling(band.seam_bottom, band.top_set, band.axis, band.base);
    }

    if (band.seam_bottom == -1 || band.seam_top == -1) {
      band.alive = false;
      band.dropped = "the two rims have no ruling in common to run a seam along";
      continue;
    }
    rim_seam[bottom_key] = band.seam_bottom;
    rim_seam[top_key] = band.seam_top;
  }

  // dropping a band puts its facets back
  for (std::size_t i = 0; i < bands.size(); i++) {
    if (bands[i].alive) continue;
    for (const std::size_t f : bands[i].walls) {
      consumed[f] = 0;
      band_of_loop[f] = NO_BAND;
    }
  }

  // ---- merge a run of bands lying on one declared sphere ----------------
  //
  // A sphere is not a grid to be grown. Every ring of its tessellation is
  // already a frustum whose rims are circles, so the zone is the maximal run of
  // bands joined at shared rims whose vertices all lie on one declared sphere -
  // which means the band pass has done the work and this only has to join up
  // its answer. The merged band keeps the run's outer rims, so the rules that
  // were resolved for the end bands still hold, and the flat cap at either end
  // is untouched.
  //
  // The alternative, flooding across every edge into any face whose vertices
  // are on the sphere, does not work: an OpenSCAD sphere is a closed polyhedron
  // inscribed in the sphere and its caps have every vertex on the surface too,
  // with the same sag as any other facet. There is no local geometric test that
  // separates a cap from a ring quad, because geometrically there is nothing to
  // separate - only the structure says which is which.
  {
    // which live bands meet at each rim, keyed by the rim's vertex set
    std::map<std::set<int>, std::vector<std::size_t>> at_rim;
    for (std::size_t i = 0; i < bands.size(); i++) {
      if (!bands[i].alive || !bands[i].closed) continue;
      for (const bool bottom : {true, false}) {
        const std::vector<int>& level = bottom ? bands[i].bottom_set : bands[i].top_set;
        at_rim[std::set<int>(level.begin(), level.end())].push_back(i);
      }
    }

    auto on_sphere = [&](const SphereSurface *sph, std::size_t bi) {
      for (const std::size_t f : bands[bi].walls) {
        for (const int v : loops[f]) {
          if (fabs((vertices[v] - sph->refpt).norm() - sph->r) > 1e-7 * sph->r) return false;
        }
      }
      return true;
    };

    std::vector<char> absorbed(bands.size(), 0);
    for (const auto& surface : surfaces) {
      const auto *sph = dynamic_cast<const SphereSurface *>(surface.get());
      if (sph == nullptr) continue;

      for (std::size_t seed = 0; seed < bands.size(); seed++) {
        if (!bands[seed].alive || absorbed[seed] || bands[seed].zone != nullptr) continue;
        if (!bands[seed].closed || !on_sphere(sph, seed)) continue;

        // walk the run outwards from the seed, one rim at a time
        std::vector<std::size_t> run{seed};
        for (const bool up : {false, true}) {
          std::size_t cur = seed;
          for (;;) {
            const std::vector<int>& level = up ? bands[cur].top_set : bands[cur].bottom_set;
            const auto it = at_rim.find(std::set<int>(level.begin(), level.end()));
            if (it == at_rim.end() || it->second.size() != 2) break;
            const std::size_t next = it->second[0] == cur ? it->second[1] : it->second[0];
            if (absorbed[next] || next == seed) break;
            if (std::find(run.begin(), run.end(), next) != run.end()) break;  // closed on itself
            if (!on_sphere(sph, next)) break;
            run.push_back(next);
            absorbed[next] = 1;
            cur = next;
          }
        }
        if (run.size() < 2) continue;

        // the ends of the run are the bands with a rim no other band in it uses
        std::map<std::set<int>, int> uses;
        for (const std::size_t bi : run) {
          for (const bool bottom : {true, false}) {
            const std::vector<int>& level = bottom ? bands[bi].bottom_set : bands[bi].top_set;
            uses[std::set<int>(level.begin(), level.end())]++;
          }
        }
        std::size_t low = bands.size(), high = bands.size();
        for (const std::size_t bi : run) {
          const std::set<int> b(bands[bi].bottom_set.begin(), bands[bi].bottom_set.end());
          const std::set<int> t(bands[bi].top_set.begin(), bands[bi].top_set.end());
          if (uses[b] == 1) low = bi;
          if (uses[t] == 1) high = bi;
        }
        if (low == bands.size() || high == bands.size()) {
          // No free rim at either end: the run closes on itself, which is a
          // torus. It is only accepted when a torus was declared - `sph` here
          // is a sphere, and a closed run of bands on a sphere is impossible,
          // so this always falls through to the torus pass below.
          for (const std::size_t bi : run) absorbed[bi] = 0;
          continue;
        }

        std::vector<std::size_t> walls;
        for (const std::size_t bi : run) {
          walls.insert(walls.end(), bands[bi].walls.begin(), bands[bi].walls.end());
        }

        // Read both ends *before* writing anything. The merged band is one of
        // the run - it has to be, or its facets would be put back - so the
        // reference below may alias bands[low] or bands[high], and assigning
        // through it would change what the other end still has to be read from.
        // Taken the other way round the top rim's centre came out at the second
        // ring rather than the last, its reference direction tilted 87 degrees
        // out of the rim's plane, and the flat cap bounded by it stopped being
        // flat.
        const Vector3d base = bands[low].base;
        const Vector3d top_centre = bands[high].base + bands[seed].axis * bands[high].height;
        const std::vector<int> bottom_set = bands[low].bottom_set;
        const std::vector<int> top_set = bands[high].top_set;
        const double r_bottom = bands[low].r_bottom;
        const double r_top = bands[high].r_top;
        const int seam_bottom = bands[low].seam_bottom;
        const int seam_top = bands[high].seam_top;
        const bool outward = bands[low].outward;
        // keep the run's outer rims, and with them the rules already resolved
        const std::pair<RimRef, RimRef> ends{rims[low].first, rims[high].second};

        Band& merged = bands[seed];
        merged.walls = walls;
        merged.bottom_set = bottom_set;
        merged.top_set = top_set;
        merged.base = base;
        // stated as the two centres rather than as a sum of heights, so that it
        // stays right however the run was ordered
        merged.height = merged.axis.dot(top_centre - base);
        merged.r_bottom = r_bottom;
        merged.r_top = r_top;
        merged.seam_bottom = seam_bottom;
        merged.seam_top = seam_top;
        merged.outward = outward;
        merged.zone = surface;
        rims[seed] = ends;
        for (const std::size_t bi : run) {
          if (bi == seed) continue;
          bands[bi].alive = false;
          bands[bi].dropped = nullptr;  // absorbed, not rejected: keep its facets
          absorbed[bi] = 1;
        }
        for (const std::size_t f : walls) band_of_loop[f] = seed;
        absorbed[seed] = 0;
      }
    }
  }

  // ---- merge a run of bands which closes on itself, on a declared torus ----
  //
  // The same observation as for a sphere: a torus is a stack of bands, one per
  // profile edge, and the zone is the run of them joined at shared rims. The
  // difference is only that the run has no ends - every rim is shared - so
  // there is nothing to keep, and the face is bounded by its own two seams
  // instead. That is why it needs a declaration of its own rather than falling
  // out of the ring circles: those already collapse a torus into a stack of
  // exact cones, and only a TorusSurface says the stack was one surface.
  {
    std::vector<char> absorbed(bands.size(), 0);
    for (const auto& surface : surfaces) {
      const auto *tor = dynamic_cast<const TorusSurface *>(surface.get());
      if (tor == nullptr) continue;

      auto on_torus = [&](std::size_t bi) {
        for (const std::size_t f : bands[bi].walls) {
          for (const int v : loops[f]) {
            const Vector3d rel = vertices[v] - tor->refpt;
            const double along = rel.dot(tor->normdir);
            const double radial = (rel - tor->normdir * along).norm();
            const double d = radial - tor->r_major;
            if (fabs(sqrt(d * d + along * along) - tor->r_minor) > 1e-7 * tor->r_minor) return false;
          }
        }
        return true;
      };

      std::map<std::set<int>, std::vector<std::size_t>> at_rim;
      for (std::size_t i = 0; i < bands.size(); i++) {
        if (!bands[i].alive || !bands[i].closed || bands[i].zone != nullptr) continue;
        if (!on_torus(i)) continue;
        for (const bool bottom : {true, false}) {
          const std::vector<int>& level = bottom ? bands[i].bottom_set : bands[i].top_set;
          at_rim[std::set<int>(level.begin(), level.end())].push_back(i);
        }
      }

      for (std::size_t seed = 0; seed < bands.size(); seed++) {
        if (!bands[seed].alive || absorbed[seed] || bands[seed].zone != nullptr) continue;
        if (!bands[seed].closed || !on_torus(seed)) continue;

        // Walk the ring until it comes back to the seed, leaving each band by
        // the rim it was not entered by.
        //
        // Not by its top rim, which is what the sphere pass does. A sphere's
        // bands stack monotonically along the axis, so every rim is one band's
        // top and the next one's bottom. A torus's profile turns around at its
        // widest and narrowest points, and the two bands meeting there meet top
        // to top: a walk which always follows the top rim comes straight back
        // to where it started and the run stops two bands long.
        std::vector<std::size_t> run{seed};
        std::size_t cur = seed;
        std::set<int> came_by(bands[seed].bottom_set.begin(), bands[seed].bottom_set.end());
        bool cyclic = false;
        for (;;) {
          const std::set<int> bottom(bands[cur].bottom_set.begin(), bands[cur].bottom_set.end());
          const std::set<int> top(bands[cur].top_set.begin(), bands[cur].top_set.end());
          const std::set<int>& level = bottom == came_by ? top : bottom;
          const auto it = at_rim.find(level);
          if (it == at_rim.end() || it->second.size() != 2) break;
          const std::size_t next = it->second[0] == cur ? it->second[1] : it->second[0];
          if (next == seed) {
            cyclic = true;
            break;
          }
          if (absorbed[next] || std::find(run.begin(), run.end(), next) != run.end()) break;
          run.push_back(next);
          came_by = level;
          cur = next;
        }
        if (!cyclic || run.size() < 3) continue;

        std::vector<std::size_t> walls;
        for (const std::size_t bi : run) {
          walls.insert(walls.end(), bands[bi].walls.begin(), bands[bi].walls.end());
        }

        // A torus face is bounded by nothing but its own two seams, so the only
        // thing the emitter needs from the mesh is one vertex where they cross.
        // Everything else - both circles, their centres, their radii - comes
        // out of the record.
        const int corner = bands[seed].bottom_set.front();

        // Which way the face looks: away from the tube's centre circle, which
        // for the inner half of a torus is the opposite of away from the axis.
        const Vector3d probe = vertices[loops[bands[seed].walls[0]][0]];
        const Vector3d rel = probe - tor->refpt;
        const double along = rel.dot(tor->normdir);
        const Vector3d radial = rel - tor->normdir * along;
        const Vector3d tube = tor->refpt + radial.normalized() * tor->r_major;
        const bool outward = (probe - tube).normalized().dot(loop_normals[bands[seed].walls[0]]) > 0;

        Band& merged = bands[seed];
        merged.walls = walls;
        merged.seam_bottom = corner;
        merged.seam_top = corner;
        merged.outward = outward;
        merged.zone = surface;
        for (const std::size_t bi : run) {
          if (bi == seed) continue;
          bands[bi].alive = false;
          bands[bi].dropped = nullptr;  // absorbed, not rejected: keep its facets
          absorbed[bi] = 1;
        }
        for (const std::size_t f : walls) band_of_loop[f] = seed;
      }
    }
  }

  std::size_t collapsed = 0, alive = 0, cones = 0, partial = 0, spheres = 0, tori = 0;
  for (const auto& band : bands) {
    if (!band.alive) continue;
    alive++;
    collapsed += band.walls.size();
    if (dynamic_cast<const TorusSurface *>(band.zone.get()) != nullptr) tori++;
    else if (band.zone != nullptr) spheres++;
    else if (band.isCone()) cones++;
    if (!band.closed) partial++;
  }
  if (alive > 0) {
    result.report.push_back(
      format("%d surface%s recognised (%d toroidal, %d spherical, %d conical, %d partial), "
             "%d facets replaced",
             int(alive), alive == 1 ? "" : "s", int(tori), int(spheres), int(cones), int(partial),
             int(collapsed)));
  }
  // Every band here fits its axis exactly and was declared by the model, so a
  // drop is always the topology around it rather than the surface itself.
  // Naming the rule that rejected it is the only way to tell a wall which
  // cannot be written from one which should have been.
  for (const auto& band : bands) {
    if (band.alive || band.dropped == nullptr) continue;
    result.report.push_back(format("r=%g band of %d facets left faceted: %s", band.r_bottom,
                                   int(band.walls.size()), band.dropped));
  }

  return result;
}

}  // namespace AnalyticFeatures
