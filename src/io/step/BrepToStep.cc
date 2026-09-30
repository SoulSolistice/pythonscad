/*! The neutral B-rep, written as STEP entities - see BrepToStep.h. */
#include "io/step/BrepToStep.h"

#include <map>
#include <set>
#include <utility>

namespace {

using K = StepKernel;

K::Axis2Placement *placed(std::vector<K::Entity *>& ents, const brep::Frame& frame)
{
  auto *point = new K::Point(ents, frame.origin);
  auto *axis = new K::Direction(ents, frame.axis);
  auto *ref = new K::Direction(ents, frame.ref);
  return new K::Axis2Placement(ents, axis, ref, point);
}

/*! The curve an edge is carried by, between its two vertices. */
K::RoundType *curveOf(std::vector<K::Entity *>& ents, const brep::CurveForm& form, const Vector3d& start,
                      const Vector3d& end)
{
  if (const auto *line = std::get_if<brep::Line>(&form)) {
    // The writer's own: from the edge's start, the magnitude its length.
    const double len = (end - start).norm();
    const Vector3d dir = len < 1e-12 ? Vector3d(1, 0, 0) : line->direction;
    return new K::Line(ents, new K::Point(ents, line->origin),
                       new K::Vector(ents, new K::Direction(ents, dir), len < 1e-12 ? 1.0 : len));
  }
  using Kind = brep::Conic::Kind;
  if (const auto *c = std::get_if<brep::Conic>(&form)) {
    switch (c->kind) {
    case Kind::Circle:    return new K::Circle(ents, "", placed(ents, c->frame), c->a);
    case Kind::Ellipse:   return new K::Ellipse(ents, "", placed(ents, c->frame), c->a, c->b);
    case Kind::Hyperbola: return new K::Hyperbola(ents, "", placed(ents, c->frame), c->a, c->b);
    case Kind::Parabola:  return new K::Parabola(ents, "", placed(ents, c->frame), c->a);
    }
  }
  if (const auto *s = std::get_if<brep::BSpline>(&form)) {
    std::vector<K::Point *> pts;
    for (const Vector3d& p : s->points) pts.push_back(new K::Point(ents, p));
    auto *curve = new K::BSplineCurve(ents, "", pts, s->weights);
    curve->setKnots(s->degree, s->knots, s->mults);
    return curve;
  }
  return nullptr;
}

K::SurfaceType *surfaceOf(std::vector<K::Entity *>& ents, const brep::SurfaceForm& form)
{
  if (const auto *p = std::get_if<brep::Plane>(&form)) return new K::Plane(ents, placed(ents, p->frame));
  if (const auto *c = std::get_if<brep::Cylinder>(&form))
    return new K::CylindricalSurface(ents, "", placed(ents, c->frame), c->r);
  if (const auto *c = std::get_if<brep::Cone>(&form))
    return new K::ConicalSurface(ents, "", placed(ents, c->frame), c->r, c->half_angle);
  if (const auto *s = std::get_if<brep::Sphere>(&form))
    return new K::SphericalSurface(ents, "", placed(ents, s->frame), s->r);
  if (const auto *t = std::get_if<brep::Torus>(&form))
    return new K::ToroidalSurface(ents, "", placed(ents, t->frame), t->r, t->r_minor);
  if (const auto *s = std::get_if<brep::SplineSurface>(&form)) {
    std::vector<std::vector<K::Point *>> net;
    for (const auto& row : s->points) {
      net.emplace_back();
      for (const Vector3d& p : row) net.back().push_back(new K::Point(ents, p));
    }
    auto *surface = new K::BSplineSurface(ents, "", s->degree_u, s->degree_v, net, s->weights);
    surface->setKnots(s->knots_u, s->mults_u, s->knots_v, s->mults_v);
    return surface;
  }
  if (const auto *x = std::get_if<brep::Extrusion>(&form)) {
    // The swept curve is a closed conic here - an ellipse - so no edge ends
    // bound it; its own frame states it.
    return new K::SurfaceOfLinearExtrusion(
      ents, "", curveOf(ents, x->curve, Vector3d::Zero(), Vector3d::Zero()),
      new K::Vector(ents, new K::Direction(ents, x->direction), x->length));
  }
  return nullptr;
}

K::PCurve *pcurveOf(std::vector<K::Entity *>& ents, const brep::PCurve& form, K::SurfaceType *surface,
                    K::ParametricContext *& context)
{
  if (context == nullptr) context = new K::ParametricContext(ents);
  K::Entity *flat = nullptr;
  if (const auto *line = std::get_if<brep::Line2>(&form)) {
    flat = new K::Line2d(
      ents, new K::Point2d(ents, line->origin.x(), line->origin.y()),
      new K::Vector2d(ents, new K::Direction2d(ents, line->direction.x(), line->direction.y()),
                      line->length));
  } else if (const auto *s = std::get_if<brep::BSpline2>(&form)) {
    std::vector<K::Point2d *> pts;
    for (const auto& p : s->points) pts.push_back(new K::Point2d(ents, p.x(), p.y()));
    auto *curve = new K::BSplineCurve2d(ents, pts);
    curve->setKnots(s->degree, s->knots, s->mults);
    flat = curve;
  }
  return new K::PCurve(ents, surface, new K::DefinitionalRepresentation(ents, flat, context));
}

}  // namespace

