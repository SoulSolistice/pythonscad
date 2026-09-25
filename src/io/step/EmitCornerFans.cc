/*! The fans for bent polygons - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <array>
#include <map>
#include <vector>
#include "geometry/AnalyticFeatures.h"

void StepKernel::BodyBuild::emitCornerFans()
{
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
}
