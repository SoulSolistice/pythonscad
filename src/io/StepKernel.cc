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
#include <functional>
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

/*! A band of facets around a common axis: a cylinder when both rims have the
 * same radius, a frustum when they do not.
 *
 * `walls` are the loops it is made of, dropped in favour of a single face.
 * Whether that face can be written depends on what each rim borders, which is
 * decided later - see RimRef. */
struct Band {
  std::vector<std::size_t> walls;
  Vector3d axis, base;  // base is the centre of the bottom rim
  double r_bottom = 0, r_top = 0;
  double height = 0;
  bool closed = false;   // covers the full turn, so the face is periodic
  bool outward = false;  // wall normals point away from the axis
  std::vector<int> bottom_set, top_set;
  int seam_bottom = -1, seam_top = -1;  // the ruling the seam runs along
  bool alive = true;
  const char *dropped = nullptr;  // why it was left faceted, for the report
};

/*! What lies on the other side of one rim of a band.
 *
 * A rim can only be collapsed into a CIRCLE when everything using its edges
 * agrees to the substitution. Three cases do:
 *
 *   WHOLE_LOOP  the rim is the complete bound of one neighbouring face
 *   LOOP_RUN    the rim is a consecutive run of edges inside one such loop
 *   OTHER_BAND  the rim is shared with another band, which is being collapsed
 *               too - a wall standing on a chamfer, and the case that keeps a
 *               chamfered body faceted until both halves can be written
 *
 * Anything else - most often one neighbouring face per facet - leaves the band
 * faceted. */
struct RimRef {
  enum Kind { UNRESOLVED, WHOLE_LOOP, LOOP_RUN, OTHER_BAND };
  Kind kind = UNRESOLVED;
  std::size_t loop = 0;              // WHOLE_LOOP, LOOP_RUN
  std::size_t start = 0, count = 0;  // LOOP_RUN
  std::size_t band = 0;              // OTHER_BAND
  bool wall_ccw = false;             // the wall facets run counter clockwise
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

  // Recognise bands of facets that were modelled as a surface of revolution.
  //
  // The fit alone can never decide this: a ring of N quads is exactly the mesh
  // of an N sided prism, and a cube's four sides fit a cylinder through its
  // corners with zero residual. So a band is only accepted when the model also
  // declared the matching surface - geometry from the mesh, intent from the
  // primitive that produced it.
  std::vector<Band> bands;
  std::vector<char> consumed(face_cnt, 0);
  std::vector<std::size_t> band_of_loop(face_cnt, std::size_t(-1));
  std::vector<std::pair<RimRef, RimRef>> rims;  // bottom, top - one per band

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

        // The chords - the edges which are not rulings - all lie in a plane
        // perpendicular to the axis, so two of them which are not parallel fix
        // the axis exactly.
        std::vector<Vector3d> chords;
        for (const std::size_t f : walls) {
          const int r = entry[f];
          for (const int c : {(r + 1) % 4, (r + 3) % 4}) {
            const Vector3d dir = vertices[loops[f][(c + 1) % 4]] - vertices[loops[f][c]];
            if (dir.norm() > 1e-12) chords.push_back(dir.normalized());
          }
        }
        if (chords.size() < 2) continue;
        Vector3d axis(0, 0, 0);
        for (std::size_t c = 1; c < chords.size(); c++) {
          const Vector3d n = chords[0].cross(chords[c]);
          if (n.norm() > 1e-9) {
            axis = n.normalized();
            break;
          }
        }
        if (axis.norm() < 0.5) continue;
        if (axis[2] < 0 || (axis[2] == 0 && axis[0] < 0)) axis = -axis;
        bool perpendicular_ok = true;
        for (const Vector3d& c : chords) perpendicular_ok = perpendicular_ok && fabs(c.dot(axis)) < 1e-9;
        if (!perpendicular_ok) continue;

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
        const bool part_turn =
          bottom_set.size() == walls.size() + 1 && top_set.size() == walls.size() + 1;
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
      if (edges.empty()) { *why = "no rim edges"; return false; }
      const std::set<std::size_t> in_band(band.walls.begin(), band.walls.end());

