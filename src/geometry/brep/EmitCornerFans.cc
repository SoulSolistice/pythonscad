/*! The fans for bent polygons - see BodyBuild.h. */
#include "geometry/brep/BodyBuild.h"

#include <array>
#include <map>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Exact.h"
#include "utils/printutils.h"

void step::BodyBuild::emitCornerFans()
{
  // A face is fanned for a bend, and only for one: every face here must have a
  // corner off the plane it stands on, as the writer asks it. One that has none
  // could have been written whole, and a fan in its place is a boundary the
  // model does not have (open item 39, R2).
  std::size_t fanned_flat = 0;
  for (const auto& sc : split_for_corners) {
    if (flatWhereItStands(sc.first, [&](int v) { return vertices[v]; })) fanned_flat++;
  }
  if (fanned_flat > 0) {
    LOG(message_group::Export_Error,
        "STEP export: %1$d planar face%2$s fanned into triangles though every corner is on the plane "
        "%3$s on - a fan for a bend the face does not have",
        int(fanned_flat), fanned_flat == 1 ? "" : "s", fanned_flat == 1 ? "it stands" : "they stand");
  }
  // A triangle the writer cannot write. The placement accepts a fan only when
  // every triangle winds with its polygon, so none should arrive - and one
  // dropped leaves each of its edges used once, the shell open there. Dropped
  // in silence until 2026-10-05: named now where it happens, and brep::check
  // names the edges it leaves unpaired (open item 39, R8).
  std::size_t no_plane = 0, against = 0;
  for (const auto& sc : split_for_corners) {
    const std::size_t i = sc.first;
    for (const auto& corner : sc.second) {
      const int tri[3] = {corner[0], corner[1], corner[2]};
      const Vector3d e1 = vertices[tri[1]] - vertices[tri[0]];
      const Vector3d e2 = vertices[tri[2]] - vertices[tri[0]];
      if (!recognition::statesPlane(vertices[tri[0]], vertices[tri[1]], vertices[tri[2]])) {
        no_plane++;
        continue;
      }
      brep::Loop bound;
      bound.outer = true;
      for (int j = 0; j < 3; j++) {
        bool forward = true;
        const std::size_t e = lineEdge(tri[j], tri[(j + 1) % 3], forward);
        bound.coedges.push_back(brep::Coedge{e, forward, std::nullopt});
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
      if (tn.dot(loop_normals[i]) <= 0) {
        against++;
        continue;
      }
      tn.normalize();
      Vector3d ref = e1 - tn * tn.dot(e1);
      if (ref.norm() < 1e-12) ref = AnalyticFeatures::perpendicular(tn);
      else ref.normalize();
      // A triangle of a polygon the placement bent, on the plane its corners
      // are on: as good as they are, D4 until they state their own class.
      brep::Face face;
      brep::stateSource(face, sourceOf(std::make_shared<PlaneSurface>(vertices[tri[0]], tn), {i},
                                       recognition::EvidenceClass::D4, true));
      face.form = brep::Plane{{vertices[tri[0]], tn, ref}};
      face.same_sense = true;
      face.loops.push_back(std::move(bound));
      fan_faces.push_back(std::move(face));
    }
  }
  if (no_plane + against > 0) {
    LOG(message_group::Export_Error,
        "STEP export: %1$d fan triangle%2$s the placement accepted cannot be written - %3$d with no "
        "plane, %4$d wound against the polygon they fan - and %5$s dropped, which opens the shell "
        "along %6$s edges",
        int(no_plane + against), no_plane + against == 1 ? "" : "s", int(no_plane), int(against),
        no_plane + against == 1 ? "it is" : "they are", no_plane + against == 1 ? "its" : "their");
  }
}
