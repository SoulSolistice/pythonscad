// The 2D declaration channel and the sweep table, tested on profiles built by
// hand: what each edge of a profile is (Profile2d), and what a sweep of it makes
// (SweptSurfaces). Both are pure functions of the declarations, so nothing here
// needs a node, a mesh or a backend.
#include "geometry/Profile2d.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "geometry/ClipperUtils.h"
#include "geometry/HullOfRevolution.h"
#include "geometry/Surface.h"
#include "geometry/SweptSurfaces.h"

namespace {

/*! A regular polygon sampled from a circle, the way circle() samples one. */
VectorOfVector2d sampled(const Vector2d& centre, double r, int n)
{
  VectorOfVector2d out;
  for (int i = 0; i < n; i++) {
    const double t = 2 * M_PI * i / n;
    out.emplace_back(centre + r * Vector2d(cos(t), sin(t)));
  }
  return out;
}

template <class T>
std::vector<const T *> ofKind(const std::vector<std::shared_ptr<Surface>>& surfaces)
{
  std::vector<const T *> out;
  for (const auto& s : surfaces) {
    if (const auto *t = dynamic_cast<const T *>(s.get())) out.push_back(t);
  }
  return out;
}

/*! step-rotate-extrude.scad's stepped tube: a bore, two walls and a taper. */
const VectorOfVector2d kSteppedTube{{8., 0.}, {10., 0.}, {10., 6.}, {12., 10.}, {12., 16.}, {8., 16.}};

}  // namespace

TEST_CASE("Profile2d: a sampled circle is chords of its arc, a square is lines", "[sweep-table]")
{
  const Arc2d arc{Vector2d(5., 0.), 2.};
  const VectorOfVector2d square{{0., -1.}, {1., -1.}, {1., 1.}, {0., 1.}};
  const Profile2d p = classifyProfile({sampled(Vector2d(5., 0.), 2., 16), square}, {arc}, {});
  REQUIRE(p.loops.size() == 2);
  for (const auto& e : p.loops[0]) {
    CHECK(e.kind == EdgeKind::Arc);
    CHECK(e.record == 0);
  }
  for (const auto& e : p.loops[1]) CHECK(e.kind == EdgeKind::Line);
}

TEST_CASE("Profile2d: an arc is asked at machine precision, not within a threshold", "[sweep-table]")
{
  // A point 1e-9 off the circle was a chord under the 1e-6 the linear extruder
  // used to allow; it is not on the arc, and the edge is not the arc's chord.
  const Arc2d arc{Vector2d(0., 0.), 1.};
  const Vector2d on(1., 0.);
  const Vector2d off = Vector2d(0., 1.) * (1. + 1e-9);
  CHECK(onArc(on, arc));
  CHECK_FALSE(onArc(off, arc));
  const Profile2d p = classifyProfile({VectorOfVector2d{on, off, Vector2d(-1., 0.)}}, {arc}, {});
  CHECK(p.loops[0][0].kind == EdgeKind::Line);
  // (-1, 0) back to (1, 0) is a diameter: both ends on the circle, so by
  // measurement a chord - only its maker can say otherwise (the D-shaft below)
  CHECK(p.loops[0][2].kind == EdgeKind::Arc);
}

TEST_CASE("Profile2d: every edge is classified, the closing one included", "[sweep-table]")
{
  const VectorOfVector2d triangle{{0., 0.}, {1., 0.}, {0., 1.}};
  const Profile2d p = classifyProfile({triangle}, {}, {});
  REQUIRE(p.loops[0].size() == 3);
  CHECK(p.loops[0][2].a == Vector2d(0., 1.));
  CHECK(p.loops[0][2].b == Vector2d(0., 0.));
}

TEST_CASE("revolve: each straight edge by its course", "[sweep-table]")
{
  const auto surfaces = revolve(classifyProfile({kSteppedTube}, {}, {}), Revolution{});
  // three walls, each a face; the taper's rims are the r = 10 and r = 12 walls
  // already, so no radius is evidence only
  const auto cylinders = ofKind<CylinderSurface>(surfaces);
  REQUIRE(cylinders.size() == 3);
  for (const auto *c : cylinders) CHECK(c->declaresFace());
  // the taper's cone, anchored at its wider rim
  const auto cones = ofKind<ConeSurface>(surfaces);
  REQUIRE(cones.size() == 1);
  CHECK(cones[0]->r == 12.);
  CHECK(cones[0]->slope == 0.5);
  CHECK(cones[0]->refpt == Vector3d(0., 0., 10.));
  // the two annuli
  const auto planes = ofKind<PlaneSurface>(surfaces);
  REQUIRE(planes.size() == 2);
  CHECK(planes[0]->refpt[2] == 0.);
  CHECK(planes[1]->refpt[2] == 16.);
}

