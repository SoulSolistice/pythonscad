// The neutral B-rep's topology contract - see Brep.h. Every body here is built
// by hand from the solid it describes, and every expectation is read off that
// solid: a cube has 8 corners, 12 edges and 6 faces; a cylinder closed by one
// seam has 2 vertices, 3 edges and 3 faces; both have V - E + F = 2.
#include "geometry/brep/Brep.h"

#include <algorithm>
#include <cmath>
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
  face.form = brep::Plane{{p0, n, (body.vertices[cycle[1]].point - p0).normalized()}};
  face.loops.push_back(loop);
  face.evidence = EvidenceClass::D0;
  face.record_evidence = EvidenceClass::D0;
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
  wall.form = brep::Cylinder{brep::Frame{}, 1.0};
  wall.evidence = EvidenceClass::D0;
  wall.record_evidence = EvidenceClass::D0;
  // Round the bottom, up the seam, back round the top, down the seam.
  wall.loops.push_back({{Coedge{0, true, std::nullopt}, Coedge{2, true, std::nullopt},
                         Coedge{1, false, std::nullopt}, Coedge{2, false, std::nullopt}},
                        true});
  brep::Face bottom;
  bottom.surface = std::make_shared<PlaneSurface>(Vector3d::Zero(), Vector3d(-z));
  bottom.form = brep::Plane{{Vector3d::Zero(), Vector3d(-z), Vector3d::UnitX()}};
  bottom.evidence = EvidenceClass::D0;
  bottom.record_evidence = EvidenceClass::D0;
  bottom.loops.push_back({{Coedge{0, false, std::nullopt}}, true});
  brep::Face top;
  top.surface = std::make_shared<PlaneSurface>(Vector3d(0, 0, 1), z);
  top.form = brep::Plane{{Vector3d(0, 0, 1), z, Vector3d::UnitX()}};
  top.evidence = EvidenceClass::D0;
  top.record_evidence = EvidenceClass::D0;
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

TEST_CASE("an edge no face uses, and a vertex no edge ends at, are not part of the body", "[brep]")
{
  Body body = cube();
  body.vertices.push_back({Vector3d(2, 0, 0), EvidenceClass::D0, 8});
  body.vertices.push_back({Vector3d(3, 0, 0), EvidenceClass::D0, 9});
  brep::Edge stray;
  stray.start = 0;
  stray.end = 8;
  stray.curve.form = brep::Line{Vector3d(0, 0, 0), Vector3d(1, 0, 0)};
  stray.evidence = EvidenceClass::D0;
  body.edges.push_back(stray);
  const auto defects = brep::check(body);
  CHECK(says(defects, "edge 12 is used by no face"));
  // Vertex 8 ends only the stray edge, and vertex 9 ends nothing.
  CHECK(says(defects, "vertex 8 bounds no edge a face uses"));
  CHECK(says(defects, "vertex 9 bounds no edge a face uses"));
  CHECK(defects.size() == 3);
}

TEST_CASE("a line from a vertex to itself is named; a circle closed on one vertex is not", "[brep]")
{
  // A face whose corners the placement merged keeps the edge between them: a
  // line from corner 0 to corner 0, inserted into the bottom face's loop where
  // it still closes. The cylinder's circles start and end at one vertex each
  // and are whole curves.
  Body body = cube();
  brep::Edge none;
  none.start = none.end = 0;
  none.curve.form = brep::Line{body.vertices[0].point, Vector3d(1, 0, 0)};
  none.evidence = EvidenceClass::D0;
  body.edges.push_back(none);
  auto& coedges = body.faces[0].loops[0].coedges;
  coedges.insert(coedges.begin(), Coedge{body.edges.size() - 1, true, std::nullopt});
  CHECK(says(brep::check(body), "edge 12 is a line from a vertex to itself"));
  CHECK_FALSE(says(brep::check(cylinder()), "from a vertex to itself"));
}

