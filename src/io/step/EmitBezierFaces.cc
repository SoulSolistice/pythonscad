/*! The Bezier patches, written - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "io/step/BrepMirror.h"

// Built on the B-rep, faces and edges alike, and made as no entity (open item
// 39, step 2d, the fifth move): what the reader made of the entities this
// emitter made before, value for value - a placement's directions as given, a
// Bezier's one span, a line from its first vertex along the unit vector to its
// second.
void StepKernel::BodyBuild::emitBezierFaces()
{
  std::map<std::set<int>, std::size_t> run_edges;  // run vertices -> its B-rep edge
  auto placement = [](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
    brep::Frame frame;
    frame.origin = origin;
    frame.axis = dir;
    frame.ref = towards;
    return frame;
  };
  auto add_edge = [&](std::size_t from, std::size_t to, brep::CurveForm form, bool stated_exact) {
    brep::Edge edge;
    edge.start = from;
    edge.end = to;
    edge.same_sense = true;
    edge.curve.form = std::move(form);
    edge.curve.stated_exact = stated_exact;
    brep_body.edges.push_back(std::move(edge));
    return brep_body.edges.size() - 1;
  };
  for (std::size_t pi = 0; pi < bezier_patches.size(); pi++) {
    const AnalyticFeatures::Patch& patch = bezier_patches[pi];
    if (!patch.alive) continue;
    const auto *bez = dynamic_cast<const BezierPatchSurface *>(patch.surface.get());
    if (bez == nullptr) continue;
    const std::shared_ptr<Surface> quadric = pi < patch_quadric.size() ? patch_quadric[pi] : nullptr;

    brep::SurfaceForm surface;
    if (const auto *cyl = dynamic_cast<const CylinderSurface *>(quadric.get())) {
      // The reference direction is the radius through the start of the first
      // rail, so the face occupies theta from zero forwards rather than a
      // stretch wrapping through the surface's own seam. Its maker (stateQuadric) has
      // already oriented the axis to make that sweep the positive one.
      const Vector3d axis = cyl->normdir.normalized();
      const Vector3d rel = bez->control(0, 0) - cyl->refpt;
      const Vector3d ref = (rel - axis * axis.dot(rel)).normalized();
      surface = brep::Cylinder{placement(cyl->refpt, axis, ref), cyl->r};
    } else if (const auto *sph = dynamic_cast<const SphereSurface *>(quadric.get())) {
      // The polar axis is the patch's apex, so the octant is the (theta, phi)
      // rectangle below the pole and its three bounding arcs are two
      // meridians and one arc of the equator - no seam crosses it.
      const Vector3d axis = sph->normdir.normalized();
      const Vector3d rel = bez->control(0, 0) - sph->refpt;
      const Vector3d ref = (rel - axis * axis.dot(rel)).normalized();
      surface = brep::Sphere{placement(sph->refpt, axis, ref), sph->r};
    } else {
      brep::SplineSurface net;
      for (int i = 0; i <= bez->degree_u; i++) {
        std::vector<Vector3d> row;
        std::vector<double> row_w;
        for (int j = 0; j <= bez->degree_v; j++) {
          row.push_back(bez->control(i, j));
          row_w.push_back(bez->weight(i, j));
        }
        net.points.push_back(row);
        if (bez->isRational()) net.weights.push_back(row_w);
      }
      // A fillet's patch is rational - its middle weight is what makes the arc a
      // circle rather than a parabola - and the weights have to be written, or
      // the face describes a different surface from the mesh it replaces.
      // One span each way, `degree + 1` points along it.
      net.degree_u = bez->degree_u;
      net.degree_v = bez->degree_v;
      net.knots_u = net.knots_v = {0, 1};
      net.mults_u = {bez->degree_u + 1, bez->degree_u + 1};
      net.mults_v = {bez->degree_v + 1, bez->degree_v + 1};
      surface = std::move(net);
    }

    brep::Loop loop;
    loop.outer = true;
    for (const auto& run : patch.runs) {
      const std::set<int> key(run.verts.begin(), run.verts.end());
      const std::size_t from = brepVertex(run.verts.front());
      const std::size_t to = brepVertex(run.verts.back());
      std::size_t edge = 0;
      const auto known = run_edges.find(key);
      if (known != run_edges.end()) {
        edge = known->second;
      } else if (run.straight) {
        const Vector3d p = brep_body.vertices[from].point;
        Vector3d v = brep_body.vertices[to].point - p;
        v /= v.norm();
        edge = add_edge(from, to, brep::Line{p, v}, false);
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
          // the patch's own boundary
          edge = add_edge(
            from, to,
            brep::Conic{brep::Conic::Kind::Circle, placement(centre, normal, ref), radius, radius},
            true);
        } else {
          brep::BSpline spline;
          spline.points = AnalyticFeatures::runControlPoints(patch, run, vertices, &spline.weights);
          spline.degree = int(spline.points.size()) - 1;
          spline.knots = {0, 1};
          spline.mults = {int(spline.points.size()), int(spline.points.size())};
          edge = add_edge(from, to, std::move(spline), true);  // a row of the patch's own net
        }
        run_edges.emplace(key, edge);
      }
      // The curve was built running from whichever patch reached it first.
      const bool sense = brep_body.edges[edge].start == from;
      loop.coedges.push_back(brep::Coedge{edge, sense, std::nullopt});

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
        bez->project(vertices[facet[0]], u, v);
        const double h = 1e-6;
        const Vector3d du =
          bez->evaluate(std::min(1.0, u + h), v) - bez->evaluate(std::max(0.0, u - h), v);
        const Vector3d dv =
          bez->evaluate(u, std::min(1.0, v + h)) - bez->evaluate(u, std::max(0.0, v - h));
        outward = du.cross(dv).dot(mesh_normal) > 0;
      }
    }

    // Written on the quadric the patch is, where recognition found one
    // (BezierPatchSurface::stateQuadric, by its maker) - a closed form of the
    // patch, the record the face stands for - and otherwise on the patch itself.
    brep::Face face;
    step::FaceSource source = sourceOf(patch.surface, patch.facets, std::nullopt, false);
    source.carrier = quadric;
    source.carrier_derived = quadric != nullptr;
    step::stateSource(face, source);
    face.same_sense = outward;
    face.form = std::move(surface);
    face.loops.push_back(std::move(loop));
    analytic_faces.push_back(std::move(face));
  }
}
