// The B-rep read off a STEP file's entities - see BrepMirror.h. Each graph is
// built by hand as the writer builds one, and every expectation is read off
// the solid it describes: a unit cube is 8 corners, 12 edges and 6 faces.
#include "io/step/BrepMirror.h"

#include <algorithm>
#include <catch2/catch_all.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "geometry/Surface.h"

using K = StepKernel;

namespace {

/*! The entities of one hand-built file, freed with it. */
struct Graph {
  std::vector<K::Entity *> ents;
  std::vector<K::Face *> faces;
  std::map<const K::Face *, step::FaceSource> sources;
  ~Graph()
  {
    for (auto *e : ents) delete e;
  }
  K::Axis2Placement *axis(const Vector3d& at, const Vector3d& z, const Vector3d& x)
  {
    return new K::Axis2Placement(ents, new K::Direction(ents, z), new K::Direction(ents, x),
                                 new K::Point(ents, at));
  }
};

/*! The unit cube as the planar writer writes it: one VERTEX_POINT per corner,
 * one LINE edge per pair of corners, each face's bound counter clockwise from
 * outside - or, for `reversed_face`, listed the other way with the bound's
 * orientation turned, which says the same loop. */
void cube(Graph& g, int reversed_face = -1)
{
  std::vector<K::Vertex *> v;
  std::vector<Vector3d> at;
  for (int i = 0; i < 8; i++) {
    at.emplace_back(double(i & 1), double((i >> 1) & 1), double((i >> 2) & 1));
    v.push_back(new K::Vertex(g.ents, new K::Point(g.ents, at.back())));
  }
  std::map<std::pair<int, int>, K::EdgeCurve *> edge;
  const std::vector<std::vector<int>> cycles = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4},
                                                {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
  for (std::size_t f = 0; f < cycles.size(); f++) {
    const auto& c = cycles[f];
    std::vector<K::OrientedEdge *> oriented;
    for (std::size_t i = 0; i < c.size(); i++) {
      const int a = c[i], b = c[(i + 1) % c.size()];
      const auto key = std::make_pair(std::min(a, b), std::max(a, b));
      auto it = edge.find(key);
      if (it == edge.end()) {
        auto *line = new K::Line(
          g.ents, new K::Point(g.ents, at[key.first]),
          new K::Vector(g.ents, new K::Direction(g.ents, at[key.second] - at[key.first]), 1.0));
        it = edge.emplace(key, new K::EdgeCurve(g.ents, v[key.first], v[key.second], line, true)).first;
      }
      oriented.push_back(new K::OrientedEdge(g.ents, it->second, a == key.first));
    }
    bool bound_dir = true;
    if (int(f) == reversed_face) {
      std::reverse(oriented.begin(), oriented.end());
      for (auto *o : oriented) o->dir = !o->dir;
      bound_dir = false;
    }
    const Vector3d n = (at[c[1]] - at[c[0]]).cross(at[c[2]] - at[c[0]]).normalized();
    std::vector<K::FaceBound *> bounds{
      new K::FaceBound(g.ents, new K::EdgeLoop(g.ents, oriented), bound_dir, true)};
    auto *face = new K::Face(g.ents, bounds,
                             new K::Plane(g.ents, g.axis(at[c[0]], n, (at[c[1]] - at[c[0]]))), true);
    g.faces.push_back(face);
    g.sources[face] = step::FaceSource{std::make_shared<PlaneSurface>(at[c[0]], n),
                                       {f},
                                       recognition::EvidenceClass::D0,
                                       false,
                                       recognition::EvidenceClass::D0};
  }
}

std::vector<std::vector<std::size_t>> oneShell(std::size_t n)
{
  std::vector<std::size_t> all;
  for (std::size_t i = 0; i < n; i++) all.push_back(i);
  return {all};
}

bool says(const std::vector<std::string>& defects, const std::string& part)
{
  return std::any_of(defects.begin(), defects.end(),
                     [&](const std::string& d) { return d.find(part) != std::string::npos; });
}

/*! The defects the mirror shows, less the classes vertices and edges do not
 * state yet - the gap step 2 of open item 39 is closing, counted elsewhere. */
std::vector<std::string> structural(const brep::Body& body)
{
  std::vector<std::string> out;
  for (const auto& d : brep::check(body)) {
    if (d.find(" states no evidence class") == std::string::npos) out.push_back(d);
  }
  return out;
}

}  // namespace

TEST_CASE("a cube's entities mirror as a closed shell of 8 corners, 12 edges and 6 faces",
          "[brep-mirror]")
{
  Graph g;
  cube(g);
  const brep::Body body = step::mirrorBrep(g.faces, g.sources, oneShell(g.faces.size()));
  CHECK(body.vertices.size() == 8);
  CHECK(body.edges.size() == 12);
  CHECK(body.faces.size() == 6);
  CHECK(structural(body).empty());
  CHECK(std::holds_alternative<brep::Plane>(body.faces[0].form));
  CHECK(std::holds_alternative<brep::Line>(body.edges[0].curve.form));
  CHECK(body.faces[3].facets == std::vector<std::size_t>{3});
  CHECK(body.faces[3].evidence == recognition::EvidenceClass::D0);
}

TEST_CASE("a bound used against its loop reads as the same loop", "[brep-mirror]")
{
  Graph g;
  cube(g, 2);
  CHECK(structural(step::mirrorBrep(g.faces, g.sources, oneShell(g.faces.size()))).empty());
}

TEST_CASE("a face the writer made and left out of the file opens the shell", "[brep-mirror]")
{
  // A fan triangle dropped in silence (open item 39, R8) is exactly this: its
  // neighbours keep the edges they share with it, used once.
  Graph g;
  cube(g);
  auto shells = oneShell(g.faces.size());
  shells[0].pop_back();
  std::vector<K::Face *> written(g.faces.begin(), g.faces.end() - 1);
  const auto defects = structural(step::mirrorBrep(written, g.sources, shells));
  CHECK(says(defects, "not once each way"));
}

TEST_CASE("a face nobody registered stands on no record and states no class", "[brep-mirror]")
{
  Graph g;
  cube(g);
  g.sources.erase(g.faces[4]);
  const auto defects = brep::check(step::mirrorBrep(g.faces, g.sources, oneShell(g.faces.size())));
  CHECK(says(defects, "face 4 stands on no surface"));
  CHECK(says(defects, "face 4 states no evidence class"));
}

TEST_CASE("a spline written without knots is one Bezier span, and says so", "[brep-mirror]")
{
  Graph g;
  cube(g);
  // Replace one edge's line by a cubic through its ends - the writer's
  // BSplineCurve with no knots set, which means one span over (0, 1).
  K::EdgeCurve *e = g.faces[0]->faceBounds[0]->edgeLoop->faces[0]->edge;
  const Vector3d a = e->vert1->point->pt, b = e->vert2->point->pt;
  std::vector<K::Point *> pts;
  for (int i = 0; i < 4; i++) pts.push_back(new K::Point(g.ents, a + (b - a) * (i / 3.0)));
  e->round = new K::BSplineCurve(g.ents, "", pts);
  const brep::Body body = step::mirrorBrep(g.faces, g.sources, oneShell(g.faces.size()));
  const auto *spline = std::get_if<brep::BSpline>(&body.edges[0].curve.form);
  REQUIRE(spline != nullptr);
  CHECK(spline->degree == 3);
  CHECK(spline->knots == std::vector<double>{0, 1});
  CHECK(spline->mults == std::vector<int>{4, 4});
  CHECK(structural(body).empty());
}
