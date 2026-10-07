/*! How far a solid reaches, and what a hull makes of its operands' reach -
 * every expectation read off the primitives by hand. */
#include "geometry/recognition/Support.h"

#include <catch2/catch_all.hpp>
#include <memory>

#include "geometry/Surface.h"
#include "geometry/recognition/Classify.h"

namespace {

std::shared_ptr<const SolidTree> ball(const Vector3d& c, double r)
{
  auto s = std::make_shared<DeclaredSolid>();
  s->bounds = {std::make_shared<SphereSurface>(c, Vector3d(0, 0, 1), r)};
  s->interior = c;
  s->extent = BoundingBox(c - Vector3d::Constant(r), c + Vector3d::Constant(r));
  return SolidTree::declared(s, "sphere");
}

}  // namespace

TEST_CASE("a primitive's support value is its own closed form", "[support]")
{
  double h = 0, err = 0;
  // A ball r = 1 about (3, 0, 0): 3 + 1 along +x, its radius times |n| across.
  REQUIRE(recognition::supportValue(*ball({3, 0, 0}, 1), {1, 0, 0}, h, err));
  CHECK(h == Catch::Approx(4).margin(1e-14));
  REQUIRE(recognition::supportValue(*ball({3, 0, 0}, 1), {0, 2, 0}, h, err));
  CHECK(h == Catch::Approx(2).margin(1e-14));

  // A cylinder r = 2 about z between z = 0 and 5: along (1, 0, 1) its top rim
  // reaches 5 + 2.
  auto cyl = std::make_shared<DeclaredSolid>();
  cyl->bounds = {std::make_shared<CylinderSurface>(Vector3d(0, 0, 0), Vector3d(0, 0, 1), 2),
                 std::make_shared<PlaneSurface>(Vector3d(0, 0, 0), Vector3d(0, 0, 1)),
                 std::make_shared<PlaneSurface>(Vector3d(0, 0, 5), Vector3d(0, 0, 1))};
  cyl->interior = {0, 0, 2.5};
  REQUIRE(recognition::supportValue(*SolidTree::declared(cyl), {1, 0, 1}, h, err));
  CHECK(h == Catch::Approx(7).margin(1e-14));

  // The unit cube, its two planes of each axis declared with one normal: its
  // far corner along (1, 1, 1), its near face along -x.
  auto cube = std::make_shared<DeclaredSolid>();
  for (int k = 0; k < 3; k++) {
    Vector3d e(0, 0, 0);
    e[k] = 1;
    cube->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, 0), e));
    cube->bounds.push_back(std::make_shared<PlaneSurface>(e, e));
  }
  cube->interior = {0.5, 0.5, 0.5};
  cube->extent = BoundingBox(Vector3d(0, 0, 0), Vector3d(1, 1, 1));
  REQUIRE(recognition::supportValue(*SolidTree::declared(cube), {1, 1, 1}, h, err));
  CHECK(h == Catch::Approx(3).margin(1e-14));
  REQUIRE(recognition::supportValue(*SolidTree::declared(cube), {-1, 0, 0}, h, err));
  CHECK(h == Catch::Approx(0).margin(1e-14));
}

TEST_CASE("a point on an operand is on the hull where nothing reaches further", "[support][hull]")
{
  // Two unit balls, about the origin and (3, 0, 0): their hull is a capsule.
  const auto a = ball({0, 0, 0}, 1), b = ball({3, 0, 0}, 1);
  const auto hull =
    SolidTree::enveloping(BoundingBox(Vector3d(-1, -1, -1), Vector3d(4, 1, 1)), {a, b}, "hull()");
  using recognition::Where;
  // The capsule's far end: on the hull, on the first ball.
  const auto end = recognition::classify(*hull, {-1, 0, 0});
  REQUIRE(end.where == Where::On);
  REQUIRE(end.on.size() == 1);
  // Facing the other ball: b reaches 3 + 1 along +x, past the point's 1 - buried.
  CHECK(recognition::classify(*hull, {1, 0, 0}).where == Where::In);
  // On the capsule's straight side, where both balls reach exactly as far.
  CHECK(recognition::classify(*hull, {0, 1, 0}).where == Where::On);
  CHECK(recognition::classify(*hull, {3, 0, 1}).where == Where::On);
}