      std::set<std::size_t> others;
      for (const auto& edge : edges) {
        const auto it = loop_edges_map.find(edge);
        if (it == loop_edges_map.end()) { *why = "a rim edge belongs to no loop"; return false; }
        std::size_t outside = face_cnt;
        int count = 0;
        for (const std::size_t user : it->second) {
          if (in_band.count(user)) continue;
          count++;
          outside = user;
        }
        if (count != 1) { *why = "a rim edge is used by more than two faces"; return false; }
        others.insert(outside);
      }

      out.wall_ccw = wall_runs_ccw(bi, *edges.begin());

      if (others.size() == 1) {
        const std::size_t nb = *others.begin();
        if (band_of_loop[nb] != std::size_t(-1)) { *why = "the rim borders a single facet of another band"; return false; }  // a one facet band
        if (!loop_valid[nb] || consumed[nb]) { *why = "the neighbouring face was dropped"; return false; }
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
        if (cnt != edges.size() || cnt >= n) { *why = "the rim is not a run of its neighbour's edges"; return false; }
        std::size_t start = n;
        for (std::size_t j = 0; j < n; j++) {
          if (on_rim[j] == 0 || on_rim[(j + n - 1) % n] != 0) continue;
          if (start != n) { *why = "the rim is split across its neighbour's loop"; return false; }
          start = j;
        }
        if (start == n) { *why = "the rim covers its neighbour's whole loop twice"; return false; }
        out.kind = RimRef::LOOP_RUN;
        out.loop = nb;
        out.start = start;
        out.count = cnt;
        return true;
      }

