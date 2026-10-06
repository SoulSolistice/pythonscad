/*! The solid a declared sweep bounds - SweptRegion. Every expectation is read
 * off the declaration by hand, never off the code. */
#include "geometry/SweptRegion.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <string>

#include "geometry/Surface.h"

namespace {

// One turn of a square profile 2 wide, centred on radius 10, rising 8 a turn,
// about z from the origin with theta measured from x. At theta the profile's
// centre is at radius 10 and height 8 theta / 2 pi; a station's (dr, dz) is
// inside the square where both lie in [-1, 1].
std::shared_ptr<SweepSurface> square(double turns)
{
  std::string why;
  return SweepSurface::make(Vector3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0),
                            SweepLaws::constant(10, {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}), 8, turns, 0,
                            why);
}

Vector3d at(double theta_deg, double radius, double z)
{
  const double th = theta_deg * M_PI / 180;
  return {radius * cos(th), radius * sin(th), z};
}

}  // namespace

TEST_CASE("a point is inside a swept square where its station's offset is inside the square",
          "[swept-region]")
{
  const SweptRegion region(square(1));
  using W = SolidRegion::Where;
  // A quarter turn: t = 1/4, the centre at radius 10 and height 2.
  CHECK(region.classify(at(90, 10.5, 2.5)).where == W::In);
  CHECK(region.classify(at(90, 11.5, 2)).where == W::Out);  // dr = 1.5
  CHECK(region.classify(at(90, 10, 3.5)).where == W::Out);  // dz = 1.5
  // On the profile's outer edge, dr = 1: on the sweep, and only on it.
  const auto edge = region.classify(at(90, 11, 2));
  REQUIRE(edge.where == W::On);
  REQUIRE(edge.on.size() == 1);
  CHECK(edge.on.front() == &region.sweep());
  // Half a turn on, at the same angle and a pitch higher, t would be 5/4:
  // beyond the end, so nothing of this turn holds it.
  CHECK(region.classify(at(90, 10.5, 10)).where == W::Out);
}

TEST_CASE("a sweep's ends close it with the profile there", "[swept-region]")
{
  const SweptRegion region(square(1));
  using W = SolidRegion::Where;
  // theta 0, height 0.5: t = 0, inside the profile - on the first end's plane.
  const auto start = region.classify(at(0, 10.5, 0.5));
  REQUIRE(start.where == W::On);
  CHECK(start.on.front() == &region.end(false));
  // One turn later, height 8.5: t = 1, on the last end's plane.
  const auto finish = region.classify(at(0, 10.5, 8.5));
  REQUIRE(finish.where == W::On);
  CHECK(finish.on.front() == &region.end(true));
  // At t = 0 but outside the profile: off the solid, not on its end.
  CHECK(region.classify(at(0, 12, 0)).where == W::Out);
  // Half a turn into a sweep of a quarter turn: beyond its end.
  const SweptRegion quarter(square(0.25));
  CHECK(quarter.classify(at(180, 10, 4)).where == W::Out);
  CHECK(quarter.classify(at(45, 10, 1)).where == W::In);
}

TEST_CASE("a swept region moves with its sweep", "[swept-region]")
{
  const SweptRegion region(square(1));
  Transform3d shift = Transform3d::Identity();
  shift.translate(Vector3d(0, 0, 100));
  const auto moved = region.transformed(shift, Similarity::by(1.0));
  REQUIRE(moved != nullptr);
  CHECK(moved->classify(at(90, 10.5, 102.5)).where == SolidRegion::Where::In);
  CHECK(moved->classify(at(90, 10.5, 2.5)).where == SolidRegion::Where::Out);
}
