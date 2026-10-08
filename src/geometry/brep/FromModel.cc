/*! A B-rep written from the model's boundary - see FromModel.h. */
#include "geometry/brep/FromModel.h"

#include <cmath>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "geometry/Surface.h"
#include "geometry/model/Boundary.h"

namespace brep {

namespace {

/*! A conic's principal axes: origin + u cos t + v sin t is origin + M cos(t -
 * t*) + N sin(t - t*), M and N orthogonal, tan 2 t* = 2 u.v / (|u|^2 -
 * |v|^2); where N is the longer, a quarter turn on makes it the major axis
 * without turning the direction round. */
Conic principal(const model::Curve& k)
{
  const double star = 0.5 * std::atan2(2 * k.u.dot(k.v), k.u.squaredNorm() - k.v.squaredNorm());
  Vector3d major = k.u * std::cos(star) + k.v * std::sin(star);
  Vector3d minor = k.v * std::cos(star) - k.u * std::sin(star);
  if (minor.norm() > major.norm()) {
    const Vector3d m = major;
    major = minor;
    minor = -m;
  }
  Conic c;
  c.kind = k.circle ? Conic::Kind::Circle : Conic::Kind::Ellipse;
  c.frame = Frame{k.origin, Vector3d(major.cross(minor).normalized()), Vector3d(major.normalized())};
  c.a = major.norm();
  c.b = k.circle ? c.a : minor.norm();
  return c;
}

}  // namespace

std::optional<Body> fromModel(const SolidTree& tree, std::string& why)
{
  const std::optional<model::Boundary> boundary = model::evaluate(tree, why);
  if (!boundary) return std::nullopt;
  Body body;
  for (const model::Vertex& v : boundary->vertices)
    body.vertices.push_back(Vertex{v.at, EvidenceClass::D1, -1});
  for (const model::Edge& e : boundary->edges) {
    Edge edge;
    edge.start = e.from;
    edge.end = e.to;
    if (e.curve.kind == model::Curve::Kind::Line) {
      edge.curve.form = Line{e.curve.at(e.t0), e.curve.u.normalized()};
    } else {
      edge.curve.form = principal(e.curve);
    }
    edge.same_sense = true;  // each runs from t0 to t1, its curve's own way
    edge.evidence = EvidenceClass::D1;
    body.edges.push_back(edge);
  }
  for (const model::Face& f : boundary->faces) {
    Face face;
    face.surface = f.surface;
    face.carrier = f.surface;
    face.same_sense = f.along;
    if (const auto *plane = dynamic_cast<const PlaneSurface *>(f.surface.get())) {
      const Vector3d n = plane->normdir.normalized();
      face.form = Plane{Frame{plane->refpt, n, Vector3d(n.unitOrthogonal())}};
    } else {
      const auto *cyl = static_cast<const CylinderSurface *>(f.surface.get());
      const Vector3d a = cyl->normdir.normalized();
      face.form = Cylinder{Frame{cyl->refpt, a, Vector3d(a.unitOrthogonal())}, cyl->r};
    }
    for (const model::Loop& l : f.loops) {
      Loop loop;
      loop.outer = l.outer;
      for (const model::EdgeUse& u : l.uses)
        loop.coedges.push_back(Coedge{u.edge, u.forward, std::nullopt});
      face.loops.push_back(std::move(loop));
    }
    face.evidence = EvidenceClass::D0;
    face.record_evidence = EvidenceClass::D0;
    body.faces.push_back(std::move(face));
  }
  body.shells = connectedShells(body);
  const std::vector<std::string> defects = check(body);
  if (!defects.empty()) {
    why = "the evaluated body is not well formed: " + defects.front();
    return std::nullopt;
  }
  stateClasses(body);
  return body;
}

}  // namespace brep
