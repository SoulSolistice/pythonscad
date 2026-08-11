/*Copyright(c) 2018, slugdev
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met :
1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
3. All advertising materials mentioning features or use of this software
must display the following acknowledgement :
This product includes software developed by slugdev.
4. Neither the name of the slugdev nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY SLUGDEV ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.IN NO EVENT SHALL SLUGDEV BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.*/

#include "StepKernel.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include <map>
#include <utility>
#include <array>
#include <set>
#include <iomanip>  // put_time
StepKernel::StepKernel()
{
}

StepKernel::~StepKernel()
{
  // every entity registers itself in `entities` in its constructor, so this
  // frees exactly the entities allocated by this kernel, each of them once
  for (auto *entity : entities) delete entity;
  entities.clear();
}

namespace {

// Newell's method. In contrast to the cross product of the first two edges it
// is stable for concave corners (where the cross product points the wrong way)
// and for polygons whose first three vertices happen to be collinear (where
// the cross product collapses to zero). The magnitude is twice the area.
Vector3d polygonNormal(const std::vector<Vector3d>& vertices, const std::vector<int>& poly)
{
  Vector3d norm(0, 0, 0);
  const size_t n = poly.size();
  for (size_t i = 0; i < n; i++) {
    const Vector3d& a = vertices[poly[i]];
    const Vector3d& b = vertices[poly[(i + 1) % n]];
    norm[0] += (a[1] - b[1]) * (a[2] + b[2]);
    norm[1] += (a[2] - b[2]) * (a[0] + b[0]);
    norm[2] += (a[0] - b[0]) * (a[1] + b[1]);
  }
  return norm;
}

// An arbitrary unit vector perpendicular to norm
Vector3d perpendicular(const Vector3d& norm)
{
  const Vector3d axis = fabs(norm[0]) < 0.9 ? Vector3d(1, 0, 0) : Vector3d(0, 1, 0);
  return norm.cross(axis).normalized();
}

// The axis the normal points along most; dropping it projects the plane onto
// the remaining two coordinates without ever collapsing it.
int dominantAxis(const Vector3d& norm)
{
  const double ax = fabs(norm[0]), ay = fabs(norm[1]), az = fabs(norm[2]);
  if (ax >= ay && ax >= az) return 0;
  return ay >= az ? 1 : 2;
}

std::array<double, 2> projectPoint(const Vector3d& pt, int drop)
{
  return {pt[(drop + 1) % 3], pt[(drop + 2) % 3]};
}

void projectLoop(const std::vector<Vector3d>& vertices, const std::vector<int>& loop, int drop,
                 std::vector<std::array<double, 2>>& out)
{
  out.clear();
  out.reserve(loop.size());
  for (const int ind : loop) out.push_back(projectPoint(vertices[ind], drop));
}

double loopArea2d(const std::vector<std::array<double, 2>>& poly)
{
  double area = 0;
  for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    area += (poly[j][0] + poly[i][0]) * (poly[j][1] - poly[i][1]);
  }
  return fabs(area) * 0.5;
}

// Crossing number test. The half open comparison on the y coordinate makes a
// ray which passes exactly through a vertex count once instead of twice, which
// is what the 3D ray cast in mergeTriangles() gets wrong on concentric loops.
bool pointInLoop2d(const std::vector<std::array<double, 2>>& poly, const std::array<double, 2>& pt)
{
  bool inside = false;
  for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    if ((poly[i][1] > pt[1]) != (poly[j][1] > pt[1])) {
      const double x =
        (poly[j][0] - poly[i][0]) * (pt[1] - poly[i][1]) / (poly[j][1] - poly[i][1]) + poly[i][0];
      if (pt[0] < x) inside = !inside;
    }
  }
  return inside;
}

/*! A ring of facets recognised as one cylinder.
 *
 * `walls` are the loops the ring is made of, which are dropped in favour of a
 * single CYLINDRICAL_SURFACE face. `bottom_loop`/`top_loop` are the loops of
 * the neighbouring faces that bound the same rims - a disc or the hole of an
 * annulus - and their N straight edges collapse into one CIRCLE each. */
struct CylinderRing {
  std::vector<std::size_t> walls;
  Vector3d axis, base;  // base is the centre of the bottom rim
  double radius = 0;
  double height = 0;
  std::size_t bottom_loop = 0, top_loop = 0;
  bool bottom_ccw = false, top_ccw = false;  // rim traversal, right handed about axis
  bool outward = false;                      // wall normals point away from the axis
  int seam_bottom = -1, seam_top = -1;       // the vertices the seam runs between
};

/*! A band of facets recognised as part of one cylinder, cut short of a full
 * turn - a wall interrupted by a channel, a slot or a lug.
 *
 * Unlike a full ring it is not periodic, so it needs no seam: the face is
 * bounded by an arc at either rim and the band's two end edges. Each arc
 * replaces a *run* of edges inside the neighbouring face's loop rather than the
 * whole loop, which is the only structural difference to CylinderRing. */
struct CylinderArc {
  std::vector<std::size_t> walls;
  Vector3d axis, base;  // base is the centre of the bottom rim
  double radius = 0;
  double height = 0;
  bool outward = false;  // wall normals point away from the axis
  std::size_t bottom_loop = 0, top_loop = 0;
  // the run of edges each rim occupies in its neighbour, as a start index into
  // that loop and a number of edges
  std::size_t bottom_start = 0, bottom_count = 0;
  std::size_t top_start = 0, top_count = 0;
};

/*! One run of loop edges replaced by a single arc. */
struct ArcSubstitution {
  std::size_t start = 0, count = 0;
  StepKernel::EdgeCurve *edge = nullptr;
  bool sense = true;  // orientation for this loop's own traversal
};

/*! Least squares circle centre for points known to lie on a circle about
 * `axis`, returned projected onto the plane at `level`.
 *
 * Averaging is not good enough here. The centroid of a *full* rim lies on the
 * axis, which is why the closed ring case can get away with it, but the
 * centroid of an arc sits inside the chord and taking it for the centre puts
 * the axis somewhere else entirely - on a 54 degree arc of a radius 78 wall it
 * came out at radius 266, and the wall was rejected as not fitting itself.
 *
 * Kasa's linearisation (x^2 + y^2 = 2ax + 2by + c) is exact when the points
 * really are concyclic, and every caller re-measures the residual afterwards,
 * so an inexact fit is caught rather than trusted. */
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

/*! The vertex of `level` which sits directly above `v` along `axis`. */
int vertexAbove(int v, const std::vector<int>& level, const Vector3d& axis, double tol,
                const std::vector<Vector3d>& vertices)
{
  for (const int candidate : level) {
    const Vector3d rel = vertices[candidate] - vertices[v];
    if ((rel - axis * axis.dot(rel)).norm() < tol) return candidate;
  }
  return -1;
}

/*! Signed rotation of a loop about an axis: positive when it runs counter
 * clockwise in the right handed sense. */