      // shared with another band, which has to be collapsed too
      std::set<std::size_t> nb_bands;
      for (const std::size_t f : others) nb_bands.insert(band_of_loop[f]);
      if (nb_bands.size() != 1 || *nb_bands.begin() == std::size_t(-1)) { *why = "the rim borders one face per facet"; return false; }
      const std::size_t other = *nb_bands.begin();
      if (!bands[other].alive) { *why = "the band sharing this rim was dropped"; return false; }
      // only between two full turns: a shared rim covered by several partial
      // bands would have to be split into arcs on both sides at once
      if (!band.closed || !bands[other].closed) { *why = "a shared rim needs both bands to cover the full turn"; return false; }
      if (others.size() != bands[other].walls.size()) { *why = "the shared rim does not cover the whole neighbouring band"; return false; }
      out.kind = RimRef::OTHER_BAND;
      out.band = other;
      return true;
    };

    // The end edges of a partial band have to be edges the mesh already has.
    auto ends_line_up = [&](const RimRef& bottom, const RimRef& top) {
      const std::vector<int>& nb_bottom = loops[bottom.loop];
      const std::vector<int>& nb_top = loops[top.loop];
      const int b_first = nb_bottom[bottom.start];
      const int b_last = nb_bottom[(bottom.start + bottom.count) % nb_bottom.size()];
      const int t_first = nb_top[top.start];
      const int t_last = nb_top[(top.start + top.count) % nb_top.size()];
      return loop_edges_map.count(edge_key(b_first, t_last)) != 0 &&
             loop_edges_map.count(edge_key(t_first, b_last)) != 0;
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

        // A full turn collapses each rim into a closed CIRCLE, which can only
        // replace a whole loop or the matching rim of another band; a partial
        // band collapses each rim into an arc, which only ever replaces a run.
        // Anything else would put a closed circle in the middle of a loop.
        const bool shapes_ok =
          bands[i].closed ? (bottom.kind != RimRef::LOOP_RUN && top.kind != RimRef::LOOP_RUN)
                          : (bottom.kind == RimRef::LOOP_RUN && top.kind == RimRef::LOOP_RUN);
        if (!shapes_ok) {
          bands[i].alive = false;
          bands[i].dropped = "a rim is a run of a loop, but the band covers the full turn";
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
    // bands share a rim they have to use the same vertex - the CIRCLE between
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
        band_of_loop[f] = std::size_t(-1);
      }
    }

    std::size_t collapsed = 0, alive = 0, cones = 0, partial = 0;
    for (const auto& band : bands) {
      if (!band.alive) continue;
      alive++;
      collapsed += band.walls.size();
      if (fabs(band.r_bottom - band.r_top) > 1e-9 * std::max(band.r_bottom, band.r_top)) cones++;
      if (!band.closed) partial++;
    }
    if (alive > 0) {
      printf("STEP export: %d surface%s recognised (%d conical, %d partial), %d facets replaced\n",
             int(alive), alive == 1 ? "" : "s", int(cones), int(partial), int(collapsed));
    }
    // Every band here fits its axis exactly and was declared by the model, so a
    // drop is always the topology around it rather than the surface itself.
    // Naming the rule that rejected it is the only way to tell a wall which
    // cannot be written from one which should have been.
    for (const auto& band : bands) {
      if (band.alive || band.dropped == nullptr) continue;
      printf("STEP export: r=%g band of %d facets left faceted: %s\n", band.r_bottom,
             int(band.walls.size()), band.dropped);
    }
  }

  // Emit the recognised bands: one CYLINDRICAL_SURFACE or CONICAL_SURFACE face
  // each, bounded by a circle or an arc at either rim.
  //
  // A band which covers the full turn is periodic, so it cannot be bounded by
  // the rims alone - the loop walks up a seam and back down it, the same edge
  // used once in each direction. One which does not is bounded by an arc at
  // either rim and the band's two end edges, and needs no seam.
  std::map<std::size_t, std::pair<EdgeCurve *, bool>> rim_of_loop;  // whole loop -> circle
  std::map<std::size_t, std::vector<ArcSubstitution>> arc_subs;     // runs inside a loop
  std::map<std::set<int>, EdgeCurve *> shared_rim_edges;            // rim vertices -> circle

  for (std::size_t i = 0; i < bands.size(); i++) {
    const Band& band = bands[i];
    if (!band.alive) continue;
    const Vector3d top_centre = band.base + band.axis * band.height;
    const bool is_cone =
      fabs(band.r_bottom - band.r_top) > 1e-9 * std::max(band.r_bottom, band.r_top);

    auto placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
      auto point = new Point(entities, origin);
      auto dir_axis = new Direction(entities, dir);
      auto dir_ref = new Direction(entities, towards);
      return new Axis2Placement(entities, dir_axis, dir_ref, point);
    };

    // a radial direction to measure the surface's own parameterisation from
    const int ref_vertex = band.closed ? band.seam_bottom : band.bottom_set.front();
    const Vector3d rel = vertices[ref_vertex] - band.base;
    const Vector3d ref = (rel - band.axis * band.axis.dot(rel)).normalized();

    SurfaceType *surface = nullptr;
    if (!is_cone) {
      surface = new CylindricalSurface(entities, "", placement(band.base, band.axis, ref),
                                       band.r_bottom);
    } else {
      // ISO 10303 wants a half angle in (0, pi/2), so a cone which narrows
      // along the axis is written from its other end instead.
      const bool widens = band.r_top > band.r_bottom;
      const Vector3d origin = widens ? band.base : top_centre;
      const Vector3d dir = widens ? band.axis : Vector3d(-band.axis);
      const double r0 = widens ? band.r_bottom : band.r_top;
      const double half_angle = atan(fabs(band.r_top - band.r_bottom) / band.height);
      surface = new ConicalSurface(entities, "", placement(origin, dir, ref), r0, half_angle);
    }

    // the rims
    EdgeCurve *rim_edge[2] = {nullptr, nullptr};
    bool rim_sense[2] = {true, true};
    for (int side = 0; side < 2; side++) {
      const bool bottom = side == 0;
      const RimRef& rim = bottom ? rims[i].first : rims[i].second;
      const std::vector<int>& level = bottom ? band.bottom_set : band.top_set;
      const Vector3d centre = bottom ? band.base : top_centre;
      const double radius = bottom ? band.r_bottom : band.r_top;
      const std::set<int> key(level.begin(), level.end());

      const auto shared = shared_rim_edges.find(key);
      if (shared != shared_rim_edges.end()) {
        // the other side of a shared rim already made the circle
        rim_edge[side] = shared->second;
        rim_sense[side] = rim.wall_ccw;
        continue;
      }

      if (band.closed) {
        const int seam = bottom ? band.seam_bottom : band.seam_top;
        const Vector3d seam_rel = vertices[seam] - centre;
        auto circle = new Circle(entities, "", placement(centre, band.axis, seam_rel.normalized()),
                                 radius);
        Vertex *vert = get_vertex(seam);
        // a full circle is one edge whose two ends are the same vertex
        rim_edge[side] = new EdgeCurve(entities, vert, vert, circle, true);
        rim_sense[side] = rim.wall_ccw;
        if (rim.kind == RimRef::OTHER_BAND) shared_rim_edges.emplace(key, rim_edge[side]);
        else rim_of_loop.emplace(rim.loop, std::make_pair(rim_edge[side], !rim.wall_ccw));
      } else {
        // an arc, from one end of the run to the other
        const std::vector<int>& nb_loop = loops[rim.loop];
        const std::size_t n = nb_loop.size();
        const int first = nb_loop[rim.start];
        const int last = nb_loop[(rim.start + rim.count) % n];
        // the neighbour runs opposite to the wall, and a CIRCLE is counter
        // clockwise about its own axis
        const int from = rim.wall_ccw ? last : first;
        const int to = rim.wall_ccw ? first : last;
        auto circle = new Circle(
          entities, "", placement(centre, band.axis, (vertices[from] - centre).normalized()), radius);
        rim_edge[side] = new EdgeCurve(entities, get_vertex(from), get_vertex(to), circle, true);
        rim_sense[side] = rim.wall_ccw;
        arc_subs[rim.loop].push_back({rim.start, rim.count, rim_edge[side], !rim.wall_ccw});
      }
    }

    std::vector<OrientedEdge *> loop;
    std::vector<EdgeCurve *> face_edges_here;
    if (band.closed) {
      auto edge_seam = create_line_edge_curve(get_vertex(band.seam_bottom),
                                              get_vertex(band.seam_top), true);
      loop.push_back(new OrientedEdge(entities, rim_edge[0], rim_sense[0]));
      loop.push_back(new OrientedEdge(entities, edge_seam, true));
      loop.push_back(new OrientedEdge(entities, rim_edge[1], rim_sense[1]));
      loop.push_back(new OrientedEdge(entities, edge_seam, false));
      face_edges_here = {rim_edge[0], rim_edge[1], edge_seam};
    } else {
      // walk back along the bottom rim, up the end edge, back along the top
      // rim, down the other end edge
      const std::vector<int>& nb_bottom = loops[rims[i].first.loop];
      const std::vector<int>& nb_top = loops[rims[i].second.loop];
      const int b_first = nb_bottom[rims[i].first.start];
      const int b_last = nb_bottom[(rims[i].first.start + rims[i].first.count) % nb_bottom.size()];
      const int t_first = nb_top[rims[i].second.start];
      const int t_last = nb_top[(rims[i].second.start + rims[i].second.count) % nb_top.size()];

      bool up_dir = true, down_dir = true;
      EdgeCurve *edge_up = get_line_from_map(edge_map, b_first, t_last, get_vertex(b_first),
                                             get_vertex(t_last), up_dir, merged_edge_cnt);
      EdgeCurve *edge_down = get_line_from_map(edge_map, t_first, b_last, get_vertex(t_first),
                                               get_vertex(b_last), down_dir, merged_edge_cnt);
      loop.push_back(new OrientedEdge(entities, rim_edge[0], rim_sense[0]));
      loop.push_back(new OrientedEdge(entities, edge_up, up_dir));
      loop.push_back(new OrientedEdge(entities, rim_edge[1], rim_sense[1]));
      loop.push_back(new OrientedEdge(entities, edge_down, down_dir));
      face_edges_here = {rim_edge[0], rim_edge[1], edge_up, edge_down};
    }

    auto edge_loop = new EdgeLoop(entities, loop);
    std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};

    // The surface normal of a cylinder or a cone points away from its axis, so
    // a bore - where the material is outside it - is the opposite sense.
    sfaces_extra.push_back(new Face(entities, bounds, surface, band.outward));
    face_edges_extra.push_back(face_edges_here);
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

    // a rim of a recognised band is one circular edge instead of n straight
    // ones, shared with the band's face
    const auto rim = rim_of_loop.find(i);
    if (rim != rim_of_loop.end()) {
      oriented_edges.push_back(new OrientedEdge(entities, rim->second.first, rim->second.second));
      loop_edges[i].push_back(rim->second.first);
    } else if (arc_subs.count(i) != 0) {
      // one or more runs of this loop's edges belong to a partial band and are
      // replaced, in place, by the single arc that band is bounded by
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
      else if (func_name == "CONICAL_SURFACE") ent = new ConicalSurface(entities);
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
