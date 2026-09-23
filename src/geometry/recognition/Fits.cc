/*! The approximation's fitters: the smooth regions no declaration took, and the
 * cylinder, cone, turned surface or sweep fitted to one. D3 by construction.
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

std::shared_ptr<Surface> fitCylinder(const Mesh& mesh, const SmoothRegion& region, double tol)
{
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<Vector3d>& normals = *mesh.normals;
  if (region.facets.size() < 3) return nullptr;

  // The axis is the direction every facet normal is perpendicular to, so it is
  // the direction the normals' scatter matrix is *smallest* in. Weighting by
  // area keeps a swarm of tiny facets from outvoting the shape.
  Eigen::Matrix3d scatter = Eigen::Matrix3d::Zero();
  std::set<int> verts;
  for (const std::size_t f : region.facets) {
    const std::vector<int>& loop = loops[f];
    if (loop.size() < 3) continue;
    for (const int v : loop) verts.insert(v);
    double area = 0;
    for (std::size_t i = 1; i + 1 < loop.size(); i++) {
      area +=
        (vertices[loop[i]] - vertices[loop[0]]).cross(vertices[loop[i + 1]] - vertices[loop[0]]).norm() /
        2;
    }
    const Vector3d n = normals[f].normalized();
    scatter += area * n * n.transpose();
  }
  if (verts.size() < 6) return nullptr;

  Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(scatter);
  if (solver.info() != Eigen::Success) return nullptr;
  const Vector3d axis = solver.eigenvectors().col(0).normalized();
  const double lo = solver.eigenvalues()[0], mid = solver.eigenvalues()[1], hi = solver.eigenvalues()[2];
  if (!(hi > 0)) return nullptr;
  // Two conditions, and they refuse different things. The normals must lie in a
  // plane - or there is no axis - and they must genuinely spread within it, or
  // the region is flat and every axis in its plane fits equally well.
  if (lo > 1e-6 * hi) return nullptr;
  if (mid < 1e-3 * hi) return nullptr;

  // A least squares circle through the vertices projected onto the plane
  // perpendicular to the axis: x^2 + y^2 + Dx + Ey + F = 0, linear in D, E, F.
  const Vector3d ref = perpendicular(axis);
  const Vector3d ref2 = axis.cross(ref);
  const Vector3d origin = vertices[*verts.begin()];
  Eigen::Matrix3d ata = Eigen::Matrix3d::Zero();
  Vector3d atb = Vector3d::Zero();
  for (const int v : verts) {
    const Vector3d rel = vertices[v] - origin;
    const double x = rel.dot(ref), y = rel.dot(ref2);
    const Vector3d row(x, y, 1.0);
    ata += row * row.transpose();
    atb += row * -(x * x + y * y);
  }
  if (fabs(ata.determinant()) < 1e-18) return nullptr;
  const Vector3d sol = ata.inverse() * atb;
  const double cx = -sol[0] / 2, cy = -sol[1] / 2;
  const double rsq = cx * cx + cy * cy - sol[2];
  if (!(rsq > 0)) return nullptr;
  const double r = sqrt(rsq);
  if (!(r > tol)) return nullptr;

  const Vector3d centre = origin + ref * cx + ref2 * cy;
  for (const int v : verts) {
    const Vector3d rel = vertices[v] - centre;
    const double radial = (rel - axis * rel.dot(axis)).norm();
    if (fabs(radial - r) > tol) return nullptr;
  }
  return std::make_shared<CylinderSurface>(centre, axis, r);
}

/*! The cone a region lies on, or nullptr.
 *
 * `fitCylinder` cannot stand in for this and does not try: a cone's normals
 * make a constant angle with its axis rather than lying in a plane, so the
 * coplanarity test that finds a cylinder's axis correctly refuses one. That
 * refusal is why the reference lid fitted no quadric at all to walls which are
 * mostly cone - its hose socket is bored as `cylinder(r1, r2, h)` over most of
 * its depth, and the exported geometry shows the radius falling a millimetre
 * over fifty-five of z.
 *
 * The apex is found first and it is found linearly, which is the whole trick.
 * A ruling of a cone lies in the tangent plane along it, so for every point P
 * with normal n on a cone of apex A, `n . (P - A) = 0` - three unknowns, one
 * linear equation per vertex, and no starting guess to converge from. The axis
 * then falls out as the mean of the rulings from that apex, because they are
 * distributed about it, and the half angle as their mean angle to it.
 *
 * A cylinder is the degenerate case with its apex at infinity, and the system
 * says so by becoming ill conditioned; that is refused here rather than fitted
 * to something huge and nearly parallel.
 */