double loopTurnDirection(const std::vector<Vector3d>& vertices, const std::vector<int>& loop,
                         const Vector3d& axis, const Vector3d& centre)
{
  double total = 0;
  const std::size_t n = loop.size();
  for (std::size_t i = 0; i < n; i++) {
    const Vector3d a = vertices[loop[i]] - centre;
    const Vector3d b = vertices[loop[(i + 1) % n]] - centre;
    total += axis.dot(a.cross(b));
  }
  return total;
}

/*! Distance from a point to the line through `base` along `axis`. */
double distanceToAxis(const Vector3d& pt, const Vector3d& base, const Vector3d& axis)
{
  const Vector3d rel = pt - base;
  return (rel - axis * axis.dot(rel)).norm();
}

int uf_find(std::vector<int>& parent, int x)
{
  while (parent[x] != x) {
    parent[x] = parent[parent[x]];
    x = parent[x];
  }
  return x;
}

}  // namespace

StepKernel::EdgeCurve *StepKernel::create_line_edge_curve(StepKernel::Vertex *vert1,
                                                          StepKernel::Vertex *vert2, bool dir)
{
  // curve 1
  auto line_point1 = new Point(entities, vert1->point->pt);
  Vector3d v = vert2->point->pt - vert1->point->pt;
  const double len = v.norm();
  // A DIRECTION must have a non zero magnitude. Without this guard a zero
  // length edge is written as DIRECTION('',(0.,0.,0.)), which importers report
  // as a degenerated face.
  if (len < 1e-12) v = Vector3d(1, 0, 0);
  else v /= len;

  auto line_dir1 = new Direction(entities, v);
  auto line_vector1 = new Vector(entities, line_dir1, len > 1e-12 ? len : 1.0);
  auto line1 = new Line(entities, line_point1, line_vector1);
  //  auto surf_curve1 = new SurfaceCurve(entities, line1);
  return new EdgeCurve(entities, vert1, vert2, line1, dir);
}

