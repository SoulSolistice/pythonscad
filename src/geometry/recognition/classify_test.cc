// Where a point is with respect to the model's solid - see Classify.h. Every
// expectation here is read off the model by hand; none is captured from a run.
#include "geometry/recognition/Classify.h"

#include <catch2/catch_all.hpp>
#include <memory>

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
  CHECK(at(u, 3, 1, 1) == Where::On);         // only b's face
  CHECK(at(u, 1.5, 0, 1) == Where::Unknown);  // both on y = 0: needs the half-spaces
  const auto x = SolidTree::combine(Op::Intersection, a, b);
  CHECK(at(x, 1.5, 1, 1) == Where::In);
  CHECK(at(x, 0.5, 1, 1) == Where::Out);
  CHECK(at(x, 1, 1, 1) == Where::On);  // b's face x = 1, inside a
}

TEST_CASE("a mirrored or sheared solid keeps its inside", "[classify]")
{
  Transform3d mirror = Transform3d::Identity();
  mirror.linear()(0, 0) = -1;
  const auto m = cylinder(10, 0, 0, 10)->transformed(mirror);
  CHECK(at(m, 0, 0, 5) == Where::In);
  CHECK(at(m, 0, 0, 15) == Where::Out);
  Transform3d shear = Transform3d::Identity();
  shear.linear()(0, 2) = 1;
  const auto s = box({0, 0, 0}, {1, 1, 1})->transformed(shear);
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