std::shared_ptr<Surface> fitCone(const Mesh& mesh, const SmoothRegion& region, double tol)
{
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<Vector3d>& normals = *mesh.normals;
  if (region.facets.size() < 4) return nullptr;

  // n . A = n . P, weighted by area so a swarm of slivers cannot outvote the
  // shape, exactly as the cylinder's axis is weighted.
  Eigen::Matrix3d ata = Eigen::Matrix3d::Zero();
  Vector3d atb = Vector3d::Zero();
  std::set<int> verts;
  double total_area = 0;
  for (const std::size_t f : region.facets) {
    const std::vector<int>& loop = loops[f];
    if (loop.size() < 3) continue;
    for (const int v : loop) verts.insert(v);
    double area = 0;
    for (std::size_t i = 1; i + 1 < loop.size(); i++) {
      area +=
        (vertices[loop[i]] - vertices[loop[0]]).cross(vertices[loop[i + 1]] - vertices[loop[0]]).norm() /
        2;
    }
    if (!(area > 0)) continue;
    total_area += area;
    Vector3d centroid = Vector3d::Zero();
    for (const int v : loop) centroid += vertices[v];
    centroid /= double(loop.size());
    const Vector3d n = normals[f].normalized();
    ata += area * n * n.transpose();
    atb += area * n * n.dot(centroid);
  }
  if (verts.size() < 6 || !(total_area > 0)) return nullptr;

  // Ill conditioned means the apex is nowhere in particular - the normals span
  // too little, or they are all perpendicular to one direction, which is a
  // cylinder and fitCylinder's business.
  Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(ata);
  if (solver.info() != Eigen::Success) return nullptr;
  const double lo = solver.eigenvalues()[0], hi = solver.eigenvalues()[2];
  if (!(hi > 0) || lo < 1e-6 * hi) return nullptr;

  const Vector3d apex = ata.inverse() * atb;

  // The axis is the mean ruling. They are spread about it, so nothing else in
  // their average survives.
  Vector3d axis = Vector3d::Zero();
  for (const int v : verts) {
    const Vector3d ray = vertices[v] - apex;
    if (ray.norm() > 1e-12) axis += ray.normalized();
  }
  if (axis.norm() < 1e-6) return nullptr;  // rulings cancel: a full double cone
  axis.normalize();

  // The half angle, and the check that there is one - a spread of angles is a
  // region that is not a cone however well its apex solved.
  double sum_angle = 0, worst_angle = 0;
  std::size_t n_rays = 0;
  for (const int v : verts) {
    const Vector3d ray = vertices[v] - apex;
    if (ray.norm() < 1e-12) continue;
    const double a = acos(std::clamp(ray.normalized().dot(axis), -1.0, 1.0));
    sum_angle += a;
    n_rays++;
  }
  if (n_rays < 6) return nullptr;
  const double alpha = sum_angle / double(n_rays);
  if (!(alpha > 1e-4) || alpha > M_PI / 2 - 1e-4) return nullptr;
  for (const int v : verts) {
    const Vector3d ray = vertices[v] - apex;
    if (ray.norm() < 1e-12) continue;
    worst_angle =
      std::max(worst_angle, fabs(acos(std::clamp(ray.normalized().dot(axis), -1.0, 1.0)) - alpha));
  }

  // Refuse a taper so slight that a cylinder describes it: that is a better
  // surface for the same facets and fitCylinder has already had its turn.
  const double slope = tan(alpha);
  if (slope < 1e-4) return nullptr;

  // Every vertex on it, measured as a distance rather than as an angle.
  double along = 0;
  for (const int v : verts) along += (vertices[v] - apex).dot(axis);
  along /= double(verts.size());
  if (!(along > 0)) return nullptr;
  for (const int v : verts) {
    const Vector3d rel = vertices[v] - apex;
    const double t = rel.dot(axis);
    if (!(t > 0)) return nullptr;  // on the other nappe, or past the apex
    const double radial = (rel - axis * t).norm();
    if (fabs(radial - t * slope) * cos(alpha) > tol) return nullptr;
  }
  (void)worst_angle;

  const double r = along * slope;
  if (!(r > tol)) return nullptr;
  return std::make_shared<ConeSurface>(apex + axis * along, axis, r, slope);
}

