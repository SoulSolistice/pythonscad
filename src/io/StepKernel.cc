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

#include <limits>
#include "StepKernel.h"
#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Exact.h"
#include "geometry/recognition/Seam.h"
#include "geometry/recognition/Pipeline.h"
#include "geometry/recognition/Reports.h"
#include "io/step/BoundaryCycles.h"
#include "io/step/CornerLadder.h"
#include "io/step/CrossingArc.h"
#include "io/step/CurveChecks.h"
#include "io/step/LoopGeometry.h"
#include "io/step/PlaneSections.h"
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
using namespace step;

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

// The implicit-surface machinery these passes solve with - `surfaceImplicit`,
// `projectOntoBoth`, `isQuadric`, `declaredBand` - lives in AnalyticFeatures,
// beside `closestOnSurface`, because it is geometry rather than STEP and
// `export_step.cc` needs the same Newton for its corner placement.
using AnalyticFeatures::declaredBand;
using AnalyticFeatures::isQuadric;
using AnalyticFeatures::projectOntoBoth;
using AnalyticFeatures::surfaceImplicit;

using AnalyticFeatures::projectOntoSurfaceAndPlanes;

/*! One run of loop edges replaced by a single arc. */
struct ArcSubstitution {
  std::size_t start = 0, count = 0;
  StepKernel::EdgeCurve *edge = nullptr;
  bool sense = true;  // orientation for this loop's own traversal
};

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

