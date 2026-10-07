/*! The solid a faceted generator's faces bound - every expectation read off a
 * unit cube by hand. */
#include "geometry/FacetedRegion.h"

#include <catch2/catch_all.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "geometry/Surface.h"

namespace {

using W = SolidRegion::Where;

/*! The unit cube [0, 1]^3 as twelve triangles wound outwards - or inwards. Its
 * four sides are named for `side` where given, its top and bottom for none. */
std::vector<FacetedRegion::Face> cube(const std::shared_ptr<const GridSurface>& side = nullptr,
                                      bool inwards = false)
{
  const Vector3d v[8] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                         {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
  // Each quad counter clockwise seen from outside, as two triangles.
  const int quads[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                           {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
  std::vector<FacetedRegion::Face> out;
  for (int q = 0; q < 6; q++) {
    const auto& k = quads[q];
    const bool wall = q >= 2;
    const auto record = wall ? side : nullptr;
    for (const auto& t : {std::array<int, 3>{k[0], k[1], k[2]}, std::array<int, 3>{k[0], k[2], k[3]}}) {
      FacetedRegion::Face f{v[t[0]], v[t[1]], v[t[2]], record};
      if (inwards) std::swap(f.b, f.c);
      out.push_back(f);
    }
  }
  return out;
}

}  // namespace

TEST_CASE("a polyhedron's faces bound its solid", "[faceted-region]")
{
  const FacetedRegion region(cube());
  REQUIRE(region.closed());
  CHECK(region.classify({0.5, 0.5, 0.5}).where == W::In);
  CHECK(region.classify({2, 0.5, 0.5}).where == W::Out);
  CHECK(region.classify({1, 0.5, 0.5}).where == W::On);
  CHECK(region.classify({1, 1, 1}).where == W::On);  // a corner
  // Wound the other way round, the same solid.
  const FacetedRegion inside_out(cube(nullptr, true));
  CHECK(inside_out.classify({0.5, 0.5, 0.5}).where == W::In);
  CHECK(inside_out.classify({2, 0.5, 0.5}).where == W::Out);
  // A face missing: no solid is bounded, and nothing is answered.
  auto open = cube();
  open.pop_back();
  CHECK_FALSE(FacetedRegion(open).closed());
}

TEST_CASE("a face named for a declared grid is that grid's", "[faceted-region]")
{
  // The cube's four sides as a grid: the unit square swept from z = 0 to 1, two
  // stations, closed round - between two stations its surface is the
  // straight lines between them, so the sides themselves.
  std::string why;
  const std::vector<std::vector<Vector3d>> rows{{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
                                                {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
  std::shared_ptr<const GridSurface> grid = GridSurface::fromRows(rows, true, why);
  REQUIRE(grid != nullptr);
  const FacetedRegion region(cube(grid));
  REQUIRE(region.closed());
  CHECK(region.classify({0.5, 0.5, 0.5}).where == W::In);
  CHECK(region.classify({1.5, 0.5, 0.5}).where == W::Out);
  const auto wall = region.classify({1, 0.5, 0.5});
  REQUIRE(wall.where == W::On);
  REQUIRE(wall.on.size() == 1);
  CHECK(wall.on.front() == grid.get());
  // On the top, a face named for none: on the solid, and on no record.
  const auto top = region.classify({0.5, 0.5, 1});
  CHECK(top.where == W::On);
  CHECK(top.on.empty());
}

TEST_CASE("near a grid face, the grid decides the side, not the face", "[faceted-region]")
{
  // A declared grid interpolates along its sweep and is straight across its
  // profile. A square profile - radius 9 to 11, height -1 to 1 - at five
  // stations 45 degrees apart round z: between two stations the outer wall's
  // chord passes 11 cos 22.5 = 10.163 from the axis, while the interpolant
  // follows the circle of radius 11. At 112.5 degrees, half way between the
  // middle stations, radius 10.6 is beyond the chord - the facets' winding
  // says Out - and between the walls the grid states, so In; only the grid can
  // say so.
  std::vector<std::vector<Vector3d>> rows;
  const double profile[4][2] = {{9, -1}, {11, -1}, {11, 1}, {9, 1}};
  for (int k = 0; k < 5; k++) {
    const double t = M_PI / 4 * k;
    rows.emplace_back();
    for (const auto& q : profile) rows.back().emplace_back(q[0] * cos(t), q[0] * sin(t), q[1]);
  }
  std::string why;
  std::shared_ptr<const GridSurface> grid = GridSurface::fromRows(rows, true, why);
  REQUIRE(grid != nullptr);
  REQUIRE(grid->tessellationBound() > 0.437);  // the point's distance past the chord
  std::vector<FacetedRegion::Face> faces;
  for (int k = 0; k + 1 < 5; k++) {
    for (int i = 0; i < 4; i++) {
      const int j = (i + 1) % 4;
      faces.push_back({rows[k][i], rows[k + 1][i], rows[k + 1][j], grid});
      faces.push_back({rows[k][i], rows[k + 1][j], rows[k][j], grid});
    }
  }
  // The two ends, each the profile square in its own plane.
  faces.push_back({rows[0][0], rows[0][1], rows[0][2], nullptr});
  faces.push_back({rows[0][0], rows[0][2], rows[0][3], nullptr});
  faces.push_back({rows[4][0], rows[4][2], rows[4][1], nullptr});
  faces.push_back({rows[4][0], rows[4][3], rows[4][2], nullptr});
  const FacetedRegion region(faces);
  REQUIRE(region.closed());
  const auto at = [](double r) {
    const double t = 112.5 * M_PI / 180;
    return Vector3d(r * cos(t), r * sin(t), 0);
  };
  CHECK(region.classify(at(10)).where == W::In);    // between the walls, far from both
  CHECK(region.classify(at(10.6)).where == W::In);  // past the chord, inside the grid
  CHECK(region.classify(at(12)).where == W::Out);   // beyond the outer wall
}

TEST_CASE("a face named for a declared cylinder is that cylinder's, where its faces stand",
          "[faceted-region]")
{
  // A hexagonal prism, z from 0 to 5, its corners on the cylinder r = 10 about
  // z and its six walls named for it; its caps its own planes. A wall's
  // chord passes 10 cos 30 = 8.660 from the axis, so the wall stands for the
  // cylinder within 10 (1 - cos 30) = 1.340 of it.
  const auto cyl = std::make_shared<const CylinderSurface>(Vector3d(0, 0, 0), Vector3d(0, 0, 1), 10);
  const auto polar = [](double r, double degrees, double z) {
    const double t = degrees * M_PI / 180;
    return Vector3d(r * cos(t), r * sin(t), z);
  };
  std::vector<FacetedRegion::Face> faces;
  for (int k = 0; k < 6; k++) {
    // The last wall closes on the first corner itself: sin(360 degrees) is not 0.
    const int next = 60 * ((k + 1) % 6);
    const Vector3d b0 = polar(10, 60 * k, 0), b1 = polar(10, next, 0);
    const Vector3d t0 = polar(10, 60 * k, 5), t1 = polar(10, next, 5);
    faces.push_back({b0, b1, t1, cyl});
    faces.push_back({b0, t1, t0, cyl});
  }
  for (int k = 1; k < 5; k++) {
    faces.push_back({polar(10, 0, 0), polar(10, 60 * (k + 1), 0), polar(10, 60 * k, 0), nullptr});
    faces.push_back({polar(10, 0, 5), polar(10, 60 * k, 5), polar(10, 60 * (k + 1), 5), nullptr});
  }
  const FacetedRegion region(faces);
  REQUIRE(region.closed());
  CHECK(region.classify({0, 0, 2.5}).where == W::In);
  CHECK(region.classify({20, 0, 2.5}).where == W::Out);
  // At 30 degrees, mid wall: 9.5 is past the chord - the faces' winding says
  // Out - and inside the cylinder.
  CHECK(region.classify(polar(9.5, 30, 2.5)).where == W::In);
  const auto wall = region.classify(polar(10, 30, 2.5));
  REQUIRE(wall.where == W::On);
  REQUIRE(wall.on.size() == 1);
  CHECK(wall.on.front() == cyl.get());
  // At 10 degrees the chord is 8.660 / cos 20 = 9.216 out: 9.5 is past it,
  // 10.05 past the cylinder.
  CHECK(region.classify(polar(9.5, 10, 2.5)).where == W::In);
  CHECK(region.classify(polar(10.05, 10, 2.5)).where == W::Out);
  // At the top, 0.84 beside the cap and inside the cylinder: the cap's plane
  // is as near as the wall's band, and which side of it the point is the
  // cylinder does not say.
  CHECK(region.classify(polar(9.5, 30, 5)).where == W::Unknown);
  // On the cylinder 0.2 above the walls: within their band, and beyond the
  // piece they stand for - its normal there meets no wall.
  CHECK(region.classify(polar(10, 10, 5.2)).where == W::Unknown);
  // A point taken onto the cylinder is on the solid where the walls stand,
  // and nowhere else on the infinite cylinder.
  const std::vector<const Surface *> on{cyl.get()};
  CHECK(region.classifyOn(polar(10, 30, 2.5), on).where == W::On);
  CHECK(region.classifyOn(polar(10, 30, 50), on).where == W::Out);
  // Moved evenly, still stated; squashed, the cylinder is elliptic and states
  // no band, and nor does the solid.
  Transform3d turn = Transform3d::Identity();
  turn.rotate(Eigen::AngleAxisd(M_PI / 4, Vector3d::UnitZ()));
  const auto turned = region.transformed(turn, Similarity::by(1.0));
  REQUIRE(turned != nullptr);
  CHECK(turned->classify(polar(9.5, 75, 2.5)).where == W::In);
  Transform3d squash = Transform3d::Identity();
  squash.scale(Vector3d(2, 1, 1));
  CHECK(region.transformed(squash, scaleSimilarity(Vector3d(2, 1, 1))) == nullptr);
}
