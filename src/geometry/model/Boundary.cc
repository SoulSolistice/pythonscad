/*! The model's boundary, evaluated from its declared tree - see Boundary.h. */
#include "geometry/model/Boundary.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "geometry/MachinePrecision.h"
#include "geometry/Surface.h"
#include "geometry/model/Arrangement.h"
#include "geometry/model/Classify.h"
#include "geometry/recognition/Exact.h"

namespace model {

using recognition::forwardError;
using recognition::parallel;
using recognition::perpendicular;
using recognition::sameLine;

Vector3d Curve::at(double t) const
{
  if (kind == Kind::Line) return origin + u * t;
  return origin + u * std::cos(t) + v * std::sin(t);
}

Vector3d Curve::tangent(double t) const
{
  if (kind == Kind::Line) return u;
  return v * std::cos(t) - u * std::sin(t);
}

double Curve::parameterOf(const Vector3d& p) const
{
  const Vector3d w = p - origin;
  if (kind == Kind::Line) return u.dot(w) / u.squaredNorm();
  Eigen::Matrix2d m;
  m << u.dot(u), u.dot(v), u.dot(v), v.dot(v);
  const Eigen::Vector2d cs = m.inverse() * Eigen::Vector2d(u.dot(w), v.dot(w));
  return std::atan2(cs[1], cs[0]);
}

namespace {

constexpr double kTurn = 2 * M_PI;

/*! Every bound of every declared solid in the tree, one record per surface. */
bool collect(const SolidTree& t, std::vector<std::shared_ptr<Surface>>& out, std::string& why)
{
  using Op = SolidTree::Op;
  switch (t.op()) {
  case Op::Union:
  case Op::Intersection:
  case Op::Difference:
    for (const auto& child : t.children()) {
      if (!collect(*child, out, why)) return false;
    }
    return true;
  case Op::Declared:
    for (const auto& b : t.solid()->bounds) {
      bool seen = false;
      for (const auto& s : out) seen = seen || s->sameAs(*b);
      if (!seen) out.push_back(b);
    }
    return true;
  default: why = "an operand states no declared solid: " + t.what(); return false;
  }
}

/*! A cylinder's frame: a point and the unit axis, and the unit e1, e2 its
 * angle is measured in - the angle a plane's conic across it is
 * parameterised by. */
struct CylinderFrame {
  Vector3d c, a, e1, e2;
  double r;
};

CylinderFrame frameOf(const CylinderSurface& cyl)
{
  CylinderFrame f;
  f.c = cyl.refpt;
  f.a = cyl.normdir.normalized();
  f.e1 = f.a.unitOrthogonal();
  f.e2 = f.a.cross(f.e1);
  f.r = cyl.r;
  return f;
}

/*! The curves two declared surfaces meet in, in closed form. */
bool curvesOf(const Surface *x, const Surface *y, std::vector<Curve>& out, std::string& why)
{
  const auto *px = dynamic_cast<const PlaneSurface *>(x), *py = dynamic_cast<const PlaneSurface *>(y);
  const auto *cx = dynamic_cast<const CylinderSurface *>(x),
             *cy = dynamic_cast<const CylinderSurface *>(y);
  if (px != nullptr && py != nullptr) {
    const Vector3d n1 = px->normdir.normalized(), n2 = py->normdir.normalized();
    const Vector3d d = n1.cross(n2);
    if (d.norm() <= forwardError(8, 1.0)) return true;  // parallel: they never meet, or are one
    Matrix3d m;
    m.row(0) = n1.transpose();
    m.row(1) = n2.transpose();
    m.row(2) = d.transpose();
    Curve line;
    line.origin = m.inverse() * Vector3d(n1.dot(px->refpt), n2.dot(py->refpt), d.dot(px->refpt));
    line.u = d.normalized();
    out.push_back(line);
    return true;
  }
  if (cx != nullptr && cy != nullptr) {
    if (sameLine(cx->refpt, cx->normdir, cy->refpt, cy->normdir)) return true;  // coaxial: never meet
    why = "two declared cylinders not on one axis, which meet in a quartic";
    return false;
  }
  const PlaneSurface *p = px != nullptr ? px : py;
  const CylinderSurface *c = cx != nullptr ? cx : cy;
  if (p == nullptr || c == nullptr) {
    why = "a declared surface that is neither a plane nor a cylinder";
    return false;
  }
  const CylinderFrame f = frameOf(*c);
  const Vector3d n = p->normdir.normalized();
  if (perpendicular(n, f.a)) {
    // Along the axis: two rulings where the plane is nearer the axis than
    // the radius, none where further; tangent, the surfaces only touch.
    const double delta = n.dot(f.c - p->refpt);
    if (std::fabs(std::fabs(delta) - f.r) <= forwardError(16, f.c.norm() + p->refpt.norm() + f.r)) {
      why = "a declared plane tangent to a declared cylinder";
      return false;
    }
    if (std::fabs(delta) > f.r) return true;
    const Vector3d foot = f.c - n * delta;
    const Vector3d across = n.cross(f.a).normalized();
    const double half = std::sqrt(f.r * f.r - delta * delta);
    for (const double side : {1.0, -1.0}) {
      Curve ruling;
      ruling.origin = foot + across * (side * half);
      ruling.u = f.a;
      out.push_back(ruling);
    }
    return true;
  }
  // Across the axis or oblique to it: origin + u cos t + v sin t, t the
  // cylinder's own angle from e1 - so a loop's turn round the axis is read
  // off its arcs' parameters.
  const double ma = n.dot(f.a);
  Curve conic;
  conic.kind = Curve::Kind::Conic;
  conic.origin = f.c + f.a * (n.dot(p->refpt - f.c) / ma);
  conic.circle = parallel(n, f.a);
  conic.u = conic.circle ? Vector3d(f.e1 * f.r) : Vector3d(f.e1 * f.r - f.a * (f.r * n.dot(f.e1) / ma));
  conic.v = conic.circle ? Vector3d(f.e2 * f.r) : Vector3d(f.e2 * f.r - f.a * (f.r * n.dot(f.e2) / ma));
  out.push_back(conic);
  return true;
}

/*! A surface's own unit normal at a point on it: a plane's normdir, a
 * cylinder's radially out. */
Vector3d normalAt(const Surface *s, const Vector3d& p)
{
  if (const auto *plane = dynamic_cast<const PlaneSurface *>(s)) return plane->normdir.normalized();
  Vector3d n(0, 0, 0);
  s->normalAt(p, n);
  return n.normalized();
}

bool contains(const std::vector<const Surface *>& on, const Surface *s)
{
  return std::find(on.begin(), on.end(), s) != on.end();
}

/*! One use of an edge by the face of `surface` facing `along`. */
struct Use {
  std::size_t edge;
  bool forward;
  bool along;
};

}  // namespace

std::optional<Boundary> evaluate(const SolidTree& tree, std::string& why)
{
  std::vector<std::shared_ptr<Surface>> records;
  if (!collect(tree, records, why)) return std::nullopt;
  std::vector<const Surface *> raw;
  for (const auto& r : records) {
    if (dynamic_cast<const PlaneSurface *>(r.get()) == nullptr &&
        dynamic_cast<const CylinderSurface *>(r.get()) == nullptr) {
      why = "a declared surface that is neither a plane nor a cylinder";
      return std::nullopt;
    }
    raw.push_back(r.get());
  }

  // Vertices: where three meet, one per place however many triples find it.
  std::vector<ArrangementPoint> triples;
  if (!triplePoints(raw, triples)) {
    why = "three declared surfaces that meet in no closed form";
    return std::nullopt;
  }
  double reach = 1;
  for (const auto& p : triples) reach = std::max(reach, p.at.norm());
  const double same = forwardError(64, reach);
  std::vector<Vertex> points;
  for (const auto& p : triples) {
    bool merged = false;
    for (Vertex& v : points) {
      if ((v.at - p.at).norm() > same) continue;
      for (const Surface *s : p.on) {
        if (!contains(v.on, s)) v.on.push_back(s);
      }
      merged = true;
      break;
    }
    if (!merged) points.push_back(Vertex{p.at, p.on});
  }

  Boundary out;
  std::map<std::size_t, std::size_t> written;  // point -> out.vertices
  auto vertexOf = [&](std::size_t point) {
    const auto it = written.find(point);
    if (it != written.end()) return it->second;
    out.vertices.push_back(points[point]);
    return written[point] = out.vertices.size() - 1;
  };
  std::map<const Surface *, std::vector<Use>> uses;

  // Edges: each piece of each curve between its vertices, asked at its middle.
  for (std::size_t i = 0; i < raw.size(); i++) {
    for (std::size_t j = i + 1; j < raw.size(); j++) {
      const Surface *s = raw[i], *t = raw[j];
      std::vector<Curve> curves;
      if (!curvesOf(s, t, curves, why)) return std::nullopt;
      for (const Curve& k : curves) {
        // Its vertices: the points on both surfaces, and on this curve of the
        // two - a plane along a cylinder's axis meets it in two rulings.
        std::vector<std::pair<double, std::size_t>> on;
        for (std::size_t pi = 0; pi < points.size(); pi++) {
          const Vertex& v = points[pi];
          if (!contains(v.on, s) || !contains(v.on, t)) continue;
          const double param = k.parameterOf(v.at);
          if ((k.at(param) - v.at).norm() > same) continue;
          on.emplace_back(param, pi);
        }
        std::sort(on.begin(), on.end());
        std::vector<std::tuple<double, double, std::size_t, std::size_t>> pieces;
        if (k.kind == Curve::Kind::Line) {
          // A bounded solid's edge on a line runs between two vertices; the
          // rays beyond the last are no edge of it.
          for (std::size_t a = 0; a + 1 < on.size(); a++)
            pieces.emplace_back(on[a].first, on[a + 1].first, on[a].second, on[a + 1].second);
        } else if (on.empty()) {
          // A whole conic with no vertex on it: one closed edge, and a vertex
          // of its own where its parameter starts.
          points.push_back(Vertex{k.at(0), {s, t}});
          pieces.emplace_back(0.0, kTurn, points.size() - 1, points.size() - 1);
        } else {
          for (std::size_t a = 0; a < on.size(); a++) {
            const std::size_t b = (a + 1) % on.size();
            double t1 = on[b].first;
            if (b == 0) t1 += kTurn;
            pieces.emplace_back(on[a].first, t1, on[a].second, on[b].second);
          }
        }
        for (const auto& [t0, t1, from, to] : pieces) {
          const double mid = (t0 + t1) / 2;
          const Vector3d m = k.at(mid);
          // In or out of the solid, the piece bounds nothing - whatever else
          // passes through it; on it, the quadrants say how.
          const Classification where = classifyAt(tree, m, {s, t});
          if (where.where == Where::In || where.where == Where::Out) continue;
          bool in[2][2];
          if (where.where != Where::On || !quadrants(tree, m, s, t, in)) {
            why = "a point of a curve the model states no side at, (" + std::to_string(m[0]) + ", " +
                  std::to_string(m[1]) + ", " + std::to_string(m[2]) + ")";
            return std::nullopt;
          }
          const bool s_parts[2] = {in[0][0] != in[1][0], in[0][1] != in[1][1]};
          const bool t_parts[2] = {in[0][0] != in[0][1], in[1][0] != in[1][1]};
          const int s_count = int(s_parts[0]) + int(s_parts[1]),
                    t_count = int(t_parts[0]) + int(t_parts[1]);
          // An edge where both part an inside quadrant from an outside one;
          // where one alone does, its face runs on across the curve.
          if (s_count == 0 || t_count == 0) continue;
          if (s_count == 2 || t_count == 2) {
            why = "two solids touching along a curve";
            return std::nullopt;
          }
          const int js = s_parts[0] ? 0 : 1, it = t_parts[0] ? 0 : 1;
          // A face faces out of the inside quadrant it bounds.
          const bool along_s = !in[1][js], along_t = !in[it][1];
          // Each face lies, within its surface, on the side of the other
          // surface it parts; a loop runs with its face on its left.
          const Vector3d ns = normalAt(s, m), nt = normalAt(t, m), tau = k.tangent(mid);
          const Vector3d w_s = (nt - ns * ns.dot(nt)) * (js ? 1.0 : -1.0);
          const Vector3d w_t = (ns - nt * nt.dot(ns)) * (it ? 1.0 : -1.0);
          const double side_s = (along_s ? ns : Vector3d(-ns)).cross(tau).dot(w_s);
          const double side_t = (along_t ? nt : Vector3d(-nt)).cross(tau).dot(w_t);
          if (side_s == 0 || side_t == 0 || (side_s > 0) == (side_t > 0)) {
            why = "an edge its two faces do not run opposite ways";
            return std::nullopt;
          }
          out.edges.push_back(Edge{s, t, k, t0, t1, vertexOf(from), vertexOf(to)});
          uses[s].push_back(Use{out.edges.size() - 1, side_s > 0, along_s});
          uses[t].push_back(Use{out.edges.size() - 1, side_t > 0, along_t});
        }
      }
    }
  }

  // Faces: on each surface, the uses chained into loops and the loops grouped.
  for (const auto& record : records) {
    const Surface *S = record.get();
    const auto found = uses.find(S);
    if (found == uses.end()) continue;
    for (const bool along : {true, false}) {
      std::vector<Use> mine;
      for (const Use& u : found->second) {
        if (u.along == along) mine.push_back(u);
      }
      if (mine.empty()) continue;
      auto startOf = [&](const Use& u) {
        const Edge& e = out.edges[u.edge];
        return u.forward ? e.from : e.to;
      };
      auto endOf = [&](const Use& u) {
        const Edge& e = out.edges[u.edge];
        return u.forward ? e.to : e.from;
      };
      std::multimap<std::size_t, std::size_t> leaving;
      for (std::size_t x = 0; x < mine.size(); x++) leaving.emplace(startOf(mine[x]), x);
      for (const auto& [vertex, x] : leaving) {
        if (leaving.count(vertex) > 1) {
          why = "a face pinched at a vertex";
          return std::nullopt;
        }
      }
      std::vector<Loop> loops;
      std::vector<char> taken(mine.size(), 0);
      for (std::size_t x0 = 0; x0 < mine.size(); x0++) {
        if (taken[x0]) continue;
        Loop loop;
        std::size_t x = x0;
        while (!taken[x]) {
          taken[x] = 1;
          loop.uses.push_back(EdgeUse{mine[x].edge, mine[x].forward});
          const auto next = leaving.find(endOf(mine[x]));
          if (next == leaving.end()) {
            why = "a face's boundary that does not close";
            return std::nullopt;
          }
          x = next->second;
        }
        loops.push_back(std::move(loop));
      }

      if (const auto *plane = dynamic_cast<const PlaneSurface *>(S)) {
        // In the plane's own coordinates, e1 x e2 the face's outward normal:
        // a loop's exact signed area - a line's x0 y1 - x1 y0, a conic arc's
        // in closed form - outer loops counter clockwise, holes clockwise.
        const Vector3d nu =
          along ? Vector3d(plane->normdir.normalized()) : Vector3d(-plane->normdir.normalized());
        const Vector3d e1 = nu.unitOrthogonal(), e2 = nu.cross(e1);
        auto to2 = [&](const Vector3d& p) { return Vector2d(e1.dot(p), e2.dot(p)); };
        auto area = [&](const Loop& loop) {
          double twice = 0;
          for (const EdgeUse& u : loop.uses) {
            const Edge& e = out.edges[u.edge];
            double piece = 0;
            if (e.curve.kind == Curve::Kind::Line) {
              const Vector2d p0 = to2(e.curve.at(e.t0)), p1 = to2(e.curve.at(e.t1));
              piece = p0[0] * p1[1] - p1[0] * p0[1];
            } else {
              const Vector2d c = to2(e.curve.origin), uu = to2(e.curve.u), vv = to2(e.curve.v);
              const double dc = std::cos(e.t1) - std::cos(e.t0), ds = std::sin(e.t1) - std::sin(e.t0);
              piece = c[0] * (uu[1] * dc + vv[1] * ds) - c[1] * (uu[0] * dc + vv[0] * ds) +
                      (uu[0] * vv[1] - uu[1] * vv[0]) * (e.t1 - e.t0);
            }
            twice += u.forward ? piece : -piece;
          }
          return twice / 2;
        };
        // Whether point q lies inside a loop: crossings of the ray from q
        // along e1, counted exactly against lines and conic arcs.
        auto inside = [&](const Loop& loop, const Vector2d& q) {
          bool in = false;
          for (const EdgeUse& u : loop.uses) {
            const Edge& e = out.edges[u.edge];
            if (e.curve.kind == Curve::Kind::Line) {
              const Vector2d p0 = to2(e.curve.at(e.t0)), p1 = to2(e.curve.at(e.t1));
              if ((p0[1] > q[1]) == (p1[1] > q[1])) continue;
              const double x = p0[0] + (q[1] - p0[1]) * (p1[0] - p0[0]) / (p1[1] - p0[1]);
              if (x > q[0]) in = !in;
              continue;
            }
            const Vector2d c = to2(e.curve.origin), uu = to2(e.curve.u), vv = to2(e.curve.v);
            const double R = std::hypot(uu[1], vv[1]), C = q[1] - c[1];
            if (!(R > 0) || std::fabs(C) >= R) continue;
            const double phi = std::atan2(vv[1], uu[1]), spread = std::acos(C / R);
            for (const double root : {phi + spread, phi - spread}) {
              const double t = e.t0 + std::fmod(std::fmod(root - e.t0, kTurn) + kTurn, kTurn);
              if (t >= e.t1) continue;
              if (c[0] + uu[0] * std::cos(t) + vv[0] * std::sin(t) > q[0]) in = !in;
            }
          }
          return in;
        };
        std::vector<double> areas;
        for (const Loop& l : loops) areas.push_back(area(l));
        std::vector<Face> faces;
        std::vector<std::size_t> face_of_outer(loops.size(), std::size_t(-1));
        for (std::size_t l = 0; l < loops.size(); l++) {
          if (areas[l] <= 0) continue;
          face_of_outer[l] = faces.size();
          Face face{record, along, {loops[l]}};
          face.loops.front().outer = true;
          faces.push_back(std::move(face));
        }
        for (std::size_t l = 0; l < loops.size(); l++) {
          if (areas[l] > 0) continue;
          const Edge& first = out.edges[loops[l].uses.front().edge];
          const Vector2d q = to2(out.vertices[loops[l].uses.front().forward ? first.from : first.to].at);
          std::size_t best = std::size_t(-1);
          for (std::size_t o = 0; o < loops.size(); o++) {
            if (areas[o] <= 0 || !inside(loops[o], q)) continue;
            if (best == std::size_t(-1) || areas[o] < areas[best]) best = o;
          }
          if (best == std::size_t(-1)) {
            why = "a hole in a plane that no face of it holds";
            return std::nullopt;
          }
          Loop hole = loops[l];
          hole.outer = false;
          faces[face_of_outer[best]].loops.push_back(std::move(hole));
        }
        for (Face& f : faces) out.faces.push_back(std::move(f));
        continue;
      }

      // A cylinder: in (angle, height), right handed about the outward radial
      // normal. A loop's turn round the axis is its arcs' parameter spans - a
      // conic across it is parameterised by its angle, a ruling turns none.
      const auto *cyl = static_cast<const CylinderSurface *>(S);
      const CylinderFrame f = frameOf(*cyl);
      const double facing = along ? 1.0 : -1.0;
      auto turnOf = [&](const Loop& loop) {
        double turn = 0;
        for (const EdgeUse& u : loop.uses) {
          const Edge& e = out.edges[u.edge];
          if (e.curve.kind == Curve::Kind::Conic) turn += (u.forward ? 1 : -1) * (e.t1 - e.t0);
        }
        return turn * facing;
      };
      std::vector<Face> faces;
      std::vector<std::pair<double, std::size_t>>
        round;  // height where the loop crosses a reference angle
      double reference = std::numeric_limits<double>::quiet_NaN();
      for (std::size_t l = 0; l < loops.size(); l++) {
        const double turn = turnOf(loops[l]);
        if (std::fabs(turn) < M_PI) {
          // A patch that does not go round: as its own face, outer - a hole in
          // a cylinder's face needs a curve two cylinders meet in, refused above.
          Face face{record, along, {loops[l]}};
          face.loops.front().outer = true;
          faces.push_back(std::move(face));
          continue;
        }
        if (std::isnan(reference)) {
          for (const EdgeUse& u : loops[l].uses) {
            const Edge& e = out.edges[u.edge];
            if (e.curve.kind == Curve::Kind::Conic) {
              reference = (e.t0 + e.t1) / 2;
              break;
            }
          }
        }
        int crossings = 0;
        double height = 0;
        for (const EdgeUse& u : loops[l].uses) {
          const Edge& e = out.edges[u.edge];
          if (e.curve.kind != Curve::Kind::Conic) continue;
          const double t = e.t0 + std::fmod(std::fmod(reference - e.t0, kTurn) + kTurn, kTurn);
          if (t >= e.t1) continue;
          crossings++;
          height = f.a.dot(e.curve.at(t) - f.c);
        }
        if (crossings != 1) {
          why = "a loop round a cylinder that crosses an angle more than once";
          return std::nullopt;
        }
        round.emplace_back(height, l);
      }
      // Loops round the axis, by height: each band from one going round with
      // its face above to the next going round with its face below.
      std::sort(round.begin(), round.end());
      if (round.size() % 2 != 0) {
        why = "loops round a cylinder that do not pair into bands";
        return std::nullopt;
      }
      for (std::size_t b = 0; b < round.size(); b += 2) {
        const Loop& lower = loops[round[b].second];
        const Loop& upper = loops[round[b + 1].second];
        if (!(turnOf(lower) > 0) || !(turnOf(upper) < 0)) {
          why = "loops round a cylinder that do not pair into bands";
          return std::nullopt;
        }
        Face face{record, along, {lower, upper}};
        for (Loop& l : face.loops) l.outer = false;
        faces.push_back(std::move(face));
      }
      for (Face& face : faces) out.faces.push_back(std::move(face));
    }
  }
  return out;
}

}  // namespace model
