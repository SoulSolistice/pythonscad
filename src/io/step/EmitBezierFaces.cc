/*! The Bezier patches, written - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include "geometry/AnalyticFeatures.h"

void StepKernel::BodyBuild::emitBezierFaces()
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
