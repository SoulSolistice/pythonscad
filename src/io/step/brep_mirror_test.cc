// The B-rep read off a STEP file's entities - see BrepMirror.h. Each graph is
// built by hand as the writer builds one, and every expectation is read off
// the solid it describes: a unit cube is 8 corners, 12 edges and 6 faces.
#include "io/step/BrepMirror.h"
#include "io/step/BrepToStep.h"

#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <typeinfo>
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

TEST_CASE("a rational spline with knots is written rational, with its knots", "[step-kernel]")
{
  // A quadratic arc of a circle over one span, stated with explicit knots: the
  // middle weight is cos(45 degrees). Written without its weights it is a
  // parabola, not the arc - which is what the knotted branch of the serialiser
  // did (open item 39, R19).
  std::vector<K::Entity *> ents;
  std::vector<K::Point *> pts{new K::Point(ents, Vector3d(1, 0, 0)),
                              new K::Point(ents, Vector3d(1, 1, 0)),
                              new K::Point(ents, Vector3d(0, 1, 0))};
  auto *arc = new K::BSplineCurve(ents, "", pts, std::vector<double>{1, std::sqrt(0.5), 1});
  arc->setKnots(2, {0, 1}, {3, 3});
  std::ostringstream out;
  arc->serialize(out);
  const std::string text = out.str();
  CHECK(text.find("RATIONAL_B_SPLINE_CURVE") != std::string::npos);
  CHECK(text.find("B_SPLINE_CURVE_WITH_KNOTS((3,3),(0.,1.),.UNSPECIFIED.)") != std::string::npos);
  CHECK(text.find("B_SPLINE_CURVE(2,") != std::string::npos);
  // And the forms the writer makes today are unchanged: a Bezier with no knots
  // set is one span of its own degree.
  auto *bezier = new K::BSplineCurve(ents, "", pts);
  std::ostringstream plain;
  bezier->serialize(plain);
  CHECK(plain.str().find("B_SPLINE_CURVE_WITH_KNOTS('',2,(") != std::string::npos);
  CHECK(plain.str().find("(3,3),(0.,1.),.UNSPECIFIED.);") != std::string::npos);
  for (auto *e : ents) delete e;
}

namespace {

/*! How many entities of each type, live ones only - what a STEP file's census
 * counts. */
std::map<std::string, int> census(const std::vector<K::Entity *>& ents)
{
  std::map<std::string, int> out;
  for (const auto *e : ents) {
    if (e->live) out[typeid(*e).name()]++;
  }
  return out;
}

}  // namespace

TEST_CASE("a body translated to STEP reads back as the same body, entity for entity", "[brep-translate]")
{
  // The cube as the writer writes it, with one edge carried as a SURFACE_CURVE
  // with a pcurve on its first face - a straight line across that face's (u, v)
  // whose span is 2.5, which a direction alone does not state.
  Graph g;
  cube(g);
  K::EdgeCurve *wrapped = g.faces[0]->faceBounds[0]->edgeLoop->faces[0]->edge;
  auto *context = new K::ParametricContext(g.ents);
  auto *flat = new K::Line2d(g.ents, new K::Point2d(g.ents, 0.5, 0.25),
                             new K::Vector2d(g.ents, new K::Direction2d(g.ents, 0.6, 0.8), 2.5));
  wrapped->round =
    new K::SurfaceCurve(g.ents, wrapped->round,
                        {new K::PCurve(g.ents, g.faces[0]->surface,
                                       new K::DefinitionalRepresentation(g.ents, flat, context))});

  const brep::Body body = step::mirrorBrep(g.faces, g.sources, oneShell(g.faces.size()));
  std::vector<K::Entity *> out;
  K::ParametricContext *made = nullptr;
  const std::vector<K::Face *> faces = step::writeBrep(out, body, made);
  std::map<const K::Face *, step::FaceSource> sources;
  for (std::size_t i = 0; i < faces.size(); i++) sources[faces[i]] = g.sources.at(g.faces[i]);
  const brep::Body back = step::mirrorBrep(faces, sources, oneShell(faces.size()));

  // The same body: every vertex where it was, every edge between the same two
  // with the same kind of curve and sense, every loop the same coedges.
  REQUIRE(back.vertices.size() == body.vertices.size());
  REQUIRE(back.edges.size() == body.edges.size());
  REQUIRE(back.faces.size() == body.faces.size());
  for (std::size_t v = 0; v < body.vertices.size(); v++)
    CHECK(back.vertices[v].point == body.vertices[v].point);
  for (std::size_t e = 0; e < body.edges.size(); e++) {
    CHECK(back.edges[e].start == body.edges[e].start);
    CHECK(back.edges[e].end == body.edges[e].end);
    CHECK(back.edges[e].same_sense == body.edges[e].same_sense);
    CHECK(back.edges[e].curve.form.index() == body.edges[e].curve.form.index());
  }
  for (std::size_t f = 0; f < body.faces.size(); f++) {
    CHECK(back.faces[f].form.index() == body.faces[f].form.index());
    REQUIRE(back.faces[f].loops.size() == body.faces[f].loops.size());
    for (std::size_t l = 0; l < body.faces[f].loops.size(); l++) {
      const auto& a = body.faces[f].loops[l].coedges;
      const auto& b = back.faces[f].loops[l].coedges;
      REQUIRE(a.size() == b.size());
      for (std::size_t i = 0; i < a.size(); i++) {
        CHECK(a[i].edge == b[i].edge);
        CHECK(a[i].forward == b[i].forward);
        CHECK(a[i].pcurve.has_value() == b[i].pcurve.has_value());
      }
    }
  }
  // The pcurve, span and all.
  const auto& pc = back.faces[0].loops[0].coedges[0].pcurve;
  REQUIRE(pc.has_value());
  const auto *line = std::get_if<brep::Line2>(&*pc);
  REQUIRE(line != nullptr);
  CHECK(line->length == 2.5);
  CHECK(line->origin == Vector2d(0.5, 0.25));

  // And the same entities: a census by type of what the writer built and of
  // what the translator wrote from its B-rep.
  CHECK(census(out) == census(g.ents));
  for (auto *e : out) delete e;
}

TEST_CASE("the translator writes nothing for a body with a face on no form or an edge on no curve",
          "[brep-translate]")
{
  Graph g;
  cube(g);
  brep::Body body = step::mirrorBrep(g.faces, g.sources, oneShell(g.faces.size()));
  for (int which = 0; which < 2; which++) {
    brep::Body broken = body;
    if (which == 0) broken.faces[2].form = std::monostate{};
    else broken.edges[4].curve.form = std::monostate{};
    std::vector<K::Entity *> out;
    K::ParametricContext *made = nullptr;
    CHECK(step::writeBrep(out, broken, made).empty());
    CHECK(out.empty());  // refused before making anything
  }
}
