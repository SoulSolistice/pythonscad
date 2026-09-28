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

namespace {

/*! A 2 x 1 rectangle whose bottom side is drawn as two edges meeting at (1, 0):
 * collinear, so Clipper drops that vertex and makes one edge of the two. Every
 * edge is named; the two halves of the bottom are declared `left` and `right`.
 * Drawn clockwise, Clipper turns it over, and the run lies the other way
 * between the merged edge's neighbours. */
Polygon2d splitBottom(EdgeDecl left, EdgeDecl right, bool clockwise = false)
{
  Outline2d o;
  if (!clockwise) {
    for (const auto& p :
         {Vector2d(0, 0), Vector2d(1, 0), Vector2d(2, 0), Vector2d(2, 1), Vector2d(0, 1)}) {
      o.vertices.push_back(p);
    }
  } else {
    for (const auto& p :
         {Vector2d(0, 1), Vector2d(2, 1), Vector2d(2, 0), Vector2d(1, 0), Vector2d(0, 0)}) {
      o.vertices.push_back(p);
    }
  }
  const EdgeId first = reserveEdgeIds(5);
  for (int i = 0; i < 5; i++) o.edge_ids.push_back(first + EdgeId(i));
  Polygon2d poly(o);
  for (int i = 0; i < 5; i++) poly.edge_decls[first + EdgeId(i)] = EdgeDecl{EdgeKind::Line, -1};
  // edge i runs from vertex i to vertex i + 1
  poly.edge_decls[first + EdgeId(clockwise ? 3 : 0)] = left;
  poly.edge_decls[first + EdgeId(clockwise ? 2 : 1)] = right;
  return poly;
}

/*! The declaration of the result's edge from (0, 0) to (2, 0), or none. */
const EdgeDecl *bottomOf(const Polygon2d& poly)
{
  for (const auto& outline : poly.untransformedOutlines()) {
    const std::size_t n = outline.vertices.size();
    for (std::size_t i = 0; i < n; i++) {
      const Vector2d a = outline.vertices[i], b = outline.vertices[(i + 1) % n];
      const bool bottom = a[1] == 0 && b[1] == 0 && std::fabs(a[0] - b[0]) == 2;
      if (!bottom) continue;
      if (outline.edge_ids.size() != n || outline.edge_ids[i] == 0) return nullptr;
      const auto it = poly.edge_decls.find(outline.edge_ids[i]);
      return it == poly.edge_decls.end() ? nullptr : &it->second;
    }
  }
  return nullptr;
}

}  // namespace

TEST_CASE("an edge Clipper merges from one maker's run is what the run is", "[clipper][e6]")
{
  // Until 2026-09-28 the merged edge was unnamed, and the profile classifier
  // measured what it was: a Bezier's chord became a line wherever the grid put
  // an end outside the control box (checklist E6).
  const std::unique_ptr<Polygon2d> lines =
    ClipperUtils::sanitize(splitBottom(EdgeDecl{EdgeKind::Line, -1}, EdgeDecl{EdgeKind::Line, -1}));
  REQUIRE(lines->untransformedOutlines().size() == 1);
  REQUIRE(lines->untransformedOutlines()[0].vertices.size() == 4);  // the vertex at (1, 0) is gone
  const EdgeDecl *merged = bottomOf(*lines);
  REQUIRE(merged != nullptr);
  CHECK(merged->kind == EdgeKind::Line);

  const std::unique_ptr<Polygon2d> turned = ClipperUtils::sanitize(
    splitBottom(EdgeDecl{EdgeKind::Line, -1}, EdgeDecl{EdgeKind::Line, -1}, /* clockwise */ true));
  REQUIRE(turned->untransformedOutlines().size() == 1);
  REQUIRE(turned->untransformedOutlines()[0].vertices.size() == 4);
  const EdgeDecl *turned_merged = bottomOf(*turned);
  REQUIRE(turned_merged != nullptr);
  CHECK(turned_merged->kind == EdgeKind::Line);
}

TEST_CASE("an edge Clipper merges from a run of two declarations stays unnamed", "[clipper][e6]")
{
  // The makers disagree about what the run is, so nothing is derived.
  const std::unique_ptr<Polygon2d> mixed =
    ClipperUtils::sanitize(splitBottom(EdgeDecl{EdgeKind::Line, -1}, EdgeDecl{EdgeKind::Arc, -1}));
  REQUIRE(mixed->untransformedOutlines().size() == 1);
  REQUIRE(mixed->untransformedOutlines()[0].vertices.size() == 4);
  CHECK(bottomOf(*mixed) == nullptr);

  const std::unique_ptr<Polygon2d> turned = ClipperUtils::sanitize(
    splitBottom(EdgeDecl{EdgeKind::Line, -1}, EdgeDecl{EdgeKind::Arc, -1}, /* clockwise */ true));
  REQUIRE(turned->untransformedOutlines()[0].vertices.size() == 4);
  CHECK(bottomOf(*turned) == nullptr);
}
