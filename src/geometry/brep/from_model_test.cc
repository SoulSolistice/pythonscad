/*! The B-rep evaluated from the model's declarations - every expectation read
 * off examples/step_test/phantom-sliver.scad by hand. */
#include "geometry/brep/FromModel.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "geometry/SolidTree.h"
#include "geometry/Surface.h"

namespace {

using Op = SolidTree::Op;

/*! cylinder(r, h) from z0 about z, as its generator declares it: the wall and
 * both caps; and, for a slice of `angle` degrees, its two flat sides. */
std::shared_ptr<const SolidTree> cylinder(double r, double z0, double h, double angle = 360)
{
  auto s = std::make_shared<DeclaredSolid>();
  const Vector3d z(0, 0, 1);
  s->bounds.push_back(std::make_shared<CylinderSurface>(Vector3d(0, 0, z0), z, r));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, z0), z));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, z0 + h), z));
  s->interior = Vector3d(0, 0, z0 + h / 2);
  if (angle < 360) {
    const double a = angle * M_PI / 180;
    s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, 0), Vector3d(0, 1, 0)));
    s->bounds.push_back(
      std::make_shared<PlaneSurface>(Vector3d(0, 0, 0), Vector3d(-std::sin(a), std::cos(a), 0)));
    s->interior = Vector3d(r / 2 * std::cos(a / 2), r / 2 * std::sin(a / 2), z0 + h / 2);
  }
  s->extent = BoundingBox(Vector3d(-r, -r, z0), Vector3d(r, r, z0 + h));
  return SolidTree::declared(s);
}

bool on(const brep::Face& f, const Surface& s)
{
  return f.surface != nullptr && f.surface->sameAs(s);
}

}  // namespace

TEST_CASE("the phantom coupon's B-rep is the model's, not the mesh's", "[brep][from-model]")
{
  // A body r = 20, z 0 to 20, less a bore r = 15, less a quarter slot from
  // z = 10 up, r 14.9 to 17. The slot's inner wall is inside the bore's air,
  // so the model has no face on it: eight faces - the outer wall, the bore,
  // two caps, and the slot's floor, outer wall and two flat sides.
  const auto body = cylinder(20, 0, 20);
  const auto bore = cylinder(15, -1, 22);
  const auto slot = SolidTree::combine(Op::Difference, cylinder(17, 10, 11, 90), cylinder(14.9, 9, 13));
  const auto tree =
    SolidTree::combine(Op::Difference, SolidTree::combine(Op::Difference, body, bore), slot);
  std::string why;
  const auto brep = brep::fromModel(*tree, why);
  INFO(why);
  REQUIRE(brep.has_value());
  CHECK(brep::check(*brep).empty());
  CHECK(brep->shells.size() == 1);
  CHECK(brep->faces.size() == 8);
  const CylinderSurface cutter(Vector3d(0, 0, 9), Vector3d(0, 0, 1), 14.9);
  const CylinderSurface bore_wall(Vector3d(0, 0, -1), Vector3d(0, 0, 1), 15);
  const PlaneSurface top(Vector3d(0, 0, 20), Vector3d(0, 0, 1)),
    bottom(Vector3d(0, 0, 0), Vector3d(0, 0, 1));
  int caps = 0;
  for (const brep::Face& f : brep->faces) {
    CHECK_FALSE(on(f, cutter));
    // The caps face out of the solid: the top along its plane's +z, the
    // bottom against it.
    if (on(f, top)) {
      CHECK(f.same_sense);
      caps++;
    }
    if (on(f, bottom)) {
      CHECK_FALSE(f.same_sense);
      caps++;
    }
    // The bore runs all the way round below the slot and is notched by it
    // above: two loops, each round the axis, neither outer.
    if (on(f, bore_wall)) {
      REQUIRE(f.loops.size() == 2);
      CHECK_FALSE(f.loops[0].outer);
      CHECK_FALSE(f.loops[1].outer);
    }
  }
  CHECK(caps == 2);
  // A cutter that is not inside the bore's air - r = 15.5 - has a face: the
  // slot's inner wall, nine faces.
  const auto deeper = SolidTree::combine(
    Op::Difference, SolidTree::combine(Op::Difference, body, bore),
    SolidTree::combine(Op::Difference, cylinder(17, 10, 11, 90), cylinder(15.5, 9, 13)));
  const auto with_wall = brep::fromModel(*deeper, why);
  REQUIRE(with_wall.has_value());
  CHECK(with_wall->faces.size() == 9);
}

TEST_CASE("two declared planes at one height are one level of the grid", "[brep][from-model]")
{
  // A ring r 5 to 10, z 0 to 10, and on it a ring r 5 to 7, z 10 to 20: the
  // first's top and the second's bottom are one plane, z = 10. Six faces:
  // the outer walls r = 10 and r = 7, the bore r = 5 all the way up - one
  // cylinder, declared by both - the bottom, the step at z = 10 from r 7 to
  // 10, and the top.
  const auto lower = SolidTree::combine(Op::Difference, cylinder(10, 0, 10), cylinder(5, -1, 12));
  const auto upper = SolidTree::combine(Op::Difference, cylinder(7, 10, 10), cylinder(5, 9, 12));
  std::string why;
  const auto brep = brep::fromModel(*SolidTree::combine(Op::Union, lower, upper), why);
  INFO(why);
  REQUIRE(brep.has_value());
  CHECK(brep::check(*brep).empty());
  CHECK(brep->faces.size() == 6);
}
