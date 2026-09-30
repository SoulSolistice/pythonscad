/*! The B-rep the file holds, read off its entities - see BodyBuild.h. */
#include "io/step/BrepMirror.h"

#include "io/step/BodyBuild.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "geometry/brep/Brep.h"
#include "io/step/LoopGeometry.h"
#include "utils/printutils.h"

using step::uf_find;

namespace {

using K = StepKernel;

brep::Frame frameOf(const K::Axis2Placement *axis)
{
  brep::Frame frame;
  if (axis == nullptr || axis->point == nullptr || axis->dir1 == nullptr || axis->dir2 == nullptr)
    return frame;
  frame.origin = axis->point->pt;
  frame.axis = axis->dir1->pt.normalized();
  frame.ref = axis->dir2->pt.normalized();
  return frame;
}

/*! The knots a curve or a net of `n` points carries where none were set: one
 * Bezier span over (0, 1), which is what the writer means by leaving them out. */
void bezierKnots(std::size_t n, int& degree, std::vector<double>& knots, std::vector<int>& mults)
{
  degree = int(n) - 1;
  knots = {0, 1};
  mults = {int(n), int(n)};
}

brep::CurveForm curveOf(const K::RoundType *round)
{
  if (const auto *on = dynamic_cast<const K::SurfaceCurve *>(round)) return curveOf(on->curve);
  if (const auto *line = dynamic_cast<const K::Line *>(round)) {
    if (line->point == nullptr || line->vector == nullptr || line->vector->dir == nullptr) return {};
    return brep::Line{line->point->pt, line->vector->dir->pt.normalized()};
  }
  using Kind = brep::Conic::Kind;
  if (const auto *c = dynamic_cast<const K::Circle *>(round))
    return brep::Conic{Kind::Circle, frameOf(c->axis), c->r, c->r};
  if (const auto *e = dynamic_cast<const K::Ellipse *>(round))
    return brep::Conic{Kind::Ellipse, frameOf(e->axis), e->semi_1, e->semi_2};
  if (const auto *h = dynamic_cast<const K::Hyperbola *>(round))
    return brep::Conic{Kind::Hyperbola, frameOf(h->axis), h->semi_axis, h->semi_imag};
  if (const auto *p = dynamic_cast<const K::Parabola *>(round))
    return brep::Conic{Kind::Parabola, frameOf(p->axis), p->focal_dist, 0};
  if (const auto *s = dynamic_cast<const K::BSplineCurve *>(round)) {
    brep::BSpline spline;
    for (const auto *pt : s->pts) spline.points.push_back(pt->pt);
    spline.weights = s->weights;
    if (s->knots.empty()) {
      bezierKnots(s->pts.size(), spline.degree, spline.knots, spline.mults);
    } else {
      spline.degree = s->spline_degree;
      spline.knots = s->knots;
      spline.mults = s->mults;
    }
    return spline;
  }
  return {};  // a curve the B-rep has no form for: check() names it
}

brep::PCurve pcurveOf(const K::PCurve *pc)
{
  const K::Entity *curve = pc->rep != nullptr ? pc->rep->curve : nullptr;
  if (const auto *line = dynamic_cast<const K::Line2d *>(curve)) {
    brep::Line2 out;
    if (line->point != nullptr) out.origin = Vector2d(line->point->u, line->point->v);
    if (line->vector != nullptr && line->vector->dir != nullptr)
      out.direction = Vector2d(line->vector->dir->u, line->vector->dir->v);
    return out;
  }
  brep::BSpline2 out;
  if (const auto *s = dynamic_cast<const K::BSplineCurve2d *>(curve)) {
    for (const auto *pt : s->pts) out.points.emplace_back(pt->u, pt->v);
    if (s->knots.empty()) {
      bezierKnots(s->pts.size(), out.degree, out.knots, out.mults);
    } else {
      out.degree = s->spline_degree;
      out.knots = s->knots;
      out.mults = s->mults;
    }
  }
  return out;
}

brep::SurfaceForm surfaceOf(const K::SurfaceType *surface)
{
  if (const auto *p = dynamic_cast<const K::Plane *>(surface)) return brep::Plane{frameOf(p->axis)};
  if (const auto *c = dynamic_cast<const K::CylindricalSurface *>(surface))
    return brep::Cylinder{frameOf(c->axis), c->r};
  if (const auto *c = dynamic_cast<const K::ConicalSurface *>(surface))
    return brep::Cone{frameOf(c->axis), c->r, c->half_angle};
  if (const auto *s = dynamic_cast<const K::SphericalSurface *>(surface))
    return brep::Sphere{frameOf(s->axis), s->r};
  if (const auto *t = dynamic_cast<const K::ToroidalSurface *>(surface))
    return brep::Torus{frameOf(t->axis), t->r_major, t->r_minor};
  if (const auto *s = dynamic_cast<const K::BSplineSurface *>(surface)) {
    brep::SplineSurface net;
    for (const auto& row : s->net) {
      net.points.emplace_back();
      for (const auto *pt : row) net.points.back().push_back(pt->pt);
    }
    net.weights = s->weights;
    net.degree_u = s->degree_u;
    net.degree_v = s->degree_v;
    if (s->knots_u.empty() || s->knots_v.empty()) {
      // A Bezier patch: one span each way, `degree + 1` points along it.
      net.knots_u = net.knots_v = {0, 1};
      net.mults_u = {s->degree_u + 1, s->degree_u + 1};
      net.mults_v = {s->degree_v + 1, s->degree_v + 1};
    } else {
      net.knots_u = s->knots_u;
      net.knots_v = s->knots_v;
      net.mults_u = s->mults_u;
      net.mults_v = s->mults_v;
    }
    return net;
  }
  if (const auto *x = dynamic_cast<const K::SurfaceOfLinearExtrusion *>(surface)) {
    brep::Extrusion out;
    out.curve = curveOf(dynamic_cast<const K::RoundType *>(x->curve));
    if (x->sweep != nullptr && x->sweep->dir != nullptr)
      out.direction = x->sweep->dir->pt.normalized() * x->sweep->length;
    return out;
  }
  return {};  // a surface the B-rep has no form for: check() names it
}

}  // namespace