TEST_CASE("a face is written on the record it stands for, or on a closed form stated derived from it",
          "[brep]")
{
  // The cylinder's faces, each carried by its own record: nothing to name.
  Body body = cylinder();
  for (auto& face : body.faces) face.carrier = face.surface;
  CHECK(brep::checkAttribution(body).empty());

  // A carrier with no statement at all is named.
  Body unstated = cylinder();
  CHECK(says(brep::checkAttribution(unstated), "face 0 states no carrier"));

  // The wall carried by another record of the same cylinder - one surface,
  // two records - is the record it stands for.
  body.faces[0].carrier = std::make_shared<CylinderSurface>(Vector3d(0, 0, 5), Vector3d::UnitZ(), 1.0);
  CHECK(brep::checkAttribution(body).empty());

  // The wall written on a cylinder of another radius than its record's - a
  // face standing for one surface written on another - is named; stated
  // derived from the record, it is the maker's closed form and is not.
  body.faces[0].carrier = std::make_shared<CylinderSurface>(Vector3d::Zero(), Vector3d::UnitZ(), 2.0);
  CHECK(says(brep::checkAttribution(body), "face 0 is written on a cylinder that is not a cylinder"));
  body.faces[0].carrier_derived = true;
  CHECK(brep::checkAttribution(body).empty());

  // A cylinder's form carried by a cone: the form does not write its carrier.
  body.faces[0].carrier_derived = false;
  body.faces[0].carrier = std::make_shared<ConeSurface>(Vector3d::Zero(), Vector3d::UnitZ(), 1.0, 1.0);
  CHECK(says(brep::checkAttribution(body), "face 0 is written as a cylinder on a cone"));
}

