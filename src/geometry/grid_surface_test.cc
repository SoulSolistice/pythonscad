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

  SECTION("a point the boolean created, but on the sweep, is a member too")
  {
    // The declared points alone are not enough, and this is the case that says
    // so. A boolean which trims the sweep gives its facets new corners; those
    // corners still lie on the swept surface, and refusing them left 237 of 300
    // facets unclaimed on a ridge cut at its base. So membership is by the
    // surface, and a point the generator never emitted but which lies on the
    // sweep is a member.
    const Vector3d on = grid.evaluate(0.371, 0.5);
    CHECK(grid.pointMember(scratch, on) == 1);

    // A chord midpoint *is* a member, and deliberately so. The mesh is the
    // tessellation of this surface, so a boolean cutting it produces vertices
    // on the chords; refusing them is what left 63 facets of 300 claimed.
    // Membership is therefore within the grid's own tessellation band - the
    // widest the interpolant stands off its own chords - which is exactly the
    // resolution the model was built at and no looser.
    const Vector3d chord = (grid.at(5, 0) + grid.at(6, 0)) / 2.0;
    CHECK(grid.pointMember(scratch, chord) == 1);
    CHECK(grid.tessellationBand() > 0);

    // Ten bands off the surface is not a member, so the tolerance is a bound
    // and not a licence.
    double u = 0, v = 0;
    REQUIRE(grid.project(on, u, v));
    const Vector3d du = grid.evaluate(std::min(1.0, u + 1e-4), v) - grid.evaluate(u, v);
    const Vector3d dv = grid.evaluate(u, std::min(1.0, v + 1e-4)) - grid.evaluate(u, v);
    const Vector3d away = du.cross(dv).normalized() * (10 * grid.tessellationBand());
    CHECK(grid.pointMember(scratch, on + away) == 0);
    CHECK(grid.pointMember(scratch, Vector3d(0, 0, 0)) == 0);
  }

  SECTION("the interpolant passes through the stations it was built from")
  {
    // A cubic B-spline fitted to the columns, so this is interpolation and not
    // approximation: the declared points have to come back exactly, or the
    // surface describes something other than what was swept.
    double worst = 0;
    for (int i = 0; i < rows; i++) {
      for (int j = 0; j < cols; j++) {
        double u = 0, v = 0;
        REQUIRE(grid.project(grid.at(i, j), u, v));
        worst = std::max(worst, (grid.evaluate(u, v) - grid.at(i, j)).norm());
      }
    }
    INFO("worst station deviation " << worst);
    CHECK(worst < 1e-9);
  }

  SECTION("projection finds the near turn of a helix, not a neighbouring one")
  {
    // A helix passes close to itself once per pitch, so a projection which
    // starts from the middle of the parameter square converges onto the wrong
    // turn and reports a point as on the surface when it is a pitch away. The
    // coarse sample before Newton is what prevents that.
    const Vector3d target = grid.evaluate(0.5, 0.0);
    double u = 0, v = 0;
    REQUIRE(grid.project(target, u, v));
    CHECK(u == Catch::Approx(0.5).margin(0.02));
    CHECK((grid.evaluate(u, v) - target).norm() < 1e-9);
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
