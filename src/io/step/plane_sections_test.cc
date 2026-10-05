// The conic a plane cuts from a quadric (io/step/PlaneSections), against the
// section worked out by hand: the cone x^2 + y^2 = z^2 cut by a plane parallel
// to one of its generators is a parabola, by one a hair off it a hyperbola,
// and by one across its axis a circle. What is asserted is what a file relies
// on: that the curve written is the section, through the section's own points.
#include "io/step/PlaneSections.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>

#include "geometry/Surface.h"
#include "geometry/brep/Brep.h"

using namespace step;

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
