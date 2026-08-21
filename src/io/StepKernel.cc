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
#include "geometry/AnalyticFeatures.h"
#include "utils/printutils.h"
#include <algorithm>  // std::reverse
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

/*! One run of loop edges replaced by a single arc. */
struct ArcSubstitution {
  std::size_t start = 0, count = 0;
  StepKernel::EdgeCurve *edge = nullptr;
  bool sense = true;  // orientation for this loop's own traversal
};

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
                                const std::vector<Vector4d>& faceNormals, double tol, bool analytic,
                                bool approximate)
{
  // `curves` and `surfaces` carry the analytic geometry the model was built
  // from: a ring of N quads is exactly the mesh of an N sided prism, so the
  // facets alone never say which was meant.
  //
  // `surfaces` is what the recogniser matches against. `curves` is deliberately
  // ignored, and it costs nothing today: the only Curve subclass is ArcCurve,
  // the only thing which produces one is import_step.cc, and every arc the
  // exporter writes is a rim it derived from the mesh itself once the band was
  // accepted. A declared arc would only add something for a circular edge whose
  // neighbouring wall is *not* recognised - an imported mesh being written back
  // out - which is a feature nobody has asked for yet.
  (void)curves;
  if (!surfaces.empty()) {
    int cylinders = 0, spheres = 0, tori = 0, patches = 0, grids = 0;
    for (const auto& surface : surfaces) {
      if (dynamic_cast<const CylinderSurface *>(surface.get()) != nullptr) cylinders++;
      else if (dynamic_cast<const SphereSurface *>(surface.get()) != nullptr) spheres++;
      else if (dynamic_cast<const TorusSurface *>(surface.get()) != nullptr) tori++;
      else if (dynamic_cast<const BezierPatchSurface *>(surface.get()) != nullptr) patches++;
      else if (dynamic_cast<const GridSurface *>(surface.get()) != nullptr) grids++;
    }
    // Swept grids are named only when there are some. Every other kind is listed
    // unconditionally because a zero there is informative - a model that meant
    // to declare a cylinder and did not is what the line exists to expose - but
    // a grid is declared by hand and by name, so its absence says nothing, and
    // mentioning it always would churn every fixture quoting this line.
    std::string extra;
    if (grids > 0) extra = ", " + std::to_string(grids) + " swept grid";
    LOG(
      "STEP export: %1$d analytic surface%2$s available (%3$d cylindrical, %4$d spherical, "
      "%5$d toroidal, %6$d Bezier%7$s)",
      int(surfaces.size()), surfaces.size() == 1 ? "" : "s", cylinders, spheres, tori, patches, extra);
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
      // Nothing encloses it, which is the evidence that it is not the boundary
      // of a hole in anything: it is an outer bound. It used to be marked
      // invalid and dropped here, and dropping a face is never an option - its
      // edges are then used by one face instead of two and the shell is open
      // along every one of them. An analytic export of the bayonet lid came out
      // with 94 such edges over 61 faces, all of them in the one annulus where
      // this fired, and the only sign was a line on stdout nobody reads. See
      // *The dropped loop* in doc/step-export.md.
      parents[i] = -1;
      loop_is_hole[i] = 0;
      // Reverse it only if that is what marked it a hole. The rule above also
      // marks a loop a hole because mergeTriangles() gave it a parent, and such
      // a loop can be wound the right way already - reversing it would then turn
      // a correct face into a backwards one. Wind it so the face agrees with the
      // mesh normal, and therefore with the neighbours it shares edges with.
      if (i < faceNormals.size()) {
        const Vector3d ref = faceNormals[i].head<3>();
        if (ref.squaredNorm() > 0.5 && ref.dot(loop_normals[i]) < 0) {
          std::reverse(loops[i].begin(), loops[i].end());
          loop_normals[i] = -loop_normals[i];
        }
      }
      orphan_cnt++;
    }
  }

  if (degenerated_cnt > 0) {
    LOG(message_group::Export_Warning, "STEP export: skipped %1$d degenerated face%2$s", degenerated_cnt,
        degenerated_cnt == 1 ? "" : "s");
  }
  if (reparented_cnt > 0) {
    LOG(message_group::Export_Warning, "STEP export: moved %1$d hole%2$s to the enclosing face",
        reparented_cnt, reparented_cnt == 1 ? "" : "s");
  }
  if (orphan_cnt > 0) {
    LOG(message_group::Export_Warning,
        "STEP export: kept %1$d reversed loop%2$s without an enclosing face as %3$s own face",
        orphan_cnt, orphan_cnt == 1 ? "" : "s", orphan_cnt == 1 ? "its" : "their");
  }

  std::vector<Face *> sfaces_extra;
  std::vector<std::vector<EdgeCurve *>> face_edges_extra;

  // Declared here rather than with the loop building below because a partial
  // cylinder's two end edges are ordinary straight edges shared with a
  // neighbouring planar face, so both have to come from the same map.
  std::map<std::pair<int, int>, EdgeCurve *> edge_map;
  int merged_edge_cnt = 0;

  // Recognise the bands of facets that were modelled as a surface of
  // revolution. The recogniser is format neutral and lives in
  // geometry/AnalyticFeatures - everything below this point is the STEP
  // specific half, turning its answer into entities.
  AnalyticFeatures::Result features;
  std::vector<AnalyticFeatures::Patch> bezier_patches;
  // Parallel to `bezier_patches`: the exact quadric that patch lies on, or null
  // where it is written as the spline it is in general. See quadricOfPatch.
  std::vector<std::shared_ptr<Surface>> patch_quadric;
  features.consumed.assign(face_cnt, 0);  // nothing collapsed unless it says so
  if (analytic) {
    // Say so even when there is nothing, so that a model whose declarations
    // never arrived cannot be mistaken for a build that predates them. The
    // availability line above prints only when the list is non-empty, which
    // makes those two cases look identical - silence.
    if (surfaces.empty()) LOG("STEP export: no analytic surfaces were declared");
    AnalyticFeatures::Mesh mesh;
    mesh.vertices = &vertices;
    mesh.loops = &loops;
    mesh.valid = &loop_valid;
    mesh.is_hole = &loop_is_hole;
    mesh.normals = &loop_normals;
    features = AnalyticFeatures::recogniseSurfacesOfRevolution(mesh, surfaces, model_tol);
    for (const auto& line : features.report) LOG("STEP export: %1$s", line);

    // Bezier patches are recognised here and written further down, with the
    // B_SPLINE_SURFACE_WITH_KNOTS faces. The report is emitted either way, and
    // before the writing: it says whether the fillet declarations reached the
    // exporter and whether the regions and their boundary runs came out right,
    // which a file with no B-spline face in it cannot distinguish from a model
    // that declared none. A patch that is silently not recognised looks exactly
    // like one that was never declared - the same trap the band report exists
    // for.
    std::vector<std::string> patch_report;
    std::vector<AnalyticFeatures::Patch> patches =
      AnalyticFeatures::recogniseBezierPatches(mesh, surfaces, features.consumed, patch_report);
    for (const auto& line : patch_report) LOG("STEP export: %1$s", line);
    std::size_t curved_runs = 0, straight_runs = 0, mesh_edges = 0, covered = 0, live = 0;
    for (const auto& patch : patches) {
      if (!patch.alive) continue;
      live++;
      covered += patch.facets.size();
      for (const auto& run : patch.runs) {
        (run.straight ? straight_runs : curved_runs)++;
        if (!run.verts.empty()) mesh_edges += run.verts.size() - 1;
      }
    }
    if (live > 0) {
      LOG(
        "STEP export: their boundaries are %1$d curved runs over %2$d mesh edges, and "
        "%3$d straight edges",
        int(curved_runs), int(mesh_edges), int(straight_runs));

      // What each run borders decides whether it can be collapsed at all, and
      // it is the last thing the emission needs that has never been seen on a
      // real model. A run left unresolved is one the substitution cannot make.
      int whole = 0, part = 0, shared = 0, stuck = 0;
      for (const auto& patch : patches) {
        if (!patch.alive) continue;
        for (const auto& run : patch.runs) {
          switch (run.kind) {
          case AnalyticFeatures::Patch::Run::WHOLE_LOOP:  whole++; break;
          case AnalyticFeatures::Patch::Run::LOOP_RUN:    part++; break;
          case AnalyticFeatures::Patch::Run::OTHER_PATCH: shared++; break;
          default:                                        stuck++; break;
          }
        }
      }
      LOG(
        "STEP export: those runs border %1$d whole faces, %2$d stretches of a face, "
        "%3$d other patches, %4$d unresolved",
        whole, part, shared, stuck);
      LOG("STEP export: written as %1$d faces instead of %2$d", int(face_cnt - covered + live),
          int(face_cnt));
    }
    // Only patches whose every boundary can be substituted are written. One
    // that cannot stays faceted, which is always a valid export.
    for (auto& patch : patches) {
      if (!patch.alive) continue;
      for (const auto& run : patch.runs) {
        if (run.kind == AnalyticFeatures::Patch::Run::UNRESOLVED) {
          patch.alive = false;
          patch.dropped = "one of its boundaries borders more than one face";
          break;
        }
      }
      if (!patch.alive) continue;
      for (const std::size_t f : patch.facets) features.consumed[f] = 1;
    }

    // Which of the surviving patches are exactly quadrics.
    //
    // Since the fillet's Beziers went rational an edge strip is an exact
    // cylinder quadrant and a corner an exact sphere octant, and writing them
    // as such is the difference between a face a CAD kernel can offset, thread
    // and pattern and one it merely tolerates. Everything else - a varying
    // radius, faces that are not perpendicular - is a genuine spline and stays
    // one.
    patch_quadric.assign(patches.size(), nullptr);
    for (std::size_t p = 0; p < patches.size(); p++) {
      if (!patches[p].alive) continue;
      const auto *bez = dynamic_cast<const BezierPatchSurface *>(patches[p].surface.get());
      if (bez != nullptr) patch_quadric[p] = AnalyticFeatures::quadricOfPatch(*bez, model_tol);
    }

    // A curved run shared with another patch becomes one EdgeCurve used by
    // both, so the two have to agree on what kind of curve it is: a quadric
    // face is bounded by CIRCLEs and a spline face by curves read off its own
    // net, and a face bounded by the other kind is one no importer can check
    // against its surface. Withdrawing a patch whose partner is not a quadric
    // can withdraw that partner's own partner in turn, so this runs to a fixed
    // point rather than once. A *straight* run is a LINE either way and
    // constrains nothing.
    for (bool changed = true; changed;) {
      changed = false;
      for (std::size_t p = 0; p < patches.size(); p++) {
        if (patch_quadric[p] == nullptr) continue;
        for (const auto& run : patches[p].runs) {
          if (run.straight || run.kind != AnalyticFeatures::Patch::Run::OTHER_PATCH) continue;
          if (run.patch < patch_quadric.size() && patch_quadric[run.patch] != nullptr) continue;
          patch_quadric[p] = nullptr;
          changed = true;
          break;
        }
      }
    }

    int quad_cyl = 0, quad_sph = 0;
    for (std::size_t p = 0; p < patches.size(); p++) {
      if (patch_quadric[p] == nullptr) continue;
      if (dynamic_cast<const CylinderSurface *>(patch_quadric[p].get()) != nullptr) quad_cyl++;
      else quad_sph++;
    }
    if (live > 0) {
      LOG(
        "STEP export: %1$d of %2$d patches are exactly quadrics - %3$d cylindrical, "
        "%4$d spherical",
        quad_cyl + quad_sph, int(live), quad_cyl, quad_sph);
    }
    bezier_patches = patches;

    // What is left, and what it would take to do better.
    //
    // Everything above writes a surface only where the model declared one and
    // the mesh fits it exactly. What remains is the geometry OpenSCAD never
    // held the mathematics for - a polyhedron() over a computed point list, a
    // helical thread - and it is written as planes, which is always correct and
    // on the reference part is 99.8% of the uncovered area.
    //
    // `band` is what makes this measurable rather than a matter of taste. The
    // mesh does not say where the true surface is; it says the true surface
    // passes through these vertices and cannot stray further than the sagitta
    // of the facets between them. A fitted surface inside that band asserts
    // nothing the mesh does not already allow. One outside it is inventing
    // geometry - which is exactly what a B-spline fitted through this project's
    // own thread did at the run-out, overshooting 0.378mm where the band is
    // 0.109mm, while scoring 1e-13 against the vertices it interpolated.
    // What a declared sweep claims. This is the other half of the measurement
    // below: uncoveredRegions() says the mesh has lost the ordering a fit would
    // need, and a GridSurface is the generator handing that ordering back. The
    // question it has to answer is whether the declaration still matches the
    // mesh once the booleans have run - a record which matches nothing is
    // harmless but useless, and looks identical to one that was never made.
    for (const auto& surface : surfaces) {
      const auto *grid = dynamic_cast<const GridSurface *>(surface.get());
      if (grid == nullptr) continue;
      std::size_t whole = 0, partly = 0;
      std::vector<Vector3d> unused;  // pointMember's scratch argument, as elsewhere
      for (std::size_t i = 0; i < loops.size(); i++) {
        if (!loop_valid[i] || loop_is_hole[i]) continue;
        std::size_t corners = 0;
        for (const int v : loops[i]) {
          corners += const_cast<GridSurface *>(grid)->pointMember(unused, vertices[v]);
        }
        if (corners == loops[i].size()) whole++;
        else if (corners > 0) partly++;
      }
      LOG(
        // The band is printed because it is the tolerance being trusted. A
        // declared sweep is smooth and the mesh is its tessellation, so a
        // vertex the boolean created lies on a facet rather than on the
        // surface, up to the sagitta of a station away. That is the widest a
        // claim here can be wrong by, and it is the model's own resolution
        // rather than a constant somebody chose.
        "STEP export: a declared %1$dx%2$d sweep claims %3$d facets whole, %4$d cut across it, "
        "within its tessellation band of %5$.4f",
        grid->rows, grid->cols, int(whole), int(partly), grid->tessellationBand());
    }

    // What those claimed facets look like as a face: one sheet, and a boundary
    // split into the runs that would each have to become one curve. That is the
    // step between claiming facets and writing them, and it is reported before
    // anything is written for the same reason the rest of this is - a sweep
    // that cannot be made into a face has to say so, rather than looking like a
    // sweep that was never declared.
    std::vector<std::string> grid_report;
    const std::vector<AnalyticFeatures::Patch> grid_patches =
      AnalyticFeatures::recogniseGridPatches(mesh, surfaces, features.consumed, grid_report);
    for (const auto& line : grid_report) LOG("STEP export: %1$s", line);
    if (!grid_patches.empty()) {
      // Nothing is written yet: a trimmed sweep's boundary is neither a row nor
      // a column of its own net, so each run needs a curve fitted onto the
      // surface before the face can be emitted.
      LOG(message_group::Export_Warning,
          "STEP export: declared sweeps are recognised but not yet written - their boundary "
          "curves are not fitted");
    }

    if (approximate) {
      double smooth_angle = 25.0;
      if (const char *env = getenv("OPENSCAD_STEP_SMOOTH_ANGLE")) smooth_angle = atof(env);
      const std::vector<AnalyticFeatures::SmoothRegion> regions =
        AnalyticFeatures::uncoveredRegions(mesh, features.consumed, smooth_angle * M_PI / 180.0);
      std::size_t left = 0;
      double worst_band = 0.0;
      for (const auto& region : regions) {
        left += region.facets.size();
        worst_band = std::max(worst_band, region.band);
      }
      if (regions.empty()) {
        LOG("STEP export: approximation found nothing left to fit");
      } else {
        LOG(
          "STEP export: %1$d smooth region%2$s left faceted, %3$d facets in all; the "
          "tessellation leaves at most %4$.4f to fit inside",
          int(regions.size()), regions.size() == 1 ? "" : "s", int(left), worst_band);
        const std::size_t show = std::min<std::size_t>(regions.size(), 3);
        for (std::size_t i = 0; i < show; i++) {
          LOG(
            "STEP export:   region of %1$d facets, area %2$.1f, band %3$.4f (typical %4$.4f), "
            "worst dihedral %5$.1f degrees",
            int(regions[i].facets.size()), regions[i].area, regions[i].band, regions[i].median_band,
            regions[i].worst_dihedral * 180.0 / M_PI);
          // Whether a fit is even available here, which is a different question
          // from whether it would be accurate.
          LOG(
            "STEP export:     grid %1$.0f% regular over %2$d interior vertices at valence "
            "%3$d - %4$s",
            regions[i].regularity * 100.0, int(regions[i].interior_vertices),
            int(regions[i].modal_valence),
            regions[i].interior_vertices == 0
              ? "too thin to tell - every vertex is on the boundary"
              : (regions[i].regularity > 0.95
                   ? "the generator's ordering survives, a fit could be made"
                   : "the ordering is gone, only a declaration could describe this"));
        }
        // Nothing is fitted yet, so nothing is at risk: every region above stays
        // faceted. Saying so is the point - a pass which silently did nothing
        // would look exactly like one which found nothing.
        LOG(message_group::Export_Warning,
            "STEP export: approximation is measuring only - all %1$d region%2$s stay faceted",
            int(regions.size()), regions.size() == 1 ? "" : "s");
      }
    }
  }
  const std::vector<AnalyticFeatures::Band>& bands = features.bands;
  const std::vector<std::pair<AnalyticFeatures::RimRef, AnalyticFeatures::RimRef>>& rims = features.rims;
  const std::vector<char>& consumed = features.consumed;

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
    const AnalyticFeatures::Band& band = bands[i];
    if (!band.alive) continue;

    // A torus is closed in both directions, so its face is bounded by nothing
    // but its own two seams: the major circle through one profile station and
    // the meridian circle at one longitude, each a closed circle through the
    // same vertex and each used once in either direction. That is the same four
    // edge loop a periodic cylinder uses - the doubly periodic case needs no
    // new shape, only a second seam - and it needs nothing from the mesh beyond
    // that one vertex, since both circles come out of the declared record.
    // Only the complete one. A run with a free rim at either end - a rounded
    // corner of a revolved profile, which sweeps a quarter of a torus - is a
    // ring like any other band: two rim circles and one seam, written by the
    // generic path below with a ToroidalSurface under it.
    const auto *whole_torus = dynamic_cast<const TorusSurface *>(band.zone.get());
    if (whole_torus != nullptr && band.seam_bottom != band.seam_top) whole_torus = nullptr;
    if (const auto *tor = whole_torus) {
      const Vector3d centre = tor->refpt;
      const Vector3d axis = tor->normdir.normalized();
      const Vector3d corner = vertices[band.seam_bottom];
      const Vector3d rel = corner - centre;
      const double along = rel.dot(axis);
      const Vector3d radial = (rel - axis * along).normalized();
      // the point on the tube's centre circle nearest the corner
      const Vector3d tube = centre + radial * tor->r_major;

      auto placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
        auto point = new Point(entities, origin);
        auto dir_axis = new Direction(entities, dir);
        auto dir_ref = new Direction(entities, towards);
        return new Axis2Placement(entities, dir_axis, dir_ref, point);
      };

      auto surface =
        new ToroidalSurface(entities, "", placement(centre, axis, radial), tor->r_major, tor->r_minor);
      Vertex *vert = get_vertex(band.seam_bottom);

      // the major circle: round the axis, through the corner
      auto major = new Circle(entities, "", placement(centre + axis * along, axis, radial),
                              (rel - axis * along).norm());
      auto edge_major = new EdgeCurve(entities, vert, vert, major, true);

      // the meridian circle: round the tube, in the plane the axis and the
      // radial direction span
      auto meridian = new Circle(
        entities, "", placement(tube, radial.cross(axis), (corner - tube).normalized()), tor->r_minor);
      auto edge_meridian = new EdgeCurve(entities, vert, vert, meridian, true);

      std::vector<OrientedEdge *> loop{
        new OrientedEdge(entities, edge_major, true),
        new OrientedEdge(entities, edge_meridian, true),
        new OrientedEdge(entities, edge_major, false),
        new OrientedEdge(entities, edge_meridian, false),
      };
      auto edge_loop = new EdgeLoop(entities, loop);
      std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};
      sfaces_extra.push_back(new Face(entities, bounds, surface, band.outward));
      face_edges_extra.push_back({edge_major, edge_meridian});
      continue;
    }

    const Vector3d top_centre = band.base + band.axis * band.height;
    const bool is_cone = band.isCone();

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
    if (const auto *tor = dynamic_cast<const TorusSurface *>(band.zone.get())) {
      // A partial torus: the placement comes from the record, not from the
      // band's rims. Its own axis and a radial direction through the seam are
      // what the parameterisation is measured from, exactly as for the complete
      // one above.
      const Vector3d axis = tor->normdir.normalized();
      const Vector3d rel_seam = vertices[band.seam_bottom] - tor->refpt;
      const Vector3d radial = (rel_seam - axis * rel_seam.dot(axis)).normalized();
      surface = new ToroidalSurface(entities, "", placement(tor->refpt, axis, radial), tor->r_major,
                                    tor->r_minor);
    } else if (band.zone != nullptr) {
      const auto *sph = dynamic_cast<const SphereSurface *>(band.zone.get());
      surface = new SphericalSurface(entities, "", placement(sph->refpt, band.axis, ref), sph->r);
    } else if (!is_cone) {
      surface =
        new CylindricalSurface(entities, "", placement(band.base, band.axis, ref), band.r_bottom);
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
      const AnalyticFeatures::RimRef& rim = bottom ? rims[i].first : rims[i].second;
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
        auto circle =
          new Circle(entities, "", placement(centre, band.axis, seam_rel.normalized()), radius);
        Vertex *vert = get_vertex(seam);
        // a full circle is one edge whose two ends are the same vertex
        rim_edge[side] = new EdgeCurve(entities, vert, vert, circle, true);
        rim_sense[side] = rim.wall_ccw;
        if (rim.kind == AnalyticFeatures::RimRef::OTHER_BAND)
          shared_rim_edges.emplace(key, rim_edge[side]);
        else rim_of_loop.emplace(rim.loop, std::make_pair(rim_edge[side], !rim.wall_ccw));
      } else {
        // An arc, from one end of the rim to the other. A CIRCLE is counter
        // clockwise about its own axis, so the arc is always written that way
        // and the face's own direction is carried by rim_sense.
        const int from = rim.ccw_start, to = rim.ccw_end;
        auto circle = new Circle(
          entities, "", placement(centre, band.axis, (vertices[from] - centre).normalized()), radius);
        rim_edge[side] = new EdgeCurve(entities, get_vertex(from), get_vertex(to), circle, true);
        rim_sense[side] = rim.wall_ccw;
        if (rim.kind == AnalyticFeatures::RimRef::OTHER_BAND_ARC) {
          // shared with another partial band: one arc, two curved faces, no
          // planar loop anywhere along it
          shared_rim_edges.emplace(key, rim_edge[side]);
        } else {
          arc_subs[rim.loop].push_back({rim.start, rim.count, rim_edge[side], !rim.wall_ccw});
        }
      }
    }

    std::vector<OrientedEdge *> loop;
    std::vector<EdgeCurve *> face_edges_here;
    if (band.closed) {
      // The seam of a periodic face has to lie *on* the surface. Up a cylinder
      // or a cone that is a straight ruling; over a sphere it is a meridian,
      // and the straight line between the same two vertices is a chord that
      // sags off the surface by far more than the modelling tolerance - 0.05mm
      // on a radius 10 sphere at $fn=32. So a spherical zone seams with an arc
      // of a great circle instead.
      //
      // Nothing else refers to the seam: it is used twice by this one face and
      // by nothing at all in the mesh, so replacing the line costs no
      // neighbouring loop a rewrite.
      EdgeCurve *edge_seam = nullptr;
      if (const auto *tor = dynamic_cast<const TorusSurface *>(band.zone.get())) {
        // Over a torus the seam is a meridian: an arc of the tube's own circle,
        // about the point on the tube's centre circle at the seam's longitude.
        // Same argument as the sphere below - a straight chord sags off the
        // surface - and the same construction, one level in.
        const Vector3d axis = tor->normdir.normalized();
        const Vector3d rel_seam = vertices[band.seam_bottom] - tor->refpt;
        const Vector3d radial = (rel_seam - axis * rel_seam.dot(axis)).normalized();
        const Vector3d tube = tor->refpt + radial * tor->r_major;
        const Vector3d from = (vertices[band.seam_bottom] - tube).normalized();
        const Vector3d to = (vertices[band.seam_top] - tube).normalized();
        const Vector3d normal = from.cross(to);
        auto meridian =
          new Circle(entities, "", placement(tube, normal.normalized(), from), tor->r_minor);
        edge_seam = new EdgeCurve(entities, get_vertex(band.seam_bottom), get_vertex(band.seam_top),
                                  meridian, true);
      } else if (band.zone != nullptr) {
        const auto *sph = dynamic_cast<const SphereSurface *>(band.zone.get());
        const Vector3d from = (vertices[band.seam_bottom] - sph->refpt).normalized();
        const Vector3d to = (vertices[band.seam_top] - sph->refpt).normalized();
        // this normal is the one that makes the sweep from `from` to `to`
        // counter clockwise and shorter than half a turn, which a zone's
        // latitude range always is - the flat cap at either end sees to that
        const Vector3d normal = from.cross(to);
        auto meridian =
          new Circle(entities, "", placement(sph->refpt, normal.normalized(), from), sph->r);
        edge_seam = new EdgeCurve(entities, get_vertex(band.seam_bottom), get_vertex(band.seam_top),
                                  meridian, true);
      } else {
        edge_seam =
          create_line_edge_curve(get_vertex(band.seam_bottom), get_vertex(band.seam_top), true);
      }
      loop.push_back(new OrientedEdge(entities, rim_edge[0], rim_sense[0]));
      loop.push_back(new OrientedEdge(entities, edge_seam, true));
      loop.push_back(new OrientedEdge(entities, rim_edge[1], rim_sense[1]));
      loop.push_back(new OrientedEdge(entities, edge_seam, false));
      face_edges_here = {rim_edge[0], rim_edge[1], edge_seam};
    } else {
      // walk along the bottom rim, up the end edge, back along the top rim,
      // down the other end edge - so the ends are where one rim's traversal
      // finishes and the other's begins
      const int b_from = rims[i].first.traversalStart();
      const int b_to = rims[i].first.traversalEnd();
      const int t_from = rims[i].second.traversalStart();
      const int t_to = rims[i].second.traversalEnd();

      bool up_dir = true, down_dir = true;
      EdgeCurve *edge_up = get_line_from_map(edge_map, b_to, t_from, get_vertex(b_to),
                                             get_vertex(t_from), up_dir, merged_edge_cnt);
      EdgeCurve *edge_down = get_line_from_map(edge_map, t_to, b_from, get_vertex(t_to),
                                               get_vertex(b_from), down_dir, merged_edge_cnt);
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

  // ---- Bezier patches ----------------------------------------------------
  //
  // A patch face is bounded by its runs, each of which is one curve read off
  // the same control net the surface is written from - so the curve provably
  // lies on the surface rather than approximately. A seam between two patches
  // is one EdgeCurve used by both, in opposite senses; a run cutting into a
  // planar neighbour is spliced into that loop through the same substitution
  // the arcs use.
  {
    std::map<std::set<int>, EdgeCurve *> run_edges;  // run vertices -> its curve
    auto placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
      auto point = new Point(entities, origin);
      auto dir_axis = new Direction(entities, dir);
      auto dir_ref = new Direction(entities, towards);
      return new Axis2Placement(entities, dir_axis, dir_ref, point);
    };
    for (std::size_t pi = 0; pi < bezier_patches.size(); pi++) {
      const AnalyticFeatures::Patch& patch = bezier_patches[pi];
      if (!patch.alive) continue;
      const auto *bez = dynamic_cast<const BezierPatchSurface *>(patch.surface.get());
      if (bez == nullptr) continue;
      const std::shared_ptr<Surface> quadric = pi < patch_quadric.size() ? patch_quadric[pi] : nullptr;

      SurfaceType *surface = nullptr;
      if (const auto *cyl = dynamic_cast<const CylinderSurface *>(quadric.get())) {
        // The reference direction is the radius through the start of the first
        // rail, so the face occupies theta from zero forwards rather than a
        // stretch wrapping through the surface's own seam. quadricOfPatch has
        // already oriented the axis to make that sweep the positive one.
        const Vector3d axis = cyl->normdir.normalized();
        const Vector3d rel = bez->control(0, 0) - cyl->refpt;
        const Vector3d ref = (rel - axis * axis.dot(rel)).normalized();
        surface = new CylindricalSurface(entities, "", placement(cyl->refpt, axis, ref), cyl->r);
      } else if (const auto *sph = dynamic_cast<const SphereSurface *>(quadric.get())) {
        // The polar axis is the patch's apex, so the octant is the (theta, phi)
        // rectangle below the pole and its three bounding arcs are two
        // meridians and one arc of the equator - no seam crosses it.
        const Vector3d axis = sph->normdir.normalized();
        const Vector3d rel = bez->control(0, 0) - sph->refpt;
        const Vector3d ref = (rel - axis * axis.dot(rel)).normalized();
        surface = new SphericalSurface(entities, "", placement(sph->refpt, axis, ref), sph->r);
      } else {
        std::vector<std::vector<Point *>> net;
        std::vector<std::vector<double>> wnet;
        for (int i = 0; i <= bez->degree_u; i++) {
          std::vector<Point *> row;
          std::vector<double> row_w;
          for (int j = 0; j <= bez->degree_v; j++) {
            row.push_back(new Point(entities, bez->control(i, j)));
            row_w.push_back(bez->weight(i, j));
          }
          net.push_back(row);
          if (bez->isRational()) wnet.push_back(row_w);
        }
        // A fillet's patch is rational - its middle weight is what makes the arc a
        // circle rather than a parabola - and the weights have to be written, or
        // the face describes a different surface from the mesh it replaces.
        surface = new BSplineSurface(entities, "", bez->degree_u, bez->degree_v, net, wnet);
      }

      std::vector<OrientedEdge *> loop;
      std::vector<EdgeCurve *> face_edges_here;
      for (const auto& run : patch.runs) {
        const std::set<int> key(run.verts.begin(), run.verts.end());
        Vertex *from = get_vertex(run.verts.front());
        Vertex *to = get_vertex(run.verts.back());
        EdgeCurve *edge = nullptr;
        const auto known = run_edges.find(key);
        if (known != run_edges.end()) {
          edge = known->second;
        } else if (run.straight) {
          edge = create_line_edge_curve(from, to, true);
          run_edges.emplace(key, edge);
        } else {
          // On a quadric face the run is a circular arc lying on that quadric,
          // and writing it as a CIRCLE rather than as a spline off the net is
          // the other half of the same change: a kernel offsets and patterns
          // along a circle, and only tolerates a spline. The two describe the
          // same curve, so the substitution into the neighbouring planar loop
          // is unchanged. The classification above guarantees the patch on the
          // far side of a shared run agrees about which of the two it is.
          Vector3d centre, normal;
          double radius = 0;
          if (quadric != nullptr &&
              AnalyticFeatures::runCircle(patch, run, vertices, centre, normal, radius)) {
            const Vector3d ref = (vertices[run.verts.front()] - centre).normalized();
            auto circle = new Circle(entities, "", placement(centre, normal, ref), radius);
            edge = new EdgeCurve(entities, from, to, circle, true);
          } else {
            std::vector<Point *> cp;
            std::vector<double> cw;
            for (const auto& c : AnalyticFeatures::runControlPoints(patch, run, vertices, &cw)) {
              cp.push_back(new Point(entities, c));
            }
            auto curve = new BSplineCurve(entities, "", cp, cw);
            edge = new EdgeCurve(entities, from, to, curve, true);
          }
          run_edges.emplace(key, edge);
        }
        // The curve was built running from whichever patch reached it first.
        const bool sense = edge->vert1 == from;
        loop.push_back(new OrientedEdge(entities, edge, sense));
        face_edges_here.push_back(edge);

        // and the neighbouring planar face gives up the segments it replaces
        // The region boundary is walked in the direction its own facets go, so
        // the face on the far side necessarily goes the other way. Two faces
        // traversing a shared edge the same way is precisely what leaves a
        // shell open.
        if (run.kind == AnalyticFeatures::Patch::Run::WHOLE_LOOP) {
          rim_of_loop[run.loop] = {edge, !sense};
        } else if (run.kind == AnalyticFeatures::Patch::Run::LOOP_RUN) {
          arc_subs[run.loop].push_back({run.start, run.count, edge, !sense});
        }
      }

      // Which way the patch faces: compare its own normal with the mesh it
      // replaces, since du x dv has no reason to agree with the facets.
      //
      // A quadric's normal is the radial one - away from the axis of a cylinder,
      // away from the centre of a sphere - so a fillet, where the material is
      // on the far side of the surface from the axis, is the opposite sense.
      // Same question as the bands answer with Band::outward, one facet at a
      // time. The facet centroid is used rather than a corner, which on a
      // corner patch can be the apex.
      bool outward = true;
      if (!patch.facets.empty()) {
        const std::vector<int>& facet = loops[patch.facets[0]];
        const Vector3d& mesh_normal = loop_normals[patch.facets[0]];
        if (quadric != nullptr && !facet.empty()) {
          Vector3d centroid = Vector3d::Zero();
          for (const int v : facet) centroid += vertices[v];
          centroid /= double(facet.size());
          Vector3d radial;
          if (const auto *cyl = dynamic_cast<const CylinderSurface *>(quadric.get())) {
            const Vector3d axis = cyl->normdir.normalized();
            const Vector3d rel = centroid - cyl->refpt;
            radial = rel - axis * axis.dot(rel);
          } else {
            radial = centroid - quadric->refpt;
          }
          outward = radial.dot(mesh_normal) > 0;
        } else {
          double u = 0.5, v = 0.5;
          const_cast<BezierPatchSurface *>(bez)->project(vertices[facet[0]], u, v);
          const double h = 1e-6;
          const Vector3d du =
            bez->evaluate(std::min(1.0, u + h), v) - bez->evaluate(std::max(0.0, u - h), v);
          const Vector3d dv =
            bez->evaluate(u, std::min(1.0, v + h)) - bez->evaluate(u, std::max(0.0, v - h));
          outward = du.cross(dv).dot(mesh_normal) > 0;
        }
      }

      auto edge_loop = new EdgeLoop(entities, loop);
      std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};
      sfaces_extra.push_back(new Face(entities, bounds, surface, outward));
      face_edges_extra.push_back(face_edges_here);
    }
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
        EdgeCurve *edge_curve = get_line_from_map(edge_map, ind, indn, get_vertex(ind), get_vertex(indn),
                                                  edge_dir, merged_edge_cnt);
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
    if (ref.norm() < 1e-12) ref = AnalyticFeatures::perpendicular(norm);
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
    LOG(message_group::Export_Error, "Cannot open %1$s", file_name);
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
          LOG(message_group::Export_Warning, "Unknown Type %1$s", func_name);
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
