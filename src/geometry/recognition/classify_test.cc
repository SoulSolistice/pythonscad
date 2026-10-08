// Where a point is with respect to the model's solid - see Classify.h. Every
// expectation here is read off the model by hand; none is captured from a run.
#include "geometry/recognition/Classify.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "geometry/FacetedRegion.h"
#include "geometry/HullOfRevolution.h"
#include "geometry/SolidTree.h"
#include "geometry/Surface.h"

using recognition::classify;
using recognition::Where;
using Op = SolidTree::Op;

namespace {

/*! cylinder(r1, r2, h) about z from z0, as its generator declares it: the wall
 * (a cylinder, or the cone through both rims) and a cap wherever the radius is
 * not zero, with its axis at mid height inside. */
std::shared_ptr<const SolidTree> cylinder(double r1, double r2, double z0, double h)
{
  auto s = std::make_shared<DeclaredSolid>();
  const Vector3d z(0, 0, 1);
  if (r1 == r2) {
    s->bounds.push_back(std::make_shared<CylinderSurface>(Vector3d(0, 0, z0), z, r1));
  } else {
    const bool at_bottom = r1 >= r2;
    s->bounds.push_back(std::make_shared<ConeSurface>(Vector3d(0, 0, at_bottom ? z0 : z0 + h), z,
                                                      at_bottom ? r1 : r2, (r2 - r1) / h));
  }
  if (r1 > 0) s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, z0), z));
  if (r2 > 0) s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, z0 + h), z));
  s->interior = Vector3d(0, 0, z0 + h / 2);
  return SolidTree::declared(s);
}

std::shared_ptr<const SolidTree> box(const Vector3d& lo, const Vector3d& hi)
{
  auto s = std::make_shared<DeclaredSolid>();
  for (int i = 0; i < 3; i++) {
    Vector3d n(0, 0, 0);
    n[i] = 1;
    s->bounds.push_back(std::make_shared<PlaneSurface>(lo, n));
    s->bounds.push_back(std::make_shared<PlaneSurface>(hi, n));
  }
  s->interior = (lo + hi) / 2;
  return SolidTree::declared(s);
}

Where at(const std::shared_ptr<const SolidTree>& t, double x, double y, double z)
{
  return classify(*t, Vector3d(x, y, z)).where;
}

}  // namespace

TEST_CASE("a bored cylinder: in the wall, out in the bore, on either wall", "[classify]")
{
  // cylinder(r = 10, h = 20) less cylinder(r = 4) through it
  const auto part = SolidTree::combine(Op::Difference, cylinder(10, 10, 0, 20), cylinder(4, 4, -1, 22));
  CHECK(at(part, 7, 0, 10) == Where::In);
  CHECK(at(part, 2, 0, 10) == Where::Out);   // in the bore
  CHECK(at(part, 12, 0, 10) == Where::Out);  // outside the wall
  CHECK(at(part, 7, 0, 25) == Where::Out);   // above the top cap
  CHECK(at(part, 10, 0, 10) == Where::On);   // on the outer wall
  CHECK(at(part, 4, 0, 10) == Where::On);    // on the bore's wall
  CHECK(at(part, 7, 0, 20) == Where::On);    // on the top cap
  // On a bound exactly, at machine precision: a point on the wall at an angle
  // whose coordinates are not exact still lies on it within the forward error.
  CHECK(at(part, 10 * std::cos(0.7), 10 * std::sin(0.7), 3) == Where::On);
}

TEST_CASE("a subtracted operand decides past an opaque one - lid10's phantom", "[classify]")
{
  // A block less a socket, less a cutter whose inner wall - 0.1 inside the
  // socket - declares nothing. A point on that inner wall is inside the socket,
  // so it is out of the part whatever the cutter is: the model has no face
  // there, however the chords the mesh was cut with fell.
  const auto part =
    SolidTree::combine(Op::Difference,
                       SolidTree::combine(Op::Difference, box({-100, -100, 0}, {100, 100, 95}),
                                          cylinder(78.3073, 78.3073, -0.1, 95.2)),
                       SolidTree::opaque());
  CHECK(at(part, 78.2073, 0, 88) == Where::Out);
  // and the reason given is the socket that holds the point
  const auto socket = part->children()[1];
  CHECK(classify(*part, Vector3d(78.2073, 0, 88)).because == socket.get());
  // where the opaque cutter leaves it open, the cutter is named
  CHECK(classify(*part, Vector3d(90, 0, 50)).because == part->children()[2].get());
  // Where the socket does not reach, the opaque cutter leaves it unknown.
  CHECK(at(part, 90, 0, 50) == Where::Unknown);
  // And outside the block nothing is needed of the cutter either.
  CHECK(at(part, 150, 0, 50) == Where::Out);
}

