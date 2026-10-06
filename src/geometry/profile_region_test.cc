/*! The exact region a profile declares, and the solids it sweeps - every
 * expectation read off the geometry by hand. */
#include "geometry/ProfileRegion.h"
#include "geometry/ProfileSolids.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "geometry/Surface.h"

namespace {

using W = ProfileRegion::Where;

ProfileEdge line(Vector2d a, Vector2d b)
{
  ProfileEdge e;
  e.a = a;
  e.b = b;
  e.kind = EdgeKind::Line;
  e.declared = true;
  return e;
}

/*! A circle of radius r about c as n chords of its record, counter clockwise
 * from angle 0 - or clockwise, a hole. */
std::vector<ProfileEdge> circle(Vector2d c, double r, int n, int record, bool hole)
{
  std::vector<ProfileEdge> out;
  for (int k = 0; k < n; k++) {
    const double t0 = 2 * M_PI * k / n, t1 = 2 * M_PI * (k + 1) / n;
    ProfileEdge e;
    e.a = c + r * Vector2d(cos(t0), sin(t0));
    e.b = c + r * Vector2d(cos(t1), sin(t1));
    if (hole) std::swap(e.a, e.b);
    e.kind = EdgeKind::Arc;
    e.record = record;
    e.declared = true;
    out.push_back(e);
  }
  if (hole) std::reverse(out.begin(), out.end());
  return out;
}

Arc2d arc(Vector2d c, double r)
{
  Arc2d a;
  a.centre = c;
  a.r = r;
  return a;
}

Vector2d polar(double r, double deg)
{
  return r * Vector2d(cos(deg * M_PI / 180), sin(deg * M_PI / 180));
}

}  // namespace

TEST_CASE("a profile of straight edges is its polygon", "[profile-region]")
{
  Profile2d p;
  p.loops = {{line({0, 0}, {2, 0}), line({2, 0}, {2, 2}), line({2, 2}, {0, 2}), line({0, 2}, {0, 0})}};
  const ProfileRegion region(p);
  REQUIRE(region.exact());
  CHECK(region.classify({1, 1}, 0).where == W::In);
  CHECK(region.classify({3, 1}, 0).where == W::Out);
  const auto on = region.classify({2, 1}, 0);
  CHECK(on.where == W::On);
  CHECK(on.edge == 1);
}

TEST_CASE("a chord stands for its arc: the disc, not the octagon", "[profile-region]")
{
  // Eight chords of a circle r = 10: a chord's middle is 10 cos 22.5 = 9.2388
  // from the centre, so 9.5 on the bisector is outside the octagon and inside
  // the circle - in the region the profile declares.
  Profile2d disc;
  disc.arcs = {arc({0, 0}, 10)};
  disc.loops = {circle({0, 0}, 10, 8, 0, false)};
  const ProfileRegion region(disc);
  REQUIRE(region.exact());
  CHECK(region.classify(polar(9.5, 22.5), 0).where == W::In);
  CHECK(region.classify(polar(10, 22.5), 0).where == W::On);  // on the arc
  CHECK(region.classify(polar(10.1, 22.5), 0).where == W::Out);
  // The chord's own middle is no boundary: the arc bulges out, so it is inside.
  CHECK(region.classify(polar(10 * cos(M_PI / 8), 22.5), 0).where == W::In);
}

TEST_CASE("a hole's chords stand for its arcs too", "[profile-region]")
{
  // The same circle as a hole in a square: 9.5 on the bisector is inside the
  // hole's circle, so outside the region, though inside the octagon's square.
  Profile2d holed;
  holed.arcs = {arc({0, 0}, 10)};
  holed.loops = {{line({-20, -20}, {20, -20}), line({20, -20}, {20, 20}), line({20, 20}, {-20, 20}),
                  line({-20, 20}, {-20, -20})},
                 circle({0, 0}, 10, 8, 0, true)};
  const ProfileRegion region(holed);
  REQUIRE(region.exact());
  CHECK(region.classify(polar(9.5, 22.5), 0).where == W::Out);
  CHECK(region.classify(polar(10.5, 22.5), 0).where == W::In);
  CHECK(region.classify(polar(10, 22.5), 0).where == W::On);
  // A hole's chord bulges in: its middle is outside the region.
  CHECK(region.classify(polar(10 * cos(M_PI / 8), 22.5), 0).where == W::Out);
}