TEST_CASE("revolve: an edge reaching the axis sweeps its cone, apex on the axis", "[sweep-table]")
{
  // step-revolve-axis-point.scad: rotate_extrude() polygon([[0,0], [10,0], [0,20]])
  const VectorOfVector2d triangle{{0., 0.}, {10., 0.}, {0., 20.}};
  const auto surfaces = revolve(classifyProfile({triangle}, {}, {}), Revolution{});
  const auto cones = ofKind<ConeSurface>(surfaces);
  REQUIRE(cones.size() == 1);
  CHECK(cones[0]->r == 10.);
  CHECK(cones[0]->slope == -0.5);
  CHECK(cones[0]->refpt == Vector3d(0., 0., 0.));
  // the rim, as every vertex off the axis is, is evidence; the axis sweeps nothing
  REQUIRE(ofKind<CylinderSurface>(surfaces).size() == 1);
  CHECK(ofKind<CylinderSurface>(surfaces)[0]->r == 10.);
  CHECK_FALSE(ofKind<CylinderSurface>(surfaces)[0]->declaresFace());
  CHECK(ofKind<PlaneSurface>(surfaces).size() == 1);
}

TEST_CASE("revolve: a plane is at the level its edge is at", "[sweep-table]")
{
  // rotate_extrude hands the table the profile as it is swept - offset already
  // applied - and until 2026-09-25 added the offset to the planes a second time.
  VectorOfVector2d lifted = kSteppedTube;
  for (auto& v : lifted) v[1] += 5.;
  const auto planes = ofKind<PlaneSurface>(revolve(classifyProfile({lifted}, {}, {}), Revolution{}));
  REQUIRE(planes.size() == 2);
  CHECK(planes[0]->refpt[2] == 5.);
  CHECK(planes[1]->refpt[2] == 21.);
}

TEST_CASE("revolve: about any axis, not only z", "[sweep-table]")
{
  Revolution sweep;
  sweep.origin = Vector3d(1., 2., 3.);
  sweep.axis = Vector3d(1., 0., 0.);
  const auto cones = ofKind<ConeSurface>(revolve(classifyProfile({kSteppedTube}, {}, {}), sweep));
  REQUIRE(cones.size() == 1);
  CHECK(cones[0]->refpt == Vector3d(11., 2., 3.));
  CHECK(cones[0]->normdir == Vector3d(1., 0., 0.));
}

TEST_CASE("revolve: an arc is a sphere on the axis and a torus off it", "[sweep-table]")
{
  const Arc2d on_axis{Vector2d(0., 4.), 3.};
  const Arc2d ring{Vector2d(10., 0.), 2.};
  const auto surfaces = revolve(classifyProfile({}, {on_axis, ring}, {}), Revolution{});
  const auto spheres = ofKind<SphereSurface>(surfaces);
  const auto tori = ofKind<TorusSurface>(surfaces);
  REQUIRE(spheres.size() == 1);
  REQUIRE(tori.size() == 1);
  CHECK(spheres[0]->refpt == Vector3d(0., 0., 4.));
}

TEST_CASE("revolve: a partial turn is closed by the profile's plane at each end", "[sweep-table]")
{
  Revolution sweep;
  sweep.angle = 90.;
  const auto planes = ofKind<PlaneSurface>(revolve(classifyProfile({kSteppedTube}, {}, {}), sweep));
  // the two annuli, and the ends at 0 and 90 degrees
  CHECK(planes.size() == 4);
}

TEST_CASE("extrude: a chord is not a wall", "[sweep-table]")
{
  // a half circle closed by three straight edges, two of them clear of it
  const Arc2d arc{Vector2d(0., 0.), 5.};
  VectorOfVector2d profile;
  for (int i = 0; i <= 8; i++) {
    const double t = -M_PI / 2 + M_PI * i / 8;
    profile.emplace_back(5. * cos(t), 5. * sin(t));
  }
  profile.emplace_back(-3., 5.);
  profile.emplace_back(-3., -5.);
  Extrusion sweep;
  sweep.height = Vector3d(0., 0., 7.);
  const auto surfaces = extrude(classifyProfile({profile}, {arc}, {}), sweep);
  // two caps and the three straight walls - none of the eight chords
  CHECK(ofKind<PlaneSurface>(surfaces).size() == 5);
  REQUIRE(ofKind<CylinderSurface>(surfaces).size() == 1);
  CHECK(ofKind<CylinderSurface>(surfaces)[0]->r == 5.);
}

// A straight edge whose two ends both lie on a recorded circle - the flat of a
// D-shaft, the back of a half disc - is a chord by every measurement there is.
// Only its maker knows it is not: doc/step-export-wip.md, *Inferred where a
// declaration was possible*, item 2. This was marked [!shouldfail] until the
// maker's word reached the classifier.
TEST_CASE("extrude: a D-shaft's flat is a wall, because its maker says so", "[sweep-table]")
{
  Outline2d d;
  for (int i = 0; i <= 8; i++) {
    const double t = -M_PI / 2 + M_PI * i / 8;
    d.vertices.emplace_back(5. * cos(t), 5. * sin(t));
  }
  Polygon2d poly(d);
  poly.arcs.push_back(Arc2d{Vector2d(0., 0.), 5.});
  poly.declareEdges(EdgeKind::Arc, 0);
  // the last edge closes the outline straight back across the diameter
  poly.edge_decls[poly.untransformedOutlines()[0].edge_ids.back()] = EdgeDecl{EdgeKind::Line, -1};

  const Profile2d measured = classifyProfile({d.vertices}, poly.arcs, {});
  CHECK(measured.loops[0].back().kind == EdgeKind::Arc);  // what measurement says
  const Profile2d declared = classifyProfile(poly);
  CHECK(declared.loops[0].back().kind == EdgeKind::Line);
  CHECK(declared.loops[0].back().declared);

  Extrusion sweep;
  sweep.height = Vector3d(0., 0., 7.);
  // two caps and the flat
  CHECK(ofKind<PlaneSurface>(extrude(declared, sweep)).size() == 3);
}