TEST_CASE("a cone to an apex is its own nappe, not both", "[classify]")
{
  // cylinder(r1 = 10, r2 = 0, h = 10): the cone and its base cap. Past the apex
  // is the other nappe of the same cone, and not the solid.
  const auto cone = cylinder(10, 0, 0, 10);
  CHECK(at(cone, 0, 0, 5) == Where::In);
  CHECK(at(cone, 0, 0, 15) == Where::Out);
  CHECK(at(cone, 1, 0, 15) == Where::Out);
  CHECK(at(cone, 2, 0, -1) == Where::Out);
  CHECK(at(cone, 5, 0, 5) == Where::On);  // r = 10 - z: on the cone
}

TEST_CASE("unions and intersections, and where two faces meet it says so", "[classify]")
{
  const auto a = box({0, 0, 0}, {2, 2, 2}), b = box({1, 0, 0}, {3, 2, 2});
  const auto u = SolidTree::combine(Op::Union, a, b);
  CHECK(at(u, 1.5, 1, 1) == Where::In);
  CHECK(at(u, 2.5, 1, 1) == Where::In);
  CHECK(at(u, 4, 1, 1) == Where::Out);
  CHECK(at(u, 3, 1, 1) == Where::On);    // only b's face
  CHECK(at(u, 1.5, 0, 1) == Where::On);  // both on y = 0: the half-spaces say the union's face
  const auto x = SolidTree::combine(Op::Intersection, a, b);
  CHECK(at(x, 1.5, 1, 1) == Where::In);
  CHECK(at(x, 0.5, 1, 1) == Where::Out);
  CHECK(at(x, 1, 1, 1) == Where::On);  // b's face x = 1, inside a
}

TEST_CASE("a mirrored or sheared solid keeps its inside", "[classify]")
{
  Transform3d mirror = Transform3d::Identity();
  mirror.linear()(0, 0) = -1;
  const auto m = cylinder(10, 0, 0, 10)->transformed(mirror, Similarity::by(1.0, true));
  CHECK(at(m, 0, 0, 5) == Where::In);
  CHECK(at(m, 0, 0, 15) == Where::Out);
  Transform3d shear = Transform3d::Identity();
  shear.linear()(0, 2) = 1;
  const auto s = box({0, 0, 0}, {1, 1, 1})->transformed(shear, Similarity::unstated());
  // x' = x + z: a point is in where x' - z is in (0, 1)
  CHECK(at(s, 1.5, 0.5, 0.9) == Where::In);   // x = 0.6
  CHECK(at(s, 0.5, 0.5, 0.9) == Where::Out);  // x = -0.4, though inside the unsheared box
  CHECK(at(s, 1.5, 0.5, 1) == Where::On);     // x = 0.5 on the top face z = 1
}

TEST_CASE("an envelope rules a point out, and nothing in", "[classify][envelope]")
{
  auto box = std::make_shared<DeclaredSolid>();
  for (int i = 0; i < 3; i++) {
    Vector3d n(0, 0, 0);
    n[i] = 1;
    box->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, 0), n));
    box->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(2, 2, 2), n));
  }
  box->interior = Vector3d(1, 1, 1);
  const auto thread = SolidTree::enveloped(box);
  CHECK(at(thread, 5, 1, 1) == Where::Out);
  CHECK(at(thread, 1, 1, 1) == Where::Unknown);
  CHECK(at(thread, 2, 1, 1) == Where::Unknown);
  // lid10's root: a part united with a thread that states only where it is.
  const auto part = SolidTree::combine(Op::Union, cylinder(10, 10, 0, 20), thread);
  CHECK(at(part, 7, 0, 10) == Where::In);     // in the part
  CHECK(at(part, 10, 0, 10) == Where::On);    // on the part's wall, away from the thread
  CHECK(at(part, 1, 1, 1) == Where::In);      // in the part, whatever the thread
  CHECK(at(part, -15, 0, 10) == Where::Out);  // in neither
  // and a faceted leaf never resolved says nothing
  CHECK(at(SolidTree::faceted(), 0, 0, 0) == Where::Unknown);
}

