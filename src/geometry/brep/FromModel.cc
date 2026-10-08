/*! A B-rep written from the model's boundary - see FromModel.h. */
#include "geometry/brep/FromModel.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "geometry/Surface.h"
#include "geometry/model/Boundary.h"

namespace brep {

namespace {

constexpr double kTurn = 2 * M_PI;

/*! A conic's principal axes, and the shift from the written curve's
 * parameter s to the boundary's t = s + shift. origin + u cos t + v sin t is
 * origin + M cos(t - t*) + N sin(t - t*), M and N orthogonal, tan 2 t* =
 * 2 u.v / (|u|^2 - |v|^2); where N is the longer, a quarter turn on makes it
 * the major axis without turning the direction round. A circle keeps its own
 * parameter: its u and v are already orthogonal and of one length, and the
 * angle t* would be read from rounding. The shift is then taken a whole number
 * of turns on so that the edge's span starts in [0, 2 pi), where a reader
 * projecting its vertices onto the curve finds it. */
Conic principal(const model::Curve& k, double t0, double& shift)
{
  double star =
    k.circle ? 0.0 : 0.5 * std::atan2(2 * k.u.dot(k.v), k.u.squaredNorm() - k.v.squaredNorm());
  Vector3d major = k.u * std::cos(star) + k.v * std::sin(star);
  Vector3d minor = k.v * std::cos(star) - k.u * std::sin(star);
  if (!k.circle && minor.norm() > major.norm()) {
    const Vector3d m = major;
    major = minor;
    minor = -m;
    star += M_PI / 2;
  }
  shift = star + kTurn * std::floor((t0 - star) / kTurn);
  Conic c;
  c.kind = k.circle ? Conic::Kind::Circle : Conic::Kind::Ellipse;
  c.frame = Frame{k.origin, Vector3d(major.cross(minor).normalized()), Vector3d(major.normalized())};
  c.a = major.norm();
  c.b = k.circle ? c.a : minor.norm();
  return c;
}

/*! An edge's pcurve on a cylinder face, in the parameters the face's form is
 * written in - u the angle from the frame's ref about its axis, v the height
 * along it (the same e1 model::Boundary measures a conic's t from, so u = t).
 * A ruling and a circle across the axis are straight lines there, exactly. An
 * oblique conic is v = h0 + A cos u + B sin u, which no rational B-spline is:
 * cubic Hermite pieces, its value and slope exact at every joint, u exact
 * throughout (it is linear), and v within Delta^4 R / 384 of the curve for
 * pieces Delta apart, R = |(A, B)| bounding every derivative - so as many
 * pieces as bring that within `tolerance`. Its parameter is the written
 * curve's (s = t - shift), so the two are one parameter. */
PCurve onCylinder(const model::Edge& e, const CylinderSurface& cyl, double shift, double tolerance)
{
  const Vector3d a = cyl.normdir.normalized();
  const Vector3d ref = a.unitOrthogonal(), ref2 = a.cross(ref);
  auto height = [&](const Vector3d& p) { return a.dot(p - cyl.refpt); };
  if (e.curve.kind == model::Curve::Kind::Line) {
    const Vector3d p = e.curve.at(e.t0) - cyl.refpt;
    const double along = a.dot(e.curve.u.normalized());
    return Line2{Vector2d(std::atan2(p.dot(ref2), p.dot(ref)), height(e.curve.at(e.t0))),
                 Vector2d(0, along), 1};
  }
  const double s0 = e.t0 - shift, s1 = e.t1 - shift;
  if (e.curve.circle) return Line2{Vector2d(shift, height(e.curve.origin)), Vector2d(1, 0), 1};
  const double R = std::hypot(a.dot(e.curve.u), a.dot(e.curve.v));
  const double step = R > 0 ? std::pow(384 * tolerance / R, 0.25) : s1 - s0;
  const int pieces = std::max(1, int(std::ceil((s1 - s0) / step)));
  BSpline2 flat;
  flat.degree = 3;
  const double delta = (s1 - s0) / pieces;
  for (int i = 0; i <= pieces; i++) {
    const double s = s0 + delta * i, t = s + shift;
    const Vector2d at(t, height(e.curve.at(t)));
    const Vector2d slope(1, a.dot(e.curve.tangent(t)));
    if (i > 0) flat.points.push_back(at - slope * (delta / 3));
    flat.points.push_back(at);
    if (i < pieces) flat.points.push_back(at + slope * (delta / 3));
    flat.knots.push_back(s);
    flat.mults.push_back(i == 0 || i == pieces ? 4 : 3);
  }
  return flat;
}

}  // namespace

std::optional<Body> fromModel(const SolidTree& tree, std::string& why, double& uncertainty)
{
  const std::optional<model::Boundary> boundary = model::evaluate(tree, why);
  if (!boundary) return std::nullopt;
  // What the file declares it holds to, stated as the body build's
  // resolution is: 1e-9 of the part's size (BodyBuild::resolution).
  double size = 1.0;
  for (const model::Vertex& v : boundary->vertices) size = std::max(size, v.at.norm());
  uncertainty = 1e-9 * size;

  Body body;
  for (const model::Vertex& v : boundary->vertices)
    body.vertices.push_back(Vertex{v.at, EvidenceClass::D1, -1});
  std::vector<double> shifts(boundary->edges.size(), 0.0);
  for (std::size_t i = 0; i < boundary->edges.size(); i++) {
    const model::Edge& e = boundary->edges[i];
    Edge edge;
    edge.start = e.from;
    edge.end = e.to;
    if (e.curve.kind == model::Curve::Kind::Line) {
      edge.curve.form = Line{e.curve.at(e.t0), e.curve.u.normalized()};
    } else {
      edge.curve.form = principal(e.curve, e.t0, shifts[i]);
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
    const auto *cyl = dynamic_cast<const CylinderSurface *>(f.surface.get());
    if (const auto *plane = dynamic_cast<const PlaneSurface *>(f.surface.get())) {
      const Vector3d n = plane->normdir.normalized();
      face.form = Plane{Frame{plane->refpt, n, Vector3d(n.unitOrthogonal())}};
    } else {
      const Vector3d a = cyl->normdir.normalized();
      face.form = Cylinder{Frame{cyl->refpt, a, Vector3d(a.unitOrthogonal())}, cyl->r};
    }
    for (const model::Loop& l : f.loops) {
      Loop loop;
      loop.outer = l.outer;
      // On a cylinder each use carries its pcurve, which a reader would
      // otherwise fit itself at its own tolerance - a plane's it projects
      // exactly. The angle wraps, so each is taken the whole turns on that
      // start it where the one before it ended: a loop in the parameter
      // plane is a chain, not pieces a turn apart.
      double running = 0;
      bool first = true;
      for (const model::EdgeUse& u : l.uses) {
        Coedge coedge{u.edge, u.forward, std::nullopt};
        if (cyl != nullptr) {
          const model::Edge& e = boundary->edges[u.edge];
          PCurve pc = onCylinder(e, *cyl, shifts[u.edge], uncertainty);
          double from = 0, to = 0;  // the angle at the edge's own start and end
          if (const auto *line = std::get_if<Line2>(&pc)) {
            // A ruling stays at its angle; a circle's runs from t0 to t1, its
            // line's parameter s being t less the shift.
            const bool ruling = e.curve.kind == model::Curve::Kind::Line;
            from = ruling ? line->origin.x() : line->origin.x() + (e.t0 - shifts[u.edge]);
            to = ruling ? from : from + (e.t1 - e.t0);
          } else {
            const auto& spline = std::get<BSpline2>(pc);
            from = spline.points.front().x();
            to = spline.points.back().x();
          }
          const double start = u.forward ? from : to, end = u.forward ? to : from;
          const double turns = first ? 0.0 : std::round((running - start) / kTurn);
          if (turns != 0) {
            if (auto *line = std::get_if<Line2>(&pc)) line->origin.x() += turns * kTurn;
            else
              for (auto& p : std::get<BSpline2>(pc).points) p.x() += turns * kTurn;
          }
          running = end + turns * kTurn;
          first = false;
          coedge.pcurve = std::move(pc);
        }
        loop.coedges.push_back(std::move(coedge));
      }
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