std::vector<std::vector<std::size_t>> StepKernel::BodyBuild::shellFaces() const
{
  // A CLOSED_SHELL has to be a single connected shell: faces that share an
  // edge are one body, and a body that shares none with another is its own.
  std::vector<int> component(sfaces.size(), 0);
  for (std::size_t i = 0; i < sfaces.size(); i++) component[i] = int(i);
  std::map<EdgeCurve *, int> edge_owner;  // lookup only
  for (std::size_t i = 0; i < face_edges.size(); i++) {
    for (auto *edge : face_edges[i]) {
      auto it = edge_owner.find(edge);
      if (it == edge_owner.end()) {
        edge_owner.emplace(edge, int(i));
        continue;
      }
      const int a = uf_find(component, it->second);
      const int b = uf_find(component, int(i));
      if (a != b) component[a] = b;
    }
  }
  std::map<int, std::vector<std::size_t>> by_root;  // keyed by a face index: deterministic
  for (std::size_t i = 0; i < sfaces.size(); i++) by_root[uf_find(component, int(i))].push_back(i);
  std::vector<std::vector<std::size_t>> out;
  for (auto& kv : by_root) out.push_back(std::move(kv.second));
  return out;
}

brep::Body step::mirrorBrep(const std::vector<StepKernel::Face *>& sfaces,
                            const std::map<const StepKernel::Face *, FaceSource>& face_source,
                            const std::vector<std::vector<std::size_t>>& shells,
                            const std::map<const StepKernel::EdgeCurve *, EdgeSource>& edge_sources)
{
  using Vertex = StepKernel::Vertex;
  using EdgeCurve = StepKernel::EdgeCurve;
  using Face = StepKernel::Face;
  using FaceBound = StepKernel::FaceBound;
  using OrientedEdge = StepKernel::OrientedEdge;
  using SurfaceCurve = StepKernel::SurfaceCurve;
  using PCurve = StepKernel::PCurve;
  brep::Body body;
  // Entities by identity, for lookup only: the body's own order is the order
  // faces, loops and coedges are walked in, which is the file's.
  std::map<const Vertex *, std::size_t> vertex_index;
  std::map<const EdgeCurve *, std::size_t> edge_index;
  std::vector<const EdgeCurve *> edge_entity;
  auto vertexOf = [&](const Vertex *v) {
    const auto it = vertex_index.find(v);
    if (it != vertex_index.end()) return it->second;
    brep::Vertex out;
    if (v != nullptr && v->point != nullptr) out.point = v->point->pt;
    body.vertices.push_back(out);
    vertex_index.emplace(v, body.vertices.size() - 1);
    return body.vertices.size() - 1;
  };
  auto edgeOf = [&](const EdgeCurve *e) {
    const auto it = edge_index.find(e);
    if (it != edge_index.end()) return it->second;
    brep::Edge out;
    out.start = vertexOf(e->vert1);
    out.end = vertexOf(e->vert2);
    out.same_sense = e->dir;
    out.curve.form = curveOf(e->round);
    const auto stated = edge_sources.find(e);
    if (stated != edge_sources.end()) {
      out.curve.carrier_error = stated->second.fit;
      out.curve.stated_exact = stated->second.stated_exact;
    }
    body.edges.push_back(out);
    edge_entity.push_back(e);
    edge_index.emplace(e, body.edges.size() - 1);
    return body.edges.size() - 1;
  };

  for (const Face *face : sfaces) {
    brep::Face out;
    const auto source = face_source.find(face);
    if (source != face_source.end()) {
      out.surface = source->second.record;
      out.facets = source->second.facets;
      out.evidence = source->second.evidence;
    }
    out.same_sense = face->dir;
    out.form = surfaceOf(face->surface);
    for (const FaceBound *bound : face->faceBounds) {
      brep::Loop loop;
      loop.outer = bound->outer;
      if (bound->edgeLoop != nullptr) {
        for (const OrientedEdge *oe : bound->edgeLoop->faces) {
          loop.coedges.push_back(brep::Coedge{edgeOf(oe->edge), oe->dir, std::nullopt});
        }
      }
      // A bound used against its loop's own direction runs every coedge the
      // other way, in the other order.
      if (!bound->dir) {
        std::reverse(loop.coedges.begin(), loop.coedges.end());
        for (auto& c : loop.coedges) c.forward = !c.forward;
      }
      out.loops.push_back(std::move(loop));
    }
    body.faces.push_back(std::move(out));
  }

  // Each coedge's pcurve: its edge's, on the surface of the face that uses it -
  // and a seam, used twice by one face, its first pcurve on the first use.
  for (std::size_t f = 0; f < body.faces.size(); f++) {
    std::map<std::size_t, std::size_t> uses;  // edge -> uses so far on this face
    for (auto& loop : body.faces[f].loops) {
      for (auto& c : loop.coedges) {
        const auto *on = dynamic_cast<const SurfaceCurve *>(edge_entity[c.edge]->round);
        if (on == nullptr) continue;
        std::size_t k = uses[c.edge]++;
        for (const PCurve *pc : on->on) {
          if (pc == nullptr || pc->surface != sfaces[f]->surface) continue;
          if (k-- == 0) {
            c.pcurve = pcurveOf(pc);
            break;
          }
        }
      }
    }
  }

  for (const auto& faces : shells) body.shells.push_back(brep::Shell{faces, true});
  return body;
}

