// The 2D declaration channel and the sweep table, tested on profiles built by
// hand: what each edge of a profile is (Profile2d), and what a sweep of it makes
// (SweptSurfaces). Both are pure functions of the declarations, so nothing here
// needs a node, a mesh or a backend.
#include "geometry/Profile2d.h"

#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "core/CurveDiscretizer.h"
#include "geometry/ClipperUtils.h"
#include "geometry/HullOfRevolution.h"
#include "geometry/Surface.h"
#include "geometry/SweptSurfaces.h"
#include "geometry/linear_extrude_internals.h"
#include "geometry/recognition/Exact.h"

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

/*! A profile whose maker named every edge, as the classifier would measure it
 * - a chord of an arc where both its ends are on one, a line otherwise: what a
 * generator that declares its edges hands a sweep. */
Profile2d declared(const std::vector<VectorOfVector2d>& loops, const std::vector<Arc2d>& arcs,
                   const std::vector<Bezier2d>& beziers)
{
  const Profile2d measured = classifyProfile(loops, arcs, beziers);
  std::vector<std::vector<EdgeId>> ids;
  EdgeDecls decls;
  for (const auto& loop : measured.loops) {
    const EdgeId first = reserveEdgeIds(loop.size());
    std::vector<EdgeId> named;
    for (std::size_t i = 0; i < loop.size(); i++) {
      named.push_back(first + EdgeId(i));
      decls[first + EdgeId(i)] = EdgeDecl{loop[i].kind, loop[i].record};
    }
    ids.push_back(named);
  }
  return classifyProfile(loops, arcs, beziers, ids, &decls);
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

TEST_CASE("Profile2d: an unnamed edge is never a Bezier's chord by measurement", "[sweep-table][e6]")
{
  // Both ends inside the control box of a cubic whose one maker, text(), names
  // every edge it makes: until 2026-09-28 the box made this edge the Bezier's
  // chord. Nobody made it on the curve.
  Bezier2d bez;
  bez.degree = 3;
  bez.ctrl[0] = Vector2d(0., 0.);
  bez.ctrl[1] = Vector2d(1., 2.);
  bez.ctrl[2] = Vector2d(2., 2.);
  bez.ctrl[3] = Vector2d(3., 0.);
  const VectorOfVector2d loop{{1., 0.5}, {2., 0.5}, {1.5, 1.}};
  const Profile2d p = classifyProfile({loop}, {}, {bez});
  REQUIRE(p.loops.size() == 1);
  for (const auto& e : p.loops[0]) {
    CHECK(e.kind == EdgeKind::Line);
    CHECK(e.record == -1);
  }
}

TEST_CASE("Profile2d: every edge is classified, the closing one included", "[sweep-table]")
{
  const VectorOfVector2d triangle{{0., 0.}, {1., 0.}, {0., 1.}};
  const Profile2d p = classifyProfile({triangle}, {}, {});
  REQUIRE(p.loops[0].size() == 3);
  CHECK(p.loops[0][2].a == Vector2d(0., 1.));
  CHECK(p.loops[0][2].b == Vector2d(0., 0.));
}

TEST_CASE("sweep: an unnamed straight edge sweeps nothing, a named one its plane", "[sweep-table][e6]")
{
  // Line is only an unnamed edge's default, and a default is no maker's word:
  // declared, it put a plane under facets text() made on its Bezier patch
  // (step-extrude-text-counter, 2026-09-28), and the D0 membership its maker
  // gave them was lost to it.
  const VectorOfVector2d square{{0., 0.}, {1., 0.}, {1., 1.}, {0., 1.}};
  Extrusion sweep;
  sweep.height = Vector3d(0., 0., 2.);
  const auto unnamed = ofKind<PlaneSurface>(extrude(classifyProfile({square}, {}, {}), sweep));
  const auto named = ofKind<PlaneSurface>(extrude(declared({square}, {}, {}), sweep));
  CHECK(named.size() == unnamed.size() + 4);  // the four walls, over the same caps
  CHECK(ofKind<PlaneSurface>(revolve(classifyProfile({square}, {}, {}), Revolution{})).size() + 2 ==
        ofKind<PlaneSurface>(revolve(declared({square}, {}, {}), Revolution{})).size());
}

TEST_CASE("revolve: each straight edge by its course", "[sweep-table]")
{
  const auto surfaces = revolve(declared({kSteppedTube}, {}, {}), Revolution{});
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
  const auto surfaces = revolve(declared({triangle}, {}, {}), Revolution{});
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
  const auto planes = ofKind<PlaneSurface>(revolve(declared({lifted}, {}, {}), Revolution{}));
  REQUIRE(planes.size() == 2);
  CHECK(planes[0]->refpt[2] == 5.);
  CHECK(planes[1]->refpt[2] == 21.);
}

TEST_CASE("revolve: about any axis, not only z", "[sweep-table]")
{
  Revolution sweep;
  sweep.origin = Vector3d(1., 2., 3.);
  sweep.axis = Vector3d(1., 0., 0.);
  const auto cones = ofKind<ConeSurface>(revolve(declared({kSteppedTube}, {}, {}), sweep));
  REQUIRE(cones.size() == 1);
  CHECK(cones[0]->refpt == Vector3d(11., 2., 3.));
  CHECK(cones[0]->normdir == Vector3d(1., 0., 0.));
}

TEST_CASE("revolve: an arc is a sphere on the axis and a torus off it", "[sweep-table]")
{
  const Arc2d on_axis{Vector2d(0., 4.), 3.};
  const Arc2d ring{Vector2d(10., 0.), 2.};
  const auto surfaces = revolve(declared({}, {on_axis, ring}, {}), Revolution{});
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
  const auto planes = ofKind<PlaneSurface>(revolve(declared({kSteppedTube}, {}, {}), sweep));
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
  const auto surfaces = extrude(declared({profile}, {arc}, {}), sweep);
  // two caps and the three straight walls - none of the eight chords
  CHECK(ofKind<PlaneSurface>(surfaces).size() == 5);
  REQUIRE(ofKind<CylinderSurface>(surfaces).size() == 1);
  CHECK(ofKind<CylinderSurface>(surfaces)[0]->r == 5.);
}

TEST_CASE("extrude: an uneven scale sweeps a diagonal edge into a bilinear patch", "[sweep-table]")
{
  // step-extrude-uneven-scale.scad
  const VectorOfVector2d triangle{{0., 0.}, {10., 0.}, {0., 10.}};
  Extrusion sweep;
  sweep.height = Vector3d(0., 0., 10.);
  sweep.scale_x = 0.5;
  const auto surfaces = extrude(declared({triangle}, {}, {}), sweep);
  // the caps, and the walls over the two edges along the axes, which the scale
  // keeps parallel to themselves
  CHECK(ofKind<PlaneSurface>(surfaces).size() == 4);
  const auto patches = ofKind<BezierPatchSurface>(surfaces);
  REQUIRE(patches.size() == 1);
  CHECK(patches[0]->degree_u == 1);
  CHECK(patches[0]->degree_v == 1);
  // the hypotenuse's far end: (0, 10) stays, (10, 0) goes to (5, 0)
  CHECK(patches[0]->control(0, 1) == Vector3d(5., 0., 10.));
}

TEST_CASE("extrude: an even taper sweeps the cone through its end circles", "[sweep-table]")
{
  // step-tapered-extrude.scad
  Extrusion sweep;
  sweep.height = Vector3d(0., 0., 20.);
  sweep.scale_x = sweep.scale_y = 0.5;
  const auto surfaces = extrude(classifyProfile({}, {Arc2d{Vector2d(0., 0.), 10.}}, {}), sweep);
  const auto cones = ofKind<ConeSurface>(surfaces);
  REQUIRE(cones.size() == 1);
  CHECK(cones[0]->r == 10.);
  CHECK(cones[0]->slope == -0.25);
  // its two end circles, as evidence
  CHECK(ofKind<CylinderSurface>(surfaces).size() == 2);
  // an arc off the scaling origin slides sideways as it shrinks: an oblique cone
  const auto off = extrude(classifyProfile({}, {Arc2d{Vector2d(3., 0.), 2.}}, {}), sweep);
  CHECK(ofKind<ConeSurface>(off).empty());
}

TEST_CASE("extrude: a twist turns a circle about its own centre into itself", "[sweep-table]")
{
  Extrusion sweep;
  sweep.height = Vector3d(0., 0., 20.);
  sweep.twist = 90.;
  CHECK(ofKind<CylinderSurface>(extrude(classifyProfile({}, {Arc2d{Vector2d(0., 0.), 4.}}, {}), sweep))
          .size() == 1);
  // off the twist axis the centre turns too, and sweeps a helix
  CHECK(ofKind<CylinderSurface>(extrude(classifyProfile({}, {Arc2d{Vector2d(5., 0.), 1.}}, {}), sweep))
          .empty());
}

TEST_CASE("extrude: an oblique circle sweeps an elliptic cylinder, not nothing", "[sweep-table]")
{
  // step-extrude-oblique.scad
  Extrusion sweep;
  sweep.height = Vector3d(6., 0., 20.).normalized() * 20.;
  const auto surfaces = extrude(classifyProfile({}, {Arc2d{Vector2d(0., 0.), 4.}}, {}), sweep);
  CHECK(ofKind<CylinderSurface>(surfaces).empty());
  const auto elliptic = ofKind<EllipticCylinderSurface>(surfaces);
  REQUIRE(elliptic.size() == 1);
  // across v the circle is foreshortened in x: minor 4, major 4 / cos
  const double cos_tilt = 20. / sqrt(436.);
  CHECK(elliptic[0]->b() == Catch::Approx(4. * cos_tilt));
  CHECK(elliptic[0]->a() == Catch::Approx(4.));
}

TEST_CASE("roof: a plane over each edge, and a cone over each reflex corner of a Voronoi roof",
          "[sweep-table]")
{
  // an L, material on the left: counter clockwise, one reflex corner at (5, 5)
  const VectorOfVector2d ell{{0., 0.}, {10., 0.}, {10., 5.}, {5., 5.}, {5., 10.}, {0., 10.}};
  const Profile2d profile = declared({ell}, {}, {});
  const auto straight = roofSurfaces(profile, false);
  CHECK(ofKind<PlaneSurface>(straight).size() == 7);  // the floor and six edges
  CHECK(ofKind<ConeSurface>(straight).empty());
  const auto voronoi = roofSurfaces(profile, true);
  const auto cones = ofKind<ConeSurface>(voronoi);
  REQUIRE(cones.size() == 1);
  CHECK(cones[0]->refpt == Vector3d(5., 5., 1.));
  CHECK(cones[0]->r == 1.);
  CHECK(cones[0]->slope == 1.);
  // the plane over the bottom edge rises away from y = 0 into the material
  const auto planes = ofKind<PlaneSurface>(straight);
  CHECK(planes[1]->normdir.isApprox(Vector3d(0., -1., 1.).normalized()));
}

TEST_CASE("roof: a face's record is known by what it stands over, by its maker's names",
          "[sweep-table][e8]")
{
  // The L again. What each face stands over is the maker's to say (checklist
  // E8): the floor, an edge by its name, the reflex corner by the names of the
  // edges into it and out of it - and the record answers, not a position.
  const VectorOfVector2d ell{{0., 0.}, {10., 0.}, {10., 5.}, {5., 5.}, {5., 10.}, {0., 10.}};
  const Profile2d profile = declared({ell}, {}, {});
  const auto& loop = profile.loops[0];
  auto edge_from = [&](const Vector2d& a, const Vector2d& b) {
    for (const auto& e : loop)
      if (e.a == a && e.b == b) return e.id;
    return EdgeId(0);
  };
  const EdgeId bottom = edge_from({0., 0.}, {10., 0.});
  const EdgeId into = edge_from({10., 5.}, {5., 5.}), out = edge_from({5., 5.}, {5., 10.});
  REQUIRE(bottom != 0);
  REQUIRE(into != 0);
  REQUIRE(out != 0);

  RoofFaces faces;
  const auto voronoi = roofSurfaces(profile, true, &faces);
  auto record = [&](const RoofSite& site) -> const Surface * {
    const int at = faces.of(site);
    return at < 0 ? nullptr : voronoi[std::size_t(at)].get();
  };
  const auto *floor = dynamic_cast<const PlaneSurface *>(record({RoofSite::Kind::Floor, 0, 0}));
  REQUIRE(floor != nullptr);
  CHECK(floor->normdir == Vector3d(0., 0., 1.));
  // over the bottom edge: through it, rising away from y = 0 at slope 1
  const auto *wall = dynamic_cast<const PlaneSurface *>(record({RoofSite::Kind::Edge, bottom, 0}));
  REQUIRE(wall != nullptr);
  CHECK(wall->normdir.isApprox(Vector3d(0., -1., 1.).normalized()));
  // over the reflex corner at (5, 5): its cone, whichever way the names come
  const auto *cone = dynamic_cast<const ConeSurface *>(record({RoofSite::Kind::Corner, into, out}));
  REQUIRE(cone != nullptr);
  CHECK(cone->refpt == Vector3d(5., 5., 1.));
  CHECK(record({RoofSite::Kind::Corner, out, into}) == cone);
  // a convex corner has no cone, and a name nobody gave names nothing
  CHECK(record({RoofSite::Kind::Corner, bottom, edge_from({10., 0.}, {10., 5.})}) == nullptr);
  CHECK(record({RoofSite::Kind::Edge, 0, 0}) == nullptr);
  CHECK(record({RoofSite::Kind::Corner, into, 0}) == nullptr);
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

TEST_CASE("2D ellipse: a circle under an uneven scale keeps its curve and its chords' names",
          "[sweep-table]")
{
  Outline2d c;
  for (int i = 0; i < 16; i++) {
    const double t = 2 * M_PI * i / 16;
    c.vertices.emplace_back(2. * cos(t), 2. * sin(t));
  }
  auto poly = std::make_shared<Polygon2d>(c);
  poly->arcs.push_back(Arc2d{Vector2d(0., 0.), 2.});
  poly->declareEdges(EdgeKind::Arc, 0);
  Transform2d stretch = Transform2d::Identity();
  stretch.scale(Vector2d(2., 1.));
  poly->transform(stretch, Similarity::unstated());  // an uneven scale
  CHECK(poly->arcs.empty());
  REQUIRE(poly->ellipses.size() == 1);
  CHECK(Vector2d(poly->ellipses[0].u) == Vector2d(4., 0.));
  CHECK(Vector2d(poly->ellipses[0].v) == Vector2d(0., 2.));
  const Profile2d p = classifyProfile(*poly);
  for (const auto& e : p.loops[0]) {
    CHECK(e.kind == EdgeKind::Ellipse);
    CHECK(e.record == 0);
  }
  Extrusion sweep;
  sweep.height = Vector3d(0., 0., 20.);
  const auto elliptic = ofKind<EllipticCylinderSurface>(extrude(p, sweep));
  REQUIRE(elliptic.size() == 1);
  CHECK(elliptic[0]->a() == Catch::Approx(4.));
  CHECK(elliptic[0]->b() == Catch::Approx(2.));
#ifdef USINGZ
  // and through a boolean, record and names together
  Outline2d q;
  q.vertices = {{-10., -10.}, {10., -10.}, {10., 10.}, {-10., 10.}};
  auto square = std::make_shared<Polygon2d>(q);
  square->declareEdges(EdgeKind::Line);
  const auto cut = ClipperUtils::apply({square, poly}, Clipper2Lib::ClipType::Difference);
  REQUIRE(cut->ellipses.size() == 1);
  int ellipse_edges = 0;
  for (const auto& loop : classifyProfile(*cut).loops) {
    for (const auto& e : loop) ellipse_edges += e.kind == EdgeKind::Ellipse ? 1 : 0;
  }
  CHECK(ellipse_edges == 16);
#endif
}

TEST_CASE("sweep: says whether every curved edge became a surface", "[sweep-table][solid]")
{
  // What a sweep may state of its solid rests on this: a curved edge the table
  // makes no surface of leaves a wall that is neither a plane nor on a record
  // (SolidTree::faceted).
  Extrusion taper;
  taper.height = Vector3d(0., 0., 20.);
  taper.scale_x = taper.scale_y = 0.5;
  bool every = false;
  // a circle about the scaling origin tapers to the cone through its end circles
  (void)extrude(
    classifyProfile({sampled(Vector2d(0., 0.), 10., 24)}, {Arc2d{Vector2d(0., 0.), 10.}}, {}), taper,
    &every);
  CHECK(every);
  // one off it slides sideways as it shrinks - an oblique cone, none here
  (void)extrude(classifyProfile({sampled(Vector2d(3., 0.), 2., 24)}, {Arc2d{Vector2d(3., 0.), 2.}}, {}),
                taper, &every);
  CHECK_FALSE(every);
  // a record no edge is a chord of does not matter
  (void)extrude(classifyProfile({{{0., 0.}, {1., 0.}, {1., 1.}}}, {Arc2d{Vector2d(3., 0.), 2.}}, {}),
                taper, &every);
  CHECK(every);

  Revolution turn;
  (void)revolve(
    classifyProfile({sampled(Vector2d(20., 0.), 2., 24)}, {Arc2d{Vector2d(20., 0.), 2.}}, {}), turn,
    &every);
  CHECK(every);  // a torus
  // a Bezier turned about an axis makes nothing this table has
  Profile2d curved;
  Bezier2d bez;
  bez.degree = 2;
  bez.ctrl[0] = Vector2d(10., 0.);
  bez.ctrl[1] = Vector2d(14., 5.);
  bez.ctrl[2] = Vector2d(10., 10.);
  curved.beziers.push_back(bez);
  ProfileEdge chord;
  chord.a = Vector2d(10., 0.);
  chord.b = Vector2d(12., 5.);
  chord.kind = EdgeKind::Bezier;
  chord.record = 0;
  chord.declared = true;
  curved.loops.push_back({chord});
  (void)revolve(curved, turn, &every);
  CHECK_FALSE(every);
}

TEST_CASE("Profile2d: a profile states every edge only where its makers named them",
          "[sweep-table][solid]")
{
  // measured, not named
  const Profile2d measured =
    classifyProfile({sampled(Vector2d(0., 0.), 10., 12)}, {Arc2d{Vector2d(0., 0.), 10.}}, {});
  CHECK_FALSE(statesEveryEdge(measured));
  // named, every one a chord of the circle
  const VectorOfVector2d ring = sampled(Vector2d(0., 0.), 10., 12);
  const EdgeId first = reserveEdgeIds(ring.size());
  std::vector<EdgeId> ids;
  EdgeDecls decls;
  for (std::size_t i = 0; i < ring.size(); i++) {
    ids.push_back(first + EdgeId(i));
    decls[first + EdgeId(i)] = EdgeDecl{EdgeKind::Arc, 0};
  }
  CHECK(statesEveryEdge(classifyProfile({ring}, {Arc2d{Vector2d(0., 0.), 10.}}, {}, {ids}, &decls)));
  // a named curved edge with no record says it is curved and not which curve
  decls[first] = EdgeDecl{EdgeKind::Arc, -1};
  CHECK_FALSE(
    statesEveryEdge(classifyProfile({ring}, {Arc2d{Vector2d(0., 0.), 10.}}, {}, {ids}, &decls)));
}

TEST_CASE("splitting an outline keeps its edges' names", "[sweep-table][e6]")
{
  // linear_extrude splits a profile's edges where it twists, scales unevenly or
  // is asked for segments; a piece of an edge is still the edge its maker made,
  // and the walls built on the pieces are named by it (open item 40).
  Outline2d square;
  square.vertices = {{0., 0.}, {10., 0.}, {10., 10.}, {0., 10.}};
  const EdgeId first = reserveEdgeIds(4);
  square.edge_ids = {first, first + 1, first + 2, first + 3};
  const Outline2d split = CurveDiscretizer(12.0).splitOutline(square, 0.0, 1.0, 1.0, 1, 12);
  REQUIRE(split.vertices.size() > square.vertices.size());
  REQUIRE(split.hasEdgeIds());
  for (std::size_t i = 0; i < split.vertices.size(); i++) {
    // each piece lies along the edge it is named by
    const Vector2d a = split.vertices[i], b = split.vertices[(i + 1) % split.vertices.size()];
    const std::size_t k = std::size_t(split.edge_ids[i] - first);
    REQUIRE(k < 4);
    const Vector2d p0 = square.vertices[k], p1 = square.vertices[(k + 1) % 4];
    const Vector2d d = p1 - p0;
    CHECK(fabs(d[0] * (a[1] - p0[1]) - d[1] * (a[0] - p0[0])) == 0);
    CHECK(fabs(d[0] * (b[1] - p0[1]) - d[1] * (b[0] - p0[0])) == 0);
  }
  // and an outline nobody named stays unnamed, not named by accident
  Outline2d unnamed;
  unnamed.vertices = square.vertices;
  CHECK_FALSE(CurveDiscretizer(12.0).splitOutline(unnamed, 0.0, 1.0, 1.0, 1, 12).hasEdgeIds());
}

namespace {

/*! A square with a circular hole about the origin, every edge named by its
 * maker: the square's lines, the hole's chords of its arc. */
Polygon2d squareWithHole()
{
  Polygon2d poly;
  poly.arcs = {Arc2d{Vector2d(0., 0.), 2.}};
  Outline2d square;
  square.vertices = {{-5., -5.}, {5., -5.}, {5., 5.}, {-5., 5.}};
  const EdgeId lines = reserveEdgeIds(4);
  for (EdgeId i = 0; i < 4; i++) {
    square.edge_ids.push_back(lines + i);
    poly.edge_decls[lines + i] = EdgeDecl{EdgeKind::Line, -1};
  }
  Outline2d hole;
  hole.positive = false;
  VectorOfVector2d ring = sampled(Vector2d(0., 0.), 2., 16);
  std::reverse(ring.begin(), ring.end());
  hole.vertices = ring;
  const EdgeId chords = reserveEdgeIds(16);
  for (EdgeId i = 0; i < 16; i++) {
    hole.edge_ids.push_back(chords + i);
    poly.edge_decls[chords + i] = EdgeDecl{EdgeKind::Arc, 0};
  }
  poly.addOutline(square);
  poly.addOutline(hole);
  poly.setSanitized(true);
  return poly;
}

/*! linear_extrude's own steps over `polyref` - its vertices, its triangles and
 * the names it gives them - with the records extrude() declares from `poly`. */
std::unique_ptr<PolySet> extrudedAndNamed(const Polygon2d& poly, const Polygon2d& polyref, int slices,
                                          double scale)
{
  const Vector3d h1(0., 0., 0.), h2(0., 0., 5.);
  std::vector<Vector3d> vertices;
  PolygonIndices indices;
  int stride = 0;
  LinearExtrudeInternals::prepareVerticesAndIndices(polyref, h1, h2, slices, scale, scale, 0., vertices,
                                                    indices, stride);
  std::vector<Color4f> colors{Color4f()};
  std::vector<int> color_indices(indices.size(), 0);
  auto ps = LinearExtrudeInternals::assemblePolySetForManifold(
    polyref, vertices, indices, colors, color_indices, 1, boost::tribool(boost::indeterminate),
    stride * slices);
  Extrusion sweep;
  sweep.base = h1;
  sweep.height = h2 - h1;
  sweep.scale_x = sweep.scale_y = scale;
  SweptFaces faces;
  ps->surfaces = extrude(classifyProfile(poly), sweep, nullptr, &faces);
  LinearExtrudeInternals::nameExtrudedFacets(polyref, std::size_t(slices), faces, 0, *ps);
  return ps;
}

/*! Every triangle named by the record it is made on, derived here from where
 * its corners are and not from how it was laid out: the one face record all
 * three are on exactly - the plane, where they are on a plane and a curved
 * record both, which only a cap's rim triangle is - and none where they are on
 * none. Returns how many triangles carry a name. */
std::size_t checkNames(const PolySet& ps)
{
  REQUIRE(ps.surface_tags.size() == ps.indices.size());
  std::size_t named = 0;
  for (std::size_t f = 0; f < ps.indices.size(); f++) {
    std::vector<const Surface *> on, planes;
    for (const auto& record : ps.surfaces) {
      if (!record->declaresFace()) continue;
      bool all = true;
      for (const int v : ps.indices[f])
        all = all && recognition::onExactly(record.get(), ps.vertices[std::size_t(v)]);
      if (!all) continue;
      on.push_back(record.get());
      if (dynamic_cast<const PlaneSurface *>(record.get()) != nullptr) planes.push_back(record.get());
    }
    uint32_t expected = 0;
    if (planes.size() == 1) expected = planes[0]->tags.back();
    else if (on.size() == 1) expected = on[0]->tags.back();
    else REQUIRE(on.empty());
    CHECK(ps.surface_tags[f] == expected);
    if (ps.surface_tags[f] != 0) named++;
  }
  return named;
}

}  // namespace

TEST_CASE("linear_extrude names each triangle by the record it is made on", "[sweep-table][e8]")
{
  // Open item 40: a wall by the record its edge sweeps, a cap by its plane. The
  // cap's triangles along the hole have every corner on the cylinder as well,
  // which is what position alone could not tell apart.
  const Polygon2d poly = squareWithHole();
  const auto straight = extrudedAndNamed(poly, poly, 1, 1.0);
  CHECK(checkNames(*straight) == straight->indices.size());  // every triangle is made on a record

  // Tapered in three slices, the square's edges split into three pieces each:
  // the pieces keep their edges' names, and the hole's chords sweep the cone.
  Polygon2d split;
  split.addOutline(
    CurveDiscretizer(12.0).splitOutline(poly.untransformedOutlines()[0], 0.0, 0.5, 0.5, 3, 12));
  split.addOutline(poly.untransformedOutlines()[1]);
  split.setSanitized(true);
  REQUIRE(split.untransformedOutlines()[0].vertices.size() == 12);
  const auto tapered = extrudedAndNamed(poly, split, 3, 0.5);
  CHECK(checkNames(*tapered) == tapered->indices.size());
}

TEST_CASE("an edge name on two records names neither", "[sweep-table][e8]")
{
  // Two edges of different walls carrying one name: the sweep cannot say which
  // record the name means, and must not pick one.
  const VectorOfVector2d square{{0., 0.}, {10., 0.}, {10., 10.}, {0., 10.}};
  const EdgeId first = reserveEdgeIds(3);
  EdgeDecls decls;
  for (EdgeId i = 0; i < 3; i++) decls[first + i] = EdgeDecl{EdgeKind::Line, -1};
  const Profile2d profile =
    classifyProfile({square}, {}, {}, {{first, first, first + 1, first + 2}}, &decls);
  Extrusion sweep;
  sweep.height = Vector3d(0., 0., 5.);
  SweptFaces faces;
  const auto surfaces = extrude(profile, sweep, nullptr, &faces);
  REQUIRE(faces.walls.count(first) == 1);
  CHECK(faces.walls.at(first) == -1);
  REQUIRE(faces.walls.count(first + 1) == 1);
  CHECK(faces.walls.at(first + 1) >= 0);
  CHECK(faces.near_end >= 0);
  CHECK(faces.far_end >= 0);
  CHECK(faces.near_end != faces.far_end);
}

TEST_CASE("revolve says which record each edge's wall and each end face is made on", "[sweep-table][e8]")
{
  // The stepped tube (lines: a floor, walls, a taper) and a torus's chords,
  // turned a quarter: every named edge's wall is a record that holds the edge's
  // two ends turned to any angle, exactly; the start plane holds the profile at
  // the start and the end plane at the end. Derived from where the points go,
  // not from which row of the table made the record.
  const Arc2d ring{Vector2d(30., 8.), 3.};
  const Profile2d profile = declared({kSteppedTube, sampled(Vector2d(30., 8.), 3., 12)}, {ring}, {});
  Revolution sweep;
  sweep.start = 10.;
  sweep.angle = 90.;
  SweptFaces faces;
  const auto surfaces = revolve(profile, sweep, nullptr, &faces);
  auto turned = [](const Vector2d& p, double degrees) {
    const double a = degrees * M_PI / 180.0;
    return Vector3d(p[0] * cos(a), p[0] * sin(a), p[1]);
  };
  std::size_t walls = 0;
  for (const auto& loop : profile.loops) {
    for (const ProfileEdge& e : loop) {
      REQUIRE(e.id != 0);
      REQUIRE(faces.walls.count(e.id) == 1);
      const int k = faces.walls.at(e.id);
      if (k < 0) continue;  // an edge along the axis turns into nothing
      walls++;
      const Surface *record = surfaces[std::size_t(k)].get();
      CHECK(record->declaresFace());
      for (const double at : {10., 40., 100.}) {
        CHECK(recognition::onExactly(record, turned(e.a, at)));
        CHECK(recognition::onExactly(record, turned(e.b, at)));
      }
    }
  }
  CHECK(walls == profile.loops[0].size() + profile.loops[1].size());
  REQUIRE(faces.near_end >= 0);
  REQUIRE(faces.far_end >= 0);
  const Surface *start = surfaces[std::size_t(faces.near_end)].get();
  const Surface *end = surfaces[std::size_t(faces.far_end)].get();
  for (const auto& p : kSteppedTube) {
    CHECK(recognition::onExactly(start, turned(p, 10.)));
    CHECK(recognition::onExactly(end, turned(p, 100.)));
  }
  CHECK_FALSE(recognition::onExactly(start, turned(kSteppedTube[0], 100.)));

  // A full turn has no end faces.
  sweep.start = 0.;
  sweep.angle = 360.;
  SweptFaces whole;
  (void)revolve(profile, sweep, nullptr, &whole);
  CHECK(whole.near_end == -1);
  CHECK(whole.far_end == -1);
}