TEST_CASE("a body's shells are its connected components, in the order of their first face", "[brep]")
{
  // Two unit cubes two apart share nothing, so they are two shells of six faces
  // each. Their faces interleaved, the second cube's first: face 0 is the second
  // cube's, so its shell comes first and holds the even faces.
  const Body a = cube();
  Body b = cube();
  for (auto& v : b.vertices) v.point += Vector3d(2, 0, 0);
  Body body;
  body.vertices = a.vertices;
  body.vertices.insert(body.vertices.end(), b.vertices.begin(), b.vertices.end());
  body.edges = a.edges;
  for (brep::Edge e : b.edges) {
    e.start += a.vertices.size();
    e.end += a.vertices.size();
    body.edges.push_back(e);
  }
  for (std::size_t f = 0; f < 6; f++) {
    brep::Face from_b = b.faces[f];
    for (auto& loop : from_b.loops) {
      for (auto& c : loop.coedges) c.edge += a.edges.size();
    }
    body.faces.push_back(from_b);
    body.faces.push_back(a.faces[f]);
  }
  const std::vector<brep::Shell> shells = brep::connectedShells(body);
  REQUIRE(shells.size() == 2);
  CHECK(shells[0].faces == std::vector<std::size_t>{0, 2, 4, 6, 8, 10});
  CHECK(shells[1].faces == std::vector<std::size_t>{1, 3, 5, 7, 9, 11});
  CHECK(shells[0].closed);
  body.shells = shells;
  CHECK(brep::check(body).empty());

  // A cylinder is one shell, its caps joined to the wall by the circles alone
  // and the seam used by the wall only.
  const auto one = brep::connectedShells(cylinder());
  REQUIRE(one.size() == 1);
  CHECK(one[0].faces == std::vector<std::size_t>{0, 1, 2});
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

TEST_CASE("a spline surface's knots fit its net", "[brep]")
{
  Body body = cube();
  // Bilinear over a 2 by 2 net: knots 0 and 1, each twice in each direction.
  brep::SplineSurface net;
  net.degree_u = net.degree_v = 1;
  net.points = {{Vector3d(0, 0, 0), Vector3d(0, 1, 0)}, {Vector3d(1, 0, 0), Vector3d(1, 1, 0)}};
  net.knots_u = net.knots_v = {0, 1};
  net.mults_u = net.mults_v = {2, 2};
  body.faces[0].form = net;
  CHECK(brep::check(body).empty());
  net.mults_v = {1, 2};
  body.faces[0].form = net;
  CHECK(says(brep::check(body),
             "face 0's spline surface does not fit its knots: a 2 by 2 net of degree 1 by 1"));
  net.mults_v = {2, 2};
  net.points[1].pop_back();
  body.faces[0].form = net;
  CHECK(says(brep::check(body), "a 2 by 2 ragged net"));
}

TEST_CASE("a face stands on a surface, a loop has coedges, and a face is in one shell", "[brep]")
{
  Body body = cube();
  body.faces[1].surface = nullptr;
  body.faces[5].form = std::monostate{};
  body.faces[2].loops[0].coedges.clear();
  body.shells[0].faces.push_back(3);
  body.shells.push_back({{4}, false});
  const auto defects = brep::check(body);
  CHECK(says(defects, "face 1 stands on no surface"));
  CHECK(says(defects, "face 5 is carried by no form"));
  CHECK(says(defects, "face 2, loop 0 is empty"));
  CHECK(says(defects, "face 3 is listed twice in shell 0"));
  CHECK(says(defects, "face 4 is in two shells"));
}

// ---- certificates and classes (stateClasses) ----------------------------
//
// Every expectation below is read off the geometry by hand: a ruling of a
// cylinder lies on it and a chord across it does not; the section of the
// cylinder x^2 + y^2 = 1 by the plane through the origin tilted 45 degrees about
// x is the ellipse of semi-axes sqrt(2) and 1; a cube's corner is where three of
// its planes cross, and a cylinder's seam end is on only two surfaces.

TEST_CASE("a line is on a plane or a quadric exactly where its degree's worth of points are", "[brep]")
{
  const Vector3d z = Vector3d::UnitZ();
  const auto floor = std::make_shared<PlaneSurface>(Vector3d::Zero(), z);
  const auto wall = std::make_shared<CylinderSurface>(Vector3d::Zero(), z, 1.0);
  const brep::CurveForm line = brep::Line{};
  CHECK(brep::curveOn(line, Vector3d(0, 0, 0), Vector3d(3, 4, 0), floor.get()));
  CHECK_FALSE(brep::curveOn(line, Vector3d(0, 0, 0), Vector3d(3, 4, 1e-9), floor.get()));
  // A ruling: both ends and the middle on the cylinder.
  CHECK(brep::curveOn(line, Vector3d(1, 0, 0), Vector3d(1, 0, 5), wall.get()));
  // A chord: both ends on it, the middle 1 - 1/sqrt(2) inside.
  CHECK_FALSE(brep::curveOn(line, Vector3d(1, 0, 0), Vector3d(0, 1, 0), wall.get()));
}

TEST_CASE("a conic is on a quadric exactly where five of its points are", "[brep]")
{
  const Vector3d z = Vector3d::UnitZ();
  const auto wall = std::make_shared<CylinderSurface>(Vector3d::Zero(), z, 1.0);
  using Kind = brep::Conic::Kind;
  const brep::CurveForm rim =
    brep::Conic{Kind::Circle, {Vector3d(0, 0, 0.5), z, Vector3d::UnitX()}, 1, 1};
  CHECK(brep::curveOn(rim, Vector3d(1, 0, 0.5), Vector3d(1, 0, 0.5), wall.get()));
  const brep::CurveForm wider =
    brep::Conic{Kind::Circle, {Vector3d(0, 0, 0.5), z, Vector3d::UnitX()}, 1 + 1e-7, 1 + 1e-7};
  CHECK_FALSE(brep::curveOn(wider, Vector3d(1, 0, 0.5), Vector3d(1, 0, 0.5), wall.get()));
  // The 45 degree section: normal (0, -1, 1)/sqrt(2), major axis (0, 1, 1)/sqrt(2).
  const double h = std::sqrt(0.5);
  const brep::CurveForm cut = brep::Conic{
    Kind::Ellipse, {Vector3d::Zero(), Vector3d(0, -h, h), Vector3d(0, h, h)}, std::sqrt(2.0), 1};
  CHECK(brep::curveOn(cut, Vector3d(0, 1, 1), Vector3d(0, 1, 1), wall.get()));
  CHECK(brep::curveOn(cut, Vector3d(0, 1, 1), Vector3d(0, 1, 1),
                      std::make_shared<PlaneSurface>(Vector3d::Zero(), Vector3d(0, -h, h)).get()));
  // The same ellipse a hair too long is on the plane still, and off the cylinder.
  const brep::CurveForm longer = brep::Conic{
    Kind::Ellipse, {Vector3d::Zero(), Vector3d(0, -h, h), Vector3d(0, h, h)}, std::sqrt(2.0) + 1e-7, 1};
  CHECK_FALSE(brep::curveOn(longer, Vector3d(0, 1, 1), Vector3d(0, 1, 1), wall.get()));
}

TEST_CASE("a curve no algebra can certify is not certified", "[brep]")
{
  brep::BSpline spline;
  spline.degree = 1;
  spline.points = {Vector3d(0, 0, 0), Vector3d(1, 0, 0)};
  spline.knots = {0, 1};
  spline.mults = {2, 2};
  CHECK_FALSE(brep::curveOn(spline, Vector3d(0, 0, 0), Vector3d(1, 0, 0),
                            std::make_shared<PlaneSurface>(Vector3d::Zero(), Vector3d::UnitZ()).get()));
}

TEST_CASE("a cube's corners are where three declared planes cross, and its edges where two do", "[brep]")
{
  Body body = cube();
  const brep::ClassTally t = brep::stateClasses(body);
  CHECK(t.vertices[std::size_t(EvidenceClass::D1)] == 8);
  CHECK(t.edges[std::size_t(EvidenceClass::D1)] == 12);
  CHECK(t.vertices_off == 0);
  CHECK(t.edges_off == 0);
  CHECK(brep::check(body).empty());
}

TEST_CASE("a corner off its planes, and the edges it ends, are the tessellation's", "[brep]")
{
  Body body = cube();
  body.vertices[7].point += Vector3d(0, 0, 1e-6);
  const brep::ClassTally t = brep::stateClasses(body);
  CHECK(body.vertices[7].evidence == EvidenceClass::D4);
  CHECK(t.vertices_off == 1);
  // Of the three edges at corner 7, (5,7) and (6,7) bound the top it has left;
  // (3,7) bounds x = 1 and y = 1 only, and still lies in both.
  CHECK(t.edges_off == 2);
  CHECK(std::isnan(body.edges[0].curve.carrier_error) == (body.edges[0].evidence == EvidenceClass::D4));
}

TEST_CASE("three records that share a line fix no point on it", "[brep]")
{
  // The cube's bottom made to stand on the plane x = y through the origin.
  // Corner 0, (0,0,0), is on it and on x = 0 and y = 0 - three records, but all
  // three contain the z axis, so their normals are coplanar and the corner can
  // slide along it: not determined, the tessellation's, and not off.
  Body body = cube();
  const double h = std::sqrt(0.5);
  body.faces[0].surface = std::make_shared<PlaneSurface>(Vector3d::Zero(), Vector3d(h, -h, 0));
  brep::stateClasses(body);
  CHECK(body.vertices[0].evidence == EvidenceClass::D4);
  // Corner 3, (1,1,0), is on x = y too, and on x = 1 and y = 1: normals
  // (1,-1,0), (1,0,0), (0,1,0) - coplanar again, so D4 for the same reason.
  CHECK(body.vertices[3].evidence == EvidenceClass::D4);
  // Corner 4, (0,0,1), is untouched by the change: x = 0, y = 0, z = 1 fix it.
  CHECK(body.vertices[4].evidence == EvidenceClass::D1);
}

TEST_CASE("a seam's end is on two surfaces only, so where it sits was the tessellation's choice",
          "[brep]")
{
  Body body = cylinder();
  const brep::ClassTally t = brep::stateClasses(body);
  // Rims: the crossing of the cylinder with each cap, exactly. The seam: a
  // ruling. Its two ends: each on the wall and one cap - two normals, a curve's
  // worth of freedom.
  CHECK(t.edges[std::size_t(EvidenceClass::D1)] == 3);
  CHECK(t.vertices[std::size_t(EvidenceClass::D4)] == 2);
  CHECK(t.vertices_off == 0);
}

TEST_CASE("a stated fit is D2, a stated boundary D1, and a weaker face weakens its edges", "[brep]")
{
  Body body = cube();
  brep::BSpline spline;
  spline.degree = 1;
  spline.points = {body.vertices[body.edges[0].start].point, body.vertices[body.edges[0].end].point};
  spline.knots = {0, 1};
  spline.mults = {2, 2};
  body.edges[0].curve.form = spline;
  body.edges[0].curve.carrier_error = 1e-8;
  body.edges[1].curve.form = spline;
  body.edges[1].curve.stated_exact = true;
  body.edges[2].curve.form = spline;  // stated nothing: not certifiable, the tessellation's
  body.faces[5].record_evidence = EvidenceClass::D4;
  brep::stateClasses(body);
  CHECK(body.edges[0].evidence == EvidenceClass::D2);
  CHECK(body.edges[0].curve.carrier_error == 1e-8);
  CHECK(body.edges[1].evidence == EvidenceClass::D1);
  CHECK(body.edges[2].evidence == EvidenceClass::D4);
  CHECK(std::isnan(body.edges[2].curve.carrier_error));
  // Face 5's record, x = 1, now D4: every edge on it is D4 however exact.
  for (const auto& c : body.faces[5].loops[0].coedges)
    CHECK(body.edges[c.edge].evidence == EvidenceClass::D4);
}

TEST_CASE("a record nothing can ask is not a record the entity is off", "[brep]")
{
  // The cube with one face's record replaced by a Bezier patch - no algebraic
  // degree, no closed-form residual. Its corners and edges cannot be asked of
  // it: D4, since nothing certifies them, and not "off".
  Body body = cube();
  body.faces[1].surface = std::make_shared<BezierPatchSurface>(
    1, 1,
    std::vector<Vector3d>{Vector3d(0, 0, 1), Vector3d(0, 1, 1), Vector3d(1, 0, 1), Vector3d(1, 1, 1)});
  const brep::ClassTally t = brep::stateClasses(body);
  CHECK(t.vertices_off == 0);
  CHECK(t.edges_off == 0);
  CHECK(t.vertices_unasked == 4);  // the top's four corners
  CHECK(t.edges_unasked == 4);     // and its four edges
  CHECK(t.vertices[std::size_t(EvidenceClass::D4)] == 4);
  CHECK(t.vertices[std::size_t(EvidenceClass::D1)] == 4);
}

// ---- what fixes a vertex, and planes taken from their corners -------------
//
// A vertex needs three independent records and no more: the best three it lies
// on decide it, not every face that touches it. A plane taken from its own
// corners is as good as the best three that span it - and cannot lift itself.

namespace {

/*! Add to `body` a face on `record` over the edges `ring` - pairs of vertex
 * indices, each an edge made for it - and return its index. A face bolted on
 * for a class test: the shell is not asked to close. */
std::size_t addFace(Body& body, std::shared_ptr<const Surface> record,
                    const std::vector<std::pair<std::size_t, std::size_t>>& ring, EvidenceClass evidence,
                    bool from_corners)
{
  brep::Loop loop;
  for (const auto& e : ring) {
    brep::Edge edge;
    edge.start = e.first;
    edge.end = e.second;
    edge.curve.form = brep::Line{};
    body.edges.push_back(edge);
    loop.coedges.push_back(Coedge{body.edges.size() - 1, true, std::nullopt});
  }
  brep::Face face;
  face.surface = std::move(record);
  face.form = brep::Plane{};
  face.loops.push_back(loop);
  face.evidence = evidence;
  face.record_evidence = evidence;
  face.plane_from_corners = from_corners;
  body.faces.push_back(face);
  return body.faces.size() - 1;
}

}  // namespace

TEST_CASE("a vertex is as good as the best three records fixing it, not the weakest face at it",
          "[brep]")
{
  // Corner 7, (1,1,1), fixed by x = 1, y = 1, z = 1, all D0. A fourth face
  // through it on the plane x + y + z = 3, D4, weakens nothing it does not need.
  Body body = cube();
  const double t = 1 / std::sqrt(3.0);
  addFace(body, std::make_shared<PlaneSurface>(Vector3d(1, 1, 1), Vector3d(t, t, t)), {{7, 7}},
          EvidenceClass::D4, false);
  brep::stateClasses(body);
  CHECK(body.vertices[7].evidence == EvidenceClass::D1);
}

TEST_CASE("a plane through determined corners is determined", "[brep]")
{
  // A triangle across corners 5, 6 and 7 of the cube, on the plane its corners
  // are on - z = 1. Each corner is fixed by three of the cube's D0 planes
  // without it, so the triangle is spanned by D1 corners: D1.
  Body body = cube();
  const std::size_t tri =
    addFace(body, std::make_shared<PlaneSurface>(Vector3d(1, 0, 1), Vector3d::UnitZ()),
            {{5, 7}, {7, 6}, {6, 5}}, EvidenceClass::D4, true);
  brep::stateClasses(body);
  CHECK(body.faces[tri].evidence == EvidenceClass::D1);
}

TEST_CASE("a plane taken from its corners does not lift itself", "[brep]")
{
  // The cube's top made a plane from its own corners. Each top corner is on
  // two side planes and the top: fixed only with the top's help, and the top
  // only with theirs. Nothing outside the pair determines either - the height
  // was the tessellation's - so both stay D4: the least fixpoint, from D4 up.
  Body body = cube();
  body.faces[1].plane_from_corners = true;
  brep::stateClasses(body);
  CHECK(body.faces[1].evidence == EvidenceClass::D4);
  for (const std::size_t v : {4, 5, 6, 7}) CHECK(body.vertices[v].evidence == EvidenceClass::D4);
  for (const std::size_t v : {0, 1, 2, 3}) CHECK(body.vertices[v].evidence == EvidenceClass::D1);
}

TEST_CASE("a plane taken from its corners with a corner off it is not determined", "[brep]")
{
  // The triangle across 5, 6 and 7 again, on a plane a hair above z = 1: its
  // corners are not on it, so it asserts a plane they are not in.
  Body body = cube();
  const std::size_t tri =
    addFace(body, std::make_shared<PlaneSurface>(Vector3d(1, 0, 1 + 1e-9), Vector3d::UnitZ()),
            {{5, 7}, {7, 6}, {6, 5}}, EvidenceClass::D4, true);
  brep::stateClasses(body);
  CHECK(body.faces[tri].evidence == EvidenceClass::D4);
}

TEST_CASE("a parabola far along its arm is on its cone, and not on one opened a hair wider", "[brep]")
{
  // The cone x^2 + y^2 = z^2 cut by z = x + 1, parallel to a generator: the
  // parabola y^2 = 2x + 1. In the plane, with X = (1,0,1)/sqrt(2) along its
  // axis from the vertex (-1/2, 0, 1/2), it is s = w^2 / sqrt(2) - so focal
  // distance f = sqrt(2)/4 - and the frame's normal is (1,0,-1)/sqrt(2). An
  // edge from u = 20 to u = 21 lies far outside any fixed sample range.
  const double h = std::sqrt(0.5), f = std::sqrt(2.0) / 4;
  const brep::Frame frame{Vector3d(-0.5, 0, 0.5), Vector3d(h, 0, -h), Vector3d(h, 0, h)};
  const brep::CurveForm arc = brep::Conic{brep::Conic::Kind::Parabola, frame, f, 0};
  const Vector3d x = frame.ref, y = frame.axis.cross(frame.ref);
  auto at = [&](double u) { return Vector3d(frame.origin + x * (f * u * u) + y * (2 * f * u)); };
  const auto cone = std::make_shared<ConeSurface>(Vector3d(0, 0, 1), Vector3d::UnitZ(), 1.0, 1.0);
  CHECK(brep::curveOn(arc, at(20), at(21), cone.get()));
  const auto wider =
    std::make_shared<ConeSurface>(Vector3d(0, 0, 1), Vector3d::UnitZ(), 1.0, 1.0 + 1e-9);
  CHECK_FALSE(brep::curveOn(arc, at(20), at(21), wider.get()));
}

TEST_CASE("a corner rests on its records, not on how a band admitted a face's facets", "[brep]")
{
  // The cube with one face admitted by the tessellation band - membership D4 -
  // on a declared record, D0. Its corners are where three declared records
  // cross, whatever admitted the facets: D1.
  Body body = cube();
  body.faces[5].evidence = EvidenceClass::D4;
  brep::stateClasses(body);
  for (const std::size_t v : {1, 3, 5, 7}) CHECK(body.vertices[v].evidence == EvidenceClass::D1);
  for (const auto& c : body.faces[5].loops[0].coedges)
    CHECK(body.edges[c.edge].evidence == EvidenceClass::D1);
}

TEST_CASE("a point's distance to a curve, read off the geometry", "[brep]")
{
  using Kind = brep::Conic::Kind;
  const Vector3d z = Vector3d::UnitZ(), x = Vector3d::UnitX();
  // The x axis: (3, 0.5, 0) is 0.5 off it.
  CHECK(brep::distanceToCurve(brep::Line{Vector3d::Zero(), x}, Vector3d(3, 0.5, 0)) ==
        Catch::Approx(0.5));
  // A circle of radius 2 about z: (3,0,0) is 1 outside it; (0,0,1), on the
  // axis 1 above its plane, is sqrt(1 + 4) from every point of it.
  const brep::CurveForm circle = brep::Conic{Kind::Circle, {Vector3d::Zero(), z, x}, 2, 2};
  CHECK(brep::distanceToCurve(circle, Vector3d(3, 0, 0)) == Catch::Approx(1.0));
  CHECK(brep::distanceToCurve(circle, Vector3d(0, 0, 1)) == Catch::Approx(std::sqrt(5.0)));
  // An ellipse of semi-axes 2 along x and 1 along y: (0,3,0) is 2 from its
  // nearest point (0,1,0); its own point (2,0,0) is on it.
  const brep::CurveForm ellipse = brep::Conic{Kind::Ellipse, {Vector3d::Zero(), z, x}, 2, 1};
  CHECK(brep::distanceToCurve(ellipse, Vector3d(0, 3, 0)) == Catch::Approx(2.0));
  CHECK(brep::distanceToCurve(ellipse, Vector3d(2, 0, 0)) == Catch::Approx(0.0).margin(1e-12));
  // The parabola a(t^2 x + 2t y), a = 1: its vertex is on it, and (0,0,5) is 5
  // above the vertex, the nearest point in its plane.
  const brep::CurveForm parabola = brep::Conic{Kind::Parabola, {Vector3d::Zero(), z, x}, 1, 0};
  CHECK(brep::distanceToCurve(parabola, Vector3d::Zero()) == Catch::Approx(0.0).margin(1e-12));
  CHECK(brep::distanceToCurve(parabola, Vector3d(0, 0, 5)) == Catch::Approx(5.0));
}

TEST_CASE("an edge's ends are on its curve, to the uncertainty the file declares", "[brep][R12]")
{
  // A reader rejects an edge whose vertex is off its curve (SOLIDWORKS fault
  // code 7); the contract names it, an EXPORT-ERROR, where a report once
  // warned (open item 39, R12). Two of the cube's edges run along x and y from
  // corner 0; moved half a unit up z, corner 0 is off both - and still on the
  // third, which runs up z.
  Body body = cube();
  CHECK(brep::checkVerticesOnCurves(body, 1e-12).empty());
  body.vertices[0].point += Vector3d(0, 0, 0.5);
  const auto off = brep::checkVerticesOnCurves(body, 1e-12);
  CHECK(off.size() == 2);
  for (const std::string& line : off)
    CHECK(line.rfind("vertex 0 is 5.00e-01 off the curve of edge ", 0) == 0);
  // Within what the file declares, it is on.
  CHECK(brep::checkVerticesOnCurves(body, 0.5).empty());
}