void StepKernel::build_tri_body(const char *name, const std::vector<Vector3d>& vertices,
                                const std::vector<IndexedFace>& faces,
                                const std::vector<std::shared_ptr<Curve>>& curves,
                                const std::vector<std::shared_ptr<Surface>>& surfaces,
                                const std::vector<int>& faceParents,
                                const std::vector<Vector4d>& faceNormals, double tol, bool analytic)
{
  // `curves` and `surfaces` carry the analytic geometry the model was built
  // from. Writing CIRCLE and CYLINDRICAL_SURFACE instead of the facets needs
  // them: a ring of N quads is exactly the mesh of an N sided prism, so the
  // facets alone never say which was meant. Everything is still written as
  // planes and lines; this only reports what arrived.
  (void)curves;
  if (!surfaces.empty()) {
    int cylinders = 0;
    for (const auto& surface : surfaces) {
      if (dynamic_cast<const CylinderSurface *>(surface.get()) != nullptr) cylinders++;
    }
    printf("STEP export: %d analytic surface%s available (%d cylindrical)\n", int(surfaces.size()),
           surfaces.size() == 1 ? "" : "s", cylinders);
  }

  const double model_tol = tol > 0 ? tol : 1e-5;
  // twice the area of the smallest polygon still considered a face
  const double area_eps = 1e-12;
  const std::size_t face_cnt = faces.size();

  // Vertices which share the exact same coordinates have to end up as one
  // single VERTEX_POINT. Otherwise every face brings its own copy of each
  // corner and adjacent faces are no longer stitched along their common edge,
  // which is what makes importers report gaps in the shell.
  std::map<std::tuple<double, double, double>, int> point_map;
  std::vector<int> canonical(vertices.size(), 0);
  for (std::size_t i = 0; i < vertices.size(); i++) {
    auto key = std::make_tuple(vertices[i][0], vertices[i][1], vertices[i][2]);
    auto it = point_map.find(key);
    if (it == point_map.end()) {
      point_map.emplace(key, int(i));
      canonical[i] = int(i);
    } else {
      canonical[i] = it->second;
    }
  }

  std::vector<Vertex *> step_verts(vertices.size(), nullptr);
  auto get_vertex = [&](int ind) {
    if (step_verts[ind] == nullptr) {
      auto point = new Point(entities, vertices[ind]);
      step_verts[ind] = new Vertex(entities, point);
    }
    return step_verts[ind];
  };

  // Clean up the loops and derive a usable plane for each of them.
  std::vector<std::vector<int>> loops(face_cnt);
  std::vector<Vector3d> loop_normals(face_cnt, Vector3d(0, 0, 0));
  std::vector<char> loop_valid(face_cnt, 0);
  std::vector<char> loop_is_hole(face_cnt, 0);
  std::vector<int> parents(face_cnt, -1);
  int degenerated_cnt = 0;

  for (std::size_t i = 0; i < face_cnt; i++) {
    std::vector<int> loop;
    for (std::size_t j = 0; j < faces[i].size(); j++) {
      const int ind = canonical[faces[i][j]];
      // repeated points produce zero length edges
      if (!loop.empty() && loop.back() == ind) continue;
      loop.push_back(ind);
    }
    while (loop.size() >= 2 && loop.front() == loop.back()) loop.pop_back();
    if (loop.size() < 3) {
      degenerated_cnt++;
      continue;
    }

    Vector3d norm = polygonNormal(vertices, loop);
    if (norm.norm() < area_eps) {
      // zero area polygon, exporting it would create a face without a usable
      // surface normal
      degenerated_cnt++;
      continue;
    }
    norm.normalize();

    // All triangles of a mergeTriangles() bucket face the same way, so a merged
    // loop which winds the other way round can only be the boundary of a hole -
    // it is never a valid outer loop.
    parents[i] = faceParents[i];
    if (i < faceNormals.size()) {
      const Vector3d ref = faceNormals[i].head<3>();
      if (ref.squaredNorm() > 0.5 && ref.dot(norm) < 0) loop_is_hole[i] = 1;
    }
    if (parents[i] != -1) loop_is_hole[i] = 1;

    loops[i] = loop;
    loop_normals[i] = norm;
    loop_valid[i] = 1;
  }

  // Work out which face each hole belongs to.
  //
  // mergeTriangles() records that in faceParents, but the search behind it is
  // not reliable in two ways. It keeps the last enclosing loop it happens to
  // find instead of the innermost one, so with concentric loops in one plane a
  // hole ends up on a face further out: the face it really belongs to is then
  // written without its hole and seals the bore, which is the membrane the CAD
  // system shows. And the containment test itself casts a ray through 3D space
  // using a normal taken from the first three vertices of the loop, which fails
  // outright on concentric circular loops (the ray runs exactly through a
  // vertex of the outer loop) and leaves the hole with no parent at all.
  //
  // So do not take faceParents at face value: project the coplanar loops and
  // pick the innermost one that encloses the hole.
  int reparented_cnt = 0, orphan_cnt = 0;
  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i] || !loop_is_hole[i]) continue;

    const int previous = parents[i];
    const int drop = dominantAxis(loop_normals[i]);
    const std::array<double, 2> probe = projectPoint(vertices[loops[i][0]], drop);
    std::vector<std::array<double, 2>> cand;
    int found = -1;
    double best_area = 0;

    for (std::size_t j = 0; j < face_cnt; j++) {
      if (j == i || !loop_valid[j] || loop_is_hole[j]) continue;
      // only loops of the same bucket, i.e. the same plane, can enclose it
      if (i >= faceNormals.size() || j >= faceNormals.size()) continue;
      if (faceNormals[i].head<3>().dot(faceNormals[j].head<3>()) < 0.9999) continue;
      if (fabs(faceNormals[i][3] - faceNormals[j][3]) > 1e-4) continue;

      projectLoop(vertices, loops[j], drop, cand);
      if (!pointInLoop2d(cand, probe)) continue;
      const double area = loopArea2d(cand);
      if (found == -1 || area < best_area) {
        found = int(j);
        best_area = area;
      }
    }

    if (found != -1) {
      parents[i] = found;
      if (found != previous) reparented_cnt++;
    } else if (previous != -1 && loop_valid[previous] && !loop_is_hole[previous]) {
      parents[i] = previous;  // keep what mergeTriangles found
    } else {
      // a reversed loop without an enclosing face cannot be exported as a face
      parents[i] = -1;
      loop_valid[i] = 0;
      orphan_cnt++;
    }
  }

  if (degenerated_cnt > 0) {
    printf("STEP export: skipped %d degenerated face%s\n", degenerated_cnt,
           degenerated_cnt == 1 ? "" : "s");
  }
  if (reparented_cnt > 0) {
    printf("STEP export: moved %d hole%s to the enclosing face\n", reparented_cnt,
           reparented_cnt == 1 ? "" : "s");
  }
  if (orphan_cnt > 0) {
    printf("STEP export: dropped %d reversed loop%s without an enclosing face\n", orphan_cnt,
           orphan_cnt == 1 ? "" : "s");
  }

  std::vector<Face *> sfaces_extra;
  std::vector<std::vector<EdgeCurve *>> face_edges_extra;

  // Declared here rather than with the loop building below because a partial
  // cylinder's two end edges are ordinary straight edges shared with a
  // neighbouring planar face, so both have to come from the same map.
  std::map<std::pair<int, int>, EdgeCurve *> edge_map;
  int merged_edge_cnt = 0;

  // Recognise rings of facets that were modelled as a cylinder.
  //
  // The fit alone can never decide this: a ring of N quads is exactly the mesh
  // of an N sided prism, and a cube's four sides fit a cylinder through its
  // corners with zero residual. So a ring is only accepted when the model also
  // declared a matching CylinderSurface - geometry from the mesh, intent from
  // the primitive that produced it.
  std::vector<CylinderRing> rings;
  std::vector<CylinderArc> arc_runs;
  std::vector<char> consumed(face_cnt, 0);
  std::map<std::size_t, std::size_t> rim_of_loop;  // rim loop -> index into rings
  // runs of a loop's edges already claimed by a partial cylinder, as
  // (start, count); two bands must never rewrite the same edge
  std::map<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>> claimed_runs;

  if (analytic && !surfaces.empty()) {
    std::map<std::pair<int, int>, std::vector<std::size_t>> loop_edges_map;
    for (std::size_t i = 0; i < face_cnt; i++) {
      if (!loop_valid[i]) continue;
      const auto& loop = loops[i];
      for (std::size_t j = 0; j < loop.size(); j++) {
        const int a = loop[j], b = loop[(j + 1) % loop.size()];
        loop_edges_map[{std::min(a, b), std::max(a, b)}].push_back(i);
      }
    }

    // The rim of a band which does not close: find the single neighbouring
    // loop it borders and the consecutive run of that loop's edges it covers.
    auto resolve_rim_run = [&](const std::vector<std::size_t>& ring,
                               const std::set<std::size_t>& in_ring, const std::vector<int>& level,
                               std::size_t& nb_out, std::size_t& start_out,
                               std::size_t& count_out) -> bool {
      const std::set<int> level_set(level.begin(), level.end());
      std::set<std::pair<int, int>> rim_edges;
      for (const std::size_t f : ring) {
        const auto& loop = loops[f];
        for (std::size_t j = 0; j < loop.size(); j++) {
          const int a = loop[j], b = loop[(j + 1) % loop.size()];
          if (level_set.count(a) && level_set.count(b)) {
            rim_edges.insert({std::min(a, b), std::max(a, b)});
          }
        }
      }
      if (rim_edges.empty()) return false;

      std::size_t nb = face_cnt;
      for (const auto& edge : rim_edges) {
        const auto it = loop_edges_map.find(edge);
        if (it == loop_edges_map.end()) return false;
        std::size_t other = face_cnt;
        int outside = 0;
        for (const std::size_t user : it->second) {
          if (in_ring.count(user)) continue;
          outside++;
          other = user;
        }
        // one face on the far side of every rim edge, and the same one
        if (outside != 1) return false;
        if (nb == face_cnt) nb = other;
        else if (nb != other) return false;
      }
      if (nb == face_cnt || !loop_valid[nb] || consumed[nb] || rim_of_loop.count(nb)) return false;

      const std::vector<int>& nb_loop = loops[nb];
      const std::size_t n = nb_loop.size();
      std::vector<char> on_rim(n, 0);
      std::size_t cnt = 0;
      for (std::size_t j = 0; j < n; j++) {
        const int a = nb_loop[j], b = nb_loop[(j + 1) % n];
        if (rim_edges.count({std::min(a, b), std::max(a, b)}) != 0) {
          on_rim[j] = 1;
          cnt++;
        }
      }
      // the neighbour has to use every rim edge, and they have to sit together:
      // an arc can only replace edges which are consecutive in the loop
      if (cnt != rim_edges.size() || cnt >= n) return false;
      std::size_t start = n;
      for (std::size_t j = 0; j < n; j++) {
        if (on_rim[j] == 0 || on_rim[(j + n - 1) % n] != 0) continue;
        if (start != n) return false;
        start = j;
      }
      if (start == n) return false;

      nb_out = nb;
      start_out = start;
      count_out = cnt;
      return true;
    };

    auto run_overlaps = [&](std::size_t loop, std::size_t start, std::size_t count) {
      const auto it = claimed_runs.find(loop);
      if (it == claimed_runs.end()) return false;
      const std::size_t n = loops[loop].size();
      for (const auto& claimed : it->second) {
        for (std::size_t a = 0; a < count; a++) {
          for (std::size_t b = 0; b < claimed.second; b++) {
            if ((start + a) % n == (claimed.first + b) % n) return true;
          }
        }
      }
      return false;
    };

    for (std::size_t seed = 0; seed < face_cnt; seed++) {
      if (!loop_valid[seed] || consumed[seed] || loop_is_hole[seed]) continue;
      if (loops[seed].size() != 4) continue;

      for (int k = 0; k < 2; k++) {
        const Vector3d d0 = (vertices[loops[seed][(k + 1) % 4]] - vertices[loops[seed][k]]).normalized();
        const Vector3d d2 =
          (vertices[loops[seed][(k + 3) % 4]] - vertices[loops[seed][(k + 2) % 4]]).normalized();
        if (fabs(fabs(d0.dot(d2)) - 1.0) > 1e-9) continue;
        const Vector3d axis = (d0[2] < 0 || (d0[2] == 0 && d0[0] < 0)) ? Vector3d(-d0) : d0;

        // grow the ring across the edges that run along the axis
        std::vector<std::size_t> ring;
        std::vector<std::size_t> stack{seed};
        std::set<std::size_t> in_ring;
        while (!stack.empty()) {
          const std::size_t cur = stack.back();
          stack.pop_back();
          if (in_ring.count(cur)) continue;
          in_ring.insert(cur);
          ring.push_back(cur);
          const auto& loop = loops[cur];
          for (std::size_t j = 0; j < loop.size(); j++) {
            const int a = loop[j], b = loop[(j + 1) % loop.size()];
            const Vector3d dir = (vertices[b] - vertices[a]).normalized();
            if (fabs(fabs(dir.dot(axis)) - 1.0) > 1e-9) continue;
            for (const std::size_t nb : loop_edges_map[{std::min(a, b), std::max(a, b)}]) {
              if (nb == cur || in_ring.count(nb) || consumed[nb]) continue;
              if (loops[nb].size() == 4 && !loop_is_hole[nb]) stack.push_back(nb);
            }
          }
        }
        if (ring.size() < 3) continue;

        // every wall vertex has to sit on one of the two rims
        std::map<int, double> along;
        for (const std::size_t f : ring) {
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
        // facet. Anything else is not a band of quads around a common axis.
        const bool full_turn =
          bottom_set.size() == ring.size() && top_set.size() == ring.size();
        const bool part_turn =
          bottom_set.size() == ring.size() + 1 && top_set.size() == ring.size() + 1;
        if (!full_turn && !part_turn) continue;

        // fit: one axis line, one radius
        Vector3d base;
        if (full_turn) {
          Vector3d centroid(0, 0, 0);
          for (const auto& kv : along) centroid += vertices[kv.first];
          centroid /= double(along.size());
          base = centroid - axis * (axis.dot(centroid) - lo);
        } else if (!fitCircleCentre(vertices, bottom_set, axis, lo, base)) {
          continue;
        }
        double r_sum = 0;
        for (const auto& kv : along) r_sum += distanceToAxis(vertices[kv.first], base, axis);
        const double radius = r_sum / double(along.size());
        if (radius < model_tol) continue;
        double dev = 0;
        for (const auto& kv : along) {
          dev = std::max(dev, fabs(distanceToAxis(vertices[kv.first], base, axis) - radius));
        }
        if (dev > 1e-7 * radius) continue;

        // and the model has to have declared a cylinder here
        bool declared = false;
        for (const auto& surface : surfaces) {
          const auto *cyl = dynamic_cast<const CylinderSurface *>(surface.get());
          if (cyl == nullptr) continue;
          if (fabs(cyl->r - radius) > 1e-7 * radius) continue;
          if (fabs(fabs(cyl->normdir.normalized().dot(axis)) - 1.0) > 1e-7) continue;
          if (distanceToAxis(cyl->refpt, base, axis) > 1e-7 * radius) continue;
          declared = true;
          break;
        }
        if (!declared) continue;

        if (part_turn) {
          // A band which stops short of a full turn is bounded by an arc at
          // either rim, and an arc replaces a *run* of edges inside the
          // neighbouring loop instead of the whole of it. Both rims still have
          // to belong to exactly one neighbouring face - a rim which borders
          // one face per facet, as a wall standing on a chamfer does, has no
          // single loop to rewrite.
          CylinderArc info;
          if (!resolve_rim_run(ring, in_ring, bottom_set, info.bottom_loop, info.bottom_start,
                               info.bottom_count) ||
              !resolve_rim_run(ring, in_ring, top_set, info.top_loop, info.top_start,
                               info.top_count)) {
            continue;
          }
          if (info.bottom_loop == info.top_loop) continue;
          if (info.bottom_count != ring.size() || info.top_count != ring.size()) continue;
          if (run_overlaps(info.bottom_loop, info.bottom_start, info.bottom_count) ||
              run_overlaps(info.top_loop, info.top_start, info.top_count)) {
            continue;
          }

          // The wall traverses each rim opposite to its neighbour, so the two
          // ends have to line up: the vertex above the start of the bottom run
          // is the end of the top run, and the other way round.
          const std::vector<int>& nb_bottom = loops[info.bottom_loop];
          const std::vector<int>& nb_top = loops[info.top_loop];
          const int b_first = nb_bottom[info.bottom_start];
          const int b_last = nb_bottom[(info.bottom_start + info.bottom_count) % nb_bottom.size()];
          const int t_first = nb_top[info.top_start];
          const int t_last = nb_top[(info.top_start + info.top_count) % nb_top.size()];
          if (vertexAbove(b_first, top_set, axis, model_tol, vertices) != t_last) continue;
          if (vertexAbove(b_last, top_set, axis, model_tol, vertices) != t_first) continue;

          info.walls = ring;
          info.axis = axis;
          info.base = base;
          info.radius = radius;
          info.height = hi - lo;
          const Vector3d probe = vertices[loops[ring[0]][0]];
          const Vector3d radial = (probe - base) - axis * axis.dot(probe - base);
          info.outward = radial.normalized().dot(loop_normals[ring[0]]) > 0;

          for (const std::size_t f : ring) consumed[f] = 1;
          claimed_runs[info.bottom_loop].push_back({info.bottom_start, info.bottom_count});
          claimed_runs[info.top_loop].push_back({info.top_start, info.top_count});
          arc_runs.push_back(info);
          break;
        }

        // both rims have to be a complete bound of a neighbouring face, so that
        // collapsing them to a CIRCLE leaves that face's loop intact
        const std::set<int> bottom_key(bottom_set.begin(), bottom_set.end());
        const std::set<int> top_key(top_set.begin(), top_set.end());
        std::size_t bottom_loop = face_cnt, top_loop = face_cnt;
        for (std::size_t i = 0; i < face_cnt; i++) {
          if (!loop_valid[i] || consumed[i] || in_ring.count(i) || rim_of_loop.count(i)) continue;
          const std::set<int> key(loops[i].begin(), loops[i].end());
          if (key.size() != loops[i].size()) continue;
          if (key == bottom_key) bottom_loop = i;
          else if (key == top_key) top_loop = i;
        }
        if (bottom_loop == face_cnt || top_loop == face_cnt) continue;

        CylinderRing info;
        info.walls = ring;
        info.axis = axis;
        info.base = base;
        info.radius = radius;
        info.height = hi - lo;
        info.bottom_loop = bottom_loop;
        info.top_loop = top_loop;
        info.bottom_ccw = loopTurnDirection(vertices, loops[bottom_loop], axis, base) > 0;
        info.top_ccw = loopTurnDirection(vertices, loops[top_loop], axis, base) > 0;
        info.seam_bottom = loops[bottom_loop][0];
        for (const int v : top_set) {
          const Vector3d rel = vertices[v] - vertices[info.seam_bottom];
          if ((rel - axis * axis.dot(rel)).norm() < model_tol) info.seam_top = v;
        }
        if (info.seam_top == -1) continue;

        const Vector3d probe = vertices[loops[ring[0]][0]];
        const Vector3d radial = (probe - base) - axis * axis.dot(probe - base);
        info.outward = radial.normalized().dot(loop_normals[ring[0]]) > 0;

        for (const std::size_t f : ring) consumed[f] = 1;
        rim_of_loop[bottom_loop] = rings.size();
        rim_of_loop[top_loop] = rings.size();
        rings.push_back(info);
        break;
      }
    }

    if (!rings.empty() || !arc_runs.empty()) {
      std::size_t collapsed = 0;
      for (const auto& ring : rings) collapsed += ring.walls.size();
      for (const auto& arc : arc_runs) collapsed += arc.walls.size();
      const std::size_t total = rings.size() + arc_runs.size();
      printf("STEP export: %d cylinder%s recognised (%d partial), %d facets replaced\n", int(total),
             total == 1 ? "" : "s", int(arc_runs.size()), int(collapsed));
    }
  }

  // Emit the recognised cylinders: one CYLINDRICAL_SURFACE face each, bounded
  // by a CIRCLE at either rim and a seam running between them.
  //
  // A cylinder is periodic, so a face covering the full turn cannot be bounded
  // by the rims alone - the loop has to walk up one seam and back down it, the
  // same edge used once in each direction. The two CIRCLE edges are shared with
  // the neighbouring disc or annulus, which is why both rims had to be a
  // complete bound of one of those faces.
  std::vector<FaceBound *> ring_bounds(rings.size(), nullptr);
  std::vector<std::pair<EdgeCurve *, EdgeCurve *>> ring_circles(rings.size(), {nullptr, nullptr});

  for (std::size_t i = 0; i < rings.size(); i++) {
    const CylinderRing& ring = rings[i];
    const Vector3d top_centre = ring.base + ring.axis * ring.height;
    const Vector3d ref = (vertices[ring.seam_bottom] - ring.base).normalized();

    auto make_placement = [&](const Vector3d& origin) {
      auto point = new Point(entities, origin);
      auto dir_axis = new Direction(entities, ring.axis);
      auto dir_ref = new Direction(entities, ref);
      return new Axis2Placement(entities, dir_axis, dir_ref, point);
    };

    auto surface = new CylindricalSurface(entities, "", make_placement(ring.base), ring.radius);
    auto circle_bottom = new Circle(entities, "", make_placement(ring.base), ring.radius);
    auto circle_top = new Circle(entities, "", make_placement(top_centre), ring.radius);

    Vertex *vert_bottom = get_vertex(ring.seam_bottom);
    Vertex *vert_top = get_vertex(ring.seam_top);

    // a full circle is one edge whose two ends are the same vertex
    auto edge_bottom = new EdgeCurve(entities, vert_bottom, vert_bottom, circle_bottom, true);
    auto edge_top = new EdgeCurve(entities, vert_top, vert_top, circle_top, true);
    auto edge_seam = create_line_edge_curve(vert_bottom, vert_top, true);
    ring_circles[i] = {edge_bottom, edge_top};

    // A CIRCLE runs counter clockwise about its axis. The wall has to traverse
    // each rim opposite to the neighbouring face, so take the direction from
    // the rim loop that face will use and flip it.
    std::vector<OrientedEdge *> loop;
    loop.push_back(new OrientedEdge(entities, edge_bottom, !ring.bottom_ccw));
    loop.push_back(new OrientedEdge(entities, edge_seam, true));
    loop.push_back(new OrientedEdge(entities, edge_top, !ring.top_ccw));
    loop.push_back(new OrientedEdge(entities, edge_seam, false));

    auto edge_loop = new EdgeLoop(entities, loop);
    ring_bounds[i] = new FaceBound(entities, edge_loop, true, true);

    // The surface normal of a CYLINDRICAL_SURFACE points away from its axis, so
    // a bore - where the material is outside the cylinder - is the opposite
    // sense.
    std::vector<FaceBound *> bounds{ring_bounds[i]};
    sfaces_extra.push_back(new Face(entities, bounds, surface, ring.outward));
    face_edges_extra.push_back({edge_bottom, edge_top, edge_seam});
  }

  // Emit the partial cylinders: one CYLINDRICAL_SURFACE face bounded by an arc
  // at either rim and the band's two end edges - arc, line, arc, line, which is
  // also the construction SolidWorks writes when it splits a full cylinder in
  // half. No seam: a face which does not close is not periodic.
  //
  // Each arc replaces a run of edges inside its neighbour's loop, recorded here
  // and applied when that loop is built below.
  std::map<std::size_t, std::vector<ArcSubstitution>> arc_subs;

  for (const CylinderArc& arc : arc_runs) {
    const Vector3d top_centre = arc.base + arc.axis * arc.height;

    // A rim as its neighbour traverses it, from `first` to `last`.
    struct RimEdge {
      EdgeCurve *edge = nullptr;
      bool neighbour_sense = true;
    };
    auto make_rim = [&](std::size_t loop_ind, std::size_t start, std::size_t count,
                        const Vector3d& centre) {
      const std::vector<int>& loop = loops[loop_ind];
      const std::size_t n = loop.size();
      const int first = loop[start];
      const int last = loop[(start + count) % n];

      // Which way round the axis does the neighbour run? A CIRCLE is counter
      // clockwise about its own axis, so an arc traversed the other way is the
      // same curve used in reverse rather than a different one.
      double sweep = 0;
      for (std::size_t j = 0; j < count; j++) {
        const Vector3d a = vertices[loop[(start + j) % n]] - centre;
        const Vector3d b = vertices[loop[(start + j + 1) % n]] - centre;
        sweep += arc.axis.dot(a.cross(b));
      }
      const bool ccw = sweep > 0;
      const int from = ccw ? first : last;
      const int to = ccw ? last : first;

      auto point = new Point(entities, centre);
      auto dir_axis = new Direction(entities, arc.axis);
      auto dir_ref = new Direction(entities, (vertices[from] - centre).normalized());
      auto placement = new Axis2Placement(entities, dir_axis, dir_ref, point);
      auto circle = new Circle(entities, "", placement, arc.radius);

      RimEdge rim;
      rim.edge = new EdgeCurve(entities, get_vertex(from), get_vertex(to), circle, true);
      rim.neighbour_sense = ccw;
      arc_subs[loop_ind].push_back({start, count, rim.edge, ccw});
      return rim;
    };

    const RimEdge bottom = make_rim(arc.bottom_loop, arc.bottom_start, arc.bottom_count, arc.base);
    const RimEdge top = make_rim(arc.top_loop, arc.top_start, arc.top_count, top_centre);

    // The two end edges are ordinary straight edges, shared with whatever face
    // stands beyond the end of the band.
    const std::vector<int>& nb_bottom = loops[arc.bottom_loop];
    const std::vector<int>& nb_top = loops[arc.top_loop];
    const int b_first = nb_bottom[arc.bottom_start];
    const int b_last = nb_bottom[(arc.bottom_start + arc.bottom_count) % nb_bottom.size()];
    const int t_first = nb_top[arc.top_start];
    const int t_last = nb_top[(arc.top_start + arc.top_count) % nb_top.size()];

    bool up_dir = true, down_dir = true;
    EdgeCurve *edge_up = get_line_from_map(edge_map, b_first, t_last, get_vertex(b_first),
                                           get_vertex(t_last), up_dir, merged_edge_cnt);
    EdgeCurve *edge_down = get_line_from_map(edge_map, t_first, b_last, get_vertex(t_first),
                                             get_vertex(b_last), down_dir, merged_edge_cnt);

    // Walk the wall: back along the bottom rim, up the end edge, back along the
    // top rim, down the other end edge. Each rim is traversed opposite to its
    // neighbour, so the shared arc is used once in each direction.
    auto placement_surface = [&]() {
      auto point = new Point(entities, arc.base);
      auto dir_axis = new Direction(entities, arc.axis);
      auto dir_ref = new Direction(entities, (vertices[b_first] - arc.base).normalized());
      return new Axis2Placement(entities, dir_axis, dir_ref, point);
    };
    auto surface = new CylindricalSurface(entities, "", placement_surface(), arc.radius);

    std::vector<OrientedEdge *> loop;
    loop.push_back(new OrientedEdge(entities, bottom.edge, !bottom.neighbour_sense));
    loop.push_back(new OrientedEdge(entities, edge_up, up_dir));
    loop.push_back(new OrientedEdge(entities, top.edge, !top.neighbour_sense));
    loop.push_back(new OrientedEdge(entities, edge_down, down_dir));

    auto edge_loop = new EdgeLoop(entities, loop);
    std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};
    sfaces_extra.push_back(new Face(entities, bounds, surface, arc.outward));
    face_edges_extra.push_back({bottom.edge, top.edge, edge_up, edge_down});
  }

  // Build the loops, their edges and the carrier planes.
  std::vector<FaceBound *> face_bounds(face_cnt, nullptr);
  std::vector<Plane *> planes(face_cnt, nullptr);
  std::vector<std::vector<EdgeCurve *>> loop_edges(face_cnt);

  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i] || consumed[i]) continue;
    const std::vector<int>& loop = loops[i];
    const int n = int(loop.size());

    std::vector<OrientedEdge *> oriented_edges;

    // a rim of a recognised cylinder is one circular edge instead of n straight
    // ones, shared with the cylindrical face
    const auto rim = rim_of_loop.find(i);
    if (rim != rim_of_loop.end()) {
      const CylinderRing& ring = rings[rim->second];
      const bool bottom = (i == ring.bottom_loop);
      EdgeCurve *circle = bottom ? ring_circles[rim->second].first : ring_circles[rim->second].second;
      const bool ccw = bottom ? ring.bottom_ccw : ring.top_ccw;
      oriented_edges.push_back(new OrientedEdge(entities, circle, ccw));
      loop_edges[i].push_back(circle);
    } else if (arc_subs.count(i) != 0) {
      // one or more runs of this loop's edges belong to a partial cylinder and
      // are replaced, in place, by the single arc that cylinder is bounded by
      const std::vector<ArcSubstitution>& subs = arc_subs[i];
      std::vector<int> starts_here(n, -1);
      std::vector<char> covered(n, 0);
      for (std::size_t s = 0; s < subs.size(); s++) {
        starts_here[subs[s].start % n] = int(s);
        for (std::size_t c = 0; c < subs[s].count; c++) covered[(subs[s].start + c) % n] = 1;
      }
      for (int j = 0; j < n; j++) {
        if (starts_here[j] >= 0) {
          const ArcSubstitution& sub = subs[starts_here[j]];
          oriented_edges.push_back(new OrientedEdge(entities, sub.edge, sub.sense));
          loop_edges[i].push_back(sub.edge);
          continue;
        }
        if (covered[j] != 0) continue;
        const int ind = loop[j];
        const int indn = loop[(j + 1) % n];
        bool edge_dir = true;
        EdgeCurve *edge_curve = get_line_from_map(edge_map, ind, indn, get_vertex(ind),
                                                  get_vertex(indn), edge_dir, merged_edge_cnt);
        oriented_edges.push_back(new OrientedEdge(entities, edge_curve, edge_dir));
        loop_edges[i].push_back(edge_curve);
      }
    } else
      for (int j = 0; j < n; j++) {
        const int ind = loop[j];
        const int indn = loop[(j + 1) % n];
        bool edge_dir = true;
        EdgeCurve *edge_curve = get_line_from_map(edge_map, ind, indn, get_vertex(ind), get_vertex(indn),
                                                  edge_dir, merged_edge_cnt);
        oriented_edges.push_back(new OrientedEdge(entities, edge_curve, edge_dir));
        loop_edges[i].push_back(edge_curve);
      }

    // create the plane. The reference direction has to lie inside the plane, so
    // project the longest edge onto it instead of using it as it comes.
    const Vector3d& norm = loop_normals[i];
    Vector3d ref(0, 0, 0);
    double ref_len = 0;
    for (int j = 0; j < n; j++) {
      const Vector3d dir = vertices[loop[(j + 1) % n]] - vertices[loop[j]];
      if (dir.norm() > ref_len) {
        ref_len = dir.norm();
        ref = dir;
      }
    }
    ref -= norm * norm.dot(ref);
    if (ref.norm() < 1e-12) ref = perpendicular(norm);
    else ref.normalize();

    auto plane_point = new Point(entities, vertices[loop[0]]);
    auto plane_dir_1 = new Direction(entities, norm);
    auto plane_dir_2 = new Direction(entities, ref);
    auto plane_axis = new Axis2Placement(entities, plane_dir_1, plane_dir_2, plane_point);
    planes[i] = new Plane(entities, plane_axis);

    auto edge_loop = new EdgeLoop(entities, oriented_edges);
    face_bounds[i] = new FaceBound(entities, edge_loop, true, !loop_is_hole[i]);
  }

  // Combine every outer loop with the loops of its holes into one ADVANCED_FACE.
  std::vector<Face *> sfaces;
  std::vector<std::vector<EdgeCurve *>> face_edges;
  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i] || loop_is_hole[i] || consumed[i]) continue;
    std::vector<FaceBound *> singface;
    singface.push_back(face_bounds[i]);
    std::vector<EdgeCurve *> edges = loop_edges[i];
    for (std::size_t j = 0; j < face_cnt; j++) {
      if (!loop_valid[j] || consumed[j] || parents[j] != int(i)) continue;
      singface.push_back(face_bounds[j]);
      edges.insert(edges.end(), loop_edges[j].begin(), loop_edges[j].end());
    }

    sfaces.push_back(new Face(entities, singface, planes[i], true));
    face_edges.push_back(edges);
  }

  // the recognised cylinders are faces of the same shell
  for (std::size_t i = 0; i < sfaces_extra.size(); i++) {
    sfaces.push_back(sfaces_extra[i]);
    face_edges.push_back(face_edges_extra[i]);
  }

  // A CLOSED_SHELL has to be a single connected shell, so split disconnected
  // bodies into one MANIFOLD_SOLID_BREP each instead of stuffing all of them
  // into one shell that can never close.
  std::vector<int> component(sfaces.size(), 0);
  for (std::size_t i = 0; i < sfaces.size(); i++) component[i] = int(i);
  std::map<EdgeCurve *, int> edge_owner;
  for (std::size_t i = 0; i < face_edges.size(); i++) {
    for (auto *edge : face_edges[i]) {
      auto it = edge_owner.find(edge);
      if (it == edge_owner.end()) {
        edge_owner.emplace(edge, int(i));
        continue;
      }
      const int a = uf_find(component, it->second);
      const int b = uf_find(component, int(i));
      if (a != b) component[a] = b;
    }
  }
  std::map<int, std::vector<Face *>> shell_faces;
  for (std::size_t i = 0; i < sfaces.size(); i++) {
    shell_faces[uf_find(component, int(i))].push_back(sfaces[i]);
  }

  // units and modelling tolerance
  auto length_unit = new SiUnit(entities, SiUnit::LENGTH);
  auto angle_unit = new SiUnit(entities, SiUnit::PLANE_ANGLE);
  auto solid_angle_unit = new SiUnit(entities, SiUnit::SOLID_ANGLE);
  auto uncertainty = new UncertaintyMeasure(entities, length_unit, model_tol);
  auto geom_context =
    new GeometricContext(entities, uncertainty, length_unit, angle_unit, solid_angle_unit);

  // create the base csys
  auto base_point = new Point(entities, Vector3d(0, 0, 0));
  auto base_dir_1 = new Direction(entities, Vector3d(0, 0, 1));
  auto base_dir_2 = new Direction(entities, Vector3d(1, 0, 0));
  auto base_axis = new Axis2Placement(entities, base_dir_1, base_dir_2, base_point);

  // product structure
  const std::string body_name = name != nullptr ? name : "";
  auto app_context = new ApplicationContext(entities);
  new ApplicationProtocolDefinition(entities, app_context);
  auto prod_context = new ProductContext(entities, app_context);
  auto product = new Product(entities, body_name, prod_context);
  new ProductRelatedProductCategory(entities, product);
  auto prod_formation = new ProductDefinitionFormation(entities, product);
  auto prod_def_context = new ProductDefinitionContext(entities, app_context);
  auto product_def = new ProductDefinition(entities, prod_formation, prod_def_context);
  auto product_def_shape = new ProductDefinitionShape(entities, product_def);
  auto shape_repr = new ShapeRepresentation(entities, body_name, base_axis, geom_context);
  new ShapeDefinition_Representation(entities, product_def_shape, shape_repr);

  // build the model
  std::vector<ManifoldSolid *> solids;
  for (auto& shell : shell_faces) {
    auto closed_shell = new Shell(entities, shell.second);
    closed_shell->isOpen = false;
    solids.push_back(new ManifoldSolid(entities, 0, closed_shell));
  }

  if (!solids.empty()) {
    auto adv_brep_shape_pres = new AdvancesBrepRepresentation(entities, body_name, solids, geom_context);
    new ShapeRepresentationRelationShip(entities, shape_repr, adv_brep_shape_pres);
  }
}