void StepKernel::build_tri_body(
  const char *name, const std::vector<Vector3d>& mesh_vertices, const std::vector<IndexedFace>& faces,
  const std::vector<std::shared_ptr<Curve>>& curves,
  const std::vector<std::shared_ptr<Surface>>& surfaces, const std::vector<int32_t>& faceOrigin,
  const std::map<int, Vector3d>& cornerMoves, const std::map<int, std::size_t>& singleOwner,
  const std::map<int, std::pair<std::size_t, std::size_t>>& ownerSplit,
  const std::map<int32_t, std::vector<std::size_t>>& owned, const std::vector<int>& faceParents,
  const std::vector<Vector4d>& faceNormals, double tol, bool analytic, bool approximate,
  const std::map<int, std::vector<std::size_t>>& cornerOwners)
{
  // A working copy, because the corner placement below moves junction corners
  // onto the curve their two declared owners cross along, once recognition has
  // run and it is known which faces would mind. Nothing else writes to this.
  std::vector<Vector3d> vertices = mesh_vertices;

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
    int cylinders = 0, spheres = 0, tori = 0, patches = 0, grids = 0, cones = 0, planes = 0,
        elliptic = 0;
    for (const auto& surface : surfaces) {
      if (dynamic_cast<const PlaneSurface *>(surface.get()) != nullptr) planes++;
      else if (dynamic_cast<const CylinderSurface *>(surface.get()) != nullptr) cylinders++;
      else if (dynamic_cast<const SphereSurface *>(surface.get()) != nullptr) spheres++;
      else if (dynamic_cast<const TorusSurface *>(surface.get()) != nullptr) tori++;
      else if (dynamic_cast<const BezierPatchSurface *>(surface.get()) != nullptr) patches++;
      else if (dynamic_cast<const GridSurface *>(surface.get()) != nullptr) grids++;
      else if (dynamic_cast<const ConeSurface *>(surface.get()) != nullptr) cones++;
      else if (dynamic_cast<const EllipticCylinderSurface *>(surface.get()) != nullptr) elliptic++;
    }
    // Swept grids are named only when there are some. Every other kind is listed
    // unconditionally because a zero there is informative - a model that meant
    // to declare a cylinder and did not is what the line exists to expose - but
    // a grid is declared by hand and by name, so its absence says nothing, and
    // mentioning it always would churn every fixture quoting this line.
    std::string extra;
    if (grids > 0) extra = ", " + std::to_string(grids) + " swept grid";
    // A cone is named by hand for the same reason a grid is, so it is listed on
    // the same terms: only when there is one. Listing it always would say
    // nothing and would rewrite the EXPECT line of every fixture here.
    if (cones > 0) extra += ", " + std::to_string(cones) + " conical";
    // And an elliptic cylinder, on the same terms.
    if (elliptic > 0) extra += ", " + std::to_string(elliptic) + " elliptic";
    char pn[64] = "";
    if (planes > 0) {
      snprintf(pn, sizeof(pn), ", and %d declared plane%s", planes, planes == 1 ? "" : "s");
    }
    const std::string plane_note(pn);
    LOG(
      // Planes are counted apart from the curved surfaces rather than added in.
      // This census is what the recognisers have to match facets against, and a
      // declared plane is not that - a merged planar face is already exact, so
      // nothing is recognised *onto* a plane. It is declared so that a face of
      // the model can be told from a chord of a tessellation, which happens
      // after the merge and reads this list directly.
      "STEP export: %1$d analytic surface%2$s available (%3$d cylindrical, %4$d spherical, "
      "%5$d toroidal, %6$d Bezier%7$s)%8$s",
      int(surfaces.size()) - planes, surfaces.size() - planes == 1 ? "" : "s", cylinders, spheres, tori,
      patches, extra, plane_note.c_str());
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

  // The same rule as `canonical` above, applied at the moment it can actually be
  // obeyed. That map is built from `vertices` here, before the corner placement
  // has moved any of them, so it catches vertices the *mesh* left coincident and
  // cannot catch vertices the placement makes coincident - which is a thing a
  // correct placement does. Where a cylinder, two cones and a plane meet, the
  // corners the mesh keeps apart are one point on the surfaces, and moving each
  // onto its own owners brings them together.
  //
  // Measured on lid10, on the export that relaxed the plane veto: three mesh
  // vertices - one cylinder-and-plane corner and two cone-and-plane corners -
  // all placed at (-66.905705, -48.609840, 95.0), and three VERTEX_POINTs
  // written for one point. `validatestep.py` calls that "vertices are not
  // shared" and it is the same defect the comment above describes, arriving too
  // late for the map above to see it.
  //
  // Resolving by position *here* is late enough: every caller is emitting faces,
  // which happens after the moves are applied. Exact coordinates rather than a
  // tolerance, for the same reason the map above uses them - two corners placed
  // on the same crossing of the same surfaces land on the same doubles, and a
  // tolerance would additionally merge things that are merely close.
  std::vector<Vertex *> step_verts(vertices.size(), nullptr);
  std::map<std::tuple<double, double, double>, Vertex *> vertex_at;
  auto get_vertex = [&](int ind) {
    if (step_verts[ind] == nullptr) {
      const auto key = std::make_tuple(vertices[ind][0], vertices[ind][1], vertices[ind][2]);
      const auto it = vertex_at.find(key);
      if (it != vertex_at.end()) {
        step_verts[ind] = it->second;
      } else {
        auto point = new Point(entities, vertices[ind]);
        step_verts[ind] = new Vertex(entities, point);
        vertex_at.emplace(key, step_verts[ind]);
      }
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
  // Which gate rejected them, and how much area went with it. A skipped face
  // leaves every one of its edges used by one face only, so the shell stops
  // being closed - and "skipped 15 degenerated faces" does not say whether that
  // is 15 slivers a merge left behind or 15 real faces being dropped on the
  // floor. The two want opposite responses, so name them apart.
  int collapsed_cnt = 0, zero_area_cnt = 0;
  double lost_area = 0.0;
  // The corners of every sliver we drop. They are wanted again below: a sliver
  // of no area is how a mesh stitches a T-junction, and the vertex in the
  // middle of it has to be put back into the edge that runs past it.
  // Keyed by the two ends of the sliver's span, valued by the points in
  // between, in order along it.
  // Each middle point is kept with its position along the span, measured from
  // the lower-numbered end, so two slivers which share a span merge rather than
  // one replacing the other.
  std::map<std::pair<int, int>, std::vector<std::pair<double, int>>> sliver_spans;

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
      // Fewer than three distinct points left: the loop closed on itself.
      degenerated_cnt++;
      collapsed_cnt++;
      continue;
    }

    Vector3d norm = polygonNormal(vertices, loop);
    if (norm.norm() < area_eps) {
      // zero area polygon, exporting it would create a face without a usable
      // surface normal
      degenerated_cnt++;
      zero_area_cnt++;
      lost_area += 0.5 * norm.norm();
      recordSliverSpan(vertices, loop, sliver_spans);
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

  // Put the T-junctions back that the skipped slivers were holding shut.
  //
  // A face of no area is not noise. It is three or more collinear points, and
  // the mesh uses it to stitch a vertex sitting in the interior of another
  // face's edge back into the surface - which is why the longest edge of such
  // a sliver is exactly the sum of the others. Refusing to write it is right:
  // a face with no normal has no surface to be written on. Dropping it and
  // stopping there is not. The neighbour still spans the whole edge, the
  // vertex in the middle belongs to no face at all, and every edge the sliver
  // carried is left used once - which is exactly the "shell is not closed" an
  // importer reports, from a mesh that was manifold when it arrived.
  //
  // So split the edge that runs past the vertex. The neighbour's two halves
  // then pair with the two faces that met at the junction, and nothing needs
  // the sliver in order to say so.
  //
  // The tolerance is tight on purpose. These points are collinear to within
  // rounding - the perpendicular distance measured on the lid is around
  // 1e-13 - so there is no reason to reach for model_tol here, and good reason
  // not to: at 1e-5 this would start moving vertices that were never on the
  // edge in the first place.
  int split_cnt = 0;
  std::set<int> welded_verts;
  std::set<std::pair<int, int>> welded_spans;
  if (!sliver_spans.empty()) {
    for (std::size_t i = 0; i < face_cnt; i++) {
      if (!loop_valid[i]) continue;
      const std::size_t n = loops[i].size();
      std::vector<int> out;
      out.reserve(n);
      bool changed = false;
      for (std::size_t j = 0; j < n; j++) {
        const int p = loops[i][j];
        const int q = loops[i][(j + 1) % n];
        out.push_back(p);

        // Only an edge that *is* a sliver's span gets split, and only by that
        // sliver's own middle points. Splitting every edge which merely passes
        // through one of them is too much: two faces can share a span, and
        // both would then hand the same half-edge to a third face.
        const auto it = sliver_spans.find(p < q ? std::make_pair(p, q) : std::make_pair(q, p));
        if (it == sliver_spans.end()) continue;

        // Stored low-to-high along the span; this edge may run either way.
        const std::vector<std::pair<double, int>>& between = it->second;
        if (p < q) {
          for (auto m = between.begin(); m != between.end(); ++m) {
            if (out.back() == m->second) continue;
            out.push_back(m->second);
            welded_verts.insert(m->second);
          }
        } else {
          for (auto m = between.rbegin(); m != between.rend(); ++m) {
            if (out.back() == m->second) continue;
            out.push_back(m->second);
            welded_verts.insert(m->second);
          }
        }
        split_cnt++;
        welded_spans.insert(it->first);
        changed = true;
      }
      // Collinear insertions cannot change the plane, so loop_normals[i] and
      // everything derived from it downstream stay as they were.
      if (changed) loops[i] = out;
    }
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
    // The loop's first vertex, and not a point interior to it. Probing with an
    // interior point finds more enclosing faces, which sounds strictly better
    // and is not: on concentric rings it parents a hole onto a face which does
    // not own it, and the solid comes apart into two shells. OpenCASCADE reads
    // the result as two solids and adds them - 68422 where step-nested-rings
    // measures 31901, and 998121 where the bayonet lid measures 223482 - and
    // rejects the faces with InvalidImbricationOfWires. Tried, measured,
    // reverted; see doc/step-export-development.md.
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
      // One probe point says the loop starts inside this candidate; it does not
      // say the loop *is* inside it. See loopContains.
      if (!loopContains(vertices, loops[j], loops[i], drop)) continue;
      const double area = loopArea2d(cand);
      if (found == -1 || area < best_area) {
        found = int(j);
        best_area = area;
      }
    }

    if (found != -1) {
      parents[i] = found;
      if (found != previous) reparented_cnt++;
    } else if (previous != -1 && loop_valid[previous] && !loop_is_hole[previous] &&
               loopContains(vertices, loops[previous], loops[i], drop)) {
      // Keep what mergeTriangles found, but only if it actually holds the loop.
      // Without that condition this branch puts back the very parent the search
      // above just rejected, which is how three faces of the bayonet lid kept
      // their InvalidImbricationOfWires after the search learned to check.
      parents[i] = previous;
    } else {
      // Nothing encloses it, which is the evidence that it is not the boundary
      // of a hole in anything: it is an outer bound. It used to be marked
      // invalid and dropped here, and dropping a face is never an option - its
      // edges are then used by one face instead of two and the shell is open
      // along every one of them. An analytic export of the bayonet lid came out
      // with 94 such edges over 61 faces, all of them in the one annulus where
      // this fired, and the only sign was a line on stdout nobody reads. See
      // *The defects the checks exist for* in doc/step-export-development.md.
      parents[i] = -1;
      loop_is_hole[i] = 0;
      // Keep the winding the loop arrived with. It used to be reversed to agree
      // with the bucket's mesh normal, on the reasoning that a face should agree
      // with the neighbours it shares edges with - but the bucket normal is the
      // wrong reference for exactly the loops which reach this branch. They are
      // the ones nothing encloses, and on the bayonet lid they are notches at
      // the rim whose own winding is what the mesh said; turning them over is
      // what made them disagree with their neighbours. Reversing produced 15
      // edges used twice in the same direction there, and not reversing
      // produces none.
      orphan_cnt++;
    }
  }

  const int welded_span_cnt = int(welded_spans.size());
  if (degenerated_cnt > 0) {
    LOG(message_group::Export_Warning,
        "STEP export: skipped %1$d degenerated face%2$s - %3$d collapsed to fewer "
        "than three distinct points, %4$d had no area (%5$.3g in total)",
        degenerated_cnt, degenerated_cnt == 1 ? "" : "s", collapsed_cnt, zero_area_cnt, lost_area);
  }
  if (zero_area_cnt > welded_span_cnt) {
    // A sliver whose span no face turned out to cross is one this pass could
    // not put back, and its edges stay used once.
    LOG(message_group::Export_Warning,
        "STEP export: %1$d skipped sliver%2$s had no neighbouring edge to weld into, so "
        "the shell may not be closed there",
        zero_area_cnt - welded_span_cnt, zero_area_cnt - welded_span_cnt == 1 ? "" : "s");
  }
  if (!welded_verts.empty()) {
    const int wv = int(welded_verts.size());
    LOG(message_group::Export_Warning,
        "STEP export: welded %1$d T-junction vertex%2$s back into %3$d edge%4$s, so the "
        "faces beside the skipped slivers still pair up",
        wv, wv == 1 ? "" : "es", split_cnt, split_cnt == 1 ? "" : "s");
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
  // What recognition hands the writer - see recognition::runPipeline. The
  // names below are views into it, so the writer reads as it always has.
  recognition::Recognition rec;
  AnalyticFeatures::Result& features = rec.features;
  std::vector<AnalyticFeatures::Patch>& bezier_patches = rec.bezier_patches;
  // Runs whose curve is exactly a circular arc, keyed by their vertices - which
  // is how the emitter shares one EdgeCurve between the two faces that meet
  // there. Decided per run rather than per face; see where it is filled.
  std::set<std::set<int>>& circular_runs = rec.circular_runs;
  // Declared sweeps whose claimed region can be written as one face: a strip,
  // whose boundary stays inside the surface's parameter rectangle. One closing
  // around its profile crosses the surface's seam and is left faceted.
  std::vector<AnalyticFeatures::Patch>& grid_faces = rec.grid_faces;
  std::vector<AnalyticFeatures::Patch>& quadric_faces = rec.quadric_faces;
  // The plane sections that both of their two faces agree to write as one
  // conic, each named by the set of mesh vertices it runs through. A curve one
  // face writes and its neighbour does not is a hole in the shell, so this is a
  // joint decision and not one a face can take for itself.
  std::set<std::set<int>> section_curves;
  // The planes the model declared, kept where the writer can reach them. A
  // declared plane cannot drift with the mesh, and a section derived from one is
  // exact rather than merely close, so it is preferred wherever it agrees with
  // the face at hand.
  std::vector<std::pair<Vector3d, Vector3d>> declared_planes;  // point, unit normal
  // Which entry of `surfaces` each of those came from, so that a face written
  // on one can be attributed back to the declaration it honoured. Parallel to
  // `declared_planes`; `surfaces.size()` for a fitted plane, which no census
  // row is owed.
  std::vector<std::size_t> declared_plane_index;
  // The declaration census, indexed as `surfaces`. A declaration is written
  // when a face stands on it, evidence when something read it to identify a
  // surface without a face ever standing on it, and otherwise unused - which
  // *a declaration that is not taken is a bug report* makes a defect to
  // explain. See doc/step-rungs.md, *How to read a result*.
  std::vector<char> decl_written(surfaces.size(), 0);
  // What class actually wrote each facet: 0 the tessellation, 1 a declared
  // plane, 2 a declared curved surface, 3 a surface the approximation fitted.
  // Per facet and not per declaration, because "a face stands on this
  // declaration" cannot see a declaration that kept one facet of a hundred,
  // and cannot see ground a fit took.
  std::vector<unsigned char> facet_class(faces.size(), 0);
  struct SectionMiss {
    double tilt, offset;
  };
  std::vector<SectionMiss> section_misses;
  // Edges that lie on the curve where two declared quadrics cross, and which two
  // they are. Both faces derive the arc from the same pair of declarations, so
  // they agree on it exactly and the shell closes without either having to know
  // what the other did - which is what makes a curve taken from declarations
  // different in kind from one fitted to the mesh.
  std::map<std::pair<int, int>, std::pair<const Surface *, const Surface *>> crossing_edges;
  std::set<std::pair<int, int>> crossing_keys;  // the same, as the sections' skip set
  double crossing_fit = 0;                      // the worst any of those arcs leaves either surface
  // What the fitter knew and used to discard. A count of chorded edges cannot
  // tell a fit that missed by 3.4e-05 from one that missed by a tenth of a
  // millimetre, and those want different work.
  std::size_t arc_refused = 0, arc_no_fit = 0, arc_creased = 0, arc_uncreased = 0;
  std::vector<double> arc_errors, arc_on_quadric, arc_on_fitted;
  // Deciding which plane sections get written, over a given set of faces. This
  // is a function rather than a step because it has to be asked **twice**, on
  // two different meshes: once while it is being settled which faces are
  // analytic at all, and again immediately before they are written. The corner
  // placement runs between those two points and moves vertices - by up to 0.07
  // on step-band-family - so a section agreed on the first mesh need not exist
  // on the second, and a curve one face writes and its neighbour does not is a
  // hole in the shell. Measured before this was split in two: 132 edges with one
  // face, every one of them an ellipse the other side had stopped seeing.
  std::function<void(const std::vector<const AnalyticFeatures::Patch *>&)> decide_sections;
  // Which facet loops are not available to be written as planar faces. Filled
  // by the quadric pass and read again by its writer, so both sides of every
  // section agree about who is across it.
  std::vector<char> taken;
  // The planes that cut each quadric surface, gathered once so that every face
  // of that surface sees the same list and they agree about the curve between
  // them. Filled by the quadric pass, read again by its writer.
  std::map<const Surface *, std::vector<CutPlane>> section_planes;
  // Parallel to `bezier_patches`: the exact quadric that patch lies on, or null
  // where it is written as the spline it is in general. See quadricOfPatch.
  std::vector<std::shared_ptr<Surface>>& patch_quadric = rec.patch_quadric;
  features.consumed.assign(face_cnt, 0);  // nothing collapsed unless it says so
  if (analytic) {
    // The writer's half of "can this region be written": the sections a
    // standing set of quadric regions agree on, and whether each one's whole
    // boundary then lies on its surface. Defined here, ahead of recognition,
    // because it is this file's boundary machinery; recognition asks it
    // through recognition::Writability and decides what to do with the answer.
    auto boundary_lies_on_surface = [&](const AnalyticFeatures::Patch& patch,
                                        const std::set<std::set<int>>& agreed) {
      const auto *axial = dynamic_cast<const AxialSurface *>(patch.surface.get());
      if (axial == nullptr) return false;
      const Vector3d axis = axial->axis();
      const Vector3d origin = axial->refpt;
      double radius = 0, slope = 0;
      // A cylinder's and a cone's rims round the axis are written as circles by
      // the arc post-pass, which reads CYLINDRICAL_SURFACE and CONICAL_SURFACE
      // and nothing else; any other surface's rim is written as the section it
      // is, or not exact at all.
      bool circular = true;
      if (const auto *cyl = dynamic_cast<const CylinderSurface *>(patch.surface.get())) {
        radius = cyl->r;
      } else if (const auto *con = dynamic_cast<const ConeSurface *>(patch.surface.get())) {
        radius = con->r;
        slope = con->slope;
      } else {
        circular = false;
      }
      for (const auto& entry : boundaryCycles(patch, vertices, loops, loop_valid, taken,
                                              section_planes[patch.surface.get()], crossing_keys)) {
        const BoundaryCycle& cycle = entry.second;
        const std::size_t n = cycle.verts.size();
        if (n < 3) continue;
        // Edges a plane section covers are exact as the conic written for
        // them, so they are not asked to lie on the surface as chords - but
        // only where the face on the other side writes the same conic, which
        // is what `agreed` records, decided over all the faces at once.
        std::vector<char> covered(n, 0);
        for (const auto& bs : cycle.sections) {
          std::set<int> key;
          for (std::size_t k = 0; k < bs.count; k++) key.insert(cycle.verts[(bs.start + k) % n]);
          if (agreed.find(key) == agreed.end()) continue;
          for (std::size_t k = 0; k + 1 < bs.count; k++) covered[(bs.start + k) % n] = 1;
        }
        // How far a point is off the surface, radially. A cone's radius is a
        // function of height, which is the whole of the difference.
        auto off_surface = [&](const Vector3d& p) {
          if (!circular) return axial->offset(p);
          const Vector3d rel = p - origin;
          const double along = rel.dot(axis);
          const double want = radius + slope * along;
          return fabs((rel - axis * along).norm() - want);
        };
        for (std::size_t i = 0; i < n; i++) {
          if (covered[i]) continue;
          // An edge on the curve where this surface crosses another declared
          // one is written as that curve, so it is not asked to be a chord.
          const int u = cycle.verts[i], v = cycle.verts[(i + 1) % n];
          if (crossing_edges.count({std::min(u, v), std::max(u, v)}) != 0) continue;
          const Vector3d a = vertices[cycle.verts[i]] - origin;
          const Vector3d b = vertices[cycle.verts[(i + 1) % n]] - origin;
          const Vector3d along = b - a;
          const double rise = fabs(along.dot(axis));
          // Round the axis at constant height is exact as the arc the
          // post-pass will write. A straight edge is exact when the surface
          // contains the whole line, and the midpoint settles that on its own:
          // a line meets a quadric twice unless it lies in it, so a third
          // point on the surface means every point of it is. That is the test
          // rather than "parallel to the axis", which is only a cylinder's
          // answer - a cone's rulings run to its apex, and calling those
          // chords refused four regions of step-cut-cone for edges that were
          // exactly on the cone all along.
          const Vector3d mid = origin + (a + b) * 0.5;
          if ((rise > 1e-7 || !circular) && off_surface(mid) > 1e-7) return false;
        }
      }
      return true;
    };

    decide_sections = [&](const std::vector<const AnalyticFeatures::Patch *>& standing) {
      // Whose facets are not there to be written as a planar face: what an
      // earlier pass consumed, plus what this one is standing to claim.
      taken = features.consumed;
      for (const auto *patch : standing) {
        for (const std::size_t f : patch->facets) taken[f] = 1;
      }
      // A loop a band will use as its rim is spoken for too. It is still
      // written as a planar face, so it does not look consumed, but the band
      // replaces a stretch of it with a circle and the writer takes the band's
      // answer - so a second curve handed to the same loop is dropped in
      // silence.
      for (const auto& rim : features.rims) {
        for (const AnalyticFeatures::RimRef *side : {&rim.first, &rim.second}) {
          if (side->kind == AnalyticFeatures::RimRef::WHOLE_LOOP ||
              side->kind == AnalyticFeatures::RimRef::LOOP_RUN) {
            if (side->loop < taken.size()) taken[side->loop] = 1;
          }
        }
      }
      // And the planes those faces cut each surface with. Gathered per surface
      // rather than per region, because a plane that cuts a cone cuts all of
      // it: the region meeting the cut along a single edge could never have
      // seen the plane from that edge alone, and refused the whole surface for
      // it.
      section_planes.clear();
      section_misses.clear();
      for (const auto *patch : standing) {
        std::vector<CutPlane>& list = section_planes[patch->surface.get()];
        for (const auto& run : patch->runs) {
          if (run.loop >= loops.size() || loop_valid[run.loop] == 0) continue;
          if (taken[run.loop] != 0 || loops[run.loop].size() < 3) continue;
          CutPlane cp;
          cp.normal = loop_normals[run.loop].normalized();
          cp.on_plane = vertices[loops[run.loop][0]];
          // A declared plane in preference to the mesh's own. The fit moves
          // with the tessellation and with whatever the boolean left behind;
          // the declaration cannot move at all, and the section derived from
          // it is exact rather than merely close.
          double best_tilt = 9, best_off = 9;
          for (const auto& pl : declared_planes) {
            // How near the nearest declaration comes, whether or not it is
            // taken: a section written on the mesh's own plane is a section
            // that moves when the corners do, and knowing it missed by 1e-5
            // rather than by a right angle is the difference between a
            // tolerance to widen and a plane nobody declared.
            const double tilt = fabs(fabs(pl.second.dot(cp.normal)) - 1.0);
            const double off = fabs((cp.on_plane - pl.first).dot(pl.second));
            if (tilt < best_tilt) best_tilt = tilt;
            if (tilt <= 1e-6 && off < best_off) best_off = off;
            if (tilt > 1e-6) continue;
            if (off > 1e-6) continue;
            cp.normal = cp.normal.dot(pl.second) > 0 ? pl.second : Vector3d(-pl.second);
            cp.on_plane = pl.first;
            cp.declared = true;
            break;
          }
          if (!cp.declared) {
            section_misses.push_back({best_tilt, best_off});
          }
          bool have = false;
          for (const auto& seen : list) {
            have = fabs(fabs(seen.normal.dot(cp.normal)) - 1.0) < 1e-9 &&
                   fabs((cp.on_plane - seen.on_plane).dot(seen.normal)) < 1e-9;
            if (have) break;
          }
          if (!have) list.push_back(cp);
        }
      }
      // Which faces meet along each boundary edge, so that an edge on two
      // declared quadrics can be recognised from either side and written the
      // same way from both.
      std::map<std::pair<int, int>, std::vector<const Surface *>> along;
      auto walk_into_along = [&](const AnalyticFeatures::Patch& patch) {
        for (const auto& entry : rawBoundaryCycles(patch, loops, loop_valid, taken)) {
          const std::vector<int>& verts = entry.second.verts;
          const std::size_t n = verts.size();
          for (std::size_t i = 0; i < n; i++) {
            const int u = verts[i], v = verts[(i + 1) % n];
            along[{std::min(u, v), std::max(u, v)}].push_back(patch.surface.get());
          }
        }
      };
      for (const auto *patch : standing) walk_into_along(*patch);
      // The declared sweeps too. An edge where a sweep meets a cylinder is a
      // crossing of two declarations exactly as much as one between two
      // cylinders is, and leaving the sweeps out of this map is what made the
      // question unaskable rather than answered - `along` would only ever see
      // one owner for such an edge and drop it at the size test below.
      //
      // They are safe to add only because the grid emitter now reads
      // `crossing_curves` as well: an edge admitted here and taken by the
      // cylinder's face while the sweep's face still wrote a chord would give
      // one edge two geometries, which is how this opened the shell on 420
      // edges the first time it was tried.
      for (const auto& patch : grid_faces) walk_into_along(patch);
      crossing_edges.clear();
      double worst_arc = 0;
      for (const auto& entry : along) {
        if (entry.second.size() != 2) continue;
        const Surface *a = entry.second[0], *b = entry.second[1];
        if (a == b) continue;
        const Vector3d& p0 = vertices[entry.first.first];
        const Vector3d& p1 = vertices[entry.first.second];
        const double scale = std::max(1.0, std::max(p0.norm(), p1.norm()));
        // Both ends have to be on both surfaces already. Where a boolean made
        // the vertex that is exactly what it is - the two makers crossing -
        // and where it is not, no arc of theirs belongs here.
        bool on = true;
        for (const Surface *s : {a, b}) {
          for (const Vector3d *p : {&p0, &p1}) {
            double f = 0;
            Vector3d g;
            const double owed = std::max(1e-9 * scale, declaredBand(s));
            double d = 0;
            if (!surfaceImplicit(s, *p, f, g, &d) || g.norm() < 1e-12 || d > owed) {
              on = false;
            }
          }
        }
        if (!on) continue;
        std::vector<Vector3d> ctrl;
        // The same 1e-7 the exact tier holds every other boundary to.
        ArcOutcome why;
        if (!intersectionArc(a, b, p0, p1, 1e-7, ctrl, &why)) {
          arc_refused++;
          if (why.degree == 0 || why.best_error == std::numeric_limits<double>::infinity()) {
            arc_no_fit++;  // no degree ever got as far as a curve to measure
          } else {
            arc_errors.push_back(why.best_error);
            arc_on_quadric.push_back(why.on_quadric);
            arc_on_fitted.push_back(why.on_fitted);
            if (why.straddles_crease == 1) arc_creased++;
            else if (why.straddles_crease == 0) arc_uncreased++;
          }
          continue;
        }
        // Where the crossing curve is *planar* it is a conic, and a conic has
        // an entity of its own: the plane-section pass writes it exactly,
        // where this would write a fitted B-spline through it. Two equal
        // cylinders crossing meet in a pair of true ellipses, and taking them
        // here would be trading an exact curve for an approximation of the
        // same curve. Planarity is the whole test, and it is a property of the
        // two surfaces rather than of anything this pass chose.
        Vector3d centroid = Vector3d::Zero();
        for (const auto& c : ctrl) centroid += c;
        centroid /= double(ctrl.size());
        Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
        for (const auto& c : ctrl) cov += (c - centroid) * (c - centroid).transpose();
        const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(cov);
        // The smallest eigenvalue is the sum of squared distances to the best
        // plane, so its root over the count is the RMS off it.
        const double flat = sqrt(std::max(0.0, es.eigenvalues()(0)) / double(ctrl.size()));
        // ...but only between two of them. The sentence above is a statement
        // about quadrics: two of those meeting in a planar curve meet in a
        // conic, which ELLIPSE states exactly. A *sweep* cut by a plane is
        // not a conic and no pass in this exporter writes one for it, so
        // skipping it here does not hand the edge to something better - it
        // hands it back to the chord it was. Measured on the band family at
        // $fn 32: all 640 edges where the sweep meets the bore pass every
        // other gate and every one of them is planar to 1e-9, because each
        // spans a single facet and a short arc of anything is flat.
        if (flat <= 1e-9 * scale && isQuadric(a) && isQuadric(b)) continue;
        worst_arc = std::max(worst_arc, intersectionArcError(a, b, ctrl));
        crossing_fit = std::max(crossing_fit, worst_arc);
        crossing_edges.emplace(entry.first, std::make_pair(a, b));
      }
      crossing_keys.clear();
      for (const auto& e : crossing_edges) crossing_keys.insert(e.first);
      // A section is written where both its faces will write it. A planar face
      // across it is not a claimant - it never asks for a curve - so it counts
      // as the second, and is handed the same edge when the faces are written.
      std::map<std::set<int>, int> claims;
      for (const auto *patch : standing) {
        for (const auto& entry : boundaryCycles(*patch, vertices, loops, loop_valid, taken,
                                                section_planes[patch->surface.get()], crossing_keys)) {
          const BoundaryCycle& cycle = entry.second;
          const std::size_t n = cycle.verts.size();
          for (const auto& bs : cycle.sections) {
            std::set<int> key;
            for (std::size_t k = 0; k < bs.count; k++) key.insert(cycle.verts[(bs.start + k) % n]);
            claims[key] += bs.loop >= 0 ? 2 : 1;
          }
        }
      }
      section_curves.clear();
      for (const auto& claim : claims) {
        if (claim.second == 2) section_curves.insert(claim.first);
      }
    };

    recognition::Writability writability;
    writability.begin = [&](const std::vector<std::shared_ptr<Surface>>& passes) {
      for (std::size_t si = 0; si < passes.size(); si++) {
        const auto *pl = dynamic_cast<const PlaneSurface *>(passes[si].get());
        if (pl == nullptr) continue;
        declared_planes.emplace_back(pl->refpt, pl->normdir.normalized());
        declared_plane_index.push_back(si < surfaces.size() ? si : surfaces.size());
      }
    };
    writability.boundaryOnSurface = [&](const std::vector<const AnalyticFeatures::Patch *>& standing) {
      decide_sections(standing);
      std::vector<char> ok;
      ok.reserve(standing.size());
      for (const auto *patch : standing)
        ok.push_back(boundary_lies_on_surface(*patch, section_curves) ? 1 : 0);
      return ok;
    };
    recognition::Inputs inputs;
    inputs.mesh.vertices = &vertices;
    inputs.mesh.loops = &loops;
    inputs.mesh.valid = &loop_valid;
    inputs.mesh.is_hole = &loop_is_hole;
    inputs.mesh.normals = &loop_normals;
    inputs.surfaces = &surfaces;
    inputs.face_origin = &faceOrigin;
    inputs.owned = &owned;
    inputs.model_tol = model_tol;
    inputs.approximate = approximate;
    recognition::runPipeline(inputs, writability, rec);
  }
  const std::vector<AnalyticFeatures::Band>& bands = features.bands;
  const std::vector<std::pair<AnalyticFeatures::RimRef, AnalyticFeatures::RimRef>>& rims = features.rims;
  const std::vector<char>& consumed = features.consumed;
  // face -> the triangles it is written as, see below
  std::map<std::size_t, std::vector<std::array<int, 3>>> split_for_corners;

  // Put the junction corners on the curve their two owners cross along - but
  // only once it is known which faces are analytic, and only if no face that
  // keeps a PLANE would be bent by it.
  //
  // A corner two declared surfaces made belongs on their intersection, and
  // reportOwnership has computed that point all along. Where it may be applied
  // is the whole difficulty, and two earlier places are ruled out by
  // measurement, both recorded in doc/step-export-development.md, *Corner
  // placement*: before mergeTriangles it keeps the neighbours planar and
  // destroys the merge, at twelve to sixty-two times the faces; after the merge
  // but before recognition it bends the faceted neighbours by 0.0331 where they
  // are polygons.
  //
  // Here is the place that costs neither. Recognition has already run on the
  // untouched, fully merged mesh, so the claims are what they were; and an
  // analytic face does not care whether its corners are coplanar, only a PLANE
  // does. On step-bored-cone 52 faces use a movable corner and every one of
  // them is written analytic, so nothing is bent and nothing is split.
  //
  // All of them or none, and per export rather than per corner. A boundary half
  // moved is worse than one not moved at all - doc/step-export-development.md,
  // *Half a fix is worse than none*, measured that at 83 faulty faces against 9
  // - so where a polygon that keeps its plane would be bent, this declines the
  // lot and the export is exactly what it was.
  // The other half of the placement: a corner one declared surface shares with
  // a plane the mesh carries and nothing declared.
  //
  // Here rather than beside the two-owner case in reportOwnership, because
  // *which* plane is only answerable here. Asked of raw triangles it cannot be
  // done: a frustum cap shares an original with its wall, and no distance
  // threshold separates a cut facet near the rim from a wall facet, or a thin
  // base sliver from either - three attempts, all in
  // doc/step-export-development.md. mergeTriangles has since answered it
  // exactly, because a merged face that keeps a PLANE *is* a plane: the planes
  // at a corner are the distinct ones among the faces using it.
  //
  // One plane, and the corner goes on the conic where it meets the declared
  // surface. Two, and it is a triple point - the cut and the sliver of base a
  // tilted cut leaves behind - which belongs on neither conic and stays.
  std::map<int, Vector3d> moves = cornerMoves;
  // Which rung placed each move, so the arbiter below can weigh it against the
  // others. See kCornerRungs.
  std::map<int, CornerRung> move_rung;
  for (const auto& m : cornerMoves) move_rung[m.first] = CornerRung::Provenance;
  // what each path added
  std::size_t on_conic = 0, on_triple = 0, on_own = 0, on_faces = 0, on_plane_face = 0;

  // Whether placing corner `v` at `target` would take it off an exact surface the
  // model declared and the corner is already on - one other than `own`, which is
  // where it is being placed.
  //
  // The placement onto a conic below rests on provenance naming *one* declared
  // owner, and provenance is per original solid, decided by whole facets: an
  // original none of whose facets a boolean left whole owns nothing. Its surface
  // is then invisible to the vote, and a corner on it is handed to whatever the
  // other original at that junction owns. Measured on step-band-family at $fn
  // 24, the ridge cuts all but 2 of the bore's facets - three are needed - so
  // its 48 rim corners, generated on the r = 20 bore to 1e-14, were given the
  // r = 23 outer wall as sole owner, slid 3.0 along the cap onto it, and left the
  // bore face they bound 3.0 off its own surface. lid10 does the same to 120
  // corners of two chamfers, 0.65 and 2.25 along its z = 95 face. On both, only
  // the plane veto kept it out of the file - and it does not need the flagship
  // coupons: a bore with a frustum subtracted through its whole length, three
  // primitives, writes the bore 3.0 off itself with the veto silent.
  //
  // So the question is asked of the declarations rather than of the vote: a
  // corner on a declared quadric stays on it. That is the rule the pass placing
  // corners onto their own quadric already keeps, and the one the reroute below
  // is named for. A surface that answers by projecting is not asked - a grid
  // states a band, not a position, and a corner off it is its fit's business.
  //
  // Asked here and not of the triple-point placement, which rests on the same
  // single owner: across every fixture and flagship coupon it moved no corner
  // off a quadric, and a guard with nothing to refuse has regressions nothing
  // would see. The check before the veto below covers it, loudly.
  auto deserts_exact = [&](int v, const Surface *own, const Vector3d& target) {
    for (const auto& other : surfaces) {
      if (other.get() == own || !AnalyticFeatures::isQuadric(other.get())) continue;
      Vector3d on_now, on_then;
      if (!AnalyticFeatures::closestOnSurface(other.get(), vertices[v], on_now) ||
          (on_now - vertices[v]).norm() > 1e-9) {
        continue;
      }
      if (!AnalyticFeatures::closestOnSurface(other.get(), target, on_then) ||
          (on_then - target).norm() > 1e-9) {
        return true;
      }
    }
    return false;
  };
  if (approximate && !singleOwner.empty()) {
    std::map<int, std::vector<std::size_t>> faces_at;
    for (std::size_t i = 0; i < loops.size(); i++) {
      if (!loop_valid[i] || consumed[i] || loops[i].size() < 3) continue;
      for (const int v : loops[i]) faces_at[v].push_back(i);
    }
    std::size_t placed = 0, triple = 0, placed_triple = 0, plane_gave_up = 0, held_on_exact = 0;
    // Why the surface-against-line placement gives up, kept apart: these are
    // five unrelated reasons and one number for all of them says nothing about
    // which to work on. `t_offsurf` is the corner's own surface refusing to
    // answer, `t_faces` is the corner not having exactly two mesh planes to
    // cross - not a solver failure at all - `t_parallel` is those two planes
    // being parallel, and `t_solve` is the only one the projection owns.
    std::size_t t_offsurf = 0, t_faces = 0, t_parallel = 0, t_solve = 0;
    std::size_t ts_noconv = 0, ts_offsurf = 0, ts_plane = 0, ts_far = 0;
    double near_min = 0, near_med = 0, near_max = 0;
    std::vector<double> near_misses;
    double worst_plane = 0, worst_triple = 0;
    for (const auto& entry : singleOwner) {
      const int v = entry.first;
      if (v < 0 || std::size_t(v) >= vertices.size()) continue;
      if (moves.count(v) != 0 || entry.second >= surfaces.size()) continue;
      const auto at = faces_at.find(v);
      if (at == faces_at.end()) continue;
      Vector3d pn(0, 0, 0);
      double pd = 0, reach_here = 0;
      bool one = true, any = false;
      for (const std::size_t i : at->second) {
        const Vector3d n = loop_normals[i].normalized();
        const double d = n.dot(vertices[loops[i][0]]);
        for (const int w : loops[i]) {
          reach_here = std::max(reach_here, (vertices[w] - vertices[v]).norm());
        }
        if (!any) {
          pn = n;
          pd = d;
          any = true;
        } else if ((n - pn).norm() > 1e-6 || fabs(d - pd) > 1e-6) {
          one = false;
          break;
        }
      }
      if (!any) continue;
      if (!one) {
        // More than one plane, which is a corner where three exact things meet:
        // the declared surface and two faces of the model. That point is
        // computable, and until now it was declined outright - "belongs on
        // neither conic and stays" - which left 48 corners on step-exact-trim,
        // 6 on step-shared-arc and 2 on step-cut-cone where the mesh put them.
        //
        // The planes have to be sorted first, because not every plane at the
        // corner is a face: some are the tessellation's own chords of the very
        // surface being placed on, and a chord is not a constraint. On
        // step-exact-trim 16 of the 48 corners have three planes and one of
        // them is a chord; taking all three would over-determine the point and
        // decline it again.
        //
        // A chord's plane normal is parallel to the surface's normal there, a
        // face's is not. The bound is the tessellation's own angular half-step:
        // a facet of an n-gon lies within pi/n of the surface normal at any of
        // its corners, and nothing coarser than a hexagon is a tessellation, so
        // cos(pi/6) admits every chord. Measured, chords read 0.885 to 1.000
        // and faces 0.000 to 0.643 - the closest pair being step-band-family's
        // 0.885 against step-cut-cone's 0.643.
        //
        // That margin is the weakest thing here and it is an inference standing
        // in for something the model knew: a declared plane would make this a
        // lookup. See doc/step-export-development.md, *Corner placement*.
        std::vector<std::pair<Vector3d, double>> faces_here;
        double reach_all = 0;
        {
          const Surface *os = surfaces[entry.second].get();
          Vector3d q0;
          if (!AnalyticFeatures::closestOnSurface(os, vertices[v], q0)) {
            triple++;
            t_offsurf++;
            continue;
          }
          const double off0 = (vertices[v] - q0).norm();
          for (const std::size_t i : at->second) {
            const Vector3d n = loop_normals[i].normalized();
            const double d = n.dot(vertices[loops[i][0]]);
            // How far the corner may travel: to its nearest neighbour and no
            // further. The face's whole extent is far too loose - the line two
            // faces cross along can meet the surface twice, and on
            // step-shared-arc the far one is 2.0 away and takes a cone that was
            // exact to 1.41 out. A corner is correcting its own tessellation,
            // so it belongs nearer than the vertex next to it.
            for (const int w : loops[i]) {
              if (w == v) continue;
              const double e = (vertices[w] - vertices[v]).norm();
              if (e > 1e-12) reach_all = reach_all == 0 ? e : std::min(reach_all, e);
            }
            bool have = false;
            for (const auto& w : faces_here) {
              if ((w.first - n).norm() < 1e-6 && fabs(w.second - d) < 1e-6) have = true;
            }
            if (have) continue;
            Vector3d q1;
            if (!AnalyticFeatures::closestOnSurface(os, vertices[v] + n * 1e-4, q1)) continue;
            const double along = fabs(((vertices[v] + n * 1e-4) - q1).norm() - off0) / 1e-4;
            if (along >= cos(M_PI / 6)) continue;  // a chord of this surface
            faces_here.emplace_back(n, d);
          }
        }
        if (faces_here.size() != 2) {
          triple++;
          t_faces++;
          continue;
        }
        // Where the two faces cross is a line; where that line meets the
        // surface is the corner. Projecting onto the line is exact, so this
        // alternates between two closed forms rather than three.
        const Vector3d n1 = faces_here[0].first, n2 = faces_here[1].first;
        const double d1 = faces_here[0].second, d2 = faces_here[1].second;
        const Vector3d dir = n1.cross(n2);
        if (dir.norm() < 1e-9) {
          triple++;
          t_parallel++;
          continue;
        }
        const Surface *own3 = surfaces[entry.second].get();
        Vector3d p3 = vertices[v], q3;
        // Newton on all three at once - the surface and both planes. This used
        // to alternate between projecting onto the surface and onto the line,
        // which converges linearly at cos^2(theta) per cycle and so, under its
        // 64-iteration cap, only for lines meeting the surface above about 30
        // degrees. It gave up on 280 corners across the fixture set, 260 of
        // them on lid10, and none of them because the geometry was not there.
        const double solve_tol = 1e-12 * std::max(1.0, vertices[v].norm());
        double near3 = 0;
        const bool ok3 =
          projectOntoSurfaceAndPlanes(own3, n1, d1, n2, d2, p3, solve_tol, reach_all, &near3);
        const bool got3 = ok3 && AnalyticFeatures::closestOnSurface(own3, p3, q3);
        if (!got3) {
          ts_noconv++;
          if (std::isfinite(near3)) near_misses.push_back(near3);
        } else if ((q3 - p3).norm() > 1e-6) ts_offsurf++;
        else if (fabs(n1.dot(p3) - d1) > 1e-9 || fabs(n2.dot(p3) - d2) > 1e-9) ts_plane++;
        else if ((p3 - vertices[v]).norm() > reach_all) {
          ts_far++;
          (void)reach_all;
        }
        if (!got3 || (q3 - p3).norm() > 1e-6 || fabs(n1.dot(p3) - d1) > 1e-9 ||
            fabs(n2.dot(p3) - d2) > 1e-9 || (p3 - vertices[v]).norm() > reach_all) {
          triple++;
          t_solve++;
          continue;
        }
        worst_triple = std::max(worst_triple, (p3 - vertices[v]).norm());
        moves.emplace(v, p3);
        move_rung[v] = CornerRung::MeshTriple;
        placed_triple++;
        continue;
      }
      const Surface *own = surfaces[entry.second].get();
      Vector3d p = vertices[v], qa;
      bool ok = true;
      for (int iter = 0; iter < 64; iter++) {
        if (!AnalyticFeatures::closestOnSurface(own, p, qa)) {
          ok = false;
          break;
        }
        const Vector3d qb = qa - pn * (pn.dot(qa) - pd);
        if ((qb - p).norm() < 1e-12) {
          p = qb;
          break;
        }
        p = qb;
      }
      if (!ok || !AnalyticFeatures::closestOnSurface(own, p, qa)) {
        plane_gave_up++;
        continue;
      }
      // Tight, because the file will be read as though it were exact. Accepting
      // 1e-6 here put a corner 1.73e-07 off a plane it was placed *in*, which is
      // outside OpenCASCADE's own Precision::Confusion of 1e-7 - so the face
      // asserted a plane its corner was not on, by the kernel's own reckoning.
      // The alternating projection converges, so there is no reason to accept
      // less than it converges to.
      if ((qa - p).norm() > 1e-9 || fabs(pn.dot(p) - pd) > 1e-9) {
        plane_gave_up++;
        continue;
      }
      const double travel = (p - vertices[v]).norm();
      if (travel > reach_here) {
        plane_gave_up++;
        continue;
      }
      if (deserts_exact(v, own, p)) {
        held_on_exact++;
        continue;
      }
      worst_plane = std::max(worst_plane, travel);
      moves.emplace(v, p);
      move_rung[v] = CornerRung::MeshConic;
      placed++;
    }
    on_conic = placed;
    on_triple = placed_triple;
    if (placed_triple > 0) {
      LOG(
        "STEP export: %1$d corners where a declared surface meets two faces of the model are "
        "placed where all three cross, moving at most %2$.4f; %3$d are not - %4$d off their own "
        "surface, %5$d without two faces to cross, %6$d on parallel faces, %7$d the solve did not reach",
        int(placed_triple), worst_triple, int(triple), int(t_offsurf), int(t_faces), int(t_parallel),
        int(t_solve));
      if (!near_misses.empty()) {
        std::sort(near_misses.begin(), near_misses.end());
        near_min = near_misses.front();
        near_med = near_misses[near_misses.size() / 2];
        near_max = near_misses.back();
      }
      LOG(
        "STEP export:    of those %1$d: %2$d no answer, %3$d off the surface, %4$d off a plane, %5$d "
        "past the nearest neighbour; closest the line came to the surface inside the window "
        "%6$.3e/%7$.3e/%8$.3e min/med/max",
        int(t_solve), int(ts_noconv), int(ts_offsurf), int(ts_plane), int(ts_far), near_min, near_med,
        near_max);
    }
    if (placed > 0) {
      LOG(
        "STEP export: %1$d corners a declared surface shares with one plane of the mesh reach "
        "the conic where the two cross, moving at most %2$.4f; %3$d do not and stay where the "
        "mesh put them",
        int(placed), worst_plane, int(plane_gave_up));
    }
    if (held_on_exact > 0) {
      LOG(
        "STEP export: %1$d corners provenance gives one owner are already on another quadric the "
        "model declared, and stay on it rather than be placed off it",
        int(held_on_exact));
    }
  }

  // Ordered after the two paths above, and that order is the doctrine rather
  // than a convenience: where the model declared what a corner is on, the
  // declaration decides, and this general path applies only to what the
  // declarations left. Every path checks `moves.count(v)` first, so first refusal
  // is all the ordering does. Measured on step-exact-trim, where 32 corners are
  // reachable both ways: run first, this claimed all 32 and the declared rung
  // reported none.
  // A corner belongs where the surfaces of the analytic faces it bounds cross.
  //
  // This is the general statement of what the two placements above do for
  // special cases, and it is the one the *file* can be held to: whatever
  // provenance says about which original made a vertex, the faces written on it
  // assert that the corner lies on their surfaces, and where two of them meet it
  // asserts both. Provenance cannot answer this on its own - it is per original
  // solid, so it names a cylinder for a corner sitting on that solid's cap
  // (open item 18), and it names two coaxial cylinders 3.000 mm apart, which
  // never meet, for the corners where the band family's sweep crosses its bore.
  // Both are cases where the owners are not the surfaces the file will assert.
  //
  // Measured on `step-band-family` at $fn 32: 47 of its 1234 candidate edges
  // fall back to chords, and they are exactly the edges whose endpoints are off
  // the sweep - by 2.2e-02 to 1.0e-01, the 32-gon's own sagitta
  // 20(1 - cos(pi/32)) = 0.0963, so those ends sit on chord planes and were
  // never placed at all. A Bezier interpolates its endpoints, so no degree and
  // no better tracing can rescue an edge whose ends are not on the surface:
  // placing them is what turns those chords into the curve the two surfaces
  // actually cross along. See open items 3 and 9.
  //
  // Held to the ladder's own rules. The crossing is solved to an absolute
  // 1e-11, accepted only where the result is on *both* surfaces to 1e-9, and a
  // corner may travel no further than its nearest neighbouring vertex - which is
  // section 4's bound, and not the longest edge at it, which is the trap that
  // licensed a 1.9976 move on step-partial-cylinder.
  {
    const recognition::ReportInputs report_inputs{
      features,
      surfaces,
      recognition::Mesh{&vertices, &loops, &loop_valid, &loop_is_hole, &loop_normals},
      faceOrigin,
      owned,
      &rec.d0_regions,
      &rec.d0_representations};
    recognition::dumpLedger(report_inputs);
    recognition::reportArbitration(report_inputs);
    recognition::reportRegions(report_inputs);
    recognition::reportRepresentations(report_inputs);
  }
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
          AnalyticFeatures::projectOntoBoth(constraints[a].surface, constraints[b].surface, p, 1e-11);
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

  // A corner of a trimmed quadric that nothing else holds goes on that quadric.
  //
  // The placement above reaches a corner two surfaces made. It does not reach a
  // corner the *mesh* made in the middle of a facet, which a trim then left on
  // the boundary of the face written on that quadric: nothing declared it, so
  // provenance names no owners for it and no crossing curve applies. It is
  // nonetheless a corner of an analytic face, sitting at the inradius of the
  // polygon the surface was tessellated as - 20(1 - cos(pi/32)) = 0.0963 inside
  // the cylinder its own face is written on.
  //
  // On step-band-family that is the last 39 corners, at the height where the
  // ridge tapers to nothing and the trim runs along the bore's own facets
  // instead of across them. They are moved onto the surface their face names,
  // and only where no other exact declared surface already holds them - a
  // corner on two of them belongs where both agree, which is the crossing curve
  // the paths above compute, not on either one alone.
  if (approximate) {
    std::size_t onto_own = 0;
    double worst_own = 0;
    for (const auto& patch : quadric_faces) {
      const Surface *surf = patch.surface.get();
      if (surf == nullptr) continue;
      for (const auto& run : patch.runs) {
        for (const int v : run.verts) {
          if (v < 0 || std::size_t(v) >= vertices.size()) continue;
          if (moves.count(v) != 0) continue;
          Vector3d q;
          if (!AnalyticFeatures::closestOnSurface(surf, vertices[v], q)) continue;
          const double off = (q - vertices[v]).norm();
          if (off <= 1e-9) continue;
          bool held = false;
          for (const auto& other : surfaces) {
            if (other.get() == surf) continue;
            if (dynamic_cast<const GridSurface *>(other.get()) != nullptr) continue;
            Vector3d qo;
            if (AnalyticFeatures::closestOnSurface(other.get(), vertices[v], qo) &&
                (qo - vertices[v]).norm() <= 1e-9) {
              held = true;
              break;
            }
          }
          if (held) continue;
          moves.emplace(v, q);
          worst_own = std::max(worst_own, off);
          onto_own++;
        }
      }
    }
    on_own = onto_own;
    if (onto_own > 0) {
      LOG(
        "STEP export: %1$d corners of a trimmed quadric that nothing else holds are placed on the "
        "surface their own face is written on, moving at most %2$.4f",
        int(onto_own), worst_own);
    }
  }

  // A welded vertex stays where it is.
  //
  // Those are the insertions that keep the shell conformal: a vertex one face
  // has on an edge another face spans in one line, added to both so they meet
  // edge for edge. It therefore sits on the *boundary* of faces whose loops do
  // not contain it, and the bend gate below - which reads each face's own loop -
  // cannot see what moving it does to them. On step-declare-grid-two-bands one
  // such vertex was moved 1.73e-07 off the flat bottom it lies on the rim of,
  // and every loop in the file was still within 1e-9 of its own plane: the face
  // asserted a plane a corner of it was off, and nothing internal could tell.
  {
    std::size_t held = 0;
    for (auto it = moves.begin(); it != moves.end();) {
      if (welded_verts.count(it->first) != 0) {
        it = moves.erase(it);
        held++;
      } else {
        ++it;
      }
    }
    if (held > 0) {
      LOG(
        "STEP export: %1$d corners are left where the mesh put them: they were welded onto an "
        "edge to keep the shell conformal, and sit on faces whose loops do not name them",
        int(held));
    }
  }
  // Keep every exact thing the corner is already on.
  //
  // A corner owned by a declared quadric and a declared sweep was being put
  // where the two cross - but the two are not worth the same. The quadric
  // states where its surface is; the sweep was interpolated through the model's
  // stations and publishes a tessellation band saying how well. Aiming at their
  // crossing curve spends an exact surface to satisfy a fitted one.
  //
  // It shows up as a plane. The mesh's flat bottom on step-declare-grid.py is a
  // 95-corner face, and one of its corners is owned by the wall cylinder and
  // the ridge's sweep; placed on their crossing curve it leaves z = 0 by
  // 0.0423, and a face that big cannot be fanned without turning the part's
  // flat bottom into 93 triangles, three of them off level and one by 21
  // degrees. The corner was exactly on that plane, 0.0103 off the cylinder -
  // 19.2(1 - cos(pi/96)), the wall's own sagitta - and 0.0378 off the sweep,
  // which had declared a band of 0.1290. Nothing was wrong with it that the
  // sweep was entitled to complain about.
  //
  // So: a plane the mesh carries *and a declaration vouches for* is kept, and
  // the corner is placed on that plane crossed with its exact owner instead.
  // Measured over the fixture set, 1533 merged planes are bent by a move and
  // exactly 2 are vouched for - the flat bottoms of step-declare-grid.py and
  // -strip, matching a declaration's own plane to 0.00e+00 - so this reaches
  // the case it is for and no other.
  //
  // The direction is what makes it safe: a declaration only ever *confirms* a
  // plane the mesh already has, and never invents one. refpt is not reliably a
  // rim - declare_cylinder takes the caller's centre, and a fitted cylinder's
  // refpt is wherever the fit landed - so a plane the mesh does not carry must
  // not be conjured out of one.
  if (approximate && !moves.empty() && !ownerSplit.empty()) {
    std::map<int, std::vector<std::size_t>> faces_at;
    for (std::size_t i = 0; i < loops.size(); i++) {
      if (!loop_valid[i] || consumed[i] || loops[i].size() < 3) continue;
      for (const int v : loops[i]) faces_at[v].push_back(i);
    }
    std::size_t rerouted = 0, dropped = 0;
    double worst_reroute = 0;
    for (const auto& sp : ownerSplit) {
      const int v = sp.first;
      if (moves.count(v) == 0) continue;
      if (v < 0 || std::size_t(v) >= vertices.size()) continue;
      if (sp.second.first >= surfaces.size() || sp.second.second >= surfaces.size()) continue;
      const auto at = faces_at.find(v);
      if (at == faces_at.end()) continue;
      // The planes at this corner that are faces of the model rather than
      // tessellation, told apart by the declared surface itself.
      //
      // A chord facet of a surface is spanned by two directions tangent to it,
      // so its plane's normal is parallel to the surface's own normal there. A
      // plane that cuts *across* the surface - a cylinder's cap - has a normal
      // perpendicular to it. So the question is only whether the plane's normal
      // is closer to parallel or to perpendicular, which is a midpoint rather
      // than a tuned constant, and the two cases are nowhere near it: measured
      // over the fixtures, cutting planes read 0.000000 to 0.000003 and chords
      // read 0.885 to 1.000.
      //
      // This asks the declaration and not the mesh, which is the point. An
      // earlier version compared the plane against the one a declared surface's
      // refpt is anchored at; that works only where the anchor happens to be
      // the rim, so it vouched for a cylinder's bottom cap and not its top -
      // primitives.cc pushes one record for r1 == r2 - and never for a sweep's
      // end caps. This needs no anchor and no second declaration.
      const Surface *exact = surfaces[sp.second.first].get();
      Vector3d q_at;
      if (!AnalyticFeatures::closestOnSurface(exact, vertices[v], q_at)) continue;
      const double off_at = (vertices[v] - q_at).norm();
      std::vector<std::pair<Vector3d, double>> vouched;
      for (const std::size_t i : at->second) {
        const double nn = loop_normals[i].norm();
        if (nn < 1e-12) continue;
        const Vector3d nhat = loop_normals[i] / nn;
        const double d = nhat.dot(vertices[loops[i][0]]);
        if (fabs(nhat.dot(vertices[v]) - d) > tol) continue;  // the corner is not on it
        // How much of a step along the plane's normal the surface takes back,
        // which is |n . nS| without needing a normal from the Surface API.
        const double delta = 1e-4;
        Vector3d q_off;
        if (!AnalyticFeatures::closestOnSurface(exact, vertices[v] + nhat * delta, q_off)) continue;
        const double along = fabs(((vertices[v] + nhat * delta) - q_off).norm() - off_at) / delta;
        if (along >= 0.5) continue;  // parallel to the surface's normal: a chord
        bool have = false;
        for (const auto& w : vouched) {
          if ((w.first - nhat).norm() < 1e-6 && fabs(w.second - d) < tol) have = true;
        }
        if (!have) vouched.emplace_back(nhat, d);
      }
      if (vouched.empty()) continue;  // nothing exact is being given up
      // Already in every one of them? Then the move keeps them and stands.
      bool leaves = false;
      for (const auto& w : vouched) {
        if (fabs(w.first.dot(moves[v]) - w.second) > 1e-9) leaves = true;
      }
      if (!leaves) continue;
      // Two vouched planes and the exact owner is one constraint too many; a
      // corner on two planes is a triple point already, and it stays.
      if (vouched.size() > 1) {
        moves.erase(v);
        dropped++;
        continue;
      }
      // Place it where the exact owner crosses the plane, nearest to where the
      // mesh put it. Both are exact, so this is the answer rather than a
      // compromise between one and a fit.
      const Surface *own = exact;
      const Vector3d pn = vouched[0].first;
      const double pd = vouched[0].second;
      Vector3d p = vertices[v], q;
      bool ok = true;
      for (int iter = 0; iter < 64; iter++) {
        if (!AnalyticFeatures::closestOnSurface(own, p, q)) {
          ok = false;
          break;
        }
        const Vector3d next = q - pn * (pn.dot(q) - pd);
        if ((next - p).norm() < 1e-12) {
          p = next;
          break;
        }
        p = next;
      }
      if (!ok || !AnalyticFeatures::closestOnSurface(own, p, q) || (q - p).norm() > 1e-6 ||
          fabs(pn.dot(p) - pd) > 1e-9) {
        moves.erase(v);
        dropped++;
        continue;
      }
      // And the fit only has to agree to within what it declared.
      const auto *fit = dynamic_cast<const GridSurface *>(surfaces[sp.second.second].get());
      if (fit != nullptr && AnalyticFeatures::closestOnSurface(fit, p, q) &&
          (q - p).norm() > fit->membershipTolerance()) {
        moves.erase(v);
        dropped++;
        continue;
      }
      worst_reroute = std::max(worst_reroute, (p - vertices[v]).norm());
      moves[v] = p;
      rerouted++;
    }
    if (rerouted > 0 || dropped > 0) {
      LOG(
        "STEP export: %1$d corners are placed on a plane a declaration vouches for crossed with "
        "their exact owner rather than on the fit, moving at most %2$.4f; %3$d stay where the "
        "mesh put them",
        int(rerouted), worst_reroute, int(dropped));
    }
  }

  // No move may take a corner off the surface of an analytic face it bounds.
  //
  // The veto below asks that of a face that keeps a PLANE, and nothing asked it
  // of a curved one: the corner is written as a vertex of that face, and a face
  // bounded by a point off its own surface is describing a different surface.
  // Every path above is meant to satisfy this by construction - the crossing
  // puts a corner on both its owners, the conic and triple placements on their
  // owner, the pass onto a quadric on the face's own - and one of them did not.
  // The placement onto a conic trusted provenance's single owner, and where a
  // boolean cut every facet of a bore, that owner was the wall across the cap:
  // band-fn024's 48 bore corners were slid 3.0 off the face they bound, lid10's
  // 120 chamfer corners 0.65 and 2.25, hidden only by the plane veto. That path
  // now keeps a corner on the quadric it is already on (see deserts_exact), and
  // this is measured to be reached by no fixture and no flagship coupon, with
  // the veto and without it.
  //
  // So it is an error, not a report: reaching it means a placement has a defect
  // the file would carry. The move is held, so the file stays right, and the
  // flagship check fails on the message, so the defect cannot come back quietly.
  //
  // Against each face's own declaration: a quadric states where it is, a grid
  // or patch publishes its band - AnalyticFeatures::declaredBand - and a corner
  // already that far off is judged only on whether the move takes it further.
  // A band is not a Surface record, so its cone or cylinder is read off its
  // axis and rims. A projection that does not answer judges nothing.
  if (approximate && !moves.empty()) {
    std::map<int, std::vector<std::pair<const Surface *, std::size_t>>> bounding;
    const std::size_t NOT_A_BAND = std::size_t(-1);
    auto bound_by = [&](const std::vector<AnalyticFeatures::Patch>& patches) {
      for (const auto& patch : patches) {
        for (const auto& run : patch.runs) {
          for (const int v : run.verts) {
            if (moves.count(v) != 0) bounding[v].emplace_back(patch.surface.get(), NOT_A_BAND);
          }
        }
      }
    };
    bound_by(quadric_faces);
    bound_by(grid_faces);
    bound_by(bezier_patches);
    for (std::size_t b = 0; b < bands.size(); b++) {
      if (!bands[b].alive) continue;
      for (const std::size_t w : bands[b].walls) {
        for (const int v : loops[w]) {
          if (moves.count(v) != 0) bounding[v].emplace_back(bands[b].zone.get(), b);
        }
      }
    }
    // How far `p` is from the surface, and what that surface allows; negative
    // where it gives no answer.
    auto off_face = [&](const Surface *s, std::size_t band, const Vector3d& p, double& allowed) {
      allowed = 1e-9;
      if (band != NOT_A_BAND && s == nullptr) {
        const AnalyticFeatures::Band& bd = bands[band];
        const Vector3d axis = bd.axis.normalized();
        const Vector3d rel = p - bd.base;
        const double h = rel.dot(axis);
        const double slope = bd.height > 0 ? (bd.r_top - bd.r_bottom) / bd.height : 0;
        return fabs((rel - axis * h).norm() - (bd.r_bottom + slope * h)) / sqrt(1 + slope * slope);
      }
      Vector3d q;
      if (s == nullptr || !AnalyticFeatures::closestOnSurface(s, p, q)) return -1.0;
      allowed = std::max(AnalyticFeatures::declaredBand(s), 1e-9);
      return (q - p).norm();
    };
    std::size_t deserting = 0;
    double worst_desertion = 0;
    for (auto it = moves.begin(); it != moves.end();) {
      bool deserts = false;
      const auto faces = bounding.find(it->first);
      if (faces != bounding.end()) {
        for (const auto& face : faces->second) {
          double allowed = 0;
          const double before = off_face(face.first, face.second, vertices[it->first], allowed);
          const double after = off_face(face.first, face.second, it->second, allowed);
          if (before < 0 || after < 0) continue;
          if (after > allowed && after > before) {
            deserts = true;
            worst_desertion = std::max(worst_desertion, after);
          }
        }
      }
      if (deserts) {
        it = moves.erase(it);
        deserting++;
      } else {
        ++it;
      }
    }
    if (deserting > 0) {
      LOG(message_group::Export_Error,
          "STEP export: %1$d corners would have been moved off the surface of an analytic face they "
          "bound, by up to %2$.4f, and are held where the mesh put them - a corner placement "
          "proposed a point its own face is not on",
          int(deserting), worst_desertion);
    }
  }
  // Under the approximation flag only. The exact tier asserts nothing the mesh
  // does not already state, and moving a vertex is such an assertion - without
  // this gate step-bored-cone's analytic export comes out with a PLANE
  // disagreeing with the winding of its own bound.
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
        if (!loop_valid[i]) continue;
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
        // Against the plane the *face* will assert, which is the outer loop's -
        // its point and its normal, exactly as written below - and over every
        // corner that face carries, its holes included.
        //
        // Measuring each loop against its own plane is not the same thing and
        // lets this through: two loops coplanar in the mesh drift apart by a
        // fraction of the move, and a hole's far corner is then off the outer
        // loop's plane by that drift times the face's extent. On
        // step-declare-grid-two-bands that put a corner 1.73e-07 off a disc of
        // radius 19.2 - outside OpenCASCADE's own Precision::Confusion, so the
        // face asserted a plane its corner was not on, while every loop was
        // within 1e-9 of its own.
        const Vector3d& p0 = at_moved(loops[i][0]);
        double out_of_plane = 0;
        for (const int w : loops[i]) {
          out_of_plane = std::max(out_of_plane, fabs((at_moved(w) - p0).dot(loop_normals[i])));
        }
        for (std::size_t j = 0; j < loops.size(); j++) {
          if (!loop_valid[j] || consumed[j] || parents[j] != int(i)) continue;
          for (const int w : loops[j]) {
            out_of_plane = std::max(out_of_plane, fabs((at_moved(w) - p0).dot(loop_normals[i])));
          }
        }
        if (out_of_plane <= 1e-9) continue;
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
          worst_tri = std::min(worst_tri, 0.5 * (q1 - q0).cross(q2 - q0).dot(nhat));
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
        for (const Surface *sf : entry.second) {
          Vector3d q;
          if (!AnalyticFeatures::closestOnSurface(sf, vertices[entry.first], q)) continue;
          const double owed =
            std::max(1e-9 * std::max(1.0, vertices[entry.first].norm()), declaredBand(sf));
          const double d = (q - vertices[entry.first]).norm();
          if (d > owed) worst = std::max(worst, d);
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
      // Three populations, not one. `moves` starts as the corners two declared
      // owners cross at and then takes the conic and triple-point placements
      // above, so calling the total "their two declared owners" was false
      // wherever the other paths fired: on step-exact-trim provenance names two
      // owners for *zero* vertices and this line still claimed 40 of them.
      // Each is named now, and they sum to the total by construction - every
      // path checks `moves.count(v)` before inserting.
      LOG(
        "STEP export: %1$d corners moved, by at most %2$.4f - %3$d where two declared owners "
        "cross, %4$d onto a conic with one plane, %5$d where three exact things meet, %6$d where "
        "the two analytic faces they bound cross, %7$d where an analytic face meets the plane of "
        "the face beside it, %8$d onto the quadric their own face is written "
        "on; %9$d analytic faces now bound themselves on their own surface",
        int(moves.size()), worst, int(cornerMoves.size()), int(on_conic), int(on_triple), int(on_faces),
        int(on_plane_face), int(on_own), int(analytic_faces));
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

  // Emit the recognised bands: one CYLINDRICAL_SURFACE or CONICAL_SURFACE face
  // each, bounded by a circle or an arc at either rim.
  //
  // A band which covers the full turn is periodic, so it cannot be bounded by
  // the rims alone - the loop walks up a seam and back down it, the same edge
  // used once in each direction. One which does not is bounded by an arc at
  // either rim and the band's two end edges, and needs no seam.
  std::map<std::size_t, std::pair<EdgeCurve *, bool>> rim_of_loop;  // whole loop -> circle
  std::map<std::size_t, std::vector<ArcSubstitution>> arc_subs;     // runs inside a loop
  // Rims written as chords by both faces because the face across is fanned into
  // triangles and cannot be handed the arc. See the band writer below.
  std::size_t rim_unshared = 0;
  std::map<std::set<int>, EdgeCurve *> shared_rim_edges;  // rim vertices -> circle

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

    // A sphere which reaches both poles closes on itself, so like the complete
    // torus above it is bounded by its seam and by nothing else. What differs is
    // that a sphere is periodic in one direction only: the seam is a meridian
    // running pole to pole - half of a great circle - used once in either
    // direction, and the poles are where the two usages meet. There is no rim,
    // because the two polar caps that used to bound the run are part of this
    // face now.
    //
    // The poles come from the surface record - centre +/- r along the axis - and
    // not from the mesh, which has a vertex at neither of them. Nothing else
    // refers to them, so inventing them here costs no other loop a rewrite: the
    // same argument the seam itself rests on.
    //
    // OCCT reads this back as one Sphere with two degenerate edges, which are
    // the zero length edges it inserts at the poles itself, and measures the
    // volume as (4/3)pi r^3.
    if (band.pole_closed) {
      const auto *sph = dynamic_cast<const SphereSurface *>(band.zone.get());
      if (sph != nullptr) {
        const Vector3d centre = sph->refpt;
        const Vector3d axis = band.axis.normalized();
        // the longitude the seam runs down, which is also the direction the
        // surface's own parameterisation is measured from
        const Vector3d rel = vertices[band.seam_bottom] - centre;
        const Vector3d radial = (rel - axis * rel.dot(axis)).normalized();

        auto placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
          auto point = new Point(entities, origin);
          auto dir_axis = new Direction(entities, dir);
          auto dir_ref = new Direction(entities, towards);
          return new Axis2Placement(entities, dir_axis, dir_ref, point);
        };

        auto surface = new SphericalSurface(entities, "", placement(centre, axis, radial), sph->r);
        auto south = new Vertex(entities, new Point(entities, centre - axis * sph->r));
        auto north = new Vertex(entities, new Point(entities, centre + axis * sph->r));

        // Parameterised from the south pole, so that a quarter turn reaches the
        // seam's own longitude and a half turn the north pole. `radial x axis`
        // is the normal that makes that the positive sweep; the other way round
        // the arc runs down the far side of the sphere, and the face is bounded
        // by the wrong half of the great circle.
        auto meridian =
          new Circle(entities, "", placement(centre, radial.cross(axis), Vector3d(-axis)), sph->r);
        auto edge_seam = new EdgeCurve(entities, south, north, meridian, true);

        std::vector<OrientedEdge *> loop{
          new OrientedEdge(entities, edge_seam, true),
          new OrientedEdge(entities, edge_seam, false),
        };
        auto edge_loop = new EdgeLoop(entities, loop);
        std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};
        sfaces_extra.push_back(new Face(entities, bounds, surface, band.outward));
        face_edges_extra.push_back({edge_seam});
        continue;
      }
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
    // The mesh's own chords along a rim, where the arc cannot be handed to the
    // face across it - see below. Empty whenever the rim is written as the arc
    // it is, which is every rim on every fixture measured.
    std::vector<std::pair<EdgeCurve *, bool>> rim_chords[2];
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
        RoundType *curve = nullptr;
        if (!bottom && band.top_tilted) {
          // The rim is the section of the cylinder by a plane which is not
          // perpendicular to the axis, so it is an ellipse. Its minor axis has
          // the cylinder's own radius and lies where the cut plane meets the
          // plane through the centre perpendicular to it; its major axis runs
          // up the steepest ascent of the cut and is longer by exactly the
          // secant of the tilt.
          //
          // Unlike a circle the reference direction is not free - it is what
          // says which semi-axis is which - so the seam vertex does not sit at
          // the start of the parameterisation here. A closed edge does not
          // require it to.
          const Vector3d n = band.top_normal;
          const double cos_tilt = n.dot(band.axis);
          const Vector3d major = (band.axis - n * cos_tilt).normalized();
          curve = new Ellipse(entities, "", placement(centre, n, major), radius / cos_tilt, radius);
        } else {
          curve = new Circle(entities, "", placement(centre, band.axis, seam_rel.normalized()), radius);
        }
        Vertex *vert = get_vertex(seam);
        // a full circle is one edge whose two ends are the same vertex
        rim_edge[side] = new EdgeCurve(entities, vert, vert, curve, true);
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
          // A loop that will be fanned into triangles cannot take this arc. The
          // fan writes one straight line per triangle side and never reads
          // `arc_subs`, and a rim spanning a run of corners could not be one
          // triangle's side even if it did - so the handover is impossible
          // rather than missed, and writing the arc here leaves the conic on
          // this face and chords on that one.
          //
          // Open item 11's root cause A, which the plane sections have guarded
          // against since it was closed for them; the rims never did. On the
          // nine-line model of item 18 a cone's top rim was written as one
          // CIRCLE while the cap across it was fanned, and the export carried
          // 25 edges used by one face.
          //
          // So the rim is written as the mesh's own chords instead, taken from
          // the same edge map the fan's triangles use - which is what makes
          // them the same edges rather than a second set on the same points. A
          // worse boundary on this one face, and a closed shell.
          if (!band.closed && split_for_corners.count(rim.loop) != 0) {
            const std::vector<int>& across = loops[rim.loop];
            const std::size_t m = across.size();
            std::vector<int> walk;
            for (std::size_t k = 0; m > 0 && k <= rim.count; k++) {
              walk.push_back(across[(rim.start + k) % m]);
            }
            if (!walk.empty() && walk.front() != rim.traversalStart()) {
              std::reverse(walk.begin(), walk.end());
            }
            for (std::size_t k = 0; k + 1 < walk.size(); k++) {
              bool dir = true;
              EdgeCurve *chord = get_line_from_map(edge_map, walk[k], walk[k + 1], get_vertex(walk[k]),
                                                   get_vertex(walk[k + 1]), dir, merged_edge_cnt);
              rim_chords[side].emplace_back(chord, dir);
            }
            if (!rim_chords[side].empty()) {
              rim_unshared++;
              continue;
            }
          }
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
      auto walk_rim = [&](int side) {
        if (rim_chords[side].empty()) {
          loop.push_back(new OrientedEdge(entities, rim_edge[side], rim_sense[side]));
          face_edges_here.push_back(rim_edge[side]);
          return;
        }
        for (const auto& chord : rim_chords[side]) {
          loop.push_back(new OrientedEdge(entities, chord.first, chord.second));
          face_edges_here.push_back(chord.first);
        }
      };
      walk_rim(0);
      loop.push_back(new OrientedEdge(entities, edge_up, up_dir));
      walk_rim(1);
      loop.push_back(new OrientedEdge(entities, edge_down, down_dir));
      face_edges_here.push_back(edge_up);
      face_edges_here.push_back(edge_down);
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
          if (circular_runs.count(key) &&
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

  // ---- declared sweeps ---------------------------------------------------
  //
  // One face per claimed region, on the B-spline the declaration describes,
  // bounded by the mesh's own straight edges.
  //
  // That last part is the whole design and it is not a compromise. The obvious
  // move is to fit a curve along each boundary run, the way a fillet's rails
  // become arcs - but a fillet's rail lies in the flat face beside it, and a
  // trimmed sweep's boundary does not. Its neighbours here are the planar
  // facets the boolean left, so a curve on the sweep lies in none of them, and
  // a shared edge between the two can only be the chord it already is. Taking
  // the edges from the same map every other face uses is therefore both the
  // simplest thing and the only one that keeps the shell closed: the neighbour
  // is not asked to give anything up.
  //
  // One neighbour is not a planar facet, and it is the exception that sentence
  // does not cover: where the sweep meets a *declared* surface, the two cross
  // along a curve both of them can derive from their own declarations, and
  // `crossing_edges` holds exactly those edges. Such an edge has to be taken
  // from `crossing_curves` here for the same reason it is taken there - it is
  // one edge, and the two faces on it must name one geometry. Whichever emitter
  // reaches it first builds it and the other reuses the object, which is why
  // that map is declared above both of them rather than beside either.
  std::map<std::pair<int, int>, EdgeCurve *> crossing_curves;
  /*! One face's view of a crossing edge: the surface entity it wrote and the
   *  placement that entity carries, which is what a PCURVE's parameters mean.
   *
   * Recorded per face rather than per surface because the reference direction is
   * chosen from each face's own boundary, so two faces on one cylinder do not
   * share a parameter origin and a pcurve written against the wrong one is off
   * by a rotation.
   */
  struct CrossingSide {
    SurfaceType *surface = nullptr;
    Vector3d base, axis, ref;
    double slope = 0;  // a cone's, in its written direction; zero for a cylinder
  };
  std::map<std::pair<int, int>, std::vector<CrossingSide>> crossing_sides;
  int crossing_written = 0;

  // One per file - nothing about a 2D parametric context varies.
  ParametricContext *param_context = nullptr;
  // Settle the sections and the crossing curves before *either* emitter runs.
  // This used to sit after the grid faces were written, which meant the grid
  // emitter read a set that had not been decided yet and the quadric emitter
  // read one that had: the same edge then got a crossing curve from one face
  // and a chord from the other, and the shell came apart on twice the number
  // of crossings - 1278 edges used once, on the band family at $fn 32. Its own
  // comment says it has to run after the corner placement, and it still does;
  // the placement is a long way above here.
  // A trimmed quadric, bounded by the mesh's own boundary rather than by
  // circles. The band pass writes a cylinder between two circular rims and
  // writes it better - a CIRCLE is a curve a kernel can offset and pattern
  // along - so this only ever sees what that pass could not take: a face whose
  // trim is not a plane section and has no conic to bound it with.
  //
  // The bound is the polyline the neighbouring faceted faces already use, for
  // the same reason a declared sweep's is: those faces have to close against
  // this one edge for edge, and a curve of our own devising there would open
  // the shell. The surface is exact; only its boundary is the mesh's.
  // One curve per plane section, shared by the two faces that meet along it -
  // the same rule the band pass follows for a shared rim. Without it each face
  // would write its own ellipse and the shell would come apart along them.
  // Ask again, now that the corner placement has moved what it is going to move.
  // The faces are settled; what is being re-decided is only which sections both
  // of their faces can still see, and it has to be decided on the geometry that
  // is about to be written rather than on the geometry it was chosen from.
  if (decide_sections) {
    const std::set<std::set<int>> agreed_before = section_curves;
    std::vector<const AnalyticFeatures::Patch *> live;
    live.reserve(quadric_faces.size());
    for (const auto& patch : quadric_faces) live.push_back(&patch);
    decide_sections(live);
    std::size_t lost = 0;
    for (const auto& key : agreed_before) {
      if (section_curves.find(key) == section_curves.end()) lost++;
    }
    if (lost > 0) {
      // Say so rather than let it pass: this is the one place the two meshes can
      // disagree, and in the exact tier it means a face was called exact partly
      // because a section covered chords which are now written as chords after
      // all. A plane fitted to mesh facets is what does not survive: the
      // placement moves a corner onto the surface the model declared, which is
      // exactly off the facet plane it happened to share with a neighbour. A
      // declared plane survives, because that is what the corner was moved onto.
      // An error in the **exact** tier and a warning in the approximation tier,
      // which is the difference between the two meanings this condition has.
      //
      // Under the approximation flag, losing a section's agreement now costs
      // exactness and nothing else: both faces keep the chords they agree on,
      // because a section the far face cannot be handed is no longer written by
      // the near one either. That is a worse boundary and a correct file.
      //
      // In the exact tier it is a correctness fault, because a face was called
      // exact partly on the strength of a section covering chords which are then
      // written as chords after all - the face asserts something its boundary no
      // longer supports.
      //
      // It was made an error outright earlier on 2026-09-10, when the far face
      // could still be left holding chords while the near one wrote a conic; the
      // fan fix below removed that, and with it the reason for the stronger
      // reading in the approximation tier. Measured after: f04 under a relaxed
      // corner placement reaches this and its file is valid.
      //
      // Unreachable on every coupon measured either way:
      // the band family at $fn 24, 32, 48, 64 and 96 and both reference parts
      // all report zero. It became reachable exactly once, when the corner
      // placement was relaxed on 2026-09-10, and what it produced was a file
      // whose shell was open - one boundary written as an ELLIPSE by a cylinder
      // and as a LINE by the plane beside it, two edges where there is one.
      //
      // So reaching this is not a degradation to be reported and lived with. It
      // is two faces contradicting each other about one edge, and the file is
      // wrong. `tests/step-flagship-check.py` fails on any EXPORT-ERROR, which
      // is what makes that enforceable rather than merely stated. See open item
      // 11: the fix is to decide an edge's geometry once, keyed by the edge, the
      // way `crossing_curves` already does.
      LOG(approximate ? message_group::Export_Warning : message_group::Export_Error,
          "STEP export: %1$d plane section%2$s agreed before the corners were placed and %3$s not "
          "after, so %4$s boundary is written as chords%5$s",
          int(lost), lost == 1 ? "" : "s", lost == 1 ? "does" : "do",
          lost == 1 ? "that much of a" : "that much of the",
          approximate ? "" : " under a face called exact");
    }
  }

  std::size_t reseamed = 0, reseam_full = 0;
  for (const auto& patch : grid_faces) {
    const auto *grid = dynamic_cast<const GridSurface *>(patch.surface.get());
    if (grid == nullptr) continue;

    // Where to put the seam of a closed profile, which is free to choose.
    //
    // A face here is written over the rectangle 0 <= v <= 1, so its boundary
    // must not cross the seam. One that does has nowhere to go: the pcurve is a
    // straight line in (u, v), and from v = 0.0000 to v = 0.9956 it sweeps the
    // whole profile instead of stepping the 0.0044 across. A reader then repairs
    // a face whose boundary contradicts its own surface - OpenCASCADE builds a
    // seam edge nobody wrote and widens the vertices there until they merge,
    // 1.08 on lid10, and the LINE beside it comes back bounded by a point 0.7159
    // off its own curve. Open item 15.
    //
    // A boolean that buries one span of the profile leaves the seam somewhere to
    // go: written from the column after an unused span, the face's own facets
    // are contiguous in v and no boundary crosses anything. That costs no face,
    // no refused facet and no cut - the alternatives this replaces - and it is
    // exact, the surface being the same one described from another column.
    //
    // Which spans a face uses is asked of the facets' corners rather than their
    // middles: beside the seam the profile is creased, and a facet's centroid
    // projects onto the flank across the crease - 0.022 on lid10 for one whose
    // corners are at 0.98 and beyond.
    int seam_column = 0;
    const int spans = grid->closed_v ? grid->cols : grid->cols - 1;
    if (grid->closed_v && spans > 1) {
      std::vector<char> used(spans, 0);
      for (const std::size_t f : patch.facets) {
        if (f >= loops.size()) continue;
        double lo = 2, hi = -1;
        for (const int v : loops[f]) {
          double pu = 0, pv = 0;
          if (!grid->project(vertices[v], pu, pv)) continue;
          if (grid->onSeam(pu, pv)) continue;
          lo = std::min(lo, pv);
          hi = std::max(hi, pv);
        }
        if (hi < lo) continue;
        const int span = std::clamp(int(((lo + hi) / 2) * spans), 0, spans - 1);
        used[span] = 1;
      }
      for (int k = 0; k < spans; k++) {
        if (used[k] == 0) {
          seam_column = (k + 1) % grid->cols;
          break;
        }
      }
      if (seam_column != 0) reseamed++;
      else if (std::find(used.begin(), used.end(), 0) == used.end()) reseam_full++;
    }
    const double seam_shift = grid->seamShift(seam_column);
    // Every parameter this face reports is in the surface as it was *written*,
    // which is the same surface described from another column.
    auto parameters = [&](const Vector3d& p, double& pu, double& pv) {
      if (!grid->project(p, pu, pv)) return false;
      if (seam_shift > 0) {
        pv -= seam_shift;
        if (pv < 0) pv += 1.0;
        if (pv > 1) pv -= 1.0;
      }
      return true;
    };

    int du = 0, dv = 0, nu = 0, nv = 0;
    std::vector<Vector3d> ctrl;
    std::vector<double> knots_u, knots_v;
    std::vector<int> mults_u, mults_v;
    if (!grid->splineForm(du, dv, nu, nv, ctrl, knots_u, mults_u, knots_v, mults_v, seam_column)) {
      continue;
    }

    std::vector<std::vector<Point *>> net;
    for (int i = 0; i < nu; i++) {
      std::vector<Point *> row;
      for (int j = 0; j < nv; j++) {
        row.push_back(new Point(entities, ctrl[std::size_t(i) * nv + j]));
      }
      net.push_back(row);
    }
    auto surface = new BSplineSurface(entities, "", du, dv, net);
    surface->setKnots(knots_u, mults_u, knots_v, mults_v);

    // The boundary, one cycle at a time. Patch::Run carries them in order with
    // consecutive runs sharing an endpoint, so a cycle is its runs' vertices
    // with the joins written once.
    std::map<std::size_t, std::vector<int>> cycles;
    for (const auto& run : patch.runs) {
      std::vector<int>& cycle = cycles[run.bound];
      for (std::size_t i = 0; i + 1 < run.verts.size(); i++) cycle.push_back(run.verts[i]);
    }

    std::vector<FaceBound *> bounds;
    std::vector<EdgeCurve *> face_edges_here;
    // Which cycle is the outer bound is a question about the surface's own
    // parameters, not about the model: a hole in a face is a hole in its
    // parameter rectangle. The largest area there is the one that contains the
    // others.
    std::size_t outer = 0;
    double widest = -1.0;
    std::map<std::size_t, double> area;
    for (const auto& entry : cycles) {
      std::vector<std::pair<double, double>> uv;
      for (const int v : entry.second) {
        double pu = 0, pv = 0;
        if (!parameters(vertices[v], pu, pv)) continue;
        uv.emplace_back(pu, pv);
      }
      double twice = 0.0;
      for (std::size_t i = 0; i < uv.size(); i++) {
        const auto& a = uv[i];
        const auto& b = uv[(i + 1) % uv.size()];
        twice += a.first * b.second - b.first * a.second;
      }
      area[entry.first] = fabs(twice) / 2;
      if (area[entry.first] > widest) {
        widest = area[entry.first];
        outer = entry.first;
      }
    }

    for (const auto& entry : cycles) {
      const std::vector<int>& cycle = entry.second;
      if (cycle.size() < 3) continue;
      std::vector<OrientedEdge *> loop;
      for (std::size_t i = 0; i < cycle.size(); i++) {
        const int a = cycle[i], b = cycle[(i + 1) % cycle.size()];
        bool dir = true;
        EdgeCurve *edge = nullptr;
        // The one edge a sweep shares with something that can give it a curve.
        // Same map, same key and the same EdgeCurve object as the quadric
        // emitter uses, so the two faces on this edge name one geometry however
        // the passes are ordered.
        const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
        const auto crossing = crossing_edges.find(key);
        if (crossing != crossing_edges.end()) {
          const auto found = crossing_curves.find(key);
          if (found != crossing_curves.end()) {
            edge = found->second;
            dir = edge->vert1 == get_vertex(a);
          } else {
            std::vector<Vector3d> ctrl;
            if (intersectionArc(crossing->second.first, crossing->second.second, vertices[key.first],
                                vertices[key.second], 1e-7, ctrl)) {
              std::vector<Point *> cp;
              cp.reserve(ctrl.size());
              for (const auto& c : ctrl) cp.push_back(new Point(entities, c));
              edge = new EdgeCurve(entities, get_vertex(key.first), get_vertex(key.second),
                                   new BSplineCurve(entities, "", cp), true);
              crossing_curves.emplace(key, edge);
              crossing_written++;
              dir = edge->vert1 == get_vertex(a);
            }
          }
        }
        if (edge == nullptr) {
          edge = get_line_from_map(edge_map, a, b, get_vertex(a), get_vertex(b), dir, merged_edge_cnt);
        }
        loop.push_back(new OrientedEdge(entities, edge, dir));
        face_edges_here.push_back(edge);
        // Where this edge runs across the surface, written down rather than
        // left to be guessed. A swept surface cannot be projected onto
        // reliably - see the PCURVE classes in StepKernel.h - and this is the
        // one face shape in this exporter that is trimmed by a polyline on
        // such a surface.
        //
        // Taken from the edge's own two vertices, not from a and b: an edge
        // already in the map may run the other way, and a pcurve that
        // disagrees with the direction of the curve it belongs to is worse
        // than none.
        [&]() {
          double ua = 0, va = 0, ub = 0, vb = 0;
          if (!parameters(edge->vert1->point->pt, ua, va)) return;
          if (!parameters(edge->vert2->point->pt, ub, vb)) return;
          const double su = ub - ua, sv = vb - va;
          const double plen = sqrt(su * su + sv * sv);
          // A DIRECTION has to have a magnitude, and an edge whose ends land on
          // the same parameters says nothing about where it runs anyway.
          if (plen < 1e-12) return;
          if (param_context == nullptr) param_context = new ParametricContext(entities);
          auto rep = new DefinitionalRepresentation(
            entities,
            new Line2d(entities, new Point2d(entities, ua, va),
                       new Vector2d(entities, new Direction2d(entities, su / plen, sv / plen), plen)),
            param_context);
          auto pcurve = new PCurve(entities, surface, rep);
          // An edge between two sweep faces is on both of them, and STEP allows
          // a SURFACE_CURVE to say so.
          if (auto *already = dynamic_cast<SurfaceCurve *>(edge->round)) {
            if (already->on.size() < 2) already->on.push_back(pcurve);
          } else {
            edge->round = new SurfaceCurve(entities, edge->round, {pcurve});
          }
        }();
      }
      auto edge_loop = new EdgeLoop(entities, loop);
      bounds.push_back(new FaceBound(entities, edge_loop, true, entry.first == outer));
    }
    if (bounds.empty()) continue;

    // Which way the face points. du x dv has no reason to agree with the mesh,
    // so it is compared against a facet the region actually contains.
    bool outward = true;
    if (!patch.facets.empty()) {
      const std::vector<int>& facet = loops[patch.facets.front()];
      Vector3d centroid = Vector3d::Zero();
      for (const int v : facet) centroid += vertices[v];
      centroid /= double(facet.size());
      double pu = 0, pv = 0;
      grid->project(centroid, pu, pv);
      const double h = 1e-5;
      const Vector3d d_u =
        grid->evaluate(std::min(1.0, pu + h), pv) - grid->evaluate(std::max(0.0, pu - h), pv);
      const Vector3d d_v =
        grid->evaluate(pu, std::min(1.0, pv + h)) - grid->evaluate(pu, std::max(0.0, pv - h));
      outward = d_u.cross(d_v).dot(loop_normals[patch.facets.front()]) > 0;
    }

    sfaces_extra.push_back(new Face(entities, bounds, surface, outward));
    face_edges_extra.push_back(face_edges_here);
  }

  std::map<std::set<int>, EdgeCurve *> section_edges;
  // What binding the written sections' ends to the surface cost - see SectionStats.
  SectionStats section_stats;
  // Sections the far face could not be handed, and which are therefore
  // written as chords by both rather than as a conic by one. Reached only
  // where a planar loop no longer carries the run - see below.
  std::size_t section_unshared = 0;
  std::size_t apex_seams = 0;            // cone faces closed round their apex
  std::size_t band_seams = 0;            // faces round their axis closed by a seam between two rims
  std::set<std::set<int>> section_subs;  // sections already handed to a planar face
  int sections_declared = 0, sections_fitted = 0;
  // Boundary sections refused because their plane was a facet of something
  // curved rather than a plane. See open item 26.
  int sections_on_facet = 0;
  auto conic_placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
    return new Axis2Placement(entities, new Direction(entities, dir), new Direction(entities, towards),
                              new Point(entities, origin));
  };
  for (const auto& patch : quadric_faces) {
    const auto *axial = dynamic_cast<const AxialSurface *>(patch.surface.get());
    if (axial == nullptr) continue;
    const auto *cyl = dynamic_cast<const CylinderSurface *>(patch.surface.get());
    const auto *cone = dynamic_cast<const ConeSurface *>(patch.surface.get());
    const auto *elliptic = dynamic_cast<const EllipticCylinderSurface *>(patch.surface.get());
    const Vector3d axis = axial->axis();
    const Vector3d base = axial->refpt;

    // The endpoint invariant binds *here* and not where the sections were
    // decided. The decision runs before the corner placement, on vertices the
    // mesh still holds a sagitta inside their surface; by the time a face is
    // written its corners have been put on the surfaces the file asserts, and it
    // is the written curve the invariant is about. Binding it in the decision
    // would have dropped every section on lid10 for a staleness that the
    // placement then removed.
    const std::map<std::size_t, BoundaryCycle> boundary =
      boundaryCycles(patch, vertices, loops, loop_valid, taken, section_planes[patch.surface.get()],
                     crossing_keys, true, &section_stats);
    if (boundary.empty()) continue;

    // The reference direction is a radius through the first boundary vertex that
    // has one, so the face starts where its own boundary does rather than at
    // some seam of the surface's own. "That has one" is not defensiveness: a
    // cone's face can be bounded through its apex, which is on the axis and has
    // no radius at all, and taking that one wrote a zero DIRECTION into the
    // file - a placement no reader can use, on a face that was correct in every
    // other respect.
    Vector3d ref = Vector3d::Zero();
    for (const auto& entry : boundary) {
      for (const int v : entry.second.verts) {
        const Vector3d rel = vertices[v] - base;
        const Vector3d radial = rel - axis * axis.dot(rel);
        if (radial.norm() > 1e-9) {
          ref = radial.normalized();
          break;
        }
      }
      if (ref.norm() > 0.5) break;
    }
    if (ref.norm() < 0.5) ref = AnalyticFeatures::perpendicular(axis);
    auto point = new Point(entities, base);
    auto dir_ref = new Direction(entities, ref);
    SurfaceType *surface = nullptr;
    if (elliptic != nullptr) {
      // The section across the sweep, swept: its ELLIPSE in its own principal
      // frame, and the sweep as a unit vector.
      Vector3d e1, e2;
      elliptic->frame(e1, e2);
      auto ellipse =
        new Ellipse(entities, "",
                    new Axis2Placement(entities, new Direction(entities, axis),
                                       new Direction(entities, e1), new Point(entities, base)),
                    elliptic->a(), elliptic->b());
      surface = new SurfaceOfLinearExtrusion(entities, "", ellipse,
                                             new Vector(entities, new Direction(entities, axis), 1.0));
    } else if (cyl != nullptr) {
      auto dir_axis = new Direction(entities, axis);
      surface = new CylindricalSurface(entities, "",
                                       new Axis2Placement(entities, dir_axis, dir_ref, point), cyl->r);
    } else {
      // ISO 10303 wants a half angle in (0, pi/2) and a radius growing along the
      // placement's axis, so a cone narrowing that way is written from its other
      // end - the same convention the band pass follows.
      auto dir_axis = new Direction(entities, cone->slope > 0 ? axis : Vector3d(-axis));
      surface = new ConicalSurface(entities, "", new Axis2Placement(entities, dir_axis, dir_ref, point),
                                   cone->r, atan(fabs(cone->slope)));
    }

    // Which cycle is the outer bound is a question about the surface's own
    // parameters: a hole in a face is a hole in its (theta, z) rectangle.
    std::size_t outer = 0;
    double widest = -1.0;
    const Vector3d ref2 = axis.cross(ref);
    for (const auto& entry : boundary) {
      std::vector<std::pair<double, double>> uv;
      for (const int v : entry.second.verts) {
        const Vector3d rel = vertices[v] - base;
        double t = atan2(rel.dot(ref2), rel.dot(ref));
        if (t < 0) t += 2 * M_PI;
        uv.emplace_back(t * axial->size(), axis.dot(rel));
      }
      double twice = 0.0;
      for (std::size_t i = 0; i < uv.size(); i++) {
        const auto& a = uv[i];
        const auto& b = uv[(i + 1) % uv.size()];
        twice += a.first * b.second - b.first * a.second;
      }
      if (fabs(twice) / 2 > widest) {
        widest = fabs(twice) / 2;
        outer = entry.first;
      }
    }

    std::vector<FaceBound *> bounds;
    std::vector<EdgeCurve *> face_edges_here;
    std::vector<std::pair<const BoundaryCycle *, std::vector<OrientedEdge *>>> seam_rims;
    for (const auto& entry : boundary) {
      const std::vector<int>& cycle = entry.second.verts;
      const std::size_t n = cycle.size();
      if (n < 3) continue;
      // Where the boundary is a plane section, write the ellipse it is rather
      // than the chords the mesh carries. Everything else stays a straight edge:
      // this replaces a stretch of the boundary, never the whole of it.
      std::vector<int> starts_at(n, -1);
      for (std::size_t k = 0; k < entry.second.sections.size(); k++) {
        const BoundarySection& bs = entry.second.sections[k];
        // A section the roll could not free stays a run of chords, which is what
        // the boundary test then refuses the surface for; so does one the face
        // on the other side is not writing - see `section_curves`.
        std::set<int> key;
        for (std::size_t c = 0; c < bs.count; c++) key.insert(cycle[(bs.start + c) % n]);
        // A whole rim, from the seam round to it again, is one closed curve.
        const bool whole = bs.start == 0 && bs.count == n + 1;
        if (bs.start + bs.count > n && !whole) continue;
        if (section_curves.find(key) == section_curves.end()) continue;
        starts_at[bs.start] = int(k);
      }
      std::vector<OrientedEdge *> loop;
      for (std::size_t i = 0; i < n;) {
        if (starts_at[i] >= 0) {
          const BoundarySection& bs = entry.second.sections[starts_at[i]];
          const int a = cycle[bs.start], b = cycle[(bs.start + bs.count - 1) % n];
          std::set<int> key;
          for (std::size_t k = 0; k < bs.count; k++) key.insert(cycle[(bs.start + k) % n]);
          // Where the section is shared with a planar face, that face has to be
          // able to take this very edge, and it is handed it by finding this run
          // of vertices in its loop. That search can fail: a loop fanned into
          // triangles to follow a moved corner no longer carries the run.
          //
          // It used to fail silently, and this wrote the conic anyway. The other
          // face then wrote the chords it still had, and one boundary came out
          // as an ELLIPSE from the cylinder and a LINE from the plane - two
          // edges where the geometry has one, each used once, and the shell
          // open. That is open item 11's root cause A, seen on the band family
          // at $fn 64 as `2 edge(s) used by only one face`.
          //
          // So the search comes first and it is binding: if the far face cannot
          // take the edge, this does not write it either, and both sides keep
          // the chords they agree on. Deciding once means neither side may
          // decide alone.
          std::size_t at_j = 0;
          if (bs.loop >= 0) {
            // A loop that will be fanned into triangles cannot take this edge at
            // all, whatever its corners still spell. The fan writes one straight
            // line per triangle side - it never reads `arc_subs` - and a section
            // spanning a run of several corners could not be one triangle's side
            // even if it did. So the handover is impossible, not merely missed,
            // and writing the conic here would leave the conic on this face and
            // chords on that one.
            //
            // That is open item 11's root cause A: on the band family at $fn 64
            // with the corner placement relaxed, one boundary came out as an
            // ELLIPSE from the cylinder and a LINE from the plane beside it, two
            // edges where the geometry has one. `split_for_corners` is settled
            // long before any face is written, so this is knowable here.
            if (split_for_corners.count(std::size_t(bs.loop)) != 0) {
              section_unshared++;
              starts_at[i] = -1;  // chords on both sides, which is what agrees
              continue;
            }
            const std::vector<int>& other = loops[bs.loop];
            const std::size_t m = other.size();
            const std::size_t span = bs.count - 1;
            bool shared = false;
            for (std::size_t j = 0; j < m && !shared; j++) {
              bool fwd = true, rev = true;
              for (std::size_t c = 0; c <= span; c++) {
                fwd = fwd && other[(j + c) % m] == cycle[(bs.start + c) % n];
                rev = rev && other[(j + c) % m] == cycle[(bs.start + span - c) % n];
              }
              if (!fwd && !rev) continue;
              shared = true;
              at_j = j;
            }
            if (!shared) {
              section_unshared++;
              starts_at[i] = -1;  // chords on both sides, which is what agrees
              continue;
            }
          }
          EdgeCurve *edge = nullptr;
          bool dir = true;
          const auto found = section_edges.find(key);
          if (found != section_edges.end()) {
            edge = found->second;
            dir = edge->vert1 == get_vertex(a);
          } else {
            // Counted, not refused, for the reason the promotion pass gives:
            // the conic is the wrong curve but lies on the quadric, and the
            // chord it would fall back to does not. See open item 26.
            if (planeIsFacetOfCurved(surfaces, bs.normal, {vertices[a], vertices[b]}) != nullptr) {
              sections_on_facet++;
            }
            SectionEllipse sec;
            if (!planeSectionEllipse(patch.surface.get(), bs.normal, bs.on_plane, sec)) {
              starts_at[i] = -1;  // fall back to the chords, which is what was there
              continue;
            }
            auto ell =
              new Ellipse(entities, "", conic_placement(sec.centre, bs.normal, sec.major), sec.a, sec.b);
            // An ELLIPSE runs counter clockwise about its own axis, so the edge
            // says whether it agrees - decided by whether the section's middle
            // lies on the counter clockwise side of its two ends.
            const Vector3d minor = bs.normal.cross(sec.major);
            auto param = [&](const Vector3d& q) {
              const Vector3d rel = q - sec.centre;
              return atan2(rel.dot(minor) / sec.b, rel.dot(sec.major) / sec.a);
            };
            auto ccw = [](double from, double to) {
              double d = to - from;
              while (d < 0) d += 2 * M_PI;
              while (d >= 2 * M_PI) d -= 2 * M_PI;
              return d;
            };
            const double ta = param(vertices[a]), tb = param(vertices[b]);
            if (a == b) {
              // A whole rim: the way the rim is walked from its first vertex.
              dir = ccw(ta, param(vertices[cycle[(bs.start + 1) % n]])) < M_PI;
            } else if (bs.count > 2) {
              // A vertex of the section's own interior settles it exactly.
              const double tm = param(vertices[cycle[(bs.start + bs.count / 2) % n]]);
              dir = ccw(ta, tm) < ccw(ta, tb);
            } else {
              // A section of one edge has no interior vertex to ask, and asking
              // for one returns its far end - which made the test read "equal",
              // took the same branch every time, and sent half the arcs the long
              // way round. What decides it instead is that the edge is one
              // segment of a tessellation: it spans at most a third of the way
              // round even at the coarsest $fn, so the arc that replaces it is
              // the shorter one.
              dir = ccw(ta, tb) <= M_PI;
            }
            edge = new EdgeCurve(entities, get_vertex(dir ? a : b), get_vertex(dir ? b : a), ell, true);
            section_edges.emplace(key, edge);
            if (bs.declared) sections_declared++;
            else sections_fitted++;
          }
          loop.push_back(new OrientedEdge(entities, edge, dir));
          face_edges_here.push_back(edge);
          // And the planar face across it gives up the segments the curve
          // replaces, the same way a band's rim does. The region's boundary is
          // walked the way its own facets go, so the far face necessarily goes
          // the other way; two faces traversing a shared edge the same way is
          // precisely what leaves a shell open.
          if (bs.loop >= 0 && section_subs.insert(key).second) {
            // `at_j` is where the run sits in that face's loop, found above -
            // the same search, done once, and binding rather than advisory.
            const std::size_t span = bs.count - 1;
            if (span == loops[bs.loop].size()) rim_of_loop[bs.loop] = {edge, !dir};
            else arc_subs[bs.loop].push_back({at_j, span, edge, !dir});
          }
          i = bs.start + bs.count - 1;
          continue;
        }
        const int a = cycle[i], b = cycle[(i + 1) % n];
        bool dir = true;
        EdgeCurve *edge = nullptr;
        const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
        const auto crossing = crossing_edges.find(key);
        if (crossing != crossing_edges.end()) {
          const auto found = crossing_curves.find(key);
          if (found != crossing_curves.end()) {
            edge = found->second;
            dir = edge->vert1 == get_vertex(a);
          } else {
            std::vector<Vector3d> ctrl;
            const Vector3d& p0 = vertices[key.first];
            const Vector3d& p1 = vertices[key.second];
            if (intersectionArc(crossing->second.first, crossing->second.second, p0, p1, 1e-7, ctrl)) {
              std::vector<Point *> cp;
              for (const auto& c : ctrl) cp.push_back(new Point(entities, c));
              auto curve = new BSplineCurve(entities, "", cp);
              edge = new EdgeCurve(entities, get_vertex(key.first), get_vertex(key.second), curve, true);
              crossing_curves.emplace(key, edge);
              crossing_written++;
              dir = edge->vert1 == get_vertex(a);
            }
          }
          if (edge != nullptr && (cyl != nullptr || cone != nullptr)) {
            // What this face's parameters mean, kept until the other face has
            // written its own and both pcurves can be built. Only a cylinder or a
            // cone: the pcurve is written from their parameters.
            CrossingSide side;
            side.surface = surface;
            side.base = base;
            side.axis = cone != nullptr && cone->slope < 0 ? Vector3d(-axis) : axis;
            side.ref = ref;
            side.slope = cone != nullptr ? fabs(cone->slope) : 0.0;
            crossing_sides[key].push_back(side);
          }
        }
        if (edge == nullptr) {
          edge = get_line_from_map(edge_map, a, b, get_vertex(a), get_vertex(b), dir, merged_edge_cnt);
        }
        loop.push_back(new OrientedEdge(entities, edge, dir));
        face_edges_here.push_back(edge);
        i++;
      }
      // A rim that is one whole section is one closed edge, which is a whole
      // loop on its own.
      if (loop.empty() || (loop.size() < 2 && !entry.second.seam)) continue;
      // A cone face bounded by one loop that goes once round the axis has the
      // apex inside it: the region is a disc, pinched to a point where the
      // surface is. The surface is periodic round the axis, so a face covering
      // the whole turn needs a seam, as a band's does - the loop continues from
      // where it started up a generator to the apex and back down the same
      // edge. Without one the face is not bounded in its own parameters, and a
      // reader has to invent the seam; OCCT did, which is not the same as the
      // file being right.
      if (cone != nullptr && boundary.size() == 1 && cone->slope != 0) {
        const Vector3d apex = base - axis * (cone->r / cone->slope);
        // A loop through the apex bounds the face there itself, and the apex is
        // then a vertex the faces beside it share - a Voronoi roof's reflex
        // corner. Only a loop that stays off it can wind round it.
        bool through_apex = false;
        double turn = 0.0;
        for (std::size_t k = 0; k < n && !through_apex; k++) {
          const Vector3d off = vertices[cycle[k]] - apex;
          through_apex = (off - axis * axis.dot(off)).norm() <=
                         recognition::forwardError(12, off.norm() + apex.norm());
          auto angle = [&](int v) {
            const Vector3d rel = vertices[v] - base;
            return atan2(rel.dot(ref2), rel.dot(ref));
          };
          double d = angle(cycle[(k + 1) % n]) - angle(cycle[k]);
          if (d > M_PI) d -= 2 * M_PI;
          if (d < -M_PI) d += 2 * M_PI;
          turn += d;
        }
        // A winding number, so it is an integer: round the axis once, or not.
        if (!through_apex && fabs(turn) > M_PI) {
          Vertex *start = get_vertex(cycle[0]);
          auto tip = new Vertex(entities, new Point(entities, apex));
          EdgeCurve *seam = create_line_edge_curve(start, tip, true);
          loop.push_back(new OrientedEdge(entities, seam, true));
          loop.push_back(new OrientedEdge(entities, seam, false));
          face_edges_here.push_back(seam);
          apex_seams++;
        }
      }
      if (entry.second.seam) {
        seam_rims.emplace_back(&entry.second, std::move(loop));
        continue;
      }
      auto edge_loop = new EdgeLoop(entities, loop);
      bounds.push_back(new FaceBound(entities, edge_loop, true, entry.first == outer));
    }
    if (seam_rims.size() == 2) {
      // One rim, up the seam, the other rim, and back down: each rim starts
      // where the seam meets it (sealWithSeam), so the four close.
      const int from = seam_rims[0].first->verts[0], to = seam_rims[1].first->verts[0];
      EdgeCurve *seam = create_line_edge_curve(get_vertex(from), get_vertex(to), true);
      std::vector<OrientedEdge *> joined = seam_rims[0].second;
      joined.push_back(new OrientedEdge(entities, seam, true));
      joined.insert(joined.end(), seam_rims[1].second.begin(), seam_rims[1].second.end());
      joined.push_back(new OrientedEdge(entities, seam, false));
      face_edges_here.push_back(seam);
      bounds.push_back(new FaceBound(entities, new EdgeLoop(entities, joined), true, true));
      band_seams++;
    }
    if (bounds.empty()) continue;

    // Which way the face points. A cylinder's own normal is radially outward,
    // so the mesh decides by whether one of its facets agrees.
    bool outward = true;
    if (!patch.facets.empty()) {
      const std::vector<int>& facet = loops[patch.facets.front()];
      Vector3d centroid = Vector3d::Zero();
      for (const int v : facet) centroid += vertices[v];
      centroid /= double(facet.size());
      Vector3d away;
      if (elliptic != nullptr) {
        // the surface's own normal, which for an ellipse is not the radius
        outward =
          !elliptic->normalAt(centroid, away) || away.dot(loop_normals[patch.facets.front()]) > 0;
      } else {
        const Vector3d rel = centroid - base;
        const Vector3d radial = (rel - axis * axis.dot(rel)).normalized();
        outward = radial.dot(loop_normals[patch.facets.front()]) > 0;
      }
    }

    sfaces_extra.push_back(new Face(entities, bounds, surface, outward));
    face_edges_extra.push_back(face_edges_here);
  }

  // Say where each crossing curve runs across the two surfaces it lies on, now
  // that both faces have been written and their placements are known.
  //
  // The 3D curve stays definitive - .CURVE_3D., as it is everywhere else here -
  // so a reader that ignores the pcurves sees the file it saw before. What they
  // add is that a curve on two surfaces is *said* to be on them: STEP calls that
  // a SURFACE_CURVE, and a kernel that would otherwise re-project a B-spline
  // onto a quadric and decide for itself is told the answer instead.
  int pcurved = 0;
  for (const auto& entry : crossing_curves) {
    const auto sides = crossing_sides.find(entry.first);
    if (sides == crossing_sides.end() || sides->second.size() != 2) continue;
    auto *bez = dynamic_cast<BSplineCurve *>(entry.second->round);
    if (bez == nullptr || bez->pts.size() < 2) continue;
    const int degree = int(bez->pts.size()) - 1;

    // The curve at its own collocation parameters, which are the points it was
    // fitted through, so the pcurve and the 3D curve agree there exactly.
    std::vector<Vector3d> on_curve(degree + 1);
    for (int i = 0; i <= degree; i++) {
      const double t = double(i) / degree;
      Vector3d p = Vector3d::Zero();
      double binom = 1;
      for (int j = 0; j <= degree; j++) {
        p += bez->pts[j]->pt * (binom * pow(t, j) * pow(1 - t, degree - j));
        binom = binom * (degree - j) / (j + 1);
      }
      on_curve[i] = p;
    }

    Eigen::MatrixXd m(degree + 1, degree + 1);
    for (int i = 0; i <= degree; i++) {
      const double t = double(i) / degree;
      double binom = 1;
      for (int j = 0; j <= degree; j++) {
        m(i, j) = binom * pow(t, j) * pow(1 - t, degree - j);
        binom = binom * (degree - j) / (j + 1);
      }
    }
    const Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(m);

    std::vector<PCurve *> on_both;
    for (const CrossingSide& side : sides->second) {
      const Vector3d ref2 = side.axis.cross(side.ref);
      Eigen::MatrixXd rhs(degree + 1, 2);
      double previous = 0;
      for (int i = 0; i <= degree; i++) {
        const Vector3d rel = on_curve[i] - side.base;
        double u = atan2(rel.dot(ref2), rel.dot(side.ref));
        // A surface of revolution's parameter wraps, and a curve that steps over
        // the seam has to keep counting rather than jump a turn.
        if (i > 0) {
          while (u - previous > M_PI) u -= 2 * M_PI;
          while (u - previous < -M_PI) u += 2 * M_PI;
        }
        previous = u;
        rhs(i, 0) = u;
        rhs(i, 1) = rel.dot(side.axis);
      }
      const Eigen::MatrixXd solved = qr.solve(rhs);
      std::vector<Point2d *> cp;
      for (int j = 0; j <= degree; j++) {
        cp.push_back(new Point2d(entities, solved(j, 0), solved(j, 1)));
      }
      if (param_context == nullptr) param_context = new ParametricContext(entities);
      auto rep =
        new DefinitionalRepresentation(entities, new BSplineCurve2d(entities, cp), param_context);
      on_both.push_back(new PCurve(entities, side.surface, rep));
    }
    entry.second->round = new SurfaceCurve(entities, bez, on_both);
    pcurved++;
  }
  if (pcurved > 0) {
    LOG(
      "STEP export: %1$d of those carr%2$s a pcurve on each of the two surfaces, so the curve "
      "is written as lying on them rather than left to be projected back",
      pcurved, pcurved == 1 ? "ies" : "y");
  }

  // Build the loops, their edges and the carrier planes.
  std::vector<FaceBound *> face_bounds(face_cnt, nullptr);
  std::size_t planes_off_plane = 0;
  double worst_plane_off = 0;
  // What the mesher was entitled to call coplanar, by its own rule - see
  // coplanarTolerance in GeometryEvaluator.cc, which this has to agree with.
  double mesh_coplanar_tol = 1e-9;
  {
    Vector3d lo = vertices.empty() ? Vector3d(0, 0, 0) : vertices[0];
    Vector3d hi = lo;
    for (const auto& v : vertices) {
      lo = lo.cwiseMin(v);
      hi = hi.cwiseMax(v);
    }
    mesh_coplanar_tol = std::max(1e-9, 1e-8 * (hi - lo).norm());
  }
  std::vector<Plane *> planes(face_cnt, nullptr);
  int planes_declared = 0;
  std::vector<std::vector<EdgeCurve *>> loop_edges(face_cnt);

  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i] || consumed[i] || split_for_corners.count(i) != 0) continue;
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
    // create the plane. Where the model declared one that this face lies in,
    // that is the plane: a declaration cannot drift, and a fit to mesh vertices
    // moves with the tessellation, with whatever a boolean left behind, and with
    // every vertex a later pass touches. The face's own vertices are unchanged
    // either way - the declaration is only accepted when every one of them is
    // already in it, so this replaces the *coefficients* and nothing else.
    // The plane the face's corners are on, not the one the mesh gave it before
    // the corner placement ran. Newell's formula over the loop as written, kept
    // to the mesh's sense so the face still looks the way the shell is oriented.
    // For a face the placement did not bend the two agree to 1e-9 by
    // construction - the veto is what guarantees that - and for a triangle it
    // tilted they do not, which is the whole reason a tilted triangle used to be
    // refused a move it could perfectly well take.
    Vector3d norm = loop_normals[i];
    {
      Vector3d newell = Vector3d::Zero();
      for (int j = 0; j < n; j++) {
        newell += vertices[loop[j]].cross(vertices[loop[(j + 1) % n]]);
      }
      if (newell.norm() > 1e-18) {
        newell.normalize();
        norm = newell.dot(loop_normals[i]) >= 0 ? newell : Vector3d(-newell);
      }
    }
    Vector3d on_plane = vertices[loop[0]];
    {
      double extent = 0;
      for (int j = 0; j < n; j++) {
        extent = std::max(extent, (vertices[loop[j]] - on_plane).norm());
      }
      const double tol = std::max(1e-12, 1e-9 * std::max(1.0, extent));
      for (std::size_t dp = 0; dp < declared_planes.size(); dp++) {
        const auto& declared = declared_planes[dp];
        if (fabs(fabs(declared.second.dot(norm)) - 1.0) > 1e-9) continue;
        bool holds = true;
        for (int j = 0; holds && j < n; j++) {
          holds = fabs((vertices[loop[j]] - declared.first).dot(declared.second)) <= tol;
        }
        if (!holds) continue;
        if (dp < declared_plane_index.size() && declared_plane_index[dp] < decl_written.size()) {
          decl_written[declared_plane_index[dp]] = 1;
        }
        if (i < facet_class.size()) facet_class[i] = 1;
        // Keep the mesh's sense: a declaration says which plane, not which way
        // the face looks out of it.
        norm = declared.second.dot(norm) > 0 ? declared.second : Vector3d(-declared.second);
        on_plane = declared.first + norm * (vertices[loop[0]] - declared.first).dot(norm);
        // Per face, not per loop: a face with a hole in it computes a plane for
        // its inner bound too, and counting those says a ring was written twice.
        if (!loop_is_hole[i]) planes_declared++;
        break;
      }
    }
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

    {
      // A planar face may not assert a plane one of its own bounds is not in.
      //
      // The veto above refuses a move that would bend a face; this is the same
      // property asked of what is actually written, over every bound the face
      // carries. It is measured against the mesh's own coplanarity tolerance,
      // which is what a merged loop is entitled to be warped by:
      // GeometryEvaluator's coplanarTolerance buckets triangles within
      // 1e-8 of the model's diagonal, so on lid10 - 248 mm across - ten merged
      // quads come out warped by up to 2.262e-06 against that 2.48e-06
      // allowance, and no coupon or fixture exceeds it. Anything further off was
      // bent by something else, which is a defect in this exporter and not in
      // the mesh it was given: on lid10 the corner placement bent two faces
      // through their *holes* by 1.8 and 5.7, which a kernel cannot read as one
      // planar face at all - it splits them, seals the cavity between, and reads
      // the solid 28% too large.
      double off = 0;
      for (const int w : loop) off = std::max(off, fabs((vertices[w] - on_plane).dot(norm)));
      for (std::size_t j = 0; j < face_cnt; j++) {
        if (!loop_valid[j] || loop_is_hole[i] || parents[j] != int(i)) continue;
        for (const int w : loops[j]) {
          off = std::max(off, fabs((vertices[w] - on_plane).dot(norm)));
        }
      }
      if (off > mesh_coplanar_tol) {
        planes_off_plane++;
        worst_plane_off = std::max(worst_plane_off, off);
      }
    }
    auto plane_point = new Point(entities, on_plane);
    auto plane_dir_1 = new Direction(entities, norm);
    auto plane_dir_2 = new Direction(entities, ref);
    auto plane_axis = new Axis2Placement(entities, plane_dir_1, plane_dir_2, plane_point);
    planes[i] = new Plane(entities, plane_axis);

    auto edge_loop = new EdgeLoop(entities, oriented_edges);
    face_bounds[i] = new FaceBound(entities, edge_loop, true, !loop_is_hole[i]);
  }

  if (planes_declared > 0) {
    LOG(
      "STEP export: %1$d planar face%2$s written on a plane the model declared rather than one "
      "fitted to its own corners",
      planes_declared, planes_declared == 1 ? "" : "s");
  }

  const recognition::ReportInputs report_inputs{
    features, surfaces, recognition::Mesh{&vertices, &loops, &loop_valid, &loop_is_hole, &loop_normals},
    faceOrigin, owned};
  recognition::reportDeclarationCensus(report_inputs, analytic, decl_written, facet_class);
  // The fans for the polygons the corner placement bent.
  //
  // Each triangle takes its boundary edges from the same map every other face
  // uses, so the shell still stitches along them; the diagonals are new and
  // used by exactly the two triangles that share them. Each triangle carries
  // the plane its own three corners lie on, which is the point of the fan -
  // the polygon's plane is the one they no longer all sit on.
  for (const auto& sc : split_for_corners) {
    const std::size_t i = sc.first;
    for (const auto& corner : sc.second) {
      const int tri[3] = {corner[0], corner[1], corner[2]};
      const Vector3d e1 = vertices[tri[1]] - vertices[tri[0]];
      const Vector3d e2 = vertices[tri[2]] - vertices[tri[0]];
      if (e1.cross(e2).norm() < area_eps) continue;
      std::vector<OrientedEdge *> oriented;
      std::vector<EdgeCurve *> edges_here;
      for (int j = 0; j < 3; j++) {
        const int a = tri[j], b = tri[(j + 1) % 3];
        bool dir = true;
        EdgeCurve *e =
          get_line_from_map(edge_map, a, b, get_vertex(a), get_vertex(b), dir, merged_edge_cnt);
        oriented.push_back(new OrientedEdge(entities, e, dir));
        edges_here.push_back(e);
      }
      // The triangle's own plane, not the polygon's. A triangle is planar
      // wherever its corners are, which is the whole reason for fanning one
      // out - but only if the PLANE written under it is the one its corners
      // actually lie on. Taking the polygon's normal instead left every fan
      // triangle asserting a plane its corners had just moved off, and
      // step-declare-grid-scad carried the entire 0.096 on its planes while its
      // cylinders and its sweep were already exact.
      //
      // And it is written the way the bound winds, never turned to agree with
      // the polygon. Turning it is what an inverted ear used to produce: a
      // PLANE exactly opposite the loop bounding it, which is not a face.
      // Choosing the apex is what makes that unnecessary - every ear of the
      // chosen fan winds with the polygon - so a disagreement here is a bug
      // rather than a case to paper over, and the triangle is dropped.
      Vector3d tn = e1.cross(e2);
      if (tn.dot(loop_normals[i]) <= 0) continue;
      tn.normalize();
      Vector3d ref = e1 - tn * tn.dot(e1);
      if (ref.norm() < 1e-12) ref = AnalyticFeatures::perpendicular(tn);
      else ref.normalize();
      auto axis = new Axis2Placement(entities, new Direction(entities, tn), new Direction(entities, ref),
                                     new Point(entities, vertices[tri[0]]));
      std::vector<FaceBound *> bounds;
      bounds.push_back(new FaceBound(entities, new EdgeLoop(entities, oriented), true, true));
      sfaces_extra.push_back(new Face(entities, bounds, new Plane(entities, axis), true));
      face_edges_extra.push_back(edges_here);
    }
  }

  // Combine every outer loop with the loops of its holes into one ADVANCED_FACE.
  std::vector<Face *> sfaces;
  std::vector<std::vector<EdgeCurve *>> face_edges;
  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i] || loop_is_hole[i] || consumed[i] || split_for_corners.count(i) != 0) {
      continue;
    }
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

  if (planes_off_plane > 0) {
    LOG(message_group::Export_Error,
        "STEP export: %1$d planar face%2$s assert a plane one of their own bounds is not in, by up "
        "to %3$.4f against the %4$.2e the mesh's own coplanarity allows - a bound bent by "
        "something other than the merge",
        int(planes_off_plane), planes_off_plane == 1 ? "" : "s", worst_plane_off, mesh_coplanar_tol);
  }
  // the recognised cylinders are faces of the same shell
  for (std::size_t i = 0; i < sfaces_extra.size(); i++) {
    sfaces.push_back(sfaces_extra[i]);
    face_edges.push_back(face_edges_extra[i]);
  }

  // Every edge bounding a recovered curved face was still a straight line: the
  // exporter drops a cylinder behind the mesh's polyline boundary, so the edge
  // is the chord where the surface is the arc, and the gap between them is the
  // sagitta. Measured on the band family, 366 of 388 line-on-cylinder edges lay
  // off the cylinder they bounded, by up to 0.17mm - doc/step-export-development.md
  // has the tables. Every kernel that reads such a file has to widen its
  // tolerance to swallow that; two do it silently and one declines, and none of
  // them is wrong to object.
  //
  // The 22 edges that were already exact say what the fix is. A ruling lies on a
  // cylinder by construction, so the segments running up and down the axis need
  // nothing, and it is the ones running around it - at constant height, chords
  // of a circle - that should be written as arcs of that circle instead.
  //
  // An edge belongs to two faces, so promoting it is only sound when both can
  // contain the arc. A plane can when its normal is the axis, which is what a
  // flat top or an annulus is; anything else declines, and the edge stays the
  // line it was. That is what keeps this from ever moving an edge off a face
  // that was exact already, which would trade one kernel's complaint for
  // everyone's.
  std::map<EdgeCurve *, std::vector<Face *>> edge_faces;
  for (std::size_t i = 0; i < face_edges.size(); i++) {
    for (auto *edge : face_edges[i]) edge_faces[edge].push_back(sfaces[i]);
  }

  // The axis and radius a quadric face implies for an edge, if it implies one:
  // the section at the edge's own height, which exists only when both ends are
  // at the same height and the same distance from the axis.
  auto section_of = [&](const SurfaceType *surface, const Vector3d& p1, const Vector3d& p2,
                        Vector3d& axis, Vector3d& centre, double& radius) {
    Vector3d ax, org;
    double apex_r = 0.0, slope = 0.0;
    if (const auto *cyl = dynamic_cast<const CylindricalSurface *>(surface)) {
      ax = cyl->axis->dir1->pt.normalized();
      org = cyl->axis->point->pt;
      apex_r = cyl->r;
    } else if (const auto *con = dynamic_cast<const ConicalSurface *>(surface)) {
      ax = con->axis->dir1->pt.normalized();
      org = con->axis->point->pt;
      apex_r = con->r;
      slope = tan(con->half_angle);
    } else {
      return false;
    }
    const double h1 = (p1 - org).dot(ax), h2 = (p2 - org).dot(ax);
    if (fabs(h1 - h2) > tol) return false;
    const Vector3d c = org + ax * h1;
    const double r1 = (p1 - c).norm(), r2 = (p2 - c).norm();
    const double want = apex_r + slope * h1;
    if (fabs(r1 - r2) > tol || fabs(r1 - want) > tol) return false;
    axis = ax;
    centre = c;
    radius = want;
    return true;
  };

  // Where the section is not a circle it is still a conic, and the chord is
  // still wrong.
  //
  // The arc promotion above asks for a *circle*: both ends at one height on the
  // axis and at the surface's own radius there. That is the section a plane
  // perpendicular to the axis cuts, and it is the only one it reaches. A plane
  // at any other angle cuts an ellipse, and the edge between such a plane and a
  // cone is a chord of that ellipse - written straight, lying off the cone by up
  // to 0.0759 on lid10 and off nothing else, because a chord of a plane section
  // does lie in the plane.
  //
  // That is SOLIDWORKS fault code 17 exactly - "edge does not lie on face's
  // surface" - and it is what its own repair of `f04` does at a thousandth of
  // the size: it moved no vertex, no surface and no topology, only re-fitting
  // 215 curves by at most 9.583e-05 onto the surfaces they bound. Ours are off
  // by 0.1089.
  //
  // The section pass writes these as ELLIPSE where it recognises a *run* of them
  // on a face it is building. This is the same curve for the edges it did not
  // claim, decided here because here is where the corners are final - the same
  // reason the endpoint invariant binds at the writer. `planeSectionEllipse` is
  // the same solver both use.
  int conics_promoted = 0, conics_declined = 0;
  // Rims the declaration settled, where a neighbouring chord plane would have
  // bent them into an ellipse. See open item 26.
  int rims_kept_circular = 0;
  // Which conics are cut by a declared plane, and which by a tessellation
  // chord. See open item 26.
  int conics_on_declared = 0, conics_on_chord = 0, conics_on_facet = 0;
  // Why the crossing was not available: the facet belongs to the very surface
  // being cut, or the arc would not fit.
  int facet_same_surface = 0;
  int why_noproj = 0, why_offquad = 0, why_offplane = 0, why_nosection = 0, why_shape = 0;
  int why_branch = 0, conics_ellipse = 0, conics_hyperbola = 0, conics_parabola = 0;
  double conic_worst_end = 0, why_offplane_worst = 0;
  auto promote_to_conic = [&](EdgeCurve *edge, const std::vector<Face *>& faces) {
    auto *was_line = dynamic_cast<Line *>(edge->round);
    if (was_line == nullptr) return false;
    const Vector3d p1 = edge->vert1->point->pt;
    const Vector3d p2 = edge->vert2->point->pt;
    if ((p1 - p2).norm() < 1e-12) return false;

    // One quadric and one plane, and every other face at the edge has to be
    // that same plane: a conic lies in exactly one plane, so a second one
    // through the edge would be asserting a curve it does not contain.
    std::shared_ptr<Surface> quadric;
    bool have_plane = false;
    Vector3d pnorm(0, 0, 0), ppoint(0, 0, 0);
    for (auto *face : faces) {
      if (const auto *con = dynamic_cast<const ConicalSurface *>(face->surface)) {
        if (quadric != nullptr) return false;
        quadric = std::make_shared<ConeSurface>(con->axis->point->pt, con->axis->dir1->pt, con->r,
                                                tan(con->half_angle));
      } else if (const auto *cyl = dynamic_cast<const CylindricalSurface *>(face->surface)) {
        if (quadric != nullptr) return false;
        quadric = std::make_shared<CylinderSurface>(cyl->axis->point->pt, cyl->axis->dir1->pt, cyl->r);
      } else if (const auto *pl = dynamic_cast<const Plane *>(face->surface)) {
        const Vector3d n = pl->axis->dir1->pt.normalized();
        const Vector3d o = pl->axis->point->pt;
        if (have_plane) {
          if (fabs(fabs(n.dot(pnorm)) - 1) > 1e-9 || fabs((o - ppoint).dot(pnorm)) > tol) return false;
        } else {
          have_plane = true;
          pnorm = n;
          ppoint = o;
        }
      } else {
        why_shape++;
        return false;  // a sweep or a patch: its crossing is not a conic
      }
    }
    if (quadric == nullptr || !have_plane) return false;

    // Only a chord that is actually off its quadric. A ruling of a cone lies on
    // it exactly, and so does any edge the placement already put there; moving
    // one of those would trade an exact edge for an approximation of itself.
    Vector3d q;
    double off = 0;
    for (const Vector3d *p : {&p1, &p2}) {
      if (!AnalyticFeatures::closestOnSurface(quadric.get(), *p, q)) return false;
      off = std::max(off, (q - *p).norm());
    }
    const Vector3d mid = (p1 + p2) / 2;
    if (!AnalyticFeatures::closestOnSurface(quadric.get(), mid, q)) return false;
    if ((q - mid).norm() <= tol) return false;  // the line is on the surface already

    // The invariant every substitution owes: the curve has to pass through the
    // run's own endpoints. A point is on the conic when it is in the plane and
    // on the quadric, and both are asked of both ends.
    for (const Vector3d *p : {&p1, &p2}) {
      const double owed = std::max(tol, 1e-9 * std::max(1.0, p->norm()));
      if (!AnalyticFeatures::closestOnSurface(quadric.get(), *p, q)) {
        conics_declined++;
        why_noproj++;
        return false;
      }
      if ((q - *p).norm() > owed) {
        conics_declined++;
        why_offquad++;
        conic_worst_end = std::max(conic_worst_end, (q - *p).norm());
        return false;
      }
      if (fabs((*p - ppoint).dot(pnorm)) > owed) {
        conics_declined++;
        why_offplane++;
        why_offplane_worst = std::max(why_offplane_worst, fabs((*p - ppoint).dot(pnorm)));
        return false;
      }
    }

    // A rim is a circle by construction, and no fit may overrule it.
    //
    // The plane in this section is whatever planar face sits beside the edge,
    // and that is not necessarily a plane the model declared: a facet of a
    // curved surface left faceted is a *chord* plane, and cutting the quadric
    // with it gives a conic that touches the true boundary only at the two ends
    // and leaves it in between. The corner arbiter already refuses such planes
    // as constraints - `kCornerTiers` marks MeshPlane `used = false`, because
    // "a chord is not a constraint" - and this pass had no such guard.
    //
    // Measured: step-split-surface.scad wrote its cylinder's rim at z = 10 as a
    // 45 degree ELLIPSE, semi-axes 28.318 x 20, centred at z = 30 on a model 13
    // tall - the chamfer's own slope, leaking out of a chord facet into a curve
    // belonging to the cylinder. The curve arcs off the rim between its
    // endpoints and the solid loses 1.2126 mm3, which no validator and neither
    // kernel objects to. See open item 26.
    //
    // So: where both ends sit at one level along the quadric's axis, and one
    // distance from it, the edge is an arc of that quadric's own circle. That
    // is settled by the declaration - the axis and the radius are the surface's
    // own - and the section plane is then the perpendicular one through the
    // ends, whatever plane the neighbouring face happens to lie in.
    {
      Vector3d qaxis, qbase;
      bool axial = false;
      if (const auto *cyl = dynamic_cast<const CylinderSurface *>(quadric.get())) {
        qaxis = cyl->normdir.normalized();
        qbase = cyl->refpt;
        axial = qaxis.norm() > 0;
      } else if (const auto *con = dynamic_cast<const ConeSurface *>(quadric.get())) {
        qaxis = con->normdir.normalized();
        qbase = con->refpt;
        axial = qaxis.norm() > 0;
      }
      if (axial) {
        const double h1 = qaxis.dot(p1 - qbase), h2 = qaxis.dot(p2 - qbase);
        const double scale = std::max(1.0, std::max(p1.norm(), p2.norm()));
        const double r1 = ((p1 - qbase) - qaxis * h1).norm();
        const double r2 = ((p2 - qbase) - qaxis * h2).norm();
        if (fabs(h1 - h2) <= 1e-9 * scale && fabs(r1 - r2) <= 1e-9 * scale) {
          pnorm = qaxis;
          ppoint = qbase + qaxis * h1;
          rims_kept_circular++;
        }
      }
    }
    // Is the plane doing the cutting a plane the *model* declared?
    //
    // Measured 2026-09-21: **none of them are**, on any reference part - 0 of
    // 78 on lid10, 0 of 18 on the bayonet, 0 of 37 on f04. So this pass never
    // has declaration-level support for its section plane; it always deduces
    // one, and cannot tell a genuine flat face a boolean made from a facet of
    // something curved that was left faceted. The corner arbiter refuses the
    // second as a constraint - kCornerTiers marks MeshPlane `used = false`,
    // "a chord is not a constraint" - and this pass never asked.
    //
    // Counted rather than refused, deliberately: declared planes are rare by
    // construction (item 23 measured 2 of 1533 merged planes matching one), so
    // refusing everything undeclared would refuse the sound cases with the
    // unsound. Telling them apart is what the claim ledger is for.
    {
      bool declared_plane = false;
      const double scale = std::max(1.0, ppoint.norm());
      for (const auto& d : surfaces) {
        const auto *pl = dynamic_cast<const PlaneSurface *>(d.get());
        if (pl == nullptr) continue;
        const Vector3d dn = pl->normdir.normalized();
        if (!(dn.norm() > 0) || fabs(fabs(dn.dot(pnorm)) - 1) > 1e-9) continue;
        if (fabs(dn.dot(ppoint - pl->refpt)) > 1e-9 * scale) continue;
        declared_plane = true;
        break;
      }
      if (declared_plane) conics_on_declared++;
      else conics_on_chord++;
      // Counted, *not* refused - and the measurement is why.
      //
      // A facet of something curved is not a plane, and the conic it cuts is
      // not a curve of this model: the true boundary is where the quadric meets
      // the surface that facet belongs to. But the conic does lie exactly on the
      // quadric, which is what code 17 measures, and the chord it would fall
      // back to does not. Refusing these outright was tried on 2026-09-21 and
      // took code 17 from 42 to 198 on lid10, 42 to 78 on the bayonet and 38 to
      // 100 on f04 - 156 edges put back off the faces they bound.
      //
      // So this cannot be fixed here. The neighbour is written as a plane
      // because a region of a *declared curved surface* was never claimed, and
      // once that has happened no consumer can recover what the model said. The
      // fix is upstream, in the claim; see open item 26 and
      // doc/step-recognition-design.md, *region before representation*.
      const Surface *facet_of =
        declared_plane ? nullptr : planeIsFacetOfCurved(surfaces, pnorm, {p1, p2});
      if (facet_of != nullptr) {
        conics_on_facet++;
        // And *every one of them*, on every reference part, has the facet on
        // the same surface being cut: 78 of 78 on lid10, 18 of 18 on the
        // bayonet, 31 of 31 on f04.
        //
        // So there are not two surfaces here and there is no crossing to write.
        // There is one declared surface, claimed as an analytic face over part
        // of its region and left as facets over the rest, and this edge is the
        // seam between the two halves - written as a section of the surface by
        // its own chord plane, which is not a boundary of anything.
        //
        // These edges should not exist. That is the split-surface defect of
        // open item 26 in another costume, and it cannot be repaired here:
        // refusing the conics was tried and took code 17 from 42 to 198 on
        // lid10, because the chord they fall back to is further off the face
        // than the wrong curve is. Removing the paper does not mend the tear.
        // The fix is upstream - claim the region whole - see
        // doc/step-recognition-design.md, *region before representation*.
        if (facet_of == quadric.get() || facet_of->sameAs(*quadric)) facet_same_surface++;
      }
    }
    SectionConic sec;
    if (!planeSectionConic(quadric.get(), pnorm, ppoint, sec)) {
      conics_declined++;
      why_nosection++;
      return false;
    }

    // Whichever conic it is, written as that conic and trimmed by its own two
    // vertices. The ends are on it already - that was asked above - so all that
    // is left is which way round the edge runs.
    // The edge keeps its two vertices in the order it already has them: the
    // ORIENTED_EDGEs in every loop that uses it were written against that order,
    // and swapping them leaves those loops not closing. What turns instead is
    // the *placement's* axis - a conic lies in its plane either way round, and
    // reversing the normal reverses the direction its parameter runs - which is
    // the same thing the arc promotion above does by choosing `from x to`.
    RoundType *curve = nullptr;
    Vector3d nz = sec.normal.normalized();
    Vector3d minor = nz.cross(sec.major);
    if (sec.kind == SectionConic::ELLIPSE) {
      auto param = [&](const Vector3d& p) {
        const Vector3d rel = p - sec.centre;
        return atan2(rel.dot(minor) / sec.b, rel.dot(sec.major) / sec.a);
      };
      double d = param(p2) - param(p1);
      while (d < 0) d += 2 * M_PI;
      while (d >= 2 * M_PI) d -= 2 * M_PI;
      // One segment of a tessellated boundary is the shorter of the two arcs, so
      // if the way round from vert1 to vert2 is the long one, the axis is the
      // wrong way up.
      if (d > M_PI) {
        nz = -nz;
        minor = -minor;
      }
      curve = new Ellipse(entities, "", conic_placement(sec.centre, nz, sec.major), sec.a, sec.b);
    } else if (sec.kind == SectionConic::HYPERBOLA) {
      // A HYPERBOLA entity describes one branch, the one the reference direction
      // points at, and `a cosh(t)` is never less than `a` - so both ends have to
      // be on that branch or the curve does not contain the edge at all.
      // ...and which branch is not a property of the section, it is which one
      // this edge is on: the two are mirror images and the solver's major axis
      // points at one of them arbitrarily. Turn it round rather than refuse.
      Vector3d major = sec.major;
      auto along = [&](const Vector3d& p) { return (p - sec.centre).dot(major); };
      if (along(p1) < 0 && along(p2) < 0) major = -major;
      if (along(p1) < sec.a * 0.5 || along(p2) < sec.a * 0.5) {
        // One end on each branch is not an edge of one conic at all.
        conics_declined++;
        why_branch++;
        return false;
      }
      // t increases with the minor coordinate, so the axis points whichever way
      // makes it increase from vert1 to vert2.
      if ((p1 - sec.centre).dot(nz.cross(major)) > (p2 - sec.centre).dot(nz.cross(major))) nz = -nz;
      curve = new Hyperbola(entities, "", conic_placement(sec.centre, nz, major), sec.a, sec.b);
    } else {
      if ((p1 - sec.centre).dot(minor) > (p2 - sec.centre).dot(minor)) nz = -nz;
      curve = new Parabola(entities, "", conic_placement(sec.centre, nz, sec.major), sec.a);
    }
    if (curve == nullptr) return false;
    if (sec.kind == SectionConic::ELLIPSE) conics_ellipse++;
    else if (sec.kind == SectionConic::HYPERBOLA) conics_hyperbola++;
    else conics_parabola++;

    edge->round = curve;
    edge->dir = true;
    was_line->live = false;
    was_line->vector->live = false;
    was_line->vector->dir->live = false;
    was_line->point->live = false;
    conics_promoted++;
    return true;
  };

  int arcs_promoted = 0, arcs_declined = 0;
  for (auto& entry : edge_faces) {
    EdgeCurve *edge = entry.first;
    auto *was_line = dynamic_cast<Line *>(edge->round);
    if (was_line == nullptr) continue;
    const Vector3d p1 = edge->vert1->point->pt;
    const Vector3d p2 = edge->vert2->point->pt;
    if ((p1 - p2).norm() < 1e-12) continue;

    // A quadric among the faces proposes the arc; every face then gets a veto.
    Vector3d axis, centre;
    double radius = 0.0;
    bool proposed = false;
    for (auto *face : entry.second) {
      if (section_of(face->surface, p1, p2, axis, centre, radius)) {
        proposed = true;
        break;
      }
    }
    if (!proposed) {
      promote_to_conic(edge, entry.second);
      continue;
    }

    bool agreed = true;
    for (auto *face : entry.second) {
      if (const auto *pl = dynamic_cast<const Plane *>(face->surface)) {
        // A circle about `axis` lies in a plane only if the plane is
        // perpendicular to it, and its endpoints are in the plane already.
        const Vector3d n = pl->axis->dir1->pt.normalized();
        agreed = fabs(fabs(n.dot(axis)) - 1) < 1e-9;
      } else {
        Vector3d other_axis, other_centre;
        double other_radius = 0.0;
        agreed = section_of(face->surface, p1, p2, other_axis, other_centre, other_radius) &&
                 (other_centre - centre).norm() < tol && fabs(other_radius - radius) < tol;
      }
      if (!agreed) break;
    }
    if (!agreed) {
      if (!promote_to_conic(edge, entry.second)) arcs_declined++;
      continue;
    }

    const Vector3d from = (p1 - centre).normalized();
    const Vector3d to = (p2 - centre).normalized();
    // The normal that makes the sweep from `from` to `to` counter clockwise and
    // shorter than half a turn, which one segment of a tessellated boundary
    // always is.
    const Vector3d normal = from.cross(to);
    if (normal.norm() < 1e-12) {
      arcs_declined++;
      continue;
    }
    auto arc_point = new Point(entities, centre);
    auto arc_axis = new Direction(entities, normal.normalized());
    auto arc_ref = new Direction(entities, from);
    edge->round =
      new Circle(entities, "", new Axis2Placement(entities, arc_axis, arc_ref, arc_point), radius);
    edge->dir = true;
    // The line and the three entities it alone referred to would otherwise sit
    // in the file unreferenced. `get_line_from_map` gives every edge its own,
    // so nothing else can be pointing at them.
    was_line->live = false;
    was_line->vector->live = false;
    was_line->vector->dir->live = false;
    was_line->point->live = false;
    arcs_promoted++;
  }
  if (rim_unshared > 0) {
    LOG(message_group::Export_Warning,
        "STEP export: %1$d band rim%2$s given up as one arc, the face across it being fanned into "
        "triangles, which cannot be handed an edge - both faces keep the mesh's own edges there",
        int(rim_unshared), rim_unshared == 1 ? " is" : "s are");
  }
  if (arcs_promoted > 0 || arcs_declined > 0) {
    LOG("STEP export: %1$d edge%2$s written as an arc on the surface it bounds, %3$d left straight",
        arcs_promoted, arcs_promoted == 1 ? "" : "s", arcs_declined);
  }
  if (rims_kept_circular > 0) {
    LOG(
      "STEP export: %1$d edge%2$s a circle of the quadric's own, kept from the conic a chord "
      "plane beside them would have cut - a rim is settled by the declaration, not by the facet "
      "next to it",
      rims_kept_circular, rims_kept_circular == 1 ? " is" : "s are");
  }
  if (facet_same_surface > 0) {
    LOG(message_group::Export_Warning,
        "STEP export: %1$d edge%2$s the seam between an analytic face and facets of the *same* "
        "declared surface, written as a section of that surface by its own chord plane - a "
        "boundary of nothing. The region was not claimed whole; see open item 26",
        facet_same_surface, facet_same_surface == 1 ? " is" : "s are");
  }
  if (conics_on_declared + conics_on_chord > 0) {
    LOG(
      "STEP export: of the conics considered, %1$d are cut by a plane the model declared and "
      "%2$d by a plane nothing declares - of which %3$d are facets of something curved, so the "
      "conic is the wrong curve; kept because the chord is further off the face, and the fix is "
      "upstream in the claim",
      conics_on_declared, conics_on_chord, conics_on_facet);
  }
  if (conics_promoted > 0 || conics_declined > 0) {
    LOG(
      "STEP export: %1$d chord%2$s written as the conic the plane cuts from the quadric beside "
      "it, %3$d left straight - the worst end of those was %4$.2e off the quadric",
      conics_promoted, conics_promoted == 1 ? " is" : "s are", conics_declined, conic_worst_end);
    LOG(
      "STEP export:    of those, %1$d are ellipses, %2$d hyperbolas and %3$d parabolas - a cone "
      "cut steeper than its own half angle is a hyperbola, and writing a chord for one is what "
      "leaves an edge off the face it bounds",
      conics_ellipse, conics_hyperbola, conics_parabola);
    LOG(
      "STEP export:    declined: %1$d no projection, %2$d an end off the quadric, %3$d an end off "
      "the plane (worst %4$.2e), %5$d no conic from that plane, %6$d an end on the far branch, "
      "%7$d a face that is not one",
      why_noproj, why_offquad, why_offplane, why_offplane_worst, why_nosection, why_branch, why_shape);
  }
  // Every edge in the file against its own two vertices, which is the one
  // relation a CAD kernel rejects a substituted curve on and the one nothing
  // here used to measure. It is asked of the finished entities rather than of
  // any pass's intermediate state, so no substitution can be added later that
  // quietly escapes it.
  {
    std::size_t off = 0;
    double worst = 0;
    std::vector<double> strays;
    for (const Entity *ent : entities) {
      const auto *edge = dynamic_cast<const EdgeCurve *>(ent);
      if (edge == nullptr || edge->round == nullptr) continue;
      for (const Vertex *v : {edge->vert1, edge->vert2}) {
        if (v == nullptr || v->point == nullptr) continue;
        const double d = vertexOffCurve(edge->round, v->point->pt);
        const double owed = 1e-6 * std::max(1.0, v->point->pt.norm());
        if (d <= owed) continue;
        off++;
        strays.push_back(d);
        worst = std::max(worst, d);
      }
    }
    if (off > 0) {
      std::sort(strays.begin(), strays.end());
      LOG(message_group::Export_Warning,
          "STEP export: %1$d vertex%2$s not on the curve its own edge is written as, by "
          "%3$.2e median and %4$.2e at worst - a reader checks exactly this, and calls it "
          "\"vertex does not lie on edge\'s curve\"",
          int(off), off == 1 ? " is" : "es are", strays[strays.size() / 2], worst);
    }
  }
  if (reseamed > 0 || reseam_full > 0) {
    LOG(
      "STEP export: %1$d sweep face%2$s written from a column that puts the profile's seam in a "
      "span it does not use, so no boundary of it crosses the seam; %3$d use every span and keep "
      "the seam where it was",
      int(reseamed), reseamed == 1 ? " is" : "s are", int(reseam_full));
  }
  if (section_stats.trimmed + section_stats.dropped > 0) {
    LOG(
      "STEP export: %1$d plane section%2$s shortened and %3$d dropped so that the curve written "
      "for a run passes through that run's own endpoints - the worst end was %4$.2e off the "
      "surface the section is cut from",
      int(section_stats.trimmed), section_stats.trimmed == 1 ? " was" : "s were",
      int(section_stats.dropped), section_stats.end_stray);
  }
  if (crossing_written > 0) {
    LOG(
      "STEP export: %1$d edge%2$s written as the curve where two declared surfaces cross, "
      "fitted to within %3$.2e of both - a chord and a plane section each lie on only one",
      crossing_written, crossing_written == 1 ? "" : "s", crossing_fit);
  }
  if (arc_refused > 0) {
    std::sort(arc_errors.begin(), arc_errors.end());
    const double lo = arc_errors.empty() ? 0 : arc_errors.front();
    const double med = arc_errors.empty() ? 0 : arc_errors[arc_errors.size() / 2];
    const double hi = arc_errors.empty() ? 0 : arc_errors.back();
    LOG(
      "EXPORT-WARNING: STEP export: %1$d edge%2$s could not be written as that curve and fell "
      "back to a chord - %3$d never fitted at any degree, the rest came within "
      "%4$.2e/%5$.2e/%6$.2e min/med/max of both surfaces against the %7$.0e asked for",
      int(arc_refused), arc_refused == 1 ? "" : "s", int(arc_no_fit), lo, med, hi, 1e-7);
    std::sort(arc_on_quadric.begin(), arc_on_quadric.end());
    std::sort(arc_on_fitted.begin(), arc_on_fitted.end());
    if (!arc_on_quadric.empty()) {
      LOG(
        "STEP export:    of that miss, %1$.2e median is against a surface stated algebraically "
        "and %2$.2e median against one that answers by projecting",
        arc_on_quadric[arc_on_quadric.size() / 2], arc_on_fitted[arc_on_fitted.size() / 2]);
      LOG("STEP export:    %1$d of them run across a crease in the declared profile and %2$d do not",
          int(arc_creased), int(arc_uncreased));
    }
  }
  if (sections_on_facet > 0) {
    LOG(
      "STEP export: %1$d boundary section%2$s cut by a facet of something curved rather than by a "
      "plane - the wrong curve, kept because the chord it would fall back to is further off the "
      "face; the fix is upstream, in the claim",
      sections_on_facet, sections_on_facet == 1 ? " is" : "s are");
  }
  if (band_seams > 0) {
    LOG(
      "STEP export: %1$d face%2$s round %3$s axis between two rims closed by a seam along a ruling, not "
      "cut in two",
      int(band_seams), band_seams == 1 ? "" : "s", band_seams == 1 ? "its" : "their");
  }
  if (apex_seams > 0) {
    LOG("STEP export: %1$d cone face%2$s closed round %3$s apex by a seam up a generator",
        int(apex_seams), apex_seams == 1 ? "" : "s", apex_seams == 1 ? "its" : "their");
  }
  if (sections_declared + sections_fitted > 0) {
    // Where each section's plane came from, because it is the difference
    // between a curve that is exact and one that is only as good as the mesh
    // it was fitted to. A declared plane cannot move; a fitted one moves with
    // the tessellation, with what the boolean left behind, and with the
    // tolerance the fit was given - and the section's centre and both its
    // semi-axes are differences taken against that plane.
    LOG(
      "STEP export: %1$d plane section%2$s written as the conic it is - %3$d on a plane the "
      "model declared, %4$d on one taken from the mesh",
      sections_declared + sections_fitted, sections_declared + sections_fitted == 1 ? "" : "s",
      sections_declared, sections_fitted);
    if (section_unshared > 0) {
      LOG(message_group::Export_Warning,
          "STEP export: %1$d plane section%2$s written as chords by both faces instead of as the "
          "conic by one, the face across being fanned into triangles or not carrying the run of "
          "corners it would have to be handed the edge by",
          int(section_unshared), section_unshared == 1 ? " is" : "s are");
    }
    if (!section_misses.empty()) {
      double tilt = 9, off = 9;
      std::size_t parallel = 0;
      for (const auto& m : section_misses) {
        tilt = std::min(tilt, m.tilt);
        if (m.tilt <= 1e-6) {
          parallel++;
          off = std::min(off, m.offset);
        }
      }
      LOG(
        "STEP export:    %1$d cut planes took the mesh's own: %2$d are parallel to a declared "
        "plane, nearest missing its offset by %3$.2e, and the rest tilt from the nearest by "
        "%4$.2e against the 1e-06 asked for; %5$d planes were declared in all",
        int(section_misses.size()), int(parallel), off > 8 ? 0.0 : off, tilt,
        int(declared_planes.size()));
    }
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
      else if (func_name == "ELLIPSE") ent = new Ellipse(entities);
      else if (func_name == "HYPERBOLA") ent = new Hyperbola(entities);
      else if (func_name == "PARABOLA") ent = new Parabola(entities);
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
