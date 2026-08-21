// The declaration channel for geometry OpenSCAD has no primitive for.
//
// GridSurface carries the one thing a boolean destroys and the generator still
// has: the order its points were swept in. Everything here is about that being
// exact rather than fitted - membership is a position lookup, so a facet
// belongs to the sweep when all its corners are points the generator emitted,
// and a facet the boolean created does not.
//
// Why that matters is measured elsewhere: a swept quad grid straight from a
// generator puts every interior vertex at valence 6, and the same grid trimmed
// against a wall measures 36% regular on the reference part, at which point
// nothing can recover the ordering from the mesh alone.

#include "geometry/Surface.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <vector>

namespace {

/*! A helical sweep, the shape of the thread in examples/step_test. */
std::vector<Vector3d> helix(int rows, int cols, double r = 20.0, double pitch = 8.0)
{
  std::vector<Vector3d> net;
  for (int i = 0; i < rows; i++) {
    const double t = double(i) / (rows - 1);
    const double a = 2 * M_PI * 2.0 * t;
    for (int j = 0; j < cols; j++) {
      const double u = double(j) / (cols - 1);
      net.emplace_back((r + u) * cos(a), (r + u) * sin(a), pitch * 2.0 * t + u);
    }
  }
  return net;
}

}  // namespace

TEST_CASE("a declared grid accepts the points it was built from and nothing else", "[surface][grid]")
{
  const int rows = 24, cols = 4;
  GridSurface grid(rows, cols, helix(rows, cols));
  std::vector<Vector3d> scratch;

  SECTION("every point the generator emitted is a member")
  {
    int found = 0;
    for (const auto& p : grid.net) found += grid.pointMember(scratch, p);
    CHECK(found == rows * cols);
  }

  SECTION("a point the boolean would have created is not")
  {
    // The midpoint of two adjacent stations lies *on* the swept surface to well
    // within any modelling tolerance, and is still correctly refused: it is not
    // a point the generator emitted, so the facets a trim builds around it are
    // not part of the declared sweep. Refusing them is the whole point - it is
    // what keeps a cut region faceted instead of approximated.
    const Vector3d between = (grid.at(5, 0) + grid.at(6, 0)) / 2.0;
    CHECK(grid.pointMember(scratch, between) == 0);
    CHECK(grid.pointMember(scratch, Vector3d(0, 0, 0)) == 0);
  }

  SECTION("membership survives an affine move, which a cylinder would not")
  {
    // CylinderSurface::transform has to refuse a non-uniform scale, because the
    // result is elliptical and it has no way to say so. A grid is a list of
    // points and goes wherever they go.
    Transform3d mat(Transform3d::Identity());
    mat.scale(Vector3d(2.0, 0.5, 1.5));
    mat.translate(Vector3d(3, -4, 5));
    auto moved = std::dynamic_pointer_cast<GridSurface>(grid.clone());
    REQUIRE(moved != nullptr);
    REQUIRE(moved->transform(mat));
    int found = 0;
    for (const auto& p : moved->net) found += moved->pointMember(scratch, p);
    CHECK(found == rows * cols);
    // and the original is untouched, so two geometries may share a record
    CHECK(grid.pointMember(scratch, grid.at(0, 0)) == 1);
    CHECK(moved->pointMember(scratch, grid.at(0, 0)) == 0);
  }

  SECTION("sameAs distinguishes shape, size and the points themselves")
  {
    GridSurface twin(rows, cols, helix(rows, cols));
    CHECK(grid.sameAs(twin));
    GridSurface reshaped(cols, rows, helix(rows, cols));
    CHECK(!grid.sameAs(reshaped));
    GridSurface elsewhere(rows, cols, helix(rows, cols, 21.0));
    CHECK(!grid.sameAs(elsewhere));
    GridSurface closed(rows, cols, helix(rows, cols), true);
    CHECK(!grid.sameAs(closed));
  }
}
