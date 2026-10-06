// The conic a plane cuts from a quadric (io/step/PlaneSections), against the
// section worked out by hand: the cone x^2 + y^2 = z^2 cut by a plane parallel
// to one of its generators is a parabola, by one a hair off it a hyperbola,
// and by one across its axis a circle. What is asserted is what a file relies
// on: that the curve written is the section, through the section's own points.
#include "geometry/brep/PlaneSections.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>

#include "geometry/Surface.h"
#include "geometry/brep/Brep.h"
#include "geometry/recognition/Exact.h"

using namespace brep;

namespace {

/*! The cone x^2 + y^2 = z^2, z > 0: radius 1 at height 1, rising one in one. */
std::shared_ptr<ConeSurface> cone45()
{
  return std::make_shared<ConeSurface>(Vector3d(0, 0, 1), Vector3d(0, 0, 1), 1.0, 1.0);
}

/*! The conic as a writer builds it from a section, as promoteCurves does. */
brep::CurveForm written(const SectionConic& sec)
{
  using Kind = brep::Conic::Kind;
  const Kind kind = sec.kind == SectionConic::ELLIPSE     ? Kind::Ellipse
                    : sec.kind == SectionConic::HYPERBOLA ? Kind::Hyperbola
                                                          : Kind::Parabola;
  return brep::Conic{kind, {sec.centre, sec.normal.normalized(), sec.major}, sec.a, sec.b};
}

}  // namespace

TEST_CASE("a plane parallel to a cone's generator cuts the parabola worked out by hand",
          "[planesections]")
{
  // z = x + 2 on x^2 + y^2 = z^2: y^2 = 4x + 4, so x = y^2 / 4 - 1. In the plane,
  // along (1, 0, 1) / sqrt 2 from the vertex (-1, 0, 1), u = sqrt 2 (x + 1) =
  // y^2 / (2 sqrt 2) = y^2 / (4 f): focal distance f = sqrt 2 / 2. The writer
  // had a quarter of it, so every point but the vertex was off the curve
  // (open item 39, R30).
  SectionConic sec;
  REQUIRE(planeSectionConic(cone45().get(), Vector3d(1, 0, -1), Vector3d(0, 0, 2), sec));
  REQUIRE(sec.kind == SectionConic::PARABOLA);
  CHECK((sec.centre - Vector3d(-1, 0, 1)).norm() < 1e-12);
  CHECK(std::fabs(sec.a - std::sqrt(2.0) / 2) < 1e-12);
  CHECK(std::fabs(std::fabs(sec.major.normalized().dot(Vector3d(1, 0, 1).normalized())) - 1) < 1e-12);
  // and the section's own points are on the curve as written
  const brep::CurveForm curve = written(sec);
  for (const double y : {-3.0, -1.0, 0.5, 2.0}) {
    const double x = y * y / 4 - 1;
    CHECK(brep::distanceToCurve(curve, Vector3d(x, y, x + 2)) < 1e-12);
  }
}

TEST_CASE("a plane a hair off a cone's generator cuts no parabola", "[planesections]")
{
  // Tilted 1e-10 from the generator's plane: det is about -5e-11, far outside
  // the arithmetic's own error of about 1e-16 - a hyperbola, which the chosen
  // 1e-9 of the scale squared called a parabola and wrote with a quarter of
  // even that focal distance. Its centre lies some 1e10 away, so declining it is
  // as honest an answer as the hyperbola; a parabola is not one.
  const double t = M_PI / 4 + 1e-10;
  SectionConic sec;
  if (planeSectionConic(cone45().get(), Vector3d(std::sin(t), 0, -std::cos(t)), Vector3d(0, 0, 2),
                        sec)) {
    CHECK(sec.kind == SectionConic::HYPERBOLA);
  }
}

TEST_CASE("a plane across a cone's axis cuts its circle", "[planesections]")
{
  SectionConic sec;
  REQUIRE(planeSectionConic(cone45().get(), Vector3d(0, 0, 1), Vector3d(0, 0, 3), sec));
  REQUIRE(sec.kind == SectionConic::ELLIPSE);
  CHECK(std::fabs(sec.a - 3) < 1e-12);
  CHECK(std::fabs(sec.b - 3) < 1e-12);
  CHECK((sec.centre - Vector3d(0, 0, 3)).norm() < 1e-12);
  CHECK(brep::distanceToCurve(written(sec), Vector3d(0, 3, 3)) < 1e-12);
}