std::shared_ptr<Surface> gridFromRegion(const Mesh& mesh, const SmoothRegion& region, double tol,
                                        const char **why)
{
  auto refuse = [&](const char *reason) -> std::shared_ptr<Surface> {
    if (why != nullptr) *why = reason;
    return nullptr;
  };
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  if (region.facets.size() < 2) return refuse("it is a single facet");

  // Recover the quads. A generator's sweep is a quad grid; whether it reaches
  // here as quads or as pairs of triangles depends on what merged the facets,
  // so both are accepted.
  //
  // Which edge between two triangles is the diagonal is the whole difficulty,
  // and the obvious answer is wrong. "The longest edge of both" is the
  // hypotenuse rule, and it holds for a grid of rectangles and fails for a grid
  // of skewed parallelograms - a twisted extrusion's walls, where the short
  // diagonal is shorter than the sides, and where near the boundary a side can
  // be the longest edge a triangle has. Both failures were measured on
  // step-approximate-report before this was written the second time.
  //
  // What does hold is that a swept grid's quads are nearly parallelograms. So
  // every interior edge is scored by how close the quad across it would come to
  // one, and the pairing is taken greedily from the best score down. That is a
  // heuristic, and it does not have to be right: the layout below either comes
  // out a rectangle of coordinates or it does not, so a wrong pairing is
  // refused rather than believed.
  std::map<EdgeKey, std::vector<std::size_t>> users;
  for (const std::size_t f : region.facets) {
    const std::vector<int>& loop = loops[f];
    for (std::size_t i = 0; i < loop.size(); i++) {
      users[edgeKey(loop[i], loop[(i + 1) % loop.size()])].push_back(f);
    }
  }

  std::vector<std::vector<int>> quads;
  std::set<std::size_t> paired;
  std::set<std::size_t> triangles;
  for (const std::size_t f : region.facets) {
    const std::vector<int>& loop = loops[f];
    if (loop.size() == 4) {
      quads.push_back(loop);
      paired.insert(f);
      continue;
    }
    if (loop.size() != 3) {
      return refuse("it has a facet that is neither a triangle nor a quad");
    }
    triangles.insert(f);
  }

  struct Candidate {
    double score;
    std::size_t a, b;
    std::vector<int> quad;
  };
  std::vector<Candidate> candidates;
  for (const auto& entry : users) {
    if (entry.second.size() != 2) continue;
    const std::size_t f = entry.second[0], g = entry.second[1];
    if (!triangles.count(f) || !triangles.count(g)) continue;
    auto opposite = [&](std::size_t face) {
      for (const int v : loops[face]) {
        if (v != entry.first.first && v != entry.first.second) return v;
      }
      return -1;
    };
    const int a = opposite(f), b = opposite(g);
    if (a < 0 || b < 0) continue;
    // The quad in cyclic order, and the order has to come from the mesh rather
    // than from the edge key. The key is sorted by vertex index, so taking it as
    // written gives each quad an arbitrary handedness, and the layout below -
    // which turns the same way at every step - then disagrees with itself
    // wherever two neighbours were written opposite ways round. That is what it
    // did on every wall of step-approximate-report.
    //
    // The shared edge is interior to the quad, so the quad's own boundary is
    // the four outer edges: with f running u->v, they are v->a, a->u from f and
    // u->b, b->v from g.
    int u = entry.first.first, v = entry.first.second;
    bool found = false;
    for (std::size_t i = 0; i < loops[f].size() && !found; i++) {
      const int x = loops[f][i], y = loops[f][(i + 1) % loops[f].size()];
      if (x == u && y == v) found = true;
      else if (x == v && y == u) {
        std::swap(u, v);
        found = true;
      }
    }
    if (!found) continue;
    const Vector3d p0 = vertices[v], p1 = vertices[a];
    const Vector3d p2 = vertices[u], p3 = vertices[b];
    const double perimeter = (p1 - p0).norm() + (p2 - p1).norm() + (p3 - p2).norm() + (p0 - p3).norm();
    if (!(perimeter > 0)) continue;
    const double skew = ((p1 - p0) - (p2 - p3)).norm() + ((p2 - p1) - (p3 - p0)).norm();
    candidates.push_back({skew / perimeter, f, g, {v, a, u, b}});
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& x, const Candidate& y) { return x.score < y.score; });

  // Greedy from the best score down, and then repaired. Greedy alone strands
  // triangles - it did, on every wall of step-approximate-report - because
  // taking the best pair available can leave a neighbour with none, and on a
  // grid where every quad scores about the same the order is close to
  // arbitrary. The repair is the standard one: walk an augmenting path from
  // each stranded triangle, breaking and remaking pairs along it.
  std::map<std::size_t, std::vector<std::size_t>> neighbours;
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> quad_of;
  for (std::size_t c = 0; c < candidates.size(); c++) {
    neighbours[candidates[c].a].push_back(candidates[c].b);
    neighbours[candidates[c].b].push_back(candidates[c].a);
    quad_of[{candidates[c].a, candidates[c].b}] = c;
    quad_of[{candidates[c].b, candidates[c].a}] = c;
  }
  std::map<std::size_t, std::size_t> partner;
  for (const auto& candidate : candidates) {
    if (partner.count(candidate.a) || partner.count(candidate.b)) continue;
    partner[candidate.a] = candidate.b;
    partner[candidate.b] = candidate.a;
  }
  // The repair is a heuristic, not a maximum matching. Triangle adjacency is not
  // bipartite in general, and a plain augmenting path without blossom
  // contraction can miss a path that exists around an odd cycle. That costs a
  // quad, never a wrong one: a triangle left unpaired refuses the grid and the
  // region goes out faceted.
  //
  // The depth limit is the part that matters. Each level inserts a triangle into
  // `seen` before recursing, so depth is bounded only by the number of triangles
  // in the region - tens of thousands on a large sweep, one stack frame each.
  // Running out of stack would take the process down; giving up returns the same
  // faceted fallback an unpairable triangle already produces.
  const std::size_t augment_max_depth = 4096;
  std::function<bool(std::size_t, std::set<std::size_t>&, std::size_t)> augment =
    [&](std::size_t u, std::set<std::size_t>& seen, std::size_t depth) {
      if (depth >= augment_max_depth) return false;
      for (const std::size_t v : neighbours[u]) {
        if (seen.count(v)) continue;
        seen.insert(v);
        const auto held = partner.find(v);
        if (held == partner.end()) {
          partner[v] = u;
          partner[u] = v;
          return true;
        }
        const std::size_t displaced = held->second;
        partner.erase(displaced);
        partner.erase(v);
        if (augment(displaced, seen, depth + 1)) {
          partner[v] = u;
          partner[u] = v;
          return true;
        }
        partner[v] = displaced;
        partner[displaced] = v;
      }
      return false;
    };
  for (const std::size_t f : triangles) {
    if (partner.count(f)) continue;
    std::set<std::size_t> seen{f};
    if (!augment(f, seen, 0)) return refuse("a triangle pairs with no neighbour into a quad");
  }
  for (const auto& entry : partner) {
    if (entry.first > entry.second) continue;  // once per pair
    const auto it = quad_of.find({entry.first, entry.second});
    if (it == quad_of.end()) return refuse("a pairing has no quad behind it");
    quads.push_back(candidates[it->second].quad);
    paired.insert(entry.first);
    paired.insert(entry.second);
  }
  if (quads.empty()) return refuse("no quads could be recovered");

  // Lay the quads out on integer coordinates, by flood fill from one of them.
  // A quad sharing an edge with a placed quad has two of its corners placed
  // already, and those two decide where the other two go; a grid that really is
  // one comes out as a rectangle of coordinates with nothing on top of anything
  // else, and one that is not disagrees with itself and is refused here.
  std::map<EdgeKey, std::vector<std::size_t>> quad_users;
  for (std::size_t q = 0; q < quads.size(); q++) {
    for (std::size_t i = 0; i < 4; i++) {
      quad_users[edgeKey(quads[q][i], quads[q][(i + 1) % 4])].push_back(q);
    }
  }

  std::map<int, std::pair<int, int>> coord;
  std::vector<char> placed(quads.size(), 0);
  auto place = [&](int v, int i, int j) {
    const auto it = coord.find(v);
    if (it == coord.end()) {
      coord[v] = {i, j};
      return true;
    }
    return it->second.first == i && it->second.second == j;
  };
  {
    const std::vector<int>& seed = quads[0];
    coord[seed[0]] = {0, 0};
    coord[seed[1]] = {1, 0};
    coord[seed[2]] = {1, 1};
    coord[seed[3]] = {0, 1};
    placed[0] = 1;
  }
  for (bool moved = true; moved;) {
    moved = false;
    for (std::size_t q = 0; q < quads.size(); q++) {
      if (placed[q]) continue;
      const std::vector<int>& quad = quads[q];
      int known = -1;
      for (int i = 0; i < 4; i++) {
        if (coord.count(quad[i]) && coord.count(quad[(i + 1) % 4])) {
          known = i;
          break;
        }
      }
      if (known < 0) continue;
      // The placed edge runs from `known` to `known+1`; the quad continues on
      // the far side of it, so the other two corners are that edge translated
      // by the perpendicular step.
      const std::pair<int, int> a = coord[quad[known]];
      const std::pair<int, int> b = coord[quad[(known + 1) % 4]];
      const int di = b.first - a.first, dj = b.second - a.second;
      if (abs(di) + abs(dj) != 1) return refuse("the quads do not step by one, so they are not a grid");
      const int pi = -dj, pj = di;  // turn left, into the new quad
      if (!place(quad[(known + 2) % 4], b.first + pi, b.second + pj)) {
        return refuse("the layout disagrees with itself, so the region folds or wraps");
      }
      if (!place(quad[(known + 3) % 4], a.first + pi, a.second + pj)) {
        return refuse("the layout disagrees with itself, so the region folds or wraps");
      }
      placed[q] = 1;
      moved = true;
    }
  }
  for (const char c : placed) {
    if (!c) return refuse("the quads are not one connected sheet");
  }

  int lo_i = 0, hi_i = 0, lo_j = 0, hi_j = 0;
  bool first = true;
  for (const auto& entry : coord) {
    const int i = entry.second.first, j = entry.second.second;
    lo_i = first ? i : std::min(lo_i, i);
    hi_i = first ? i : std::max(hi_i, i);
    lo_j = first ? j : std::min(lo_j, j);
    hi_j = first ? j : std::max(hi_j, j);
    first = false;
  }
  const int rows = hi_i - lo_i + 1, cols = hi_j - lo_j + 1;
  if (rows < 2 || cols < 2) return refuse("the recovered grid is one row or one column");
  if (std::size_t(rows) * cols != coord.size())
    return refuse("the recovered grid is not a full rectangle");

  std::vector<Vector3d> net(std::size_t(rows) * cols, Vector3d::Zero());
  std::vector<char> filled(std::size_t(rows) * cols, 0);
  for (const auto& entry : coord) {
    const std::size_t at = std::size_t(entry.second.first - lo_i) * cols + (entry.second.second - lo_j);
    net[at] = vertices[entry.first];
    filled[at] = 1;
  }
  for (const char c : filled) {
    if (!c) return refuse("the recovered grid has a gap in it");
  }

  // The grid is only worth declaring if the surface it describes passes through
  // the points it was built from, which is what GridSurface's own interpolation
  // decides. Cubic along the sweep needs four stations; below that the fit is
  // the mesh again and buys nothing.
  if (rows < 4) return refuse("the sweep has fewer than four stations, so there is no cubic to fit");
  auto grid = std::make_shared<GridSurface>(rows, cols, net, false);

  // One guard, and it matters more than it looks. The band is not only a
  // measurement here - it is the tolerance membership is then answered at - so
  // a wild recovery would come with a wild band and admit everything. Against
  // what, though, has to be a property of the grid itself rather than of the
  // caller's bookkeeping: a surface that bows by a quarter of a cell is not
  // describing this tessellation, whatever the region record happens to say.
  double scale = 0;
  int cells = 0;
  for (int i = 0; i + 1 < rows; i++) {
    for (int j = 0; j + 1 < cols; j++) {
      scale += (grid->at(i + 1, j + 1) - grid->at(i, j)).norm();
      cells++;
    }
  }
  if (cells == 0 || !(scale > 0)) return refuse("the recovered grid has no extent");
  scale /= cells;
  if (grid->tessellationBand() > 0.25 * scale + tol) {
    return refuse("the interpolant bows by more than a quarter of a cell");
  }
  return grid;
}