TEST_CASE("a hull of coaxial solids of revolution is declared by what its profile turns",
          "[classify][hull]")
{
  // hull() of cylinder(r = 10) over z 0..5 and cylinder(r = 6) over z 5..8: the
  // profile's hull is (10, 0), (10, 5), (6, 8) and the axis, so the solid is the
  // r = 10 wall, a cone from r 10 at z 5 to r 6 at z 8, and the two caps - at
  // z = 6 the cone is at r = 10 - 4/3 = 8.667.
  const Vector3d z(0, 0, 1);
  std::vector<std::vector<std::shared_ptr<Surface>>> operands = {
    {std::make_shared<CylinderSurface>(Vector3d(0, 0, 0), z, 10),
     std::make_shared<PlaneSurface>(Vector3d(0, 0, 0), z),
     std::make_shared<PlaneSurface>(Vector3d(0, 0, 5), z)},
    {std::make_shared<CylinderSurface>(Vector3d(0, 0, 5), z, 6),
     std::make_shared<PlaneSurface>(Vector3d(0, 0, 5), z),
     std::make_shared<PlaneSurface>(Vector3d(0, 0, 8), z)}};
  const auto solid = solidOfHullOfRevolution(operands);
  REQUIRE(solid != nullptr);
  const auto hull = SolidTree::declared(solid, "hull()");
  CHECK(at(hull, 0, 0, 4) == Where::In);
  CHECK(at(hull, 8, 0, 6) == Where::In);
  CHECK(at(hull, 9, 0, 6) == Where::Out);   // beyond the cone
  CHECK(at(hull, 10, 0, 2) == Where::On);   // on the wall
  CHECK(at(hull, 5, 0, 9) == Where::Out);   // above the top cap
  CHECK(at(hull, 0, 0, -1) == Where::Out);  // below the bottom
  CHECK(at(hull, 8, 0, 6.5) == Where::On);  // on the cone: r = 10 - (z - 5) 4/3 = 8

  // Not coaxial: no solid is claimed.
  operands[1][0] = std::make_shared<CylinderSurface>(Vector3d(1, 0, 5), z, 6);
  CHECK(solidOfHullOfRevolution(operands) == nullptr);
}

TEST_CASE("a hull holds each of its operands, whatever they are", "[classify][hull]")
{
  // hull() of two unit cubes at x 0..1 and x 3..4, whose hull is the 4 x 1 x 1
  // box, stated as nothing but the box of its operands' boxes and the operands
  // themselves. Inside an operand is inside the hull; outside the box is out;
  // between the two cubes the model says in, but the leaf cannot, and says
  // nothing. On an operand's face means nothing either: x = 1 is inside the
  // hull.
  const auto a = box(Vector3d(0, 0, 0), Vector3d(1, 1, 1));
  const auto b = box(Vector3d(3, 0, 0), Vector3d(4, 1, 1));
  const auto hull =
    SolidTree::enveloping(BoundingBox(Vector3d(0, 0, 0), Vector3d(4, 1, 1)), {a, b}, "hull()");
  REQUIRE(hull->op() == Op::Enveloped);
  CHECK(at(hull, 0.5, 0.5, 0.5) == Where::In);
  CHECK(at(hull, 3.5, 0.5, 0.5) == Where::In);
  CHECK(at(hull, 2, 0.5, 0.5) == Where::Unknown);
  CHECK(at(hull, 1, 0.5, 0.5) == Where::Unknown);
  CHECK(at(hull, 5, 0.5, 0.5) == Where::Out);
  CHECK(hull->leaves() == 1);
  CHECK(hull->declaredLeaves() == 0);

  // and it moves with its operands
  const auto moved = hull->transformed(Transform3d(Eigen::Translation3d(0, 0, 10)), Similarity::by(1.0));
  CHECK(at(moved, 0.5, 0.5, 10.5) == Where::In);
  CHECK(at(moved, 0.5, 0.5, 0.5) == Where::Out);

  // A subtracted hull takes out what its operands hold.
  const auto slab = box(Vector3d(-1, -1, -1), Vector3d(5, 2, 2));
  const auto cut = SolidTree::combine(Op::Difference, slab, hull);
  CHECK(at(cut, 0.5, 0.5, 0.5) == Where::Out);
  CHECK(at(cut, -0.5, 0.5, 0.5) == Where::In);
}