brep::Body StepKernel::BodyBuild::mirrorBrep() const
{
  return step::mirrorBrep(sfaces, face_source, shellFaces(), edge_source);
}

void StepKernel::BodyBuild::checkBrep() const
{
  if (!analytic) return;
  brep::Body body = mirrorBrep();
  const brep::ClassTally t = brep::stateClasses(body);
  const std::vector<std::string> real = brep::check(body);
  using recognition::EvidenceClass;
  auto n = [](const std::array<std::size_t, 6>& by, EvidenceClass c) { return int(by[std::size_t(c)]); };
  LOG(
    "STEP export: the B-rep as written - %1$d vertices, %2$d edges, %3$d faces in %4$d shells; vertices "
    "by what "
    "decided them %5$d D0, %6$d D1, %7$d D4 - %8$d off a face they bound, %9$d on one nothing can ask; "
    "edges "
    "%10$d D1, %11$d D2, %12$d D4 - %13$d carried off a face they bound, %14$d on one nothing can ask "
    "(open item "
    "39)",
    int(body.vertices.size()), int(body.edges.size()), int(body.faces.size()), int(body.shells.size()),
    n(t.vertices, EvidenceClass::D0), n(t.vertices, EvidenceClass::D1), n(t.vertices, EvidenceClass::D4),
    int(t.vertices_off), int(t.vertices_unasked), n(t.edges, EvidenceClass::D1),
    n(t.edges, EvidenceClass::D2), n(t.edges, EvidenceClass::D4), int(t.edges_off),
    int(t.edges_unasked));
  if (!real.empty()) {
    LOG(message_group::Export_Error,
        "STEP export: the B-rep as written breaks its own contract in %1$d place%2$s (brep::check) - "
        "first: %3$s",
        int(real.size()), real.size() == 1 ? "" : "s", real.front().c_str());
    for (std::size_t i = 1; i < real.size() && i < 6; i++) LOG("STEP export:   %1$s", real[i].c_str());
  }
}