std::vector<SmoothRegion> uncoveredRegions(const Mesh& mesh, const std::vector<char>& consumed,
                                           double smooth_angle)
{
  const std::vector<Vector3d>& verts = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;
  const std::vector<Vector3d>& normals = *mesh.normals;

  std::vector<char> eligible(loops.size(), 0);
  for (std::size_t i = 0; i < loops.size(); i++) {
    eligible[i] = valid[i] && !is_hole[i] && (i >= consumed.size() || !consumed[i]);
  }

  // edge -> the eligible facets using it
  std::map<EdgeKey, std::vector<std::size_t>> users;
  for (std::size_t i = 0; i < loops.size(); i++) {
    if (!eligible[i]) continue;
    const std::vector<int>& loop = loops[i];
    for (std::size_t j = 0; j < loop.size(); j++) {
      users[edgeKey(loop[j], loop[(j + 1) % loop.size()])].push_back(i);
    }
  }

  std::vector<SmoothRegion> out;
  std::vector<char> seen(loops.size(), 0);
  for (std::size_t seed = 0; seed < loops.size(); seed++) {
    if (!eligible[seed] || seen[seed]) continue;
    SmoothRegion region;
    std::vector<double> bands;
    std::vector<std::size_t> stack{seed};
    seen[seed] = 1;
    while (!stack.empty()) {
      const std::size_t f = stack.back();
      stack.pop_back();
      region.facets.push_back(f);
      const std::vector<int>& loop = loops[f];
      for (std::size_t j = 0; j < loop.size(); j++) {
        const int a = loop[j], b = loop[(j + 1) % loop.size()];
        const auto it = users.find(edgeKey(a, b));
        if (it == users.end()) continue;
        for (const std::size_t g : it->second) {
          if (g == f) continue;
          const double dot = std::clamp(normals[f].dot(normals[g]), -1.0, 1.0);
          const double dihedral = acos(dot);
          if (dihedral > smooth_angle) continue;
          // The band this edge leaves open: the sagitta of a circular cross
          // section through the two facets. Measured whether or not the
          // neighbour is new, because a region's band is a property of all its
          // interior edges.
          const double reach =
            std::max(reachAcross(verts, loops[f], a, b), reachAcross(verts, loops[g], a, b));
          region.worst_dihedral = std::max(region.worst_dihedral, dihedral);
          const double sagitta = (reach / 2.0) * tan(dihedral / 4.0);
          region.band = std::max(region.band, sagitta);
          bands.push_back(sagitta);
          if (seen[g]) continue;
          seen[g] = 1;
          stack.push_back(g);
        }
      }
    }
    for (const std::size_t f : region.facets) {
      const std::vector<int>& loop = loops[f];
      Vector3d acc = Vector3d::Zero();
      for (std::size_t j = 1; j + 1 < loop.size(); j++) {
        acc += (verts[loop[j]] - verts[loop[0]]).cross(verts[loop[j + 1]] - verts[loop[0]]);
      }
      region.area += acc.norm() / 2.0;
    }
    // The grid, or what is left of it. An interior vertex is one none of whose
    // incident edges is on the region's boundary; its valence is how many of
    // the region's facets meet there.
    {
      std::map<EdgeKey, int> edge_use;
      std::map<int, int> valence;
      for (const std::size_t f : region.facets) {
        const std::vector<int>& loop = loops[f];
        for (std::size_t j = 0; j < loop.size(); j++) {
          edge_use[edgeKey(loop[j], loop[(j + 1) % loop.size()])]++;
          valence[loop[j]]++;
        }
      }
      std::set<int> on_boundary;
      for (const auto& entry : edge_use) {
        if (entry.second == 1) {
          on_boundary.insert(entry.first.first);
          on_boundary.insert(entry.first.second);
        }
      }
      std::map<int, int> histogram;
      for (const auto& entry : valence) {
        if (on_boundary.count(entry.first) == 0) histogram[entry.second]++;
      }
      std::size_t total = 0, best = 0;
      for (const auto& entry : histogram) {
        total += entry.second;
        if (std::size_t(entry.second) > best) {
          best = entry.second;
          region.modal_valence = entry.first;
        }
      }
      region.interior_vertices = total;
      region.regularity = total ? double(best) / double(total) : 0.0;
    }
    if (!bands.empty()) {
      std::nth_element(bands.begin(), bands.begin() + bands.size() / 2, bands.end());
      region.median_band = bands[bands.size() / 2];
    }
    out.push_back(std::move(region));
  }
  std::sort(out.begin(), out.end(),
            [](const SmoothRegion& a, const SmoothRegion& b) { return a.area > b.area; });
  return out;
}