TEST_CASE("on two operands at once, the half-spaces each keeps decide", "[classify][neighbourhood]")
{
  // A cube less a cube sharing its face x = 1 - the eps = 0 cut: at (1, 0.5,
  // 0.5) both are on the plane, the first keeping x < 1, the second x > 1, so
  // the difference keeps x < 1 and the point is on its face.
  const auto a = box({0, 0, 0}, {1, 1, 1}), b = box({1, 0, 0}, {2, 1, 1});
  const auto d = SolidTree::combine(Op::Difference, a, b);
  const auto face = classify(*d, Vector3d(1, 0.5, 0.5));
  CHECK(face.where == Where::On);
  CHECK_FALSE(face.on.empty());
  // Two halves of one cube joined across x = 1: the seam is inside the union.
  const auto whole = SolidTree::combine(Op::Union, a, b);
  CHECK(at(whole, 1, 0.5, 0.5) == Where::In);
  // The same two halves, intersected: they share only the plane, and a
  // boolean of solids is regularized - what has no inside is no solid - so
  // there is nothing there, and the point is out.
  const auto touch = SolidTree::combine(Op::Intersection, a, b);
  CHECK(at(touch, 1, 0.5, 0.5) == Where::Out);

  // A half cylinder: r = 10 about z, 0 <= z <= 4, less the box x < 0. Where the
  // wall meets the cut, at (0, 10, 2), the wall keeps r < 10 and the box x < 0,
  // so the result keeps r < 10, x > 0 - an edge of it, on both surfaces.
  const auto half =
    SolidTree::combine(Op::Difference, cylinder(10, 10, 0, 4), box({-20, -11, -1}, {0, 11, 5}));
  const auto edge = classify(*half, Vector3d(0, 10, 2));
  CHECK(edge.where == Where::On);
  CHECK(edge.on.size() == 2);
  // On the cut plane inside the wall: the cut's face.
  CHECK(at(half, 0, 5, 2) == Where::On);
  // On the wall's far side from the cut: the wall alone, as before.
  CHECK(at(half, 10, 0, 2) == Where::On);
  // On the wall where the box removed it: outside.
  CHECK(at(half, -10, 0, 2) == Where::Out);
}

TEST_CASE("a curved bound tangent to a plane is decided where the cells disagree",
          "[classify][neighbourhood]")
{
  // A cylinder r = 10 about z less the box x < 0, |y| <= 10: the box's face
  // y = 10 touches the wall along x = 0, y = 10, where the box's face x = 0
  // meets it too. There the result - the half cylinder - keeps x > 0 inside
  // the wall: the cells disagree, and the point is on it.
  const auto half =
    SolidTree::combine(Op::Difference, cylinder(10, 10, 0, 4), box({-20, -10, -1}, {0, 10, 5}));
  CHECK(at(half, 0, 10, 2) == Where::On);
  // A box whose face y = 10 touches a cylinder's wall, less the cylinder:
  // between the face and the wall a cusp of the box survives, touching the
  // line of contact. Every cell says out; a cusp they cannot see says on, so
  // the answer is not theirs to give.
  const auto cusp =
    SolidTree::combine(Op::Difference, box({-5, 0, 0}, {5, 10, 4}), cylinder(10, 10, 0, 4));
  CHECK(at(cusp, 0, 10, 2) == Where::Unknown);
}