TEST_CASE("identity: an edge is the one edge its two ends both name", "[sweep-table]")
{
  CHECK(commonEdge(packEdges(1, 2), packEdges(2, 3)) == 2);
  CHECK(commonEdge(packEdges(1, 2), packEdges(3, 4)) == 0);  // nothing in common
  CHECK(commonEdge(packEdges(1, 2), packEdges(2, 1)) == 0);  // two: no one edge between them
  CHECK(commonEdge(packEdges(0, 2), packEdges(0, 2)) == 2);  // unnamed halves name nothing
  CHECK(commonEdge(packEdges(0, 0), packEdges(0, 0)) == 0);
}

TEST_CASE("identity: reversing an outline reverses its edges' names", "[sweep-table]")
{
  Outline2d o;
  o.vertices = {{0., 0.}, {1., 0.}, {1., 1.}, {0., 1.}};
  o.edge_ids = {10, 11, 12, 13};  // 10: (0,0)-(1,0), 11: (1,0)-(1,1), ...
  o.reverse();
  // now (0,1), (1,1), (1,0), (0,0): edge 0 is (0,1)-(1,1), which was 12
  CHECK(o.edge_ids == std::vector<EdgeId>{12, 11, 10, 13});
}

#ifdef USINGZ
TEST_CASE("identity: a D-shaft through Clipper keeps its flat a wall and its arc an arc",
          "[sweep-table]")
{
  // circle(r = 5, $fn = 32) intersected with a square whose side cuts it at x = 3
  Outline2d c;
  for (int i = 0; i < 32; i++) {
    const double t = 2 * M_PI * i / 32;
    c.vertices.emplace_back(5. * cos(t), 5. * sin(t));
  }
  auto circle = std::make_shared<Polygon2d>(c);
  circle->arcs.push_back(Arc2d{Vector2d(0., 0.), 5.});
  circle->declareEdges(EdgeKind::Arc, 0);
  Outline2d q;
  q.vertices = {{-10., -10.}, {3., -10.}, {3., 10.}, {-10., 10.}};
  auto square = std::make_shared<Polygon2d>(q);
  square->declareEdges(EdgeKind::Line);

  const auto shaft = ClipperUtils::apply({circle, square}, Clipper2Lib::ClipType::Intersection);
  const Profile2d p = classifyProfile(*shaft);
  REQUIRE(p.loops.size() == 1);
  int flats = 0, chords = 0, unnamed = 0;
  for (const auto& e : p.loops[0]) {
    if (!e.declared) unnamed++;
    else if (e.kind == EdgeKind::Line) flats++;
    else if (e.kind == EdgeKind::Arc && e.record == 0) chords++;
  }
  CHECK(unnamed == 0);
  CHECK(flats == 1);
  CHECK(chords == int(p.loops[0].size()) - 1);
}
#endif

namespace {

/*! What cylinder(h, r) translated to `z0` declares: its wall and its two caps. */
std::vector<std::shared_ptr<Surface>> cylinderRecords(double r, double z0, double h,
                                                      const Vector3d& at = Vector3d::Zero())
{
  return {std::make_shared<CylinderSurface>(at + Vector3d(0., 0., z0), Vector3d(0., 0., 1.), r),
          std::make_shared<PlaneSurface>(at + Vector3d(0., 0., z0), Vector3d(0., 0., 1.)),
          std::make_shared<PlaneSurface>(at + Vector3d(0., 0., z0 + h), Vector3d(0., 0., 1.))};
}

}  // namespace

TEST_CASE("hull of revolution: a chamfer is the cone between two rims", "[sweep-table]")
{
  // step-chamfered-cylinder.scad: hull() of an r = 10 wall on z = 2..20 and an
  // r = 8 disc on z = 0..0.01. The hull's profile runs from (8, 0) to (10, 2).
  const auto surfaces =
    declareHullOfRevolution({cylinderRecords(10., 2., 18.), cylinderRecords(8., 0., 0.01)});
  const auto cones = ofKind<ConeSurface>(surfaces);
  REQUIRE(cones.size() == 1);
  CHECK(cones[0]->r == 10.);
  CHECK(cones[0]->slope == 1.);
  CHECK(cones[0]->refpt == Vector3d(0., 0., 2.));
}

TEST_CASE("hull of revolution: operands on two axes state nothing", "[sweep-table]")
{
  const auto surfaces = declareHullOfRevolution(
    {cylinderRecords(10., 2., 18.), cylinderRecords(8., 0., 0.01, Vector3d(1., 0., 0.))});
  CHECK(surfaces.empty());
}
