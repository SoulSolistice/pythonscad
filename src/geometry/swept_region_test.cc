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

namespace {

/*! Whether `a` states exactly these outward normals, each given unnormalised,
 * in any order. */
bool states(const SolidRegion::Answer& a, const std::vector<Vector3d>& normals)
{
  if (a.sides.size() != normals.size()) return false;
  for (const Vector3d& n : normals) {
    bool found = false;
    for (const auto& side : a.sides) found = found || (side.outward - n.normalized()).norm() < 1e-12;
    if (!found) return false;
  }
  return true;
}

}  // namespace

TEST_CASE("a sweep states its side: the closed form's normal, its corners and its ends",
          "[swept-region][sides]")
{
  // P = e(theta) (10 + dr) + z (8 t + dz), theta = 2 pi t, e' = z x e. At a
  // quarter turn e = y and e' = -x, so along the sweep P_t = -x 2 pi (10 + dr)
  // + 8 z. On the outer wall, dr = 1, P_s runs along z, and the normal is e:
  // y. On the bottom, dz = -1, P_s runs along e, and P_t x P_s is
  // -2 (8, 0, 2 pi (10 + dr)) - down, leaning with the helix.
  const auto sweep = square(1);
  const SweptRegion region(sweep);
  using W = SolidRegion::Where;
  const auto wall = region.classify(at(90, 11, 2));
  REQUIRE(wall.where == W::On);
  CHECK(states(wall, {{0, 1, 0}}));
  CHECK(wall.sides.front().bound == sweep.get());
  const auto bottom = region.classify(at(90, 10, 1));
  REQUIRE(bottom.where == W::On);
  CHECK(states(bottom, {{-8, 0, -20 * M_PI}}));
  // Their corner, the profile's convex one: both, the solid in each.
  const auto corner = region.classify(at(90, 11, 1));
  REQUIRE(corner.where == W::On);
  CHECK(states(corner, {{0, 1, 0}, {-8, 0, -22 * M_PI}}));
  CHECK(corner.all);
  // The start, at theta = 0: its plane, outward against the sweep's way -
  // -y; on its rim, the outer wall's x as well, the end cutting the sweep.
  const auto start = region.classify(at(0, 10.5, 0.2));
  REQUIRE(start.where == W::On);
  CHECK(states(start, {{0, -1, 0}}));
  const auto rim = region.classify(at(0, 11, 0.2));
  REQUIRE(rim.where == W::On);
  CHECK(states(rim, {{1, 0, 0}, {0, -1, 0}}));
  CHECK(rim.all);

  // A radius that grows to 12 at half a turn and back: a key of its law,
  // where the sweep is creased along t, and no one side is stated.
  SweepLaws laws;
  laws.radius_at = {0, 0.5, 1};
  laws.radius = {10, 12, 10};
  laws.profile_at = {0};
  laws.profile = {{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
  std::string why;
  const auto bulge =
    SweepSurface::make(Vector3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0), laws, 8, 1, 0, why);
  REQUIRE(bulge != nullptr);
  const SweptRegion bulged(bulge);
  const auto crease = bulged.classify(at(180, 13, 4));
  REQUIRE(crease.where == W::On);
  CHECK(crease.sides.empty());

  // The same square wound the other way round: its outer wall is still out.
  const auto reversed =
    SweepSurface::make(Vector3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0),
                       SweepLaws::constant(10, {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}}), 8, 1, 0, why);
  REQUIRE(reversed != nullptr);
  CHECK(states(SweptRegion(reversed).classify(at(90, 11, 2)), {{0, 1, 0}}));

  // A radius growing 10 to 12 over the turn: at its end, t = 1 and theta =
  // 2 pi, e = x and e' = y, and the sweep there is the span before it - the
  // radius moving 2 per unit t. On the outer wall P_t = (2, 26 pi, 8) and P_s
  // = (0, 0, 2): the normal is (13 pi, -1, 0); the end's plane, outward along
  // the sweep's way, y.
  SweepLaws growing;
  growing.radius_at = {0, 1};
  growing.radius = {10, 12};
  growing.profile_at = {0};
  growing.profile = {{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
  const auto widening =
    SweepSurface::make(Vector3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0), growing, 8, 1, 0, why);
  REQUIRE(widening != nullptr);
  const auto last_rim = SweptRegion(widening).classify(Vector3d(13, 0, 8.2));
  REQUIRE(last_rim.where == W::On);
  CHECK(states(last_rim, {{13 * M_PI, -1, 0}, {0, 1, 0}}));
  CHECK(last_rim.all);
}