namespace {

/*! A prism over a polygon counter clockwise in xy, star shaped from its first
 * corner, z from 0 to `top`, as a faceted region: its walls named for `wall`,
 * its caps its own planes. */
std::shared_ptr<const SolidTree> prism(const std::vector<Vector3d>& polygon, double top,
                                       const std::shared_ptr<const Surface>& wall = nullptr)
{
  const Vector3d up(0, 0, top);
  const std::size_t n = polygon.size();
  std::vector<FacetedRegion::Face> faces;
  for (std::size_t i = 0; i < n; i++) {
    const Vector3d& b0 = polygon[i];
    const Vector3d& b1 = polygon[(i + 1) % n];
    faces.push_back({b0, b1, b1 + up, wall});
    faces.push_back({b0, b1 + up, b0 + up, wall});
  }
  for (std::size_t k = 1; k + 1 < n; k++) {
    faces.push_back({polygon[0], polygon[k + 1], polygon[k], nullptr});
    faces.push_back({polygon[0] + up, polygon[k] + up, polygon[k + 1] + up, nullptr});
  }
  return SolidTree::region(std::make_shared<FacetedRegion>(std::move(faces)), "prism");
}

}  // namespace

TEST_CASE("a hull states its supporting plane at a point on an operand's face", "[classify][hull]")
{
  // hull() of two unit spheres at x = 0 and x = 4: a capsule. (0, 0, 1) is
  // on the first's top, and no operand reaches above z = 1, so it is on the
  // hull - which holds the sphere, so is smooth there, normal z, the side it
  // keeps below.
  const auto sphere = [](const Vector3d& c) {
    auto s = std::make_shared<DeclaredSolid>();
    s->bounds.push_back(std::make_shared<SphereSurface>(c, Vector3d(0, 0, 1), 1.0));
    s->interior = c;
    return SolidTree::declared(s);
  };
  const auto capsule = SolidTree::enveloping(BoundingBox(Vector3d(-1, -1, -1), Vector3d(5, 1, 1)),
                                             {sphere({0, 0, 0}), sphere({4, 0, 0})}, "hull()");
  const auto top = classify(*capsule, Vector3d(0, 0, 1));
  REQUIRE(top.where == Where::On);
  REQUIRE(top.sides.size() == 1);
  CHECK((top.sides.front().outward - Vector3d(0, 0, 1)).norm() < 1e-12);
  CHECK(top.all);
  // A box resting on it, [-1, 5] x [-3, 3] x [1, 3], touches the curved top
  // along a line: between them a cusp of neither reaches the point - (0,
  // 0.1, 0.999) is below the box and outside the capsule - so the union does
  // not hold it, and the tangency is not the half-spaces' to decide.
  const auto lid = box({-1, -3, 1}, {5, 3, 3});
  CHECK(at(SolidTree::combine(Op::Union, capsule, lid), 0, 0, 1) == Where::Unknown);

  // A hull with a flat top: two unit cubes at x 0..1 and 3..4, stating their
  // extent, whose hull is [0, 4] x [0, 1]^2. On the first's top, (0.5, 0.5, 1),
  // the hull keeps z < 1; the box resting on it z > 1, the same plane: united,
  // every direction is in one, and the point is in; the box less the hull is
  // on its own face.
  const auto cube = [](const Vector3d& lo) {
    auto s = std::make_shared<DeclaredSolid>();
    for (int i = 0; i < 3; i++) {
      Vector3d n(0, 0, 0);
      n[i] = 1;
      s->bounds.push_back(std::make_shared<PlaneSurface>(lo, n));
      s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(lo + Vector3d(1, 1, 1)), n));
    }
    s->interior = lo + Vector3d(0.5, 0.5, 0.5);
    s->extent = BoundingBox(lo, Vector3d(lo + Vector3d(1, 1, 1)));
    return SolidTree::declared(s);
  };
  const auto bar = SolidTree::enveloping(BoundingBox(Vector3d(0, 0, 0), Vector3d(4, 1, 1)),
                                         {cube({0, 0, 0}), cube({3, 0, 0})}, "hull()");
  const auto flat = classify(*bar, Vector3d(0.5, 0.5, 1));
  REQUIRE(flat.where == Where::On);
  REQUIRE(flat.sides.size() == 1);
  CHECK((flat.sides.front().outward - Vector3d(0, 0, 1)).norm() < 1e-12);
  CHECK(at(SolidTree::combine(Op::Union, bar, lid), 0.5, 0.5, 1) == Where::In);
  CHECK(at(SolidTree::combine(Op::Difference, lid, bar), 0.5, 0.5, 1) == Where::On);
}