TEST_CASE("a profile turned about the axis is a torus where the profile is a circle", "[profile-region]")
{
  // A circle r = 5 about (20, 0), in (radius, height), turned a quarter turn
  // from +x: (24, 0) is inside the profile.
  Profile2d p;
  p.arcs = {arc({20, 0}, 5)};
  p.loops = {circle({20, 0}, 5, 16, 0, false)};
  auto region = std::make_shared<const ProfileRegion>(p);
  SweptRecords records;
  records.walls = {std::vector<std::shared_ptr<const Surface>>(16)};
  auto wall = std::make_shared<TorusSurface>(Vector3d(0, 0, 0), Vector3d(0, 0, 1), 20, 5);
  for (auto& w : records.walls[0]) w = wall;
  records.near_end = std::make_shared<PlaneSurface>(Vector3d(0, 0, 0), Vector3d(0, 1, 0));
  records.far_end = std::make_shared<PlaneSurface>(Vector3d(0, 0, 0), Vector3d(1, 0, 0));
  const RevolvedRegion quarter(region, 0, 90, records);
  using S = SolidRegion::Where;
  const auto at = [](double r, double deg, double z) {
    return Vector3d(r * cos(deg * M_PI / 180), r * sin(deg * M_PI / 180), z);
  };
  CHECK(quarter.classify(at(24, 45, 0)).where == S::In);
  CHECK(quarter.classify(at(24, 135, 0)).where == S::Out);  // beyond the quarter
  CHECK(quarter.classify(at(26, 45, 0)).where == S::Out);
  const auto on = quarter.classify(at(25, 45, 0));
  REQUIRE(on.where == S::On);
  CHECK(on.on.front() == wall.get());
  // At the start's half-plane, inside the profile: on the near end.
  const auto start = quarter.classify(at(24, 0, 0));
  REQUIRE(start.where == S::On);
  CHECK(start.on.front() == records.near_end.get());
  // Moved with the model: carried back through its frame.
  Transform3d up = Transform3d::Identity();
  up.translate(Vector3d(0, 0, 50));
  const auto moved = quarter.transformed(up, Similarity::by(1.0));
  REQUIRE(moved != nullptr);
  CHECK(moved->classify(at(24, 45, 50)).where == S::In);
  CHECK(moved->classify(at(24, 45, 0)).where == S::Out);
}

TEST_CASE("a profile carried along a line, tapering to half", "[profile-region]")
{
  // The square [0, 2] carried 10 up z, scaled to half at the top: at height 5
  // it is [0, 1.5], at 10 [0, 1].
  Profile2d p;
  p.loops = {{line({0, 0}, {2, 0}), line({2, 0}, {2, 2}), line({2, 2}, {0, 2}), line({0, 2}, {0, 0})}};
  auto region = std::make_shared<const ProfileRegion>(p);
  Transform3d frame = Transform3d::Identity();
  frame.linear().col(2) = Vector3d(0, 0, 10);
  const ExtrudedRegion taper(region, 0.5, 0.5, SweptRecords{}, frame);
  using S = SolidRegion::Where;
  CHECK(taper.classify({1, 1, 5}).where == S::In);
  CHECK(taper.classify({1.5, 1, 5}).where == S::On);  // the far wall at height 5
  CHECK(taper.classify({1.6, 1, 5}).where == S::Out);
  CHECK(taper.classify({1, 1, 11}).where == S::Out);
  CHECK(taper.classify({0.5, 0.5, 10}).where == S::On);  // on the top
  CHECK(taper.classify({1.5, 1.5, 10}).where == S::Out);
}
