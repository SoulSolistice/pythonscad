/*! A B-rep evaluated from the model's declarations alone - see FromModel.h. */
#include "geometry/brep/FromModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <tuple>
#include <utility>
#include <vector>

#include "geometry/MachinePrecision.h"
#include "geometry/SolidTree.h"
#include "geometry/Surface.h"
#include "geometry/recognition/Classify.h"
#include "geometry/recognition/Exact.h"

namespace brep {

namespace {

using recognition::forwardError;
using recognition::Where;

/*! Every bound of every declared solid in the tree, one record per surface;
 * false where an operand is not a declared solid. */
bool collectBounds(const SolidTree& t, std::vector<std::shared_ptr<Surface>>& out, std::string& why)
{
  using Op = SolidTree::Op;
  switch (t.op()) {
  case Op::Union:
  case Op::Intersection:
  case Op::Difference:
    for (const auto& child : t.children()) {
      if (!collectBounds(*child, out, why)) return false;
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

/*! A level of the grid along one coordinate, and the record there - null for
 * an added angle, which no surface is at. */
struct Level {
  double at = 0;
  std::shared_ptr<Surface> record;
};

/*! A grid point, by its levels: radius, angle, height. */
using Point3 = std::tuple<int, int, int>;

/*! A segment of the grid, canonically directed (+r, +theta, +z): kind 'r' from
 * radius level i to i + 1 at angle level j and height level k; 't' from angle
 * level j to j + 1 at radius level i and height level k; 'z' from height level
 * k to k + 1 at radius level i and angle level j. */
using Segment = std::tuple<char, int, int, int>;

/*! One side of a patch: a segment, and whether it is run forward. */
struct Side {
  Segment segment;
  bool forward;
};

}  // namespace

std::optional<Body> fromModel(const SolidTree& tree, std::string& why)
{
  std::vector<std::shared_ptr<Surface>> bounds;
  if (!collectBounds(tree, bounds, why)) return std::nullopt;

  // The frame: the cylinders' one axis.
  const CylinderSurface *first = nullptr;
  for (const auto& b : bounds) {
    if (const auto *c = dynamic_cast<const CylinderSurface *>(b.get())) {
      if (first == nullptr) first = c;
      else if (!recognition::sameLine(first->refpt, first->normdir, c->refpt, c->normdir)) {
        why = "two declared cylinders on different axes";
        return std::nullopt;
      }
    }
  }
  if (first == nullptr) {
    why = "no declared cylinder to take an axis from";
    return std::nullopt;
  }
  const Vector3d origin = first->refpt;
  const Vector3d axis = first->normdir.normalized();

  // Each bound as a level: a cylinder's radius, a plane's height across the
  // axis or its angle through it - a plane through the axis is at two angles,
  // half a turn apart.
  std::vector<Level> radii{{0.0, nullptr}}, heights, angles;
  std::vector<std::pair<Vector3d, std::shared_ptr<Surface>>> through;  // direction in the plane, record
  for (const auto& b : bounds) {
    if (const auto *c = dynamic_cast<const CylinderSurface *>(b.get())) {
      radii.push_back({c->r, b});
    } else if (const auto *p = dynamic_cast<const PlaneSurface *>(b.get())) {
      const Vector3d n = p->normdir.normalized();
      if (recognition::parallel(n, axis)) {
        heights.push_back({axis.dot(p->refpt - origin), b});
      } else if (recognition::perpendicular(n, axis) && recognition::onPlane(p->refpt, n, origin)) {
        through.emplace_back(axis.cross(n).normalized(), b);
      } else {
        why = "a declared plane neither across the axis nor through it";
        return std::nullopt;
      }
    } else {
      why = std::string("a declared ") + "surface that is not a cylinder or a plane";
      return std::nullopt;
    }
  }
  // Angles measured from the first plane through the axis, so that it is at
  // zero exactly; any perpendicular where there is none.
  const Vector3d ref = through.empty() ? Vector3d(axis.unitOrthogonal()) : through.front().first;
  const Vector3d side = axis.cross(ref);
  const double turn = 2 * M_PI;
  const double angle_err = forwardError(16, turn);
  auto addAngle = [&](double theta, const std::shared_ptr<Surface>& record) {
    theta = std::fmod(std::fmod(theta, turn) + turn, turn);
    for (Level& l : angles) {
      const double d = std::fabs(l.at - theta);
      if (std::min(d, turn - d) <= angle_err) {
        if (l.record == nullptr) l.record = record;
        return;
      }
    }
    angles.push_back({theta, record});
  };
  for (const auto& [d, record] : through) {
    const double theta = std::atan2(d.dot(side), d.dot(ref));
    addAngle(theta, record);
    addAngle(theta + M_PI, record);
  }
  for (int q = 0; q < 4; q++) addAngle(q * M_PI / 2, nullptr);
  auto byLevel = [](const Level& x, const Level& y) { return x.at < y.at; };
  std::sort(radii.begin(), radii.end(), byLevel);
  std::sort(heights.begin(), heights.end(), byLevel);
  std::sort(angles.begin(), angles.end(), byLevel);
  // Two records of one plane are one record already (collectBounds), so one
  // level each.
  const std::vector<Level>& zs = heights;
  if (zs.size() < 2) {
    why = "fewer than two planes across the axis";
    return std::nullopt;
  }
  const int nR = int(radii.size()), nT = int(angles.size()), nZ = int(zs.size());

  // Points of the frame, and each box's centre.
  auto at = [&](double r, double theta, double z) {
    return Vector3d(origin + axis * z + (ref * std::cos(theta) + side * std::sin(theta)) * r);
  };
  auto angleOf = [&](int j) { return j < nT ? angles[j].at : angles[j - nT].at + turn; };
  // Box (i, j, k): radius interval i from level i to i + 1 (the last to
  // infinity), angle interval j from level j to j + 1 round the turn, height
  // interval k from level k - 1 to k (the first from minus infinity, the last
  // to infinity).
  const int nRi = nR, nZi = nZ + 1;
  std::vector<char> in(std::size_t(nRi) * nT * nZi, 0);
  auto box = [&](int i, int j, int k) -> char& {
    return in[(std::size_t(i) * nT + std::size_t((j % nT + nT) % nT)) * nZi + std::size_t(k)];
  };
  const double reach = std::max(1.0, radii.back().at);
  for (int i = 0; i < nRi; i++) {
    const double r = i + 1 < nR ? (radii[i].at + radii[i + 1].at) / 2 : radii.back().at + reach;
    for (int j = 0; j < nT; j++) {
      const double theta = (angleOf(j) + angleOf(j + 1)) / 2;
      for (int k = 0; k < nZi; k++) {
        const double z = k == 0    ? zs.front().at - reach
                         : k == nZ ? zs.back().at + reach
                                   : (zs[k - 1].at + zs[k].at) / 2;
        const recognition::Classification c = recognition::classify(tree, at(r, theta, z));
        if (c.where != Where::In && c.where != Where::Out) {
          why = "a box of the grid the model does not decide";
          return std::nullopt;
        }
        const bool inside = c.where == Where::In;
        if (inside && (i == 0 || i + 1 == nRi || k == 0 || k == nZ)) {
          why = i == 0 ? "material on the axis" : "material beyond the outermost levels";
          return std::nullopt;
        }
        box(i, j, k) = inside ? 1 : 0;
      }
    }
  }

  // The patches: every box face between an In box and an Out one, with its
  // sides counter clockwise about its outward normal.
  struct Patch {
    char kind;   // 'c' cylinder at radius level, 'z' plane at height level, 't' plane at angle level
    int level;   // which level of its kind
    bool sense;  // outward along the surface's own normal: radially out, +axis, +theta
    std::array<Side, 4> sides;
  };
  std::vector<Patch> patches;
  auto patch = [&](char kind, int level, bool sense, std::array<Side, 4> sides) -> bool {
    const std::shared_ptr<Surface>& record = kind == 'c'   ? radii[level].record
                                             : kind == 'z' ? zs[level].record
                                                           : angles[level].record;
    if (record == nullptr) {
      why = "a face at an angle no declared plane is at";
      return false;
    }
    if (!sense) {
      std::reverse(sides.begin(), sides.end());
      for (Side& s : sides) s.forward = !s.forward;
    }
    patches.push_back({kind, level, sense, sides});
    return true;
  };
  for (int i = 0; i < nRi; i++) {
    for (int j = 0; j < nT; j++) {
      for (int k = 0; k < nZi; k++) {
        const bool here = box(i, j, k) != 0;
        // On the cylinder at radius level i + 1, between this box and the next out.
        if (i + 1 < nRi && here != (box(i + 1, j, k) != 0)) {
          const int ri = i + 1;
          if (!patch('c', ri, here,
                     {Side{{'t', ri, j, k - 1}, true}, Side{{'z', ri, (j + 1) % nT, k - 1}, true},
                      Side{{'t', ri, j, k}, false}, Side{{'z', ri, j, k - 1}, false}}))
            return std::nullopt;
        }
        // On the plane at height level k, between this box and the next up.
        if (k + 1 < nZi && here != (box(i, j, k + 1) != 0)) {
          if (!patch('z', k, here,
                     {Side{{'r', i, j, k}, true}, Side{{'t', i + 1, j, k}, true},
                      Side{{'r', i, (j + 1) % nT, k}, false}, Side{{'t', i, j, k}, false}}))
            return std::nullopt;
        }
        // On the plane at angle level j + 1, between this box and the next round.
        if (here != (box(i, j + 1, k) != 0)) {
          const int tj = (j + 1) % nT;
          if (!patch('t', tj, here,
                     {Side{{'z', i, tj, k - 1}, true}, Side{{'r', i, tj, k}, true},
                      Side{{'z', i + 1, tj, k - 1}, false}, Side{{'r', i, tj, k - 1}, false}}))
            return std::nullopt;
        }
      }
    }
  }

  // Faces: the patches of one record and one sense, joined across a side two
  // of them share in opposite directions. What is left of each face's sides
  // once those cancel is its boundary.
  std::vector<int> parent(patches.size());
  std::iota(parent.begin(), parent.end(), 0);
  std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
  std::map<Segment, std::vector<std::pair<int, bool>>> users;
  for (int p = 0; p < int(patches.size()); p++) {
    for (const Side& s : patches[p].sides) users[s.segment].emplace_back(p, s.forward);
  }
  for (const auto& [segment, uses] : users) {
    for (std::size_t a = 0; a < uses.size(); a++) {
      for (std::size_t b = a + 1; b < uses.size(); b++) {
        const Patch& pa = patches[uses[a].first];
        const Patch& pb = patches[uses[b].first];
        if (pa.kind == pb.kind && pa.level == pb.level && pa.sense == pb.sense &&
            uses[a].second != uses[b].second)
          parent[find(uses[a].first)] = find(uses[b].first);
      }
    }
  }
  std::map<int, std::vector<int>> members;
  for (int p = 0; p < int(patches.size()); p++) members[find(p)].push_back(p);

  Body body;
  std::map<Point3, std::size_t> vertex_of;
  std::map<Segment, std::size_t> edge_of;
  auto pointOf = [&](const Point3& v) {
    const auto [ri, tj, zk] = v;
    return at(radii[ri].at, angles[tj].at, zs[zk].at);
  };
  auto vertex = [&](const Point3& v) {
    const auto it = vertex_of.find(v);
    if (it != vertex_of.end()) return it->second;
    body.vertices.push_back(Vertex{pointOf(v), EvidenceClass::D1, -1});
    return vertex_of[v] = body.vertices.size() - 1;
  };
  auto ends = [&](const Segment& s) {
    const auto [kind, a, b, c] = s;
    if (kind == 'r') return std::pair(Point3{a, b, c}, Point3{a + 1, b, c});
    if (kind == 't') return std::pair(Point3{a, b, c}, Point3{a, (b + 1) % nT, c});
    return std::pair(Point3{a, b, c}, Point3{a, b, c + 1});
  };
  auto edge = [&](const Segment& s) {
    const auto it = edge_of.find(s);
    if (it != edge_of.end()) return it->second;
    const auto [from, to] = ends(s);
    Edge e;
    e.start = vertex(from);
    e.end = vertex(to);
    const auto [kind, a, b, c] = s;
    const Vector3d start = body.vertices[e.start].point;
    if (kind == 'r') {
      const double theta = angles[b].at;
      e.curve.form = brep::Line{start, Vector3d(ref * std::cos(theta) + side * std::sin(theta))};
    } else if (kind == 'z') {
      e.curve.form = brep::Line{start, axis};
    } else {
      Conic circle;
      circle.kind = Conic::Kind::Circle;
      circle.frame = Frame{Vector3d(origin + axis * zs[c].at), axis, ref};
      circle.a = circle.b = radii[a].at;
      e.curve.form = circle;
    }
    e.evidence = EvidenceClass::D1;
    body.edges.push_back(e);
    return edge_of[s] = body.edges.size() - 1;
  };

  for (const auto& [root, group] : members) {
    const Patch& head = patches[group.front()];
    // The boundary: sides the group's own patches do not cancel.
    std::map<Segment, int> net;
    for (const int p : group) {
      for (const Side& s : patches[p].sides) net[s.segment] += s.forward ? 1 : -1;
    }
    std::vector<Side> boundary;
    for (const auto& [segment, count] : net) {
      if (count == 1 || count == -1) boundary.push_back(Side{segment, count == 1});
      else if (count != 0) {
        why = "a face's own patches meet twice along one segment";
        return std::nullopt;
      }
    }
    // Chained into loops from vertex to vertex; a corner the face leaves twice
    // is a pinch, which this does not walk.
    std::multimap<Point3, std::size_t> leaving;
    for (std::size_t s = 0; s < boundary.size(); s++) {
      const auto [from, to] = ends(boundary[s].segment);
      leaving.emplace(boundary[s].forward ? from : to, s);
    }
    for (const auto& [corner, s] : leaving) {
      if (leaving.count(corner) > 1) {
        why = "a face pinched at a corner";
        return std::nullopt;
      }
    }
    std::vector<char> used(boundary.size(), 0);
    Face face;
    for (std::size_t s0 = 0; s0 < boundary.size(); s0++) {
      if (used[s0]) continue;
      Loop loop;
      std::size_t s = s0;
      while (!used[s]) {
        used[s] = 1;
        loop.coedges.push_back(Coedge{edge(boundary[s].segment), boundary[s].forward, std::nullopt});
        const auto [from, to] = ends(boundary[s].segment);
        const auto next = leaving.find(boundary[s].forward ? to : from);
        if (next == leaving.end()) {
          why = "a face's boundary that does not close";
          return std::nullopt;
        }
        s = next->second;
      }
      face.loops.push_back(std::move(loop));
    }
    // Which loop is outer: on a plane the one enclosing the most; on a
    // cylinder none where a loop goes all the way round, the most otherwise.
    auto loopArea = [&](const Loop& loop, bool& round) {
      double area = 0, winding = 0;
      for (const Coedge& ce : loop.coedges) {
        const Edge& e = body.edges[ce.edge];
        const Vector3d p0 = body.vertices[ce.forward ? e.start : e.end].point;
        const Vector3d p1 = body.vertices[ce.forward ? e.end : e.start].point;
        Vector2d u0, u1;
        if (head.kind == 'c') {
          // In (angle, height), the angle run on along the loop rather than
          // read afresh at each corner, where it jumps by a turn.
          auto theta = [&](const Vector3d& p) {
            return std::atan2((p - origin).dot(side), (p - origin).dot(ref));
          };
          double d = theta(p1) - theta(p0);
          if (std::holds_alternative<Conic>(e.curve.form)) {
            // An arc runs less than a turn, in its own direction.
            while (ce.forward ? d <= 0 : d >= 0) d += ce.forward ? turn : -turn;
          } else {
            d = 0;
          }
          u0 = Vector2d(winding, axis.dot(p0 - origin));
          winding += d;
          u1 = Vector2d(winding, axis.dot(p1 - origin));
        } else {
          const Vector3d n = head.kind == 'z'
                               ? axis
                               : Vector3d(axis.cross(ref * std::cos(angles[head.level].at) +
                                                     side * std::sin(angles[head.level].at)));
          const Vector3d e1 = n.unitOrthogonal(), e2 = n.cross(e1);
          u0 = Vector2d(e1.dot(p0), e2.dot(p0));
          u1 = Vector2d(e1.dot(p1), e2.dot(p1));
        }
        area += u0[0] * u1[1] - u1[0] * u0[1];
      }
      round = std::fabs(winding) > M_PI;
      return std::fabs(area);
    };
    std::size_t outer = 0;
    double most = -1;
    bool any_round = false;
    for (std::size_t l = 0; l < face.loops.size(); l++) {
      bool round = false;
      const double a = loopArea(face.loops[l], round);
      any_round = any_round || round;
      if (a > most) {
        most = a;
        outer = l;
      }
    }
    for (std::size_t l = 0; l < face.loops.size(); l++) face.loops[l].outer = !any_round && l == outer;

    const std::shared_ptr<Surface>& record = head.kind == 'c'   ? radii[head.level].record
                                             : head.kind == 'z' ? zs[head.level].record
                                                                : angles[head.level].record;
    face.surface = record;
    face.carrier = record;
    face.same_sense = head.sense;
    if (head.kind == 'c') {
      face.form = Cylinder{Frame{origin, axis, ref}, radii[head.level].at};
    } else if (head.kind == 'z') {
      face.form = Plane{Frame{Vector3d(origin + axis * zs[head.level].at), axis, ref}};
    } else {
      const double theta = angles[head.level].at;
      const Vector3d d = ref * std::cos(theta) + side * std::sin(theta);
      face.form = Plane{Frame{origin, Vector3d(axis.cross(d)), d}};
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