TEST_CASE("a region states the side it keeps at a point on two operands", "[classify][neighbourhood]")
{
  // An L, [0, 2] x [0, 1] and [0, 1] x [0, 2], z from 0 to 1, and the box
  // [1, 2]^2 that fills its notch: at the L's concave edge (1, 1) both are on
  // the planes x = 1 and y = 1. The L keeps any of x < 1, y < 1; the box both
  // of x > 1, y > 1; every direction is in one of them, and the union - the
  // square [0, 2]^2 - holds the point.
  const auto ell = prism({{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}, {1, 2, 0}, {0, 2, 0}}, 1);
  const auto square = SolidTree::combine(Op::Union, ell, box({1, 1, 0}, {2, 2, 1}));
  CHECK(at(square, 1, 1, 0.5) == Where::In);
  // The L less the box: nothing of the box is the L's, and the edge is the
  // L's own.
  const auto same = SolidTree::combine(Op::Difference, ell, box({1, 1, 0}, {2, 2, 1}));
  CHECK(at(same, 1, 1, 0.5) == Where::On);

  // A hexagonal prism, 0 <= z <= 5, its walls named for the cylinder r = 10 -
  // the solid that cylinder bounds, by the model's word - taken from that
  // declared cylinder: nothing is left. Mid wall, at 30 degrees, both are on
  // one cylinder, the same surface twice, not two tangent to each other: the
  // point is out.
  const auto wall = std::make_shared<const CylinderSurface>(Vector3d(0, 0, 0), Vector3d(0, 0, 1), 10);
  std::vector<Vector3d> hexagon;
  for (int k = 0; k < 6; k++) {
    const double t = k * M_PI / 3;
    hexagon.emplace_back(10 * std::cos(t), 10 * std::sin(t), 0);
  }
  const auto hex = prism(hexagon, 5, wall);
  const double t = M_PI / 6;
  const Vector3d mid(10 * std::cos(t), 10 * std::sin(t), 2.5);
  const auto nothing = SolidTree::combine(Op::Difference, cylinder(10, 10, 0, 5), hex);
  CHECK(classify(*nothing, mid).where == Where::Out);
  const auto both = SolidTree::combine(Op::Union, cylinder(10, 10, 0, 5), hex);
  CHECK(classify(*both, mid).where == Where::On);
}

TEST_CASE("at a point on two surfaces, the quadrants say which way the solid is",
          "[classify][quadrants]")
{
  // The unit cube's edge between x = 1 and z = 1, at (1, 0.5, 1): only the
  // quadrant against both normals goes in - an edge, each face on the side
  // of the other surface's normal it points against.
  const auto cube = box({0, 0, 0}, {1, 1, 1});
  const PlaneSurface x1(Vector3d(1, 0, 0), Vector3d(1, 0, 0)), z1(Vector3d(0, 0, 1), Vector3d(0, 0, 1));
  bool in[2][2];
  REQUIRE(recognition::quadrants(*cube, {1, 0.5, 1}, &x1, &z1, in));
  CHECK(in[0][0]);
  CHECK_FALSE(in[0][1]);
  CHECK_FALSE(in[1][0]);
  CHECK_FALSE(in[1][1]);
  // A second cube on top, [0, 1]^2 x [1, 2]: z = 1 runs through the union's
  // side, and parts nothing - every direction against x's normal goes in.
  const auto tower = SolidTree::combine(Op::Union, cube, box({0, 0, 1}, {1, 1, 2}));
  REQUIRE(recognition::quadrants(*tower, {1, 0.5, 1}, &x1, &z1, in));
  CHECK(in[0][0]);
  CHECK(in[0][1]);
  CHECK_FALSE(in[1][0]);
  CHECK_FALSE(in[1][1]);
  // A cylinder r = 10, z 0 to 20, at its rim (10, 0, 20): the wall's normal is
  // +x there, the top's +z; only the quadrant against both goes in.
  const auto drum = cylinder(10, 10, 0, 20);
  const CylinderSurface wall(Vector3d(0, 0, 0), Vector3d(0, 0, 1), 10);
  const PlaneSurface top(Vector3d(0, 0, 20), Vector3d(0, 0, 1));
  REQUIRE(recognition::quadrants(*drum, {10, 0, 20}, &wall, &top, in));
  CHECK(in[0][0]);
  CHECK_FALSE(in[0][1]);
  CHECK_FALSE(in[1][0]);
  CHECK_FALSE(in[1][1]);
}
