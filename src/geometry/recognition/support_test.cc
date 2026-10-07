/*! How far a solid reaches, and what a hull makes of its operands' reach -
 * every expectation read off the primitives by hand. */
#include "geometry/recognition/Support.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "geometry/Surface.h"
#include "geometry/recognition/Arrangement.h"
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

namespace {

/*! cylinder(r, h) about z from z0 less the box x < 0 - a half cylinder, as
 * step-hull-of-sectors builds its operands. */
std::shared_ptr<const SolidTree> halfCylinder(double r, double z0, double h)
{
  auto wall = std::make_shared<DeclaredSolid>();
  wall->bounds = {std::make_shared<CylinderSurface>(Vector3d(0, 0, z0), Vector3d(0, 0, 1), r),
                  std::make_shared<PlaneSurface>(Vector3d(0, 0, z0), Vector3d(0, 0, 1)),
                  std::make_shared<PlaneSurface>(Vector3d(0, 0, z0 + h), Vector3d(0, 0, 1))};
  wall->interior = {0, 0, z0 + h / 2};
  auto cut = std::make_shared<DeclaredSolid>();
  const Vector3d lo(-2 * r, -r - 1, z0 - 1), hi(0, r + 1, z0 + h + 1);
  for (int k = 0; k < 3; k++) {
    Vector3d e(0, 0, 0);
    e[k] = 1;
    cut->bounds.push_back(std::make_shared<PlaneSurface>(lo, e));
    cut->bounds.push_back(std::make_shared<PlaneSurface>(hi, e));
  }
  cut->interior = (lo + hi) / 2;
  return SolidTree::combine(SolidTree::Op::Difference, SolidTree::declared(wall),
                            SolidTree::declared(cut));
}

}  // namespace

TEST_CASE("a half cylinder's vertices are where its declarations meet on it", "[support][arrangement]")
{
  const auto half = halfCylinder(10, 0, 4);
  std::vector<const Surface *> surfaces;
  REQUIRE(recognition::leafSurfaces(*half, surfaces));
  std::vector<recognition::ArrangementPoint> points;
  REQUIRE(recognition::triplePoints(surfaces, points));
  // Of every triple point of its nine surfaces the solid holds four: the half
  // disc's two corners on each cap, where the cut x = 0 meets the rim.
  std::vector<Vector3d> on;
  for (const auto& p : points) {
    if (recognition::classifyAt(*half, p.at, p.on).where == recognition::Where::On) on.push_back(p.at);
  }
  REQUIRE(on.size() == 4);
  for (const Vector3d& want :
       {Vector3d(0, 10, 0), Vector3d(0, -10, 0), Vector3d(0, 10, 4), Vector3d(0, -10, 4)}) {
    bool found = false;
    for (const Vector3d& p : on) found = found || (p - want).norm() < 1e-12;
    CHECK(found);
  }
}

TEST_CASE("a boolean operand reaches as far as its boundary's farthest point", "[support][arrangement]")
{
  const auto half = halfCylinder(10, 0, 4);
  double h = 0, err = 0;
  REQUIRE(recognition::supportValue(*half, {1, 0, 0}, h, err));
  CHECK(h == Catch::Approx(10).margin(1e-12));  // the rim at +x
  REQUIRE(recognition::supportValue(*half, {-1, 0, 0}, h, err));
  CHECK(h == Catch::Approx(0).margin(1e-12));  // the cut
  REQUIRE(recognition::supportValue(*half, {0, 1, 1}, h, err));
  CHECK(h == Catch::Approx(14).margin(1e-12));  // the corner (0, 10, 4)
}

TEST_CASE("a hull of half cylinders decides its operands' faces", "[support][hull]")
{
  // step-hull-of-sectors: half(10, 4) raised by 2 and half(8, 4), hulled.
  const auto big = halfCylinder(10, 2, 4), small = halfCylinder(8, 0, 4);
  const auto hull = SolidTree::enveloping(BoundingBox(Vector3d(-10, -10, 0), Vector3d(10, 10, 6)),
                                          {big, small}, "hull()");
  using recognition::Where;
  CHECK(recognition::classify(*hull, {10, 0, 4}).where == Where::On);  // the big wall, exposed
  CHECK(recognition::classify(*hull, {8, 0, 1}).where == Where::In);   // the small wall, under the cone
  CHECK(recognition::classify(*hull, {4, 0, 0}).where == Where::On);   // the small half's floor
  CHECK(recognition::classify(*hull, {5, 0, 6}).where == Where::On);   // the big half's top
}

TEST_CASE("a point on the cone a hull makes is on it between the rims", "[support][hull][bridging]")
{
  // The same hull: between the big half's bottom rim (r = 10, z = 2) and the
  // small half's (r = 8, z = 0) it is the cone r = 8 + z, on the side x > 0
  // both halves keep. A point is asked as lying on that cone, the record the
  // hull's facets there are made on.
  const auto big = halfCylinder(10, 2, 4), small = halfCylinder(8, 0, 4);
  const auto hull = SolidTree::enveloping(BoundingBox(Vector3d(-10, -10, 0), Vector3d(10, 10, 6)),
                                          {big, small}, "hull()");
  const ConeSurface cone(Vector3d(0, 0, 0), Vector3d(0, 0, 1), 8, 1);
  using recognition::Where;
  const auto at = [&](const Vector3d& p) { return recognition::classifyAt(*hull, p, {&cone}).where; };
  CHECK(at({9, 0, 1}) == Where::On);  // between the rims
  // On the cone past the big rim - at 45 degrees, inside the hull's box, so
  // only the face's extent can rule it out.
  CHECK(at({11 * std::cos(M_PI / 4), 11 * std::sin(M_PI / 4), 3}) == Where::Out);
  CHECK(at({-9, 0, 1}) == Where::Out);  // on the side the halves are cut from
}