StepKernel::EdgeCurve *StepKernel::get_line_from_map(
  std::map<std::pair<int, int>, StepKernel::EdgeCurve *>& edge_map, int ind1, int ind2,
  StepKernel::Vertex *vert1, StepKernel::Vertex *vert2, bool& edge_dir, int& merge_cnt)
{
  // Keyed on the (deduplicated) vertex indices: the previous key was built from
  // the raw coordinates, which only matches when the two faces meeting at the
  // edge store bit identical doubles.
  const auto key = std::make_pair(std::min(ind1, ind2), std::max(ind1, ind2));
  edge_dir = true;

  auto it = edge_map.find(key);
  if (it != edge_map.end()) {
    edge_dir = (it->second->vert1 == vert1);
    merge_cnt++;
    return it->second;
  }

  StepKernel::EdgeCurve *edge_curve = create_line_edge_curve(vert1, vert2, true);
  edge_map.emplace(key, edge_curve);
  return edge_curve;
}

std::string StepKernel::read_line(std::ifstream& stp_file, bool skip_all_space)
{
  std::string line_str;
  bool leading_space = true;
  bool in_squote = false;
  bool in_dquote = false;
  bool in_comment = false;
  char old_char = '\0';
  while (stp_file) {
    char get_char = ' ';
    stp_file.get(get_char);
    if (old_char == '/' && get_char == '*' && !in_comment) {
      in_comment = true;
      line_str = line_str.substr(0, line_str.size() - 1);
    }
    if (old_char == '*' && get_char == '/' && in_comment) {
      in_comment = false;
      continue;
    }

    old_char = get_char;
    if (in_comment) continue;

    if (get_char == '\'' && !in_dquote) in_squote = !in_squote;
    if (get_char == '\"' && !in_squote) in_dquote = !in_dquote;
    if (get_char == ';' && !in_squote && !in_dquote) break;

    if (get_char == '\n' || get_char == '\r' || get_char == '\t') continue;

    if (leading_space && (get_char == ' ' || get_char == '\t')) continue;
    if (!skip_all_space) leading_space = false;
    line_str.push_back(get_char);
  }
  return line_str;
}

