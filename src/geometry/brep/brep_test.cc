// The neutral B-rep's topology contract - see Brep.h. Every body here is built
// by hand from the solid it describes, and every expectation is read off that
// solid: a cube has 8 corners, 12 edges and 6 faces; a cylinder closed by one
// seam has 2 vertices, 3 edges and 3 faces; both have V - E + F = 2.
#include "geometry/brep/Brep.h"

#include <algorithm>
#include <catch2/catch_all.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "geometry/Surface.h"

using brep::Body;
using brep::Coedge;
using brep::EvidenceClass;

namespace {

/*! A face of planar polygon `cycle`, outward normal by the right hand, whose
 * edges are shared through `edge_of` so that the two faces meeting at an edge
 * use one entity for it. */
void planarFace(Body& body, std::map<std::pair<std::size_t, std::size_t>, std::size_t>& edge_of,
                const std::vector<std::size_t>& cycle)
{
  brep::Loop loop;
  for (std::size_t i = 0; i < cycle.size(); i++) {
    const std::size_t a = cycle[i], b = cycle[(i + 1) % cycle.size()];
    const auto key = std::make_pair(std::min(a, b), std::max(a, b));
    auto it = edge_of.find(key);
    if (it == edge_of.end()) {
      brep::Edge edge;
      edge.start = key.first;
      edge.end = key.second;
      const Vector3d& p = body.vertices[edge.start].point;
      edge.curve.form = brep::Line{p, (body.vertices[edge.end].point - p).normalized()};
      edge.evidence = EvidenceClass::D0;
      body.edges.push_back(edge);
      it = edge_of.emplace(key, body.edges.size() - 1).first;
    }
    loop.coedges.push_back(Coedge{it->second, a == key.first, std::nullopt});
  }
  const Vector3d& p0 = body.vertices[cycle[0]].point;
  const Vector3d n =
    (body.vertices[cycle[1]].point - p0).cross(body.vertices[cycle[2]].point - p0).normalized();
  brep::Face face;
  face.surface = std::make_shared<PlaneSurface>(p0, n);
  face.loops.push_back(loop);
  face.evidence = EvidenceClass::D0;
  body.faces.push_back(face);
}

/*! The unit cube, [0,1]^3, each face listed counter clockwise seen from outside. */
Body cube()
{
  Body body;
  for (int i = 0; i < 8; i++) {
    body.vertices.push_back(
      {Vector3d(double(i & 1), double((i >> 1) & 1), double((i >> 2) & 1)), EvidenceClass::D0, i});
  }
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> edge_of;
  for (const auto& cycle : std::vector<std::vector<std::size_t>>{
         {0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}}) {
    planarFace(body, edge_of, cycle);
  }
  brep::Shell shell;
  for (std::size_t f = 0; f < body.faces.size(); f++) shell.faces.push_back(f);
  body.shells.push_back(shell);
  return body;
}

/*! A cylinder of radius 1 from z = 0 to 1, its wall closed on itself by one
 * seam along x = 1: two circles, the seam, the wall and two caps. */
Body cylinder()
{
  Body body;
  const Vector3d z = Vector3d::UnitZ();
  body.vertices.push_back({Vector3d(1, 0, 0), EvidenceClass::D0, -1});
  body.vertices.push_back({Vector3d(1, 0, 1), EvidenceClass::D0, -1});
  auto circle = [&](double h) {
    brep::Edge edge;
    edge.start = edge.end = h == 0 ? 0 : 1;
    edge.curve.form =
      brep::Conic{brep::Conic::Kind::Circle, {Vector3d(0, 0, h), z, Vector3d::UnitX()}, 1, 1};
    edge.evidence = EvidenceClass::D0;
    return edge;
  };
  body.edges.push_back(circle(0));  // 0, counter clockwise about z
  body.edges.push_back(circle(1));  // 1
  brep::Edge seam;
  seam.start = 0;
  seam.end = 1;
  seam.curve.form = brep::Line{Vector3d(1, 0, 0), z};
  seam.evidence = EvidenceClass::D0;
  body.edges.push_back(seam);  // 2

  brep::Face wall;
  wall.surface = std::make_shared<CylinderSurface>(Vector3d::Zero(), z, 1.0);
  wall.frame = brep::Frame{};
  wall.evidence = EvidenceClass::D0;
  // Round the bottom, up the seam, back round the top, down the seam.
  wall.loops.push_back({{Coedge{0, true, std::nullopt}, Coedge{2, true, std::nullopt},
                         Coedge{1, false, std::nullopt}, Coedge{2, false, std::nullopt}},
                        true});
  brep::Face bottom;
  bottom.surface = std::make_shared<PlaneSurface>(Vector3d::Zero(), Vector3d(-z));
  bottom.evidence = EvidenceClass::D0;
  bottom.loops.push_back({{Coedge{0, false, std::nullopt}}, true});
  brep::Face top;
  top.surface = std::make_shared<PlaneSurface>(Vector3d(0, 0, 1), z);
  top.evidence = EvidenceClass::D0;
  top.loops.push_back({{Coedge{1, true, std::nullopt}}, true});
  body.faces = {wall, bottom, top};
  body.shells.push_back({{0, 1, 2}, true});
  return body;
}

bool says(const std::vector<std::string>& defects, const std::string& part)
{
  return std::any_of(defects.begin(), defects.end(),
                     [&](const std::string& d) { return d.find(part) != std::string::npos; });
}

}  // namespace

