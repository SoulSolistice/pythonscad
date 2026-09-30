/*! The planar faces - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Exact.h"
#include "geometry/recognition/Reports.h"
#include "io/step/LoopGeometry.h"
#include "io/step/PlaneSections.h"
#include "utils/printutils.h"

using namespace step;

void StepKernel::BodyBuild::emitPlanarFaces()
{
  {
    Vector3d lo = vertices.empty() ? Vector3d(0, 0, 0) : vertices[0];
    Vector3d hi = lo;
    for (const auto& v : vertices) {
      lo = lo.cwiseMin(v);
      hi = hi.cwiseMax(v);
    }
    mesh_coplanar_tol = std::max(1e-9, 1e-8 * (hi - lo).norm());
  }
  int planes_declared = 0;

  for (std::size_t i = 0; i < face_cnt; i++) {
    // Only a loop that bounds a face the file writes. A hole of a face that is
    // fanned, or taken by an analytic face, bounds nothing: walked, it was held
    // to a plane of its own and credited to a declared plane no face stands on
    // (open item 39, R22).
    if (!boundWritten(i)) continue;
    const std::vector<int>& loop = loops[i];
    const int n = int(loop.size());

    // The loop as the B-rep's, each edge named through the reader.
    brep::Loop& bound = planar_loops[i];
    bound.outer = !loop_is_hole[i];
    auto use = [&](EdgeCurve *edge, bool sense) {
      bound.coedges.push_back(brep::Coedge{reader.edge(edge), sense, std::nullopt});
    };

    // a rim of a recognised band is one circular edge instead of n straight
    // ones, shared with the band's face
    const auto rim = rim_of_loop.find(i);
    if (rim != rim_of_loop.end()) {
      use(rim->second.first, rim->second.second);
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
          use(sub.edge, sub.sense);
          continue;
        }
        if (covered[j] != 0) continue;
        const int ind = loop[j];
        const int indn = loop[(j + 1) % n];
        bool edge_dir = true;
        EdgeCurve *edge_curve = get_line_from_map(edge_map, ind, indn, get_vertex(ind), get_vertex(indn),
                                                  edge_dir, merged_edge_cnt);
        use(edge_curve, edge_dir);
      }
    } else
      for (int j = 0; j < n; j++) {
        const int ind = loop[j];
        const int indn = loop[(j + 1) % n];
        bool edge_dir = true;
        EdgeCurve *edge_curve = get_line_from_map(edge_map, ind, indn, get_vertex(ind), get_vertex(indn),
                                                  edge_dir, merged_edge_cnt);
        use(edge_curve, edge_dir);
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
    // The record the plane is written on, and what decided it: the model's own
    // plane where the face lies in one - D0 where its triangles are named for
    // it, D1 where only its position says so (open item 39, R1) - and otherwise
    // the plane its corners are on, which is as good as they are: D4 until the
    // corners state their own class.
    using recognition::EvidenceClass;
    std::shared_ptr<const Surface> record;
    EvidenceClass decided = EvidenceClass::D4;
    bool from_corners = false;
    {
      for (std::size_t dp = 0; dp < declared_planes.size(); dp++) {
        const auto& declared = declared_planes[dp];
        if (!recognition::onPlane(declared.first, declared.second, vertices, loop)) continue;
        if (dp < declared_plane_index.size() && declared_plane_index[dp] < surfaces.size()) {
          const std::size_t si = declared_plane_index[dp];
          record = surfaces[si];
          const bool named = i < faceSurface.size() && faceSurface[i] == int32_t(si);
          decided = !named                                          ? EvidenceClass::D1
                    : i < faceDerived.size() && faceDerived[i] != 0 ? EvidenceClass::D1
                                                                    : EvidenceClass::D0;
        }
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
    if (record == nullptr) {
      record = std::make_shared<PlaneSurface>(on_plane, norm);
      from_corners = true;
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
    // A face stands on its outer loop's plane; a hole is a bound of that face
    // and carries none of its own (open item 39, R18). Its loops are added when
    // the faces are assembled.
    if (!loop_is_hole[i]) {
      brep::Face face;
      stateSource(face, sourceOf(record, {i}, decided, from_corners));
      face.form = brep::Plane{{on_plane, norm, ref}};
      face.same_sense = true;
      planar_faces.emplace(i, std::move(face));
    }
  }

  if (planes_declared > 0) {
    LOG(
      "STEP export: %1$d planar face%2$s written on a plane the model declared rather than one "
      "fitted to its own corners",
      planes_declared, planes_declared == 1 ? "" : "s");
  }

  const recognition::ReportInputs report_inputs{
    features,
    surfaces,
    recognition::Mesh{&vertices, &loops, &loop_valid, &loop_is_hole, &loop_normals},
    faceOrigin,
    owned,
    &rec.d0_regions,
    &rec.d0_representations,
    &rec.existence,
    &rec.existence_by,
    &rec.existence_open,
    &faceSurface,
    &faceDerived};
  // Where each facet's written face stands, read from the faces the writers
  // registered - the curved ones, all made by now.
  std::vector<int32_t> written_on(face_cnt, -1);
  for (const Face *face : sfaces_extra) {
    const auto source = face_source.find(face);
    if (source == face_source.end()) continue;
    const int32_t s = surfaceIndexOf(source->second.record.get());
    for (const std::size_t f : source->second.facets) {
      if (f < written_on.size()) written_on[f] = s;
    }
  }
  recognition::reportDeclarationCensus(report_inputs, analytic, approximate, decl_written, facet_class,
                                       written_on);
}
