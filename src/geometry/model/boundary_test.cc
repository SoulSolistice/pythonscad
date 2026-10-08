/*! The model's boundary evaluated from its declared tree - every expectation
 * read off the model by hand. */
#include "geometry/model/Boundary.h"

#include <catch2/catch_all.hpp>
#include <Eigen/SVD>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "geometry/SolidTree.h"
#include "geometry/Surface.h"

namespace {

using Op = SolidTree::Op;

/*! cylinder(r, h) from z0 about z, as its generator declares it. */
std::shared_ptr<const SolidTree> cylinder(double r, double z0, double h)
{
  auto s = std::make_shared<DeclaredSolid>();
  const Vector3d z(0, 0, 1);
  s->bounds.push_back(std::make_shared<CylinderSurface>(Vector3d(0, 0, z0), z, r));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, z0), z));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, z0 + h), z));
  s->interior = Vector3d(0, 0, z0 + h / 2);
  s->extent = BoundingBox(Vector3d(-r, -r, z0), Vector3d(r, r, z0 + h));
  return SolidTree::declared(s);
}

/*! translate([0, 0, 10]) rotate([30, 0, 0]) translate([-20, -20, 0])
 * cube(40), as its six planes: its bottom through (0, 0, 10) across n = (0,
 * -sin 30, cos 30), its top 40 along n, its sides at x = +-20 and 20 either
 * way along m = (0, cos 30, sin 30). */
std::shared_ptr<const SolidTree> slab()
{
  const double a = M_PI / 6;
  const Vector3d p0(0, 0, 10), n(0, -std::sin(a), std::cos(a)), m(0, std::cos(a), std::sin(a)),
    x(1, 0, 0);
  auto s = std::make_shared<DeclaredSolid>();
  s->bounds.push_back(std::make_shared<PlaneSurface>(p0, n));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(p0 + n * 40), n));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(-20, 0, 0), x));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(20, 0, 0), x));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(p0 - m * 20), m));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(p0 + m * 20), m));
  s->interior = p0 + n * 20;
  s->extent = BoundingBox(Vector3d(-60, -60, -60), Vector3d(60, 60, 60));
  return SolidTree::declared(s);
}

}  // namespace

TEST_CASE("a cylinder cut by a plane at an angle: a band, a disk and an ellipse", "[model][boundary]")
{
  // cylinder(r = 10, h = 20) less everything above the plane through (0, 0,
  // 10) tilted 30 degrees about x, z = 10 + y tan 30 - within the wall's height
  // all round. Two edges, each a whole conic closing on its own vertex: the
  // bottom circle, and the cut's ellipse with semi-axes 10 / cos 30 and 10.
  // Three faces: the wall between them, round the axis with no outer loop;
  // the bottom facing down; the cut facing up its plane's normal.
  std::string why;
  const auto boundary =
    model::evaluate(*SolidTree::combine(Op::Difference, cylinder(10, 0, 20), slab()), why);
  INFO(why);
  REQUIRE(boundary.has_value());
  REQUIRE(boundary->edges.size() == 2);
  int circles = 0, ellipses = 0;
  for (const model::Edge& e : boundary->edges) {
    CHECK(e.from == e.to);
    CHECK(e.t1 - e.t0 == Catch::Approx(2 * M_PI));
    REQUIRE(e.curve.kind == model::Curve::Kind::Conic);
    Eigen::Matrix<double, 3, 2> uv;
    uv.col(0) = e.curve.u;
    uv.col(1) = e.curve.v;
    const Eigen::Vector2d axes = Eigen::JacobiSVD<Eigen::Matrix<double, 3, 2>>(uv).singularValues();
    if (e.curve.circle) {
      circles++;
      CHECK(axes[0] == Catch::Approx(10));
      CHECK(axes[1] == Catch::Approx(10));
    } else {
      ellipses++;
      CHECK(axes[0] == Catch::Approx(10 / std::cos(M_PI / 6)));
      CHECK(axes[1] == Catch::Approx(10));
    }
  }
  CHECK(circles == 1);
  CHECK(ellipses == 1);
  REQUIRE(boundary->faces.size() == 3);
  int walls = 0, bottoms = 0, cuts = 0;
  for (const model::Face& f : boundary->faces) {
    if (dynamic_cast<const CylinderSurface *>(f.surface.get()) != nullptr) {
      walls++;
      CHECK(f.along);
      REQUIRE(f.loops.size() == 2);
      CHECK_FALSE(f.loops[0].outer);
      CHECK_FALSE(f.loops[1].outer);
      continue;
    }
    const auto *plane = dynamic_cast<const PlaneSurface *>(f.surface.get());
    REQUIRE(plane != nullptr);
    REQUIRE(f.loops.size() == 1);
    CHECK(f.loops[0].outer);
    if (plane->normdir.normalized().z() > 0.99) {
      bottoms++;
      CHECK_FALSE(f.along);  // the bottom faces down, against its plane's +z
    } else {
      cuts++;
      CHECK(f.along);  // the cut faces up its normal, (0, -sin 30, cos 30)
    }
  }
  CHECK(walls == 1);
  CHECK(bottoms == 1);
  CHECK(cuts == 1);
}
