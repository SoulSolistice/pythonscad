// What Clipper's integer grid does to the points a model states, and what
// Polygon2d::restoreGivenPoints gives back.
#include <catch2/catch_all.hpp>
#include <cmath>
#include <cstddef>
#include <memory>

#include "geometry/ClipperUtils.h"
#include "geometry/Polygon2d.h"
#include "geometry/linalg.h"

namespace {

/*! A circle written out by hand, as step-declare.scad writes it: nothing
 * upstream knows it is a circle, and every point is on radius `r` to the last
 * bit the formula allows. */
Polygon2d handWrittenCircle(double r, int n)
{
  Outline2d o;
  for (int i = 0; i < n; i++) {
    const double a = 2 * M_PI * i / n;
    o.vertices.emplace_back(r * std::cos(a), r * std::sin(a));
  }
  return Polygon2d(o);
}

}  // namespace

TEST_CASE("sanitizing a polygon keeps the points it was given", "[clipper][d0]")
{
  // Every leaf polygon passes through ClipperUtils::sanitize, which rounds each
  // coordinate to a grid 2^-27 wide. Until 2026-09-28 the points came back
  // rounded: 10 cos 11.25 degrees arrived 6e-10 off, and a declare_cylinder()
  // over the polygon was compared with the grid, not with the model.
  const Polygon2d given = handWrittenCircle(10.0, 32);
  const std::unique_ptr<Polygon2d> sanitized = ClipperUtils::sanitize(given);

  REQUIRE(sanitized->untransformedOutlines().size() == 1);
  const auto& in = given.untransformedOutlines()[0].vertices;
  const auto& out = sanitized->untransformedOutlines()[0].vertices;
  REQUIRE(out.size() == in.size());
  std::size_t found = 0;
  for (const Vector2d& v : out) {
    for (const Vector2d& w : in) found += v == w ? 1 : 0;
  }
  CHECK(found == in.size());
}

TEST_CASE("a point Clipper makes where two edges cross stays on the grid", "[clipper]")
{
  // Restoration is by the cell a given point rounds to, so a crossing - which
  // no given point rounds to - is left where the grid put it. A bowtie is one
  // outline whose edges cross once; sanitized, it is two triangles meeting at
  // that crossing, each with two corners it was given.
  Outline2d o;
  for (const auto& p : {Vector2d(0, 0), Vector2d(1.1, 0), Vector2d(0, 1.3), Vector2d(1.1, 1.3)}) {
    o.vertices.push_back(p);
  }
  const Polygon2d bowtie(o);
  const std::unique_ptr<Polygon2d> sanitized = ClipperUtils::sanitize(bowtie);

  REQUIRE(sanitized->untransformedOutlines().size() == 2);
  for (const auto& outline : sanitized->untransformedOutlines()) {
    REQUIRE(outline.vertices.size() == 3);
    std::size_t given = 0;
    for (const Vector2d& v : outline.vertices) {
      for (const Vector2d& w : o.vertices) given += v == w ? 1 : 0;
    }
    CHECK(given == 2);
  }
}
