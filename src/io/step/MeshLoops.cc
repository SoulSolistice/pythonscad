/*! The mesh's faces as loops - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>
#include "io/step/LoopGeometry.h"
#include "utils/printutils.h"

using namespace step;

void StepKernel::BodyBuild::buildLoops()
{
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

  // Clean up the loops and derive a usable plane for each of them.
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
}
