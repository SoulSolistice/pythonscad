/*! The crossing edges as curves, one per run - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "geometry/AnalyticFeatures.h"
#include "io/step/BoundaryCycles.h"
#include "io/step/CrossingArc.h"
#include "io/step/CrossingRuns.h"

using namespace step;

void StepKernel::BodyBuild::decideCrossingRuns()
{
  crossing_pieces.clear();
  piece_of_edge.clear();
  crossing_mesh_edges = 0;
  if (crossing_edges.empty()) return;

  // Which crossing region each facet is written in. A crossing is between two
  // of these, so a vertex whose facets all lie in the two regions of its run
  // has no other face's boundary through it.
  std::map<std::size_t, const AnalyticFeatures::Patch *> region_of;
  for (const auto& patch : quadric_faces) {
    for (const std::size_t f : patch.facets) region_of[f] = &patch;
  }
  for (const auto& patch : grid_faces) {
    for (const std::size_t f : patch.facets) region_of[f] = &patch;
  }
  // The regions round each vertex; null for a facet in none of them - a planar
  // face, a band, a Bezier patch - whose boundary then meets the vertex.
  std::map<int, std::set<const AnalyticFeatures::Patch *>> round;
  for (std::size_t i = 0; i < loops.size(); i++) {
    if (!loop_valid[i]) continue;
    const auto it = region_of.find(i);
    const AnalyticFeatures::Patch *region = it == region_of.end() ? nullptr : it->second;
    for (const int v : loops[i]) round[v].insert(region);
  }
  // And the vertices a face's own seam starts from: the seam is an edge there,
  // so the rim's curve has to end there. The same cycles, with the same seams,
  // the quadric emitter writes.
  std::set<int> anchors;
  for (const auto& patch : quadric_faces) {
    if (dynamic_cast<const AxialSurface *>(patch.surface.get()) == nullptr) continue;
    for (const auto& entry : boundaryCycles(patch, vertices, loops, loop_valid, taken,
                                            section_planes[patch.surface.get()], crossing_keys, true)) {
      if (entry.second.seam && !entry.second.verts.empty()) anchors.insert(entry.second.verts[0]);
    }
  }

  std::map<int, std::vector<std::pair<int, int>>> at;
  for (const auto& entry : crossing_edges) {
    at[entry.first.first].push_back(entry.first);
    at[entry.first.second].push_back(entry.first);
  }
  auto surfaces_of = [&](const std::pair<int, int>& e) {
    const auto& ab = crossing_edges.at(e);
    return std::minmax(ab.first, ab.second);
  };
  // A vertex a run passes through, rather than ends at.
  auto interior = [&](int v) {
    if (anchors.count(v) != 0) return false;
    const auto& edges = at[v];
    if (edges.size() != 2 || surfaces_of(edges[0]) != surfaces_of(edges[1])) return false;
    const auto& regions = round[v];
    return regions.size() == 2 && regions.count(nullptr) == 0;
  };
  auto other_end = [](const std::pair<int, int>& e, int v) { return e.first == v ? e.second : e.first; };
  auto other_edge = [&](int v, const std::pair<int, int>& e) {
    const auto& edges = at[v];
    return edges[0] == e ? edges[1] : edges[0];
  };

  // Where a sweep is only C0, which it states: the knots of its spline form of
  // multiplicity at least the degree, in u and in v. A curve crossing one there
  // turns a corner on it.
  std::map<const GridSurface *, std::pair<std::vector<double>, std::vector<double>>> creases;
  auto creases_of = [&](const GridSurface *grid) -> const auto& {
    auto it = creases.find(grid);
    if (it != creases.end()) return it->second;
    auto& lines = creases[grid];
    int du = 0, dv = 0, nr = 0, nc = 0;
    std::vector<Vector3d> net;
    std::vector<double> ku, kv;
    std::vector<int> mu, mv;
    if (grid->splineForm(du, dv, nr, nc, net, ku, mu, kv, mv)) {
      for (std::size_t k = 1; k + 1 < ku.size(); k++) {
        if (mu[k] >= du) lines.first.push_back(ku[k]);
      }
      for (std::size_t k = 1; k + 1 < kv.size(); k++) {
        if (mv[k] >= dv) lines.second.push_back(kv[k]);
      }
    }
    return lines;
  };
  // Which flank of each sweep the edge from `u` to `w` runs over: the interval
  // between creases its middle lies in. An edge of a crossing never straddles
  // one - the arc fitted to it would have to turn a corner, which no polynomial
  // does, and it is written as a chord instead - so this is the edge's own.
  auto flank_of = [&](int u, int w, const Surface *a, const Surface *b) {
    std::vector<std::size_t> key;
    const Vector3d mid = (vertices[u] + vertices[w]) * 0.5;
    for (const Surface *s : {a, b}) {
      const auto *grid = dynamic_cast<const GridSurface *>(s);
      if (grid == nullptr) continue;
      double pu = 0, pv = 0;
      if (!grid->project(mid, pu, pv)) {
        key.push_back(std::size_t(-1));
        continue;
      }
      const auto& lines = creases_of(grid);
      key.push_back(
        std::size_t(std::upper_bound(lines.first.begin(), lines.first.end(), pu) - lines.first.begin()));
      key.push_back(std::size_t(std::upper_bound(lines.second.begin(), lines.second.end(), pv) -
                                lines.second.begin()));
    }
    return key;
  };

  // One piece: a curve through `run`, or two pieces where the fit is refused.
  std::function<void(const std::vector<int>&, const Surface *, const Surface *)> make_piece;
  make_piece = [&](const std::vector<int>& run, const Surface *a, const Surface *b) {
    CrossingPiece piece;
    piece.a = a;
    piece.b = b;
    if (run.size() == 2) {
      // One edge: the Bezier fitted between its two ends, as it always was.
      const std::pair<int, int> key{std::min(run[0], run[1]), std::max(run[0], run[1])};
      piece.verts = {key.first, key.second};
      std::vector<Vector3d> ctrl;
      if (intersectionArc(a, b, vertices[key.first], vertices[key.second], 1e-7, ctrl)) {
        std::vector<Point *> cp;
        cp.reserve(ctrl.size());
        for (const auto& c : ctrl) cp.push_back(new Point(entities, c));
        piece.edge = new EdgeCurve(entities, get_vertex(key.first), get_vertex(key.second),
                                   new BSplineCurve(entities, "", cp), true);
      }
    } else {
      // Fitted flank by flank, and joined at the corners between them: one curve
      // with its corners where the sweep has its creases.
      std::vector<CurveFit> parts;
      std::vector<Vector3d> points{vertices[run[0]]};
      bool fitted = true;
      for (std::size_t k = 1; k < run.size() && fitted; k++) {
        points.push_back(vertices[run[k]]);
        const bool corner =
          k + 1 < run.size() && flank_of(run[k - 1], run[k], a, b) != flank_of(run[k], run[k + 1], a, b);
        if (k + 1 < run.size() && !corner) continue;
        parts.emplace_back();
        fitted = fitCrossingRun(a, b, points, 1e-7, parts.back());
        points = {vertices[run[k]]};
      }
      if (!fitted || !joinC0(parts, piece.fit)) {
        const std::size_t mid = run.size() / 2;
        make_piece(std::vector<int>(run.begin(), run.begin() + long(mid) + 1), a, b);
        make_piece(std::vector<int>(run.begin() + long(mid), run.end()), a, b);
        return;
      }
      piece.verts = run;
      std::vector<Point *> cp;
      cp.reserve(piece.fit.ctrl.size());
      for (const auto& c : piece.fit.ctrl) cp.push_back(new Point(entities, c));
      auto curve = new BSplineCurve(entities, "", cp);
      curve->setKnots(CurveFit::degree, piece.fit.knots, piece.fit.mults);
      piece.edge = new EdgeCurve(entities, get_vertex(run.front()), get_vertex(run.back()), curve, true);
    }
    const std::size_t index = crossing_pieces.size();
    for (std::size_t k = 0; k + 1 < piece.verts.size(); k++) {
      const int u = piece.verts[k], w = piece.verts[k + 1];
      piece_of_edge[{std::min(u, w), std::max(u, w)}] = index;
    }
    if (piece.edge != nullptr) {
      crossing_written++;
      crossing_mesh_edges += piece.verts.size() - 1;
    }
    crossing_pieces.push_back(std::move(piece));
  };

  std::set<std::pair<int, int>> done;
  for (const auto& entry : crossing_edges) {
    if (done.count(entry.first) != 0) continue;
    // Back along the run to where it starts; round to where it began, if it
    // closes on itself, and then from its lowest vertex.
    int v = entry.first.first;
    std::pair<int, int> edge = entry.first;
    bool closed = false;
    int lowest = std::min(entry.first.first, entry.first.second);
    while (interior(v)) {
      edge = other_edge(v, edge);
      if (edge == entry.first) {
        closed = true;
        break;
      }
      v = other_end(edge, v);
      lowest = std::min(lowest, v);
    }
    // Otherwise v is where it starts and `edge` its first edge, the last one
    // walked back along.
    if (closed) {
      v = lowest;
      const auto& edges = at[v];
      edge = std::min(edges[0], edges[1]);
    }
    // Forward from there to the run's end.
    std::vector<int> run{v};
    const int start = v;
    for (;;) {
      done.insert(edge);
      v = other_end(edge, v);
      run.push_back(v);
      if (v == start || !interior(v)) break;
      edge = other_edge(v, edge);
    }
    const auto& ab = crossing_edges.at(edge);
    make_piece(run, ab.first, ab.second);
  }
}

const StepKernel::BodyBuild::CrossingPiece *StepKernel::BodyBuild::pieceOf(int a, int b) const
{
  const auto it = piece_of_edge.find({std::min(a, b), std::max(a, b)});
  return it == piece_of_edge.end() ? nullptr : &crossing_pieces[it->second];
}

bool StepKernel::BodyBuild::walksForward(const CrossingPiece& piece, const std::vector<int>& cycle,
                                         std::size_t i, bool& ok) const
{
  const std::size_t n = cycle.size(), k = piece.verts.size() - 1;
  bool forward = true, backward = true;
  for (std::size_t c = 0; c <= k && (forward || backward); c++) {
    forward = forward && cycle[(i + c) % n] == piece.verts[c];
    backward = backward && cycle[(i + c) % n] == piece.verts[k - c];
  }
  ok = forward || backward;
  return forward;
}

std::size_t StepKernel::BodyBuild::walkStart(const std::vector<int>& cycle) const
{
  const std::size_t n = cycle.size();
  if (n < 2) return 0;
  const CrossingPiece *piece = pieceOf(cycle[0], cycle[1]);
  if (piece == nullptr || piece->verts.size() <= 2) return 0;
  // The way this cycle runs along the piece, and so which end it enters by.
  bool forward = false;
  for (std::size_t j = 0; j + 1 < piece->verts.size(); j++) {
    if (piece->verts[j] == cycle[0] && piece->verts[j + 1] == cycle[1]) forward = true;
  }
  const int entry = forward ? piece->verts.front() : piece->verts.back();
  for (std::size_t m = 0; m < n; m++) {
    if (cycle[m] == entry && pieceOf(cycle[m], cycle[(m + 1) % n]) == piece) return m;
  }
  return 0;
}