void StepKernel::read_step(std::string file_name)
{
  std::ifstream stp_file;
  stp_file.open(file_name);
  if (!stp_file) {
    printf("Cannot open %s\n", file_name.c_str());
    return;
  }
  // read the first line to get the iso stuff
  std::string iso_line = read_line(stp_file, true);

  bool data_section = false;
  std::vector<Entity *> ents;
  std::map<int, Entity *> ent_map;
  std::vector<std::string> args;
  while (stp_file) {
    std::string cur_str = read_line(stp_file, false);
    if (cur_str == "DATA") {
      data_section = true;
      continue;
    }
    if (!data_section) continue;

    if (cur_str == "ENDSEC") {
      data_section = false;
      break;
    }
    // parse the id
    int id = -1;
    if (cur_str.size() > 0 && cur_str[0] == '#' && cur_str.find('=')) {
      auto equal_pos = cur_str.find('=');
      //			auto paren_pos = cur_str.find('(');
      auto id_str = cur_str.substr(1, equal_pos - 1);
      id = std::atoi(id_str.c_str());
      auto func_start = cur_str.find_first_not_of("\t ", equal_pos + 1);
      auto func_end = cur_str.find_first_of("\t (", func_start + 1);
      auto func_name = cur_str.substr(func_start, func_end - func_start);
      bool unimplemented = false;

      // now parse the args
      auto arg_end = cur_str.find_last_of(')');
      auto arg_start = cur_str.find_first_not_of("\t (", func_end + 1);
      auto arg_str = cur_str.substr(arg_start, arg_end - arg_start);
      Entity *ent = 0;
      if (func_name == "CARTESIAN_POINT") ent = new Point(entities);
      else if (func_name == "DIRECTION") ent = new Direction(entities);
      else if (func_name == "AXIS2_PLACEMENT_3D") ent = new Axis2Placement(entities);
      else if (func_name == "PLANE") ent = new Plane(entities);
      else if (func_name == "EDGE_LOOP") ent = new EdgeLoop(entities);
      else if (func_name == "FACE_BOUND") ent = new FaceBound(entities);
      else if (func_name == "FACE_OUTER_BOUND") {
        auto face_bound = new FaceBound(entities);
        face_bound->outer = true;
        ent = face_bound;
      } else if (func_name == "ADVANCED_FACE") ent = new Face(entities);
      else if (func_name == "FACE_SURFACE") ent = new Face(entities);
      else if (func_name == "OPEN_SHELL") ent = new Shell(entities);
      else if (func_name == "CLOSED_SHELL") ent = new Shell(entities);
      else if (func_name == "SHELL_BASED_SURFACE_MODEL") ent = new ShellModel(entities);
      else if (func_name == "MANIFOLD_SURFACE_SHAPE_REPRESENTATION") ent = new ManifoldShape(entities);
      else if (func_name == "MANIFOLD_SOLID_BREP") ent = new ManifoldSolid(entities);
      else if (func_name == "VERTEX_POINT") ent = new Vertex(entities);
      else if (func_name == "SURFACE_CURVE") ent = new SurfaceCurve(entities);
      else if (func_name == "EDGE_CURVE") ent = new EdgeCurve(entities);
      else if (func_name == "ORIENTED_EDGE") ent = new OrientedEdge(entities);
      else if (func_name == "VECTOR") ent = new Vector(entities);
      else if (func_name == "LINE") ent = new Line(entities);
      else if (func_name == "CIRCLE") ent = new Circle(entities);
      else if (func_name == "CYLINDRICAL_SURFACE") ent = new CylindricalSurface(entities);
      else if (func_name == "PCURVE") unimplemented = true;
      else if (func_name == "DEFINITIONAL_REPRESENTATION") unimplemented = true;
      else if (func_name == "UNCERTAINTY_MEASURE_WITH_UNIT") unimplemented = true;
      else if (func_name == "PRODUCT_TYPE") unimplemented = true;
      else if (func_name == "APPLICATION_PROTOCOL_DEFINITION") unimplemented = true;
      else if (func_name == "APPLICATION_CONTEXT") unimplemented = true;
      else if (func_name == "SHAPE_DEFINITION_REPRESENTATION") unimplemented = true;
      else if (func_name == "PRODUCT") unimplemented = true;
      else if (func_name == "PRODUCT_DEFINITION_SHAPE") unimplemented = true;
      else if (func_name == "PRODUCT_DEFINITION") unimplemented = true;
      else if (func_name == "PRODUCT_DEFINITION_FORMATION") unimplemented = true;
      else if (func_name == "MECHANICAL_CONTEXT") unimplemented = true;
      else if (func_name == "PRODUCT_DEFINITION_CONTEXT") unimplemented = true;
      else if (func_name == "ADVANCED_BREP_SHAPE_REPRESENTATION") unimplemented = true;
      else if (func_name == "PERSON") unimplemented = true;
      else if (func_name == "DATE_TIME_ROLE") unimplemented = true;
      else if (func_name == "LOCAL_TIME") unimplemented = true;
      else if (func_name == "APPROVAL_ROLE") unimplemented = true;
      else if (func_name == "APPROVAL") unimplemented = true;
      else if (func_name == "COORDINATED_UNIVERSAL_TIME_OFFSET") unimplemented = true;
      else if (func_name == "CC_DESIGN_PERSON_AND_ORGANIZATION_ASSIGNMENT") unimplemented = true;
      else if (func_name == "DATE_AND_TIME") unimplemented = true;
      else if (func_name == "APPROVAL_DATE_TIME") unimplemented = true;
      else if (func_name == "SECURITY_CLASSIFICATION_LEVEL") unimplemented = true;
      else if (func_name == "APPROVAL_STATUS") unimplemented = true;
      else if (func_name == "CC_DESIGN_APPROVAL") unimplemented = true;
      else if (func_name == "ORGANIZATION") unimplemented = true;
      else if (func_name == "PERSON_AND_ORGANIZATION") unimplemented = true;
      else if (func_name == "CALENDAR_DATE") unimplemented = true;
      else if (func_name == "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE") unimplemented = true;
      else if (func_name == "PERSON_AND_ORGANIZATION_ROLE") unimplemented = true;
      else if (func_name == "PRODUCT_RELATED_PRODUCT_CATEGORY") unimplemented = true;
      else if (func_name == "CC_DESIGN_DATE_AND_TIME_ASSIGNMENT") unimplemented = true;
      else if (func_name == "SECURITY_CLASSIFICATION") unimplemented = true;
      else if (func_name == "APPROVAL_PERSON_ORGANIZATION") unimplemented = true;
      else if (func_name == "DESIGN_CONTEXT") unimplemented = true;
      else if (func_name == "CC_DESIGN_SECURITY_CLASSIFICATION") unimplemented = true;
      else if (func_name == "PRODUCT_CONTEXT") unimplemented = true;
      else if (func_name == "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION")
        unimplemented = true;
      else if (func_name == "STYLED_ITEM") unimplemented = true;
      else if (func_name == "PRESENTATION_STYLE_ASSIGNMENT") unimplemented = true;
      else if (func_name == "COLOUR_RGB") unimplemented = true;
      else if (func_name == "FILL_AREA_STYLE") unimplemented = true;
      else if (func_name == "SURFACE_STYLE_USAGE") unimplemented = true;
      else if (func_name == "SURFACE_SIDE_STYLE") unimplemented = true;
      else if (func_name == "SURFACE_STYLE_FILL_AREA") unimplemented = true;
      else if (func_name == "FILL_AREA_STYLE_COLOUR") unimplemented = true;
      else if (func_name == "CURVE_STYLE") unimplemented = true;
      else if (func_name == "DRAUGHTING_PRE_DEFINED_CURVE_FONT") unimplemented = true;
      else if (func_name == "SHAPE_REPRESENTATION") unimplemented = true;
      else if (func_name == "SHAPE_REPRESENTATION_RELATIONSHIP") unimplemented = true;
      else if (func_name == "PERSONAL_ADDRESS") unimplemented = true;
      else if (func_name == "PLANE_ANGLE_MEASURE_WITH_UNIT") unimplemented = true;
      else if (func_name == "PRODUCT_CATEGORY") unimplemented = true;
      else if (func_name == "PRODUCT_CATEGORY_RELATIONSHIP") unimplemented = true;
      else if (func_name == "(LENGTH_UNIT") unimplemented = true;
      else if (func_name == "(NAMED_UNIT") unimplemented = true;
      else if (func_name == "(GEOMETRIC_REPRESENTATION_CONTEXT") unimplemented = true;
      else if (func_name == "(") unimplemented = true;
      if (!ent) {
        if (unimplemented) {
          ent = new Line(entities);  // TODO fix
        } else {
          printf("Unknown Type %s\n", func_name.c_str());
          ent = new Line(entities);
        }
      }

      if (ent) {
        ent->id = id;
        ent_map[id] = ent;
        ents.push_back(ent);
        args.push_back(arg_str);
      }
    }
    //		std::cout << cur_str << "\n";
  }
  // processes all the arguments
  for (size_t i = 0; i < ents.size(); i++) {
    ents[i]->parse_args(ent_map, args[i]);
  }
  stp_file.close();
  this->entities = ents;
}
