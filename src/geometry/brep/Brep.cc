/*! The neutral B-rep's topology check - see Brep.h. */
#include "geometry/brep/Brep.h"

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

  for (std::size_t e = 0; e < body.edges.size(); e++) {
    Edge& edge = body.edges[e];
    if (std::holds_alternative<std::monostate>(edge.curve.form) || edge.start >= body.vertices.size() ||
        edge.end >= body.vertices.size()) {
      continue;  // check() names these
    }

    EvidenceClass faces = EvidenceClass::D0;
    bool certified = !faces_of_edge[e].empty(), shown_off = false, unasked = false;
    for (const std::size_t f : faces_of_edge[e]) {
      faces = weaker(faces, body.faces[f].evidence);
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

  for (std::size_t v = 0; v < body.vertices.size(); v++) {
    Vertex& vertex = body.vertices[v];
    const Vector3d& p = vertex.point;
    bool declared = false, on_all = !faces_at[v].empty(), shown_off = false, unasked = false;
    EvidenceClass faces = EvidenceClass::D0;
    std::vector<Vector3d> normals;
    for (const std::size_t f : faces_at[v]) {
      const Surface *s = body.faces[f].surface.get();
      faces = weaker(faces, body.faces[f].evidence);
      if (const auto *grid = dynamic_cast<const GridSurface *>(s)) {
        if (grid->isDeclaredPoint(p)) declared = true;
      }
      // A record with no closed-form residual - a grid, a Bezier patch - cannot
      // be asked; one that can and says no has the corner off its face.
      if (s == nullptr || algebraicDegree(s) == 0) {
        on_all = false;
        unasked = true;
        continue;
      }
      if (!s->residual(p).exact()) {
        on_all = false;
        shown_off = true;
        continue;
      }
      double value = 0;
      Vector3d grad;
      if (AnalyticFeatures::surfaceImplicit(s, p, value, grad, nullptr) && grad.norm() > 0) {
        normals.push_back(grad.normalized());
      }
    }
    // Three normals independent at the point: the largest triple product over
    // them, against its forward error - three normalisations, a cross product
    // and a dot over unit vectors.
    bool determined = false;
    for (std::size_t i = 0; i < normals.size() && !determined; i++) {
      for (std::size_t j = i + 1; j < normals.size() && !determined; j++) {
        for (std::size_t k = j + 1; k < normals.size() && !determined; k++) {
          determined =
            std::fabs(normals[i].cross(normals[j]).dot(normals[k])) > recognition::forwardError(41, 1.0);
        }
      }
    }
    // A declared point a neighbour's record does not hold is still off that
    // face: the maker's point, and a face that disagrees with it.
    if (shown_off) tally.vertices_off++;
    else if (unasked && !declared) tally.vertices_unasked++;
    vertex.evidence = declared               ? EvidenceClass::D0
                      : on_all && determined ? weaker(EvidenceClass::D1, faces)
                                             : EvidenceClass::D4;
    tally.vertices[std::size_t(vertex.evidence)]++;
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
  return out;
}

}  // namespace brep
