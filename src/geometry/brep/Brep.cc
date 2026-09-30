/*! The neutral B-rep's topology check - see Brep.h. */
#include "geometry/brep/Brep.h"

#include <map>
#include <utility>

namespace brep {

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
      // A clamped spline of n control points and degree p has n + p + 1 knots,
      // counted with their multiplicities.
      int counted = 0;
      for (const int m : spline->mults) counted += m;
      const int wanted = int(spline->points.size()) + spline->degree + 1;
      if (spline->knots.size() != spline->mults.size() || counted != wanted)
        say(name("edge", e) + "'s B-spline has " + std::to_string(spline->points.size()) +
            " control points of degree " + std::to_string(spline->degree) + " and " +
            std::to_string(counted) + " knots, not " + std::to_string(wanted));
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
