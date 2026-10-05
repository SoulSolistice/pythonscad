/*! The neutral B-rep's structural check, and the classes of its vertices and
 * edges - see Brep.h. */
#include "geometry/brep/Brep.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include "geometry/AnalyticFeatures.h"
#include "geometry/MachinePrecision.h"
#include "geometry/Surface.h"

namespace brep {

namespace {

/*! n control points of degree p want n + p + 1 knots, with multiplicities. */
bool knotsFit(std::size_t points, int degree, const std::vector<double>& knots,
              const std::vector<int>& mults)
{
  int counted = 0;
  for (const int m : mults) counted += m;
  return knots.size() == mults.size() && counted == int(points) + degree + 1;
}

/*! The algebraic degree of a record, 0 where it has none a certificate can
 * use. An elliptic cylinder is a quadric like the round one. */
int algebraicDegree(const Surface *s)
{
  if (dynamic_cast<const PlaneSurface *>(s) != nullptr) return 1;
  if (dynamic_cast<const CylinderSurface *>(s) != nullptr ||
      dynamic_cast<const ConeSurface *>(s) != nullptr ||
      dynamic_cast<const EllipticCylinderSurface *>(s) != nullptr ||
      dynamic_cast<const SphereSurface *>(s) != nullptr) {
    return 2;
  }
  if (dynamic_cast<const TorusSurface *>(s) != nullptr) return 4;
  return 0;
}

/*! A point of a carrier, with the forward error of computing it. */
struct Sample {
  Vector3d p;
  double error;
};

/*! `n` distinct points of the carrier: on a line, the segment from `start` to
 * `end` (where the file puts it); on a conic, spread over the whole curve, since
 * a certificate for the curve is one for any piece of it. */
std::vector<Sample> samples(const CurveForm& form, const Vector3d& start, const Vector3d& end, int n)
{
  std::vector<Sample> out;
  if (std::holds_alternative<Line>(form)) {
    // start + (end - start) t: a difference, a scale and a sum per coordinate.
    const double err = recognition::forwardError(9, start.norm() + end.norm());
    for (int k = 0; k < n; k++) {
      const double t = double(k) / double(n - 1);
      out.push_back({start + (end - start) * t, err});
    }
    return out;
  }
  if (const auto *c = std::get_if<Conic>(&form)) {
    const Vector3d x = c->frame.ref.normalized();
    const Vector3d y = c->frame.axis.normalized().cross(x);
    // A normalisation and a cross product for the frame (18), the functions of
    // t (1 each), two scales and two sums per coordinate - over the centre and
    // the largest offset the formula reaches.
    const double reach = c->kind == Conic::Kind::Hyperbola  ? 2 * (c->a + c->b)
                         : c->kind == Conic::Kind::Parabola ? 3 * c->a
                                                            : c->a + c->b;
    const double err = recognition::forwardError(32, c->frame.origin.norm() + reach);
    for (int k = 0; k < n; k++) {
      Vector3d p;
      if (c->kind == Conic::Kind::Circle || c->kind == Conic::Kind::Ellipse) {
        const double t = 2 * M_PI * double(k) / double(n);
        p = c->frame.origin + x * (c->a * cos(t)) + y * (c->b * sin(t));
      } else {
        const double t = -1 + 2 * double(k) / double(n - 1);
        p = c->kind == Conic::Kind::Hyperbola
              ? Vector3d(c->frame.origin + x * (c->a * cosh(t)) + y * (c->b * sinh(t)))
              : Vector3d(c->frame.origin + x * (c->a * t * t) + y * (2 * c->a * t));
      }
      out.push_back({p, err});
    }
  }
  return out;
}

}  // namespace

double distanceToCurve(const CurveForm& form, const Vector3d& p)
{
  if (const auto *line = std::get_if<Line>(&form)) {
    Vector3d dir = line->direction;
    if (dir.norm() < 1e-12) return (p - line->origin).norm();
    dir.normalize();
    const Vector3d rel = p - line->origin;
    return (rel - dir * rel.dot(dir)).norm();
  }
  if (const auto *spline = std::get_if<BSpline>(&form)) {
    // Clamped, so it starts at the first control point and ends at the last;
    // the vertex belongs to whichever end it is nearer.
    if (spline->points.empty()) return 0;
    return std::min((spline->points.front() - p).norm(), (spline->points.back() - p).norm());
  }
  const auto *c = std::get_if<Conic>(&form);
  if (c == nullptr) return 0;
  Vector3d n = c->frame.axis, major = c->frame.ref;
  if (n.norm() < 1e-12) return 0;
  n.normalize();
  if (c->kind == Conic::Kind::Circle) {
    const Vector3d rel = p - c->frame.origin;
    const double along = rel.dot(n);
    const double across = (rel - n * along).norm();
    return std::sqrt(along * along + (across - c->a) * (across - c->a));
  }
  if (major.norm() < 1e-12) return 0;
  major.normalize();
  const Vector3d minor = n.cross(major);
  const Vector3d centre = c->frame.origin;
  // Sampled over the parameter, then refined by halving the step round the best:
  // an upper bound on the distance.
  auto nearest = [&](auto at, double lo, int samples, double first_step, double floor) {
    double best_t = 0, best = std::numeric_limits<double>::infinity();
    for (int k = 0; k < samples; k++) {
      const double t = lo + k * first_step;
      const double d = (at(t) - p).norm();
      if (d < best) {
        best = d;
        best_t = t;
      }
    }
    for (double step = first_step; step > floor; step *= 0.5) {
      for (const double t : {best_t - step, best_t + step}) {
        const double d = (at(t) - p).norm();
        if (d < best) {
          best = d;
          best_t = t;
        }
      }
    }
    return best;
  };
  switch (c->kind) {
  case Conic::Kind::Ellipse:
    return nearest(
      [&](double t) { return Vector3d(centre + major * (c->a * cos(t)) + minor * (c->b * sin(t))); }, 0,
      720, 2 * M_PI / 720.0, 1e-12);
  case Conic::Kind::Hyperbola:
    return nearest(
      [&](double t) { return Vector3d(centre + major * (c->a * cosh(t)) + minor * (c->b * sinh(t))); },
      -8.0, 801, 0.02, 1e-14);
  case Conic::Kind::Parabola:
    return nearest(
      [&](double t) { return Vector3d(centre + major * (c->a * t * t) + minor * (2 * c->a * t)); },
      -30.0, 1201, 0.05, 1e-14);
  default: return 0;
  }
}

bool certifiable(const CurveForm& form, const Surface *surface)
{
  return surface != nullptr && algebraicDegree(surface) > 0 &&
         (std::holds_alternative<Line>(form) || std::holds_alternative<Conic>(form));
}

bool curveOn(const CurveForm& form, const Vector3d& start, const Vector3d& end, const Surface *surface)
{
  if (!certifiable(form, surface)) return false;
  const int d = algebraicDegree(surface);
  const bool line = std::holds_alternative<Line>(form);
  const std::vector<Sample> points = samples(form, start, end, line ? d + 1 : 2 * d + 1);
  for (const Sample& s : points) {
    const recognition::Residual r = surface->residual(s.p);
    if (!(std::fabs(r.value) <= r.bound + s.error)) return false;
  }
  return !points.empty();
}

ClassTally stateClasses(Body& body)
{
  ClassTally tally;
  auto weaker = [](EvidenceClass a, EvidenceClass b) { return std::max(a, b); };
  // Which faces use each edge, and which faces meet at each vertex.
  std::vector<std::set<std::size_t>> faces_of_edge(body.edges.size()), faces_at(body.vertices.size());
  for (std::size_t f = 0; f < body.faces.size(); f++) {
    for (const Loop& loop : body.faces[f].loops) {
      for (const Coedge& c : loop.coedges) {
        if (c.edge >= body.edges.size()) continue;
        faces_of_edge[c.edge].insert(f);
        for (const std::size_t v : {body.edges[c.edge].start, body.edges[c.edge].end}) {
          if (v < body.vertices.size()) faces_at[v].insert(f);
        }
      }
    }
  }

  // What each vertex's faces say of it, fixed by geometry: which records hold
  // it exactly, with their normals there; which cannot be asked; which say no.
  struct Holding {
    std::size_t face;
    Vector3d normal;
  };
  std::vector<std::vector<Holding>> held(body.vertices.size());
  std::vector<char> declared(body.vertices.size(), 0), off(body.vertices.size(), 0),
    unasked(body.vertices.size(), 0);
  for (std::size_t v = 0; v < body.vertices.size(); v++) {
    const Vector3d& p = body.vertices[v].point;
    for (const std::size_t f : faces_at[v]) {
      const Surface *s = body.faces[f].surface.get();
      if (const auto *grid = dynamic_cast<const GridSurface *>(s)) {
        if (grid->isDeclaredPoint(p)) declared[v] = 1;
      }
      // A record with no closed-form residual - a grid, a Bezier patch - cannot
      // be asked; one that can and says no has the vertex off its face.
      if (s == nullptr || algebraicDegree(s) == 0) {
        unasked[v] = 1;
        continue;
      }
      if (!s->residual(p).exact()) {
        off[v] = 1;
        continue;
      }
      double value = 0;
      Vector3d grad;
      if (AnalyticFeatures::surfaceImplicit(s, p, value, grad, nullptr) && grad.norm() > 0) {
        held[v].push_back({f, grad.normalized()});
      }
    }
  }
  // Three normals independent at the point: their triple product against its
  // forward error - three normalisations, a cross product and a dot over unit
  // vectors.
  auto independent = [](const Vector3d& a, const Vector3d& b, const Vector3d& c) {
    return std::fabs(a.cross(b).dot(c)) > recognition::forwardError(41, 1.0);
  };
  // A vertex's class: D0 declared, else the best independent triple of the
  // records holding it - the weakest class among the three, at least D1 - else
  // D4.
  auto vertexClass = [&](std::size_t v) {
    if (declared[v]) return EvidenceClass::D0;
    const auto& h = held[v];
    EvidenceClass best = EvidenceClass::D4;
    for (std::size_t i = 0; i < h.size(); i++) {
      for (std::size_t j = i + 1; j < h.size(); j++) {
        for (std::size_t k = j + 1; k < h.size(); k++) {
          if (!independent(h[i].normal, h[j].normal, h[k].normal)) continue;
          const EvidenceClass needs =
            weaker(EvidenceClass::D1, weaker(body.faces[h[i].face].record_evidence,
                                             weaker(body.faces[h[j].face].record_evidence,
                                                    body.faces[h[k].face].record_evidence)));
          best = std::min(best, needs);
        }
      }
    }
    return best;
  };
  // The least fixpoint over the planes taken from their own corners.
  std::vector<std::vector<std::size_t>> corners(body.faces.size());
  for (std::size_t f = 0; f < body.faces.size(); f++) {
    if (!body.faces[f].plane_from_corners) continue;
    // A plane from its own corners is its corners' - as a record, and as what
    // admits the face to it - and starts at the bottom of the fixpoint.
    body.faces[f].record_evidence = EvidenceClass::D4;
    body.faces[f].evidence = EvidenceClass::D4;
    std::set<std::size_t> seen;
    for (const Loop& loop : body.faces[f].loops) {
      for (const Coedge& c : loop.coedges) {
        if (c.edge >= body.edges.size()) continue;
        for (const std::size_t v : {body.edges[c.edge].start, body.edges[c.edge].end}) {
          if (v < body.vertices.size() && seen.insert(v).second) corners[f].push_back(v);
        }
      }
    }
  }
  for (bool changed = true; changed;) {
    changed = false;
    for (std::size_t f = 0; f < body.faces.size(); f++) {
      Face& face = body.faces[f];
      if (!face.plane_from_corners || face.surface == nullptr) continue;
      // Every corner on the plane, or the face asserts one its corner is not in.
      bool all_on = !corners[f].empty();
      for (const std::size_t v : corners[f])
        all_on = all_on && face.surface->residual(body.vertices[v].point).exact();
      if (!all_on) continue;
      // The best spanning three: the two strongest distinct corners, then the
      // first off their line - every corner before it is on that line, so no
      // other pair reaches a stronger third.
      std::vector<std::pair<EvidenceClass, std::size_t>> ranked;
      for (const std::size_t v : corners[f]) ranked.emplace_back(vertexClass(v), v);
      std::sort(ranked.begin(), ranked.end());
      const Vector3d *a = nullptr, *b = nullptr;
      EvidenceClass spans = EvidenceClass::D4;
      bool spanned = false;
      for (const auto& r : ranked) {
        const Vector3d& p = body.vertices[r.second].point;
        if (a == nullptr) {
          a = &p;
        } else if (b == nullptr) {
          if ((p - *a).norm() > recognition::forwardError(9, a->norm() + p.norm())) b = &p;
        } else if ((*b - *a).cross(p - *a).norm() >
                   recognition::forwardError(18, (a->norm() + b->norm()) * (a->norm() + p.norm()))) {
          spans = r.first;
          spanned = true;
          break;
        }
      }
      if (!spanned) continue;
      const EvidenceClass now = weaker(EvidenceClass::D1, spans);
      if (now < face.record_evidence) {
        face.record_evidence = now;
        face.evidence = now;  // a plane from corners is admitted by what spans it
        changed = true;
      }
    }
  }
  for (std::size_t v = 0; v < body.vertices.size(); v++) {
    body.vertices[v].evidence = vertexClass(v);
    if (off[v]) tally.vertices_off++;
    else if (unasked[v] && !declared[v]) tally.vertices_unasked++;
    tally.vertices[std::size_t(body.vertices[v].evidence)]++;
  }

  for (std::size_t e = 0; e < body.edges.size(); e++) {
    Edge& edge = body.edges[e];
    if (std::holds_alternative<std::monostate>(edge.curve.form) || edge.start >= body.vertices.size() ||
        edge.end >= body.vertices.size()) {
      continue;  // check() names these
    }

    EvidenceClass faces = EvidenceClass::D0;
    bool certified = !faces_of_edge[e].empty(), shown_off = false, unasked = false;
    for (const std::size_t f : faces_of_edge[e]) {
      faces = weaker(faces, body.faces[f].record_evidence);
      if (edge.curve.stated_exact) continue;
      const Surface *s = body.faces[f].surface.get();
      if (!certifiable(edge.curve.form, s)) {
        certified = false;
        unasked = true;
      } else if (!curveOn(edge.curve.form, body.vertices[edge.start].point,
                          body.vertices[edge.end].point, s)) {
        certified = false;
        shown_off = true;
      }
    }
    if (certified) {
      edge.curve.carrier_error = 0;
      edge.evidence = weaker(EvidenceClass::D1, faces);
    } else if (edge.curve.carrier_error > 0) {
      edge.evidence = weaker(EvidenceClass::D2, faces);
    } else {
      edge.curve.carrier_error = std::numeric_limits<double>::quiet_NaN();
      edge.evidence = EvidenceClass::D4;
    }
    if (shown_off) tally.edges_off++;
    else if (unasked && !certified) tally.edges_unasked++;
    tally.edges[std::size_t(edge.evidence)]++;
  }

  return tally;
}

std::vector<std::string> check(const Body& body)
{
  std::vector<std::string> out;
  auto say = [&](const std::string& what) { out.push_back(what); };
  const auto name = [](const char *kind, std::size_t i) {
    return std::string(kind) + " " + std::to_string(i);
  };

  for (std::size_t v = 0; v < body.vertices.size(); v++) {
    if (body.vertices[v].evidence == EvidenceClass::Unstated)
      say(name("vertex", v) + " states no evidence class");
  }
  for (std::size_t e = 0; e < body.edges.size(); e++) {
    const Edge& edge = body.edges[e];
    if (edge.start >= body.vertices.size() || edge.end >= body.vertices.size())
      say(name("edge", e) + " names a vertex the body does not have");
    if (edge.evidence == EvidenceClass::Unstated) say(name("edge", e) + " states no evidence class");
    if (std::holds_alternative<std::monostate>(edge.curve.form)) say(name("edge", e) + " has no curve");
    // A closed curve may start and end at one vertex; a line cannot, and one
    // that does has no length and a direction nothing determines - an edge that
    // should not exist (open item 39, R17).
    if (std::holds_alternative<Line>(edge.curve.form) && edge.start == edge.end)
      say(name("edge", e) + " is a line from a vertex to itself");
    if (const auto *spline = std::get_if<BSpline>(&edge.curve.form)) {
      if (!knotsFit(spline->points.size(), spline->degree, spline->knots, spline->mults)) {
        int counted = 0;
        for (const int m : spline->mults) counted += m;
        say(name("edge", e) + "'s B-spline has " + std::to_string(spline->points.size()) +
            " control points of degree " + std::to_string(spline->degree) + " and " +
            std::to_string(counted) + " knots, not " +
            std::to_string(int(spline->points.size()) + spline->degree + 1));
      }
      if (!spline->weights.empty() && spline->weights.size() != spline->points.size())
        say(name("edge", e) + "'s B-spline has " + std::to_string(spline->weights.size()) +
            " weights for " + std::to_string(spline->points.size()) + " control points");
    }
  }

  // Each coedge's own start and end, read through its edge's direction.
  auto ends = [&](const Coedge& c) {
    const Edge& edge = body.edges[c.edge];
    return c.forward ? std::make_pair(edge.start, edge.end) : std::make_pair(edge.end, edge.start);
  };
  for (std::size_t f = 0; f < body.faces.size(); f++) {
    const Face& face = body.faces[f];
    if (face.surface == nullptr) say(name("face", f) + " stands on no surface");
    if (std::holds_alternative<std::monostate>(face.form))
      say(name("face", f) + " is carried by no form");
    if (const auto *net = std::get_if<SplineSurface>(&face.form)) {
      const std::size_t rows = net->points.size(), cols = rows > 0 ? net->points.front().size() : 0;
      bool rect = true;
      for (const auto& row : net->points) rect = rect && row.size() == cols;
      if (!rect || !knotsFit(rows, net->degree_u, net->knots_u, net->mults_u) ||
          !knotsFit(cols, net->degree_v, net->knots_v, net->mults_v))
        say(name("face", f) + "'s spline surface does not fit its knots: a " + std::to_string(rows) +
            " by " + std::to_string(cols) + (rect ? "" : " ragged") + " net of degree " +
            std::to_string(net->degree_u) + " by " + std::to_string(net->degree_v));
    }
    if (face.evidence == EvidenceClass::Unstated) say(name("face", f) + " states no evidence class");
    if (face.record_evidence == EvidenceClass::Unstated)
      say(name("face", f) + " states no class for its record");
    std::size_t outer = 0;
    for (std::size_t l = 0; l < face.loops.size(); l++) {
      const Loop& loop = face.loops[l];
      if (loop.outer) outer++;
      const std::string where = name("face", f) + ", " + name("loop", l);
      if (loop.coedges.empty()) {
        say(where + " is empty");
        continue;
      }
      bool indices = true;
      for (const Coedge& c : loop.coedges) {
        if (c.edge >= body.edges.size()) {
          say(where + " uses " + name("edge", c.edge) + ", which the body does not have");
          indices = false;
        }
      }
      if (!indices) continue;
      for (std::size_t i = 0; i < loop.coedges.size(); i++) {
        const std::size_t here = ends(loop.coedges[i]).second;
        const std::size_t next = ends(loop.coedges[(i + 1) % loop.coedges.size()]).first;
        if (here != next)
          say(where + " does not close: coedge " + std::to_string(i) + " ends at " +
              name("vertex", here) + " and the next starts at " + name("vertex", next));
      }
    }
    if (outer > 1)
      say(name("face", f) + " has " + std::to_string(outer) + " outer loops, not at most one");
  }

  // Every face in exactly one shell, and in a closed shell every edge used
  // twice, once each way - a seam twice by the one face that closes on it.
  std::vector<int> shell_of(body.faces.size(), -1);
  for (std::size_t s = 0; s < body.shells.size(); s++) {
    const Shell& shell = body.shells[s];
    std::map<std::size_t, std::pair<int, int>> uses;  // edge -> (forward, backward)
    for (const std::size_t f : shell.faces) {
      if (f >= body.faces.size()) {
        say(name("shell", s) + " names " + name("face", f) + ", which the body does not have");
        continue;
      }
      if (shell_of[f] >= 0)
        say(name("face", f) +
            (shell_of[f] == int(s) ? " is listed twice in " + name("shell", s) : " is in two shells"));
      shell_of[f] = int(s);
      for (const Loop& loop : body.faces[f].loops) {
        for (const Coedge& c : loop.coedges) {
          if (c.edge >= body.edges.size()) continue;
          auto& u = uses[c.edge];
          (c.forward ? u.first : u.second)++;
        }
      }
    }
    if (!shell.closed) continue;
    for (const auto& kv : uses) {
      if (kv.second.first != 1 || kv.second.second != 1)
        say(name("shell", s) + " is closed and uses " + name("edge", kv.first) + " " +
            std::to_string(kv.second.first) + " times forward and " + std::to_string(kv.second.second) +
            " times backward, not once each way");
    }
  }
  for (std::size_t f = 0; f < body.faces.size(); f++) {
    if (shell_of[f] < 0) say(name("face", f) + " is in no shell");
  }

  // A body is its shells and what they refer to: an edge no face uses, or a
  // vertex no edge ends at, is not part of the solid the file states - and it
  // would still be counted among the solid's edges and classed.
  std::vector<char> edge_used(body.edges.size(), 0), vertex_used(body.vertices.size(), 0);
  for (const Face& face : body.faces) {
    for (const Loop& loop : face.loops) {
      for (const Coedge& c : loop.coedges) {
        if (c.edge < body.edges.size()) edge_used[c.edge] = 1;
      }
    }
  }
  for (std::size_t e = 0; e < body.edges.size(); e++) {
    if (edge_used[e] == 0) {
      say(name("edge", e) + " is used by no face");
      continue;
    }
    for (const std::size_t v : {body.edges[e].start, body.edges[e].end}) {
      if (v < body.vertices.size()) vertex_used[v] = 1;
    }
  }
  for (std::size_t v = 0; v < body.vertices.size(); v++) {
    if (vertex_used[v] == 0) say(name("vertex", v) + " bounds no edge a face uses");
  }
  return out;
}

std::vector<Shell> connectedShells(const Body& body)
{
  std::vector<std::size_t> root(body.faces.size());
  for (std::size_t f = 0; f < root.size(); f++) root[f] = f;
  auto find = [&](std::size_t f) {
    while (root[f] != f) f = root[f] = root[root[f]];
    return f;
  };
  // Every face using an edge joins the first face that did.
  std::vector<std::size_t> first_use(body.edges.size(), body.faces.size());
  for (std::size_t f = 0; f < body.faces.size(); f++) {
    for (const Loop& loop : body.faces[f].loops) {
      for (const Coedge& c : loop.coedges) {
        if (c.edge >= body.edges.size()) continue;
        if (first_use[c.edge] == body.faces.size()) {
          first_use[c.edge] = f;
          continue;
        }
        const std::size_t a = find(first_use[c.edge]), b = find(f);
        if (a != b) root[std::max(a, b)] = std::min(a, b);
      }
    }
  }
  // Each root is the smallest face of its component, so walking the faces in
  // order meets every component at its first face.
  std::vector<Shell> out;
  std::vector<std::size_t> shell_of_root(body.faces.size(), body.faces.size());
  for (std::size_t f = 0; f < body.faces.size(); f++) {
    const std::size_t r = find(f);
    if (shell_of_root[r] == body.faces.size()) {
      shell_of_root[r] = out.size();
      out.push_back(Shell{{}, true});
    }
    out[shell_of_root[r]].faces.push_back(f);
  }
  return out;
}

}  // namespace brep