TEST_CASE("a cube built by hand is a closed shell with 8 corners, 12 edges and 6 faces", "[brep]")
{
  const Body body = cube();
  CHECK(body.vertices.size() == 8);
  CHECK(body.edges.size() == 12);
  CHECK(body.faces.size() == 6);
  CHECK(int(body.vertices.size()) - int(body.edges.size()) + int(body.faces.size()) == 2);
  CHECK(brep::check(body).empty());
}

TEST_CASE("a cylinder closed by one seam uses the seam once each way from its wall", "[brep]")
{
  const Body body = cylinder();
  CHECK(int(body.vertices.size()) - int(body.edges.size()) + int(body.faces.size()) == 2);
  CHECK(brep::check(body).empty());
}

TEST_CASE("a coedge run the wrong way breaks its loop and its edge's pairing", "[brep]")
{
  Body body = cube();
  body.faces[0].loops[0].coedges[1].forward = !body.faces[0].loops[0].coedges[1].forward;
  const auto defects = brep::check(body);
  CHECK(says(defects, "face 0, loop 0 does not close"));
  CHECK(says(defects, "not once each way"));
}

TEST_CASE("a face left out of its shell opens it", "[brep]")
{
  Body body = cube();
  body.shells[0].faces.pop_back();
  const auto defects = brep::check(body);
  CHECK(says(defects, "face 5 is in no shell"));
  CHECK(says(defects, "1 times forward and 0 times backward"));
}

TEST_CASE("an open shell is not asked to pair its edges", "[brep]")
{
  Body body = cube();
  body.shells[0].faces.pop_back();
  body.faces.pop_back();
  body.shells[0].closed = false;
  CHECK(brep::check(body).empty());
}

TEST_CASE("every entity states what decided it", "[brep]")
{
  Body body = cylinder();
  body.vertices[1].evidence = EvidenceClass::Unstated;
  body.edges[2].evidence = EvidenceClass::Unstated;
  body.faces[0].evidence = EvidenceClass::Unstated;
  const auto defects = brep::check(body);
  CHECK(says(defects, "vertex 1 states no evidence class"));
  CHECK(says(defects, "edge 2 states no evidence class"));
  CHECK(says(defects, "face 0 states no evidence class"));
}

TEST_CASE("an index past the body's entities is named, not followed", "[brep]")
{
  Body body = cylinder();
  body.faces[1].loops[0].coedges[0].edge = 7;
  body.edges[2].end = 9;
  body.shells[0].faces.push_back(4);
  const auto defects = brep::check(body);
  CHECK(says(defects, "uses edge 7, which the body does not have"));
  CHECK(says(defects, "edge 2 names a vertex the body does not have"));
  CHECK(says(defects, "names face 4, which the body does not have"));
}

TEST_CASE("a face has at most one outer loop, and a band round its axis has none", "[brep]")
{
  Body body = cube();
  body.faces[2].loops.push_back(body.faces[2].loops[0]);
  CHECK(says(brep::check(body), "face 2 has 2 outer loops, not at most one"));

  // The cylinder's wall as ISO 10303 writes a periodic face between two rims:
  // no seam, a loop round each rim, and neither of them outer.
  Body band = cylinder();
  band.faces[0].loops = {{{Coedge{0, true, std::nullopt}}, false},
                         {{Coedge{1, false, std::nullopt}}, false}};
  band.edges.pop_back();  // the seam, no longer used
  CHECK(brep::check(band).empty());
}

TEST_CASE("an edge states its curve, and a spline's knots fit its points", "[brep]")
{
  Body body = cube();
  body.edges[3].curve.form = std::monostate{};
  // A cubic through four control points is one span: knots 0 and 1, each four
  // times - eight, as 4 + 3 + 1. Three times each is six, and wrong.
  brep::BSpline cubic;
  cubic.degree = 3;
  cubic.points = {Vector3d(0, 0, 0), Vector3d(1, 0, 0), Vector3d(2, 0, 0), Vector3d(3, 0, 0)};
  cubic.knots = {0, 1};
  cubic.mults = {4, 4};
  body.edges[4].curve.form = cubic;
  cubic.mults = {3, 3};
  body.edges[5].curve.form = cubic;
  cubic.mults = {4, 4};
  cubic.weights = {1, 1};
  body.edges[6].curve.form = cubic;
  const auto defects = brep::check(body);
  CHECK(says(defects, "edge 3 has no curve"));
  CHECK_FALSE(says(defects, "edge 4"));
  CHECK(says(defects, "edge 5's B-spline has 4 control points of degree 3 and 6 knots, not 8"));
  CHECK(says(defects, "edge 6's B-spline has 2 weights for 4 control points"));
}

TEST_CASE("a face stands on a surface, a loop has coedges, and a face is in one shell", "[brep]")
{
  Body body = cube();
  body.faces[1].surface = nullptr;
  body.faces[2].loops[0].coedges.clear();
  body.shells[0].faces.push_back(3);
  body.shells.push_back({{4}, false});
  const auto defects = brep::check(body);
  CHECK(says(defects, "face 1 stands on no surface"));
  CHECK(says(defects, "face 2, loop 0 is empty"));
  CHECK(says(defects, "face 3 is listed twice in shell 0"));
  CHECK(says(defects, "face 4 is in two shells"));
}