std::vector<std::shared_ptr<Surface>> fitRevolved(const Mesh& mesh, const SmoothRegion& region,
                                                  double tol, const char **why)
{
  auto refuse = [&](const char *reason) {
    if (why != nullptr) *why = reason;
    return std::vector<std::shared_ptr<Surface>>();
  };
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  if (region.facets.size() < 3) return refuse("it is too small to be a surface of revolution");

  // The axis, which is the whole difficulty and has no single source.
  //
  // Two closed-form fits were tried and both are degenerate on the shapes that
  // matter. The rims give it directly - a frustum's two, a sphere's two cap
  // circles - and a sphere's cap meets the band beside it at the angle of one
  // ring, 11 degrees on a 32x16 sphere, so the caps join the region and it has
  // no boundary at all. A screw-axis fit over the facet normals, solving
  // n . (a x (c - p)) = 0 as a null space, is degenerate the other way: a cone
  // and a sphere both have every normal line through one point, and the null
  // space comes out three dimensional rather than one. Measured, not assumed.
  //
  // So the axis is *proposed* and then verified. Every source of a plausible
  // direction contributes a candidate, and the ring test below - every vertex
  // on the circle its height puts it on - is what accepts one. That test is
  // strict enough that a wrong candidate cannot survive it, which is what lets
  // the proposals be as rough as they like.
  const std::vector<Vector3d>& normals = *mesh.normals;
  Vector3d anchor = Vector3d::Zero();
  double anchor_n = 0;
  std::set<int> region_verts;
  for (const std::size_t f : region.facets) {
    for (const int v : loops[f]) {
      if (!region_verts.insert(v).second) continue;
      anchor += vertices[v];
      anchor_n += 1;
    }
  }
  if (!(anchor_n > 2)) return refuse("it has too few vertices");
  anchor /= anchor_n;

  std::vector<Vector3d> candidates;
  auto propose = [&](const Vector3d& dir) {
    if (dir.norm() < 1e-9) return;
    const Vector3d unit = dir.normalized();
    for (const auto& had : candidates) {
      if (fabs(fabs(had.dot(unit)) - 1.0) < 1e-6) return;
    }
    candidates.push_back(unit);
  };

  // A rim, where the region has one. This is the frustum's case and the
  // cheapest to trust: the plane of a boundary circle is perpendicular to the
  // axis by construction.
  std::vector<std::vector<int>> cycles;
  if (boundaryCycles(loops, region.facets, cycles) == nullptr) {
    for (const auto& cycle : cycles) {
      if (cycle.size() < 3) continue;
      Vector3d centre = Vector3d::Zero();
      for (const int v : cycle) centre += vertices[v];
      centre /= double(cycle.size());
      Vector3d area = Vector3d::Zero();
      for (std::size_t i = 0; i < cycle.size(); i++) {
        area += (vertices[cycle[i]] - centre).cross(vertices[cycle[(i + 1) % cycle.size()]] - centre);
      }
      propose(area);
    }
  }

  // A cap that joined the region. A turned surface is tessellated into quads,
  // so a face with more corners than that is the polygon closing one end, and
  // its normal is the axis.
  for (const std::size_t f : region.facets) {
    if (loops[f].size() > 4) propose(normals[f]);
  }

  // And the apex, for a cone. Every tangent plane of a cone contains its apex,
  // which is a linear least squares over the facet planes; the rulings from
  // there make a constant angle with the axis, so their mean direction is it.
  {
    Eigen::Matrix3d nn = Eigen::Matrix3d::Zero();
    Vector3d nb = Vector3d::Zero();
    for (const std::size_t f : region.facets) {
      const std::vector<int>& loop = loops[f];
      if (loop.empty()) continue;
      const Vector3d n = normals[f].normalized();
      nn += n * n.transpose();
      nb += n * n.dot(vertices[loop[0]]);
    }
    if (fabs(nn.determinant()) > 1e-12) {
      const Vector3d apex = nn.inverse() * nb;
      Vector3d mean = Vector3d::Zero();
      for (const int v : region_verts) {
        const Vector3d rel = vertices[v] - apex;
        if (rel.norm() > tol) mean += rel.normalized();
      }
      propose(mean);
    }
  }
  if (candidates.empty()) return refuse("nothing proposes an axis for it");

  // Each candidate in turn, accepted only by the ring test.
  const char *last = "no candidate axis described a turned surface";
  auto tryAxis = [&](const Vector3d& axis) {
    auto give_up = [&](const char *reason) {
      last = reason;
      return std::vector<std::shared_ptr<Surface>>();
    };
    // Which ring each vertex belongs to: its height along the axis. A frustum has
    // only the two rims; a sphere has one ring per row of its tessellation, and
    // every one of them has to be declared or the bands between them are not
    // bounded by anything the recogniser can match.
    // Keyed by quantised height, but carrying the true mean height as well. The
    // key is only how vertices are grouped; using it as the height in its own
    // right rounds every ring to the quantum, which a cylinder does not care
    // about - it is infinite along its axis - and which defeats the sphere test
    // below outright, since that measures heights against a radius.
    struct Ring {
      double radius = 0, height = 0;
      int count = 0;
    };
    std::map<long long, Ring> rings;
    const double quantum = std::max(tol, 1e-9) * 100;
    std::set<int> seen;
    for (const std::size_t f : region.facets) {
      for (const int v : loops[f]) {
        if (!seen.insert(v).second) continue;
        const Vector3d rel = vertices[v] - anchor;
        const double along = rel.dot(axis);
        Ring& ring = rings[(long long)llround(along / quantum)];
        ring.radius += (rel - axis * along).norm();
        ring.height += along;
        ring.count += 1;
      }
    }
    if (rings.size() < 2) return give_up("its vertices do not lie on rings");

    // Every vertex on the ring its height puts it in, which is what makes this a
    // surface of revolution rather than a swarm that happens to have two round
    // ends.
    for (const std::size_t f : region.facets) {
      for (const int v : loops[f]) {
        const Vector3d rel = vertices[v] - anchor;
        const double along = rel.dot(axis);
        const Ring& ring = rings[(long long)llround(along / quantum)];
        const double radius = ring.radius / ring.count;
        if (fabs((rel - axis * along).norm() - radius) > tol) {
          return give_up("a vertex is off the ring its height puts it on");
        }
      }
    }

    std::vector<std::shared_ptr<Surface>> found;
    for (const auto& entry : rings) {
      const double radius = entry.second.radius / entry.second.count;
      if (!(radius > tol)) continue;  // an apex is not a rim; there is no circle there
      const Vector3d at = anchor + axis * (entry.second.height / entry.second.count);
      found.push_back(std::make_shared<CylinderSurface>(at, axis, radius));
    }
    if (found.size() < 2) return give_up("fewer than two rings carry a circle");

    // And the sphere the rings may lie on, which is worth one more test because
    // of what the band pass does with it. The rings alone already collapse a
    // tessellated sphere into a stack of exact cones - that is how a declared
    // sphere collapses too - but a SphereSurface among the declarations lets the
    // zone pass absorb that whole stack into one SPHERICAL_SURFACE. On a 32x16
    // sphere it is the difference between fifteen faces and one.
    //
    // For a ring at height h and radius r on a sphere centred at t along the
    // axis, (h - t)^2 + r^2 is the same for every ring, which is linear in t.
    if (rings.size() >= 3) {
      std::vector<std::pair<double, double>> hr;
      for (const auto& entry : rings) {
        hr.emplace_back(entry.second.height / entry.second.count,
                        entry.second.radius / entry.second.count);
      }
      const double h0 = hr.front().first, r0 = hr.front().second;
      const double h1 = hr.back().first, r1 = hr.back().second;
      if (fabs(h0 - h1) > tol) {
        const double t = (h0 * h0 + r0 * r0 - h1 * h1 - r1 * r1) / (2 * (h0 - h1));
        const double rsq = (h0 - t) * (h0 - t) + r0 * r0;
        if (rsq > 0) {
          const double radius = sqrt(rsq);
          bool spherical = true;
          for (const auto& entry : hr) {
            const double want = (entry.first - t) * (entry.first - t) + entry.second * entry.second;
            spherical = spherical && fabs(sqrt(want) - radius) <= tol;
          }
          if (spherical) {
            found.push_back(std::make_shared<SphereSurface>(anchor + axis * t, axis, radius));
          }
        }
      }
    }
    return found;
  };

  for (const auto& axis : candidates) {
    std::vector<std::shared_ptr<Surface>> found = tryAxis(axis);
    if (!found.empty()) return found;
  }
  return refuse(last);
}

}  // namespace AnalyticFeatures
