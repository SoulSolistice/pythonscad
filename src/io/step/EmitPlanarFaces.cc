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
  int planes_declared = 0, planes_by_position = 0;

  for (std::size_t i = 0; i < face_cnt; i++) {
    // Only a loop that bounds a face the file writes. A hole of a face that is
    // fanned, or taken by an analytic face, bounds nothing: walked, it was held
    // to a plane of its own and credited to a declared plane no face stands on
    // (open item 39, R22).
    if (!boundWritten(i)) continue;
    const std::vector<int>& loop = loops[i];
    const int n = int(loop.size());

    // The loop as the B-rep's: a curve an analytic face hands it named through
    // the reader, and each straight edge the B-rep's own (lineEdge).
    brep::Loop& bound = planar_loops[i];
    bound.outer = !loop_is_hole[i];
    auto use = [&](EdgeCurve *edge, bool sense) {
      bound.coedges.push_back(brep::Coedge{reader.edge(edge), sense, std::nullopt});
    };
    auto line = [&](int a, int b) {
      // Two corners the placement brought onto one point are one vertex, and
      // the mesh edge between them is no edge of the file: the loop runs on
      // from that vertex. Written, it was a line from a point to itself with a
      // direction made up for it (open item 39, R17).
      if (brepVertex(a) == brepVertex(b)) return;
      bool forward = true;
      const std::size_t e = lineEdge(a, b, forward);
      bound.coedges.push_back(brep::Coedge{e, forward, std::nullopt});
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
        line(loop[j], loop[(j + 1) % n]);
      }
    } else
      for (int j = 0; j < n; j++) line(loop[j], loop[(j + 1) % n]);
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
    // it, D1 where the name is derived or only its position says so - and
    // otherwise the plane its corners are on, which is as good as they are: D4
    // until the corners state their own class. The first declared plane the
    // face lies on is the only one: the records are merged by samePlane, and a
    // loop with a corner off a line lies in one plane (open item 39, R1).
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
          // The census counts what the name put there. Position derives a
          // face's plane, and says so: it does not make the declaration written,
          // and a face no name places is not counted on a declared plane - it
          // stood in for heritage there until 2026-09-30 (E8).
          if (named) {
            if (si < decl_written.size()) decl_written[si] = 1;
            if (i < facet_class.size()) facet_class[i] = recognition::kDeclaredPlane;
          } else if (i < facet_class.size()) {
            facet_class[i] = recognition::kPlaneByPosition;
          }
          if (!loop_is_hole[i]) (named ? planes_declared : planes_by_position)++;
        }
        // Keep the mesh's sense: a declaration says which plane, not which way
        // the face looks out of it.
        norm = declared.second.dot(norm) > 0 ? declared.second : Vector3d(-declared.second);
        on_plane = declared.first + norm * (vertices[loop[0]] - declared.first).dot(norm);
        // Counted above per face, not per loop: a face with a hole in it
        // computes a plane for its inner bound too, and counting those says a
        // ring was written twice.
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

    if (!loop_is_hole[i]) {
      // A planar face may not assert a plane one of its own bounds is not in:
      // every corner, its holes' too, on the declared plane it stands on, or all
      // on one plane where it stands on its corners' - at machine precision (C2,
      // open item 39, R2). A hole is asked with its face, never alone (R24).
      //
      // The veto above refuses a move that would bend a face; this is the same
      // property asked of what is actually written. Until 2026-09-30 it was
      // asked within 1e-8 of the model's size, the bucketing the triangle merge
      // once did; the merge asks exact coplanarity since d45fb5638, and over
      // every face of every export measured the two answers were the same. What
      // it catches is a face bent by something else: on lid10 the corner
      // placement once bent two faces through their *holes* by 1.8 and 5.7,
      // which a kernel cannot read as one planar face at all - it splits them,
      // seals the cavity between, and reads the solid 28% too large.
      std::vector<Vector3d> corners;
      for (const int w : loop) corners.push_back(vertices[w]);
      for (std::size_t j = 0; j < face_cnt; j++) {
        if (!loop_valid[j] || parents[j] != int(i)) continue;
        for (const int w : loops[j]) corners.push_back(vertices[w]);
      }
      const auto *declared = from_corners ? nullptr : dynamic_cast<const PlaneSurface *>(record.get());
      bool flat = true;
      if (declared != nullptr) {
        for (const Vector3d& p : corners)
          flat = flat && recognition::onPlane(declared->refpt, declared->normdir, p);
      } else {
        flat = recognition::allCoplanar(corners);
      }
      if (!flat) {
        // How far, for the report: against the plane the face asserts.
        double off = 0;
        for (const Vector3d& p : corners) off = std::max(off, fabs((p - on_plane).dot(norm)));
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

  if (planes_declared + planes_by_position > 0) {
    LOG(
      "STEP export: %1$d planar face%2$s written on a plane the model declared rather than one "
      "fitted to its own corners - %3$d named for it, %4$d on it by position alone, which "
      "derives the plane (D1)",
      planes_declared + planes_by_position, planes_declared + planes_by_position == 1 ? "" : "s",
      planes_declared, planes_by_position);
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
  // made - the planar ones just now, the curved ones before them.
  std::vector<int32_t> written_on(face_cnt, -1);
  for (const auto& [i, face] : planar_faces) written_on[i] = surfaceIndexOf(face.surface.get());
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