std::vector<StepKernel::Face *> step::writeBrep(std::vector<StepKernel::Entity *>& ents,
                                                const brep::Body& body,
                                                StepKernel::ParametricContext *& context)
{
  // What the B-rep states is what is written, so a face carried by no form or
  // an edge on no curve cannot be: refuse before making anything, rather than
  // write a face on nothing.
  for (const brep::Face& face : body.faces) {
    if (std::holds_alternative<std::monostate>(face.form)) return {};
  }
  for (const brep::Edge& e : body.edges) {
    if (std::holds_alternative<std::monostate>(e.curve.form)) return {};
  }

  // Surfaces first: a pcurve names the surface of the face its use is on.
  std::vector<K::SurfaceType *> surface(body.faces.size(), nullptr);
  for (std::size_t f = 0; f < body.faces.size(); f++) surface[f] = surfaceOf(ents, body.faces[f].form);

  std::vector<K::Vertex *> vertex(body.vertices.size(), nullptr);
  auto vertexOf = [&](std::size_t v) {
    if (vertex[v] == nullptr)
      vertex[v] = new K::Vertex(ents, new K::Point(ents, body.vertices[v].point));
    return vertex[v];
  };

  // Each edge's pcurves, from the coedges that carry one, in the order the
  // faces use the edge.
  std::vector<std::vector<std::pair<std::size_t, const brep::PCurve *>>> pcurves(body.edges.size());
  for (std::size_t f = 0; f < body.faces.size(); f++) {
    for (const brep::Loop& loop : body.faces[f].loops) {
      for (const brep::Coedge& c : loop.coedges) {
        if (c.pcurve) pcurves[c.edge].emplace_back(f, &*c.pcurve);
      }
    }
  }

  std::vector<K::EdgeCurve *> edge(body.edges.size(), nullptr);
  auto edgeOf = [&](std::size_t e) {
    if (edge[e] != nullptr) return edge[e];
    const brep::Edge& in = body.edges[e];
    K::Vertex *a = vertexOf(in.start);
    K::Vertex *b = vertexOf(in.end);
    K::RoundType *curve =
      curveOf(ents, in.curve.form, body.vertices[in.start].point, body.vertices[in.end].point);
    if (!pcurves[e].empty()) {
      std::vector<K::PCurve *> on;
      for (const auto& pc : pcurves[e])
        on.push_back(pcurveOf(ents, *pc.second, surface[pc.first], context));
      curve = new K::SurfaceCurve(ents, curve, on);
    }
    edge[e] = new K::EdgeCurve(ents, a, b, curve, in.same_sense);
    return edge[e];
  };

  std::vector<K::Face *> faces;
  for (std::size_t f = 0; f < body.faces.size(); f++) {
    std::vector<K::FaceBound *> bounds;
    for (const brep::Loop& loop : body.faces[f].loops) {
      std::vector<K::OrientedEdge *> oriented;
      for (const brep::Coedge& c : loop.coedges)
        oriented.push_back(new K::OrientedEdge(ents, edgeOf(c.edge), c.forward));
      bounds.push_back(new K::FaceBound(ents, new K::EdgeLoop(ents, oriented), true, loop.outer));
    }
    faces.push_back(new K::Face(ents, bounds, surface[f], body.faces[f].same_sense));
  }
  return faces;
}