TEST_CASE("a boundary's planar stretch holds only what is on its plane", "[planesections][R2]")
{
  // Six vertices of a cylinder of radius 100 cut by the plane z = 0.3 x, and
  // two more off it closing the cycle: one stretch of all six. With the middle
  // one moved 1e-10 off the plane - far below the 1e-9 of the cycle's size the
  // stretches were grouped at, far above the arithmetic's bound - no stretch
  // holds all six (open item 39, R2).
  std::vector<Vector3d> vertices;
  for (int k = 0; k < 6; k++) {
    const double a = k * M_PI / 10;
    vertices.emplace_back(100 * cos(a), 100 * sin(a), 0.3 * 100 * cos(a));
  }
  vertices.emplace_back(100 * cos(M_PI / 2), 100 * sin(M_PI / 2), 60);
  vertices.emplace_back(100.0, 0.0, 60);
  const std::vector<int> cycle{0, 1, 2, 3, 4, 5, 6, 7};
  const Vector3d axis(0, 0, 1);
  auto longest = [&](const std::vector<Vector3d>& at) {
    std::size_t most = 0;
    for (const auto& st : brep::coplanarStretches(cycle, at, axis, 0.0)) most = std::max(most, st.count);
    return most;
  };
  CHECK(longest(vertices) == 6);
  const Vector3d normal = Vector3d(-0.3, 0, 1).normalized();
  std::vector<Vector3d> moved = vertices;
  moved[3] += normal * 1e-10;
  CHECK(longest(moved) < 6);

  // A plane along the axis cuts a cylinder in rulings, no closed curve, and is
  // not a stretch; one across it is the circle the arc pass writes.
  std::vector<Vector3d> wall{{100, 0, 0},  {100, 0, 10}, {100, 0, 20},
                             {100, 0, 30}, {50, 50, 30}, {50, 50, 0}};
  CHECK(brep::coplanarStretches({0, 1, 2, 3, 4, 5}, wall, axis, 0.0).empty());
}

TEST_CASE("two quadrics cross in planes exactly where their pencil holds a pair of them",
          "[planesections][R7]")
{
  // Two cylinders of radius 10 along z and along y, x^2 + y^2 = 100 and
  // x^2 + z^2 = 100: their difference y^2 - z^2 is the planes z = y and
  // z = -y, and each cuts either cylinder in the same ellipse (open item 39, R7).
  const CylinderSurface along_z(Vector3d::Zero(), Vector3d(0, 0, 1), 10);
  const CylinderSurface along_y(Vector3d::Zero(), Vector3d(0, 1, 0), 10);
  const auto planes = pencilPlanes(&along_z, &along_y);
  REQUIRE(planes.size() == 2);
  for (const auto& pl : planes) {
    CHECK((recognition::parallel(pl.second, Vector3d(0, 1, 1)) ||
           recognition::parallel(pl.second, Vector3d(0, 1, -1))));
    CHECK(recognition::onPlane(pl.first, pl.second, Vector3d::Zero()));
    // the crossing's own points lie on one of the two
    CHECK(recognition::onPlane(pl.first, pl.second, Vector3d(10, 0, 0)));
  }
  CHECK_FALSE(
    recognition::samePlane(planes[0].first, planes[0].second, planes[1].first, planes[1].second));

  // Unequal radii cross in a quartic that splits into nothing.
  const CylinderSurface thinner(Vector3d::Zero(), Vector3d(0, 1, 0), 6);
  CHECK(pencilPlanes(&along_z, &thinner).empty());
  // Equal radii whose axes miss each other: no planes either.
  const CylinderSurface aside(Vector3d(3, 0, 0), Vector3d(0, 1, 0), 10);
  CHECK(pencilPlanes(&along_z, &aside).empty());

  // A sphere of radius 10 and a coaxial cylinder of radius 6 meet in two
  // circles, at z = 8 and z = -8: the planes across the axis.
  const SphereSurface sphere(Vector3d::Zero(), Vector3d(0, 0, 1), 10);
  const CylinderSurface bore(Vector3d::Zero(), Vector3d(0, 0, 1), 6);
  const auto rings = pencilPlanes(&sphere, &bore);
  REQUIRE(rings.size() == 2);
  for (const auto& pl : rings) CHECK(recognition::parallel(pl.second, Vector3d(0, 0, 1)));
  const bool upper = recognition::onPlane(rings[0].first, rings[0].second, Vector3d(6, 0, 8));
  CHECK(
    recognition::onPlane(rings[upper ? 1 : 0].first, rings[upper ? 1 : 0].second, Vector3d(6, 0, -8)));
  CHECK((upper || recognition::onPlane(rings[1].first, rings[1].second, Vector3d(6, 0, 8))));

  // A cone so nearly a cylinder that its apex is 1e5 away - radius 10 at z = 0,
  // widening 1e-4 a unit - and a coaxial cylinder of radius 10.0065 meet in the
  // circle at z = 65. Their pencil is ill conditioned, and its plane missed
  // that circle's own points; the pair is told by its axis instead, which says
  // circles whatever the slope.
  const ConeSurface taper(Vector3d::Zero(), Vector3d(0, 0, 1), 10.0, 1e-4);
  const CylinderSurface wall(Vector3d::Zero(), Vector3d(0, 0, 1), 10.0065);
  CHECK(coaxialRevolution(&taper, &wall));
  CHECK(coaxialRevolution(&sphere, &bore));
  CHECK_FALSE(coaxialRevolution(&along_z, &along_y));
  const CylinderSurface beside(Vector3d(1e-6, 0, 0), Vector3d(0, 0, 1), 6);
  CHECK_FALSE(coaxialRevolution(&sphere, &beside));
}
