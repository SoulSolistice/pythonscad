// What a surface says about itself: the protocol every kind answers for itself
// (Surface.h), and the elliptic cylinder, which answers it in closed form too.
#include "geometry/Surface.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "geometry/recognition/Seam.h"

namespace {

Transform3d scaling(double sx, double sy, double sz)
{
  Transform3d m = Transform3d::Identity();
  m.scale(Vector3d(sx, sy, sz));
  return m;
}

}  // namespace

TEST_CASE("elliptic cylinder: held as its section across the sweep", "[surface]")
{
  // declared from a section that is not across the sweep: the sweep leans in x
  const Vector3d d = Vector3d(1., 0., 1.).normalized();
  const EllipticCylinderSurface e(Vector3d::Zero(), d, Vector3d(3., 0., 0.), Vector3d(0., 2., 0.));
  CHECK(e.major.dot(d) == Catch::Approx(0.).margin(1e-15));
  CHECK(e.minor.dot(d) == Catch::Approx(0.).margin(1e-15));
  CHECK(e.major.dot(e.minor) == Catch::Approx(0.).margin(1e-15));
  CHECK(e.a() >= e.b());
  // the x semi-diameter slides onto the plane across the sweep: 3 cos 45 = 2.12,
  // now the major; the y one is already across it
  CHECK(e.a() == Catch::Approx(3. / sqrt(2.)));
  CHECK(e.b() == Catch::Approx(2.));
}

TEST_CASE("elliptic cylinder: membership in closed form, at machine precision", "[surface]")
{
  const EllipticCylinderSurface e(Vector3d(1., 2., 3.), Vector3d(0., 0., 1.), Vector3d(10., 0., 0.),
                                  Vector3d(0., 5., 0.));
  for (int k = 0; k < 16; k++) {
    const double t = 2 * M_PI * k / 16;
    const Vector3d p = Vector3d(1. + 10. * cos(t), 2. + 5. * sin(t), 3. + 7.5);
    CHECK(e.residual(p).exact());
  }
  CHECK_FALSE(e.residual(Vector3d(1. + 10. + 1e-9, 2., 0.)).exact());
  // the offset is a distance: 0.5 outside the major vertex
  CHECK(e.offset(Vector3d(11.5, 2., 0.)) == Catch::Approx(0.5).epsilon(0.05));
}

TEST_CASE("cylinder: an uneven scale makes it elliptic, not nothing", "[surface]")
{
  auto cyl = std::make_shared<CylinderSurface>(Vector3d::Zero(), Vector3d(0., 0., 1.), 5.);
  cyl->owners = {7};
  const auto moved = cyl->transformed(scaling(2., 1., 1.));
  REQUIRE(moved != nullptr);
  const auto *e = dynamic_cast<const EllipticCylinderSurface *>(moved.get());
  REQUIRE(e != nullptr);
  CHECK(e->a() == Catch::Approx(10.));
  CHECK(e->b() == Catch::Approx(5.));
  CHECK(e->owners == std::vector<int32_t>{7});
  // a similarity keeps it circular
  const auto even = cyl->transformed(scaling(2., 2., 2.));
  REQUIRE(dynamic_cast<const CylinderSurface *>(even.get()) != nullptr);
}

TEST_CASE("plane: a plane under any affine map", "[surface]")
{
  const PlaneSurface diagonal(Vector3d(1., 0., 0.), Vector3d(1., 1., 0.).normalized());
  const auto moved = diagonal.transformed(scaling(2., 1., 1.));
  REQUIRE(moved != nullptr);
  // x + y = 1 scaled by 2 in x is x/2 + y = 1: normal (1/2, 1)
  CHECK(moved->normdir.isApprox(Vector3d(0.5, 1., 0.).normalized()));
  CHECK(moved->residual(Vector3d(2., 0., 0.)).exact());
  CHECK(moved->residual(Vector3d(0., 1., 5.)).exact());
}

TEST_CASE("elliptic cylinder: one surface whichever section declared it", "[surface]")
{
  const Vector3d d(0., 0., 1.);
  const EllipticCylinderSurface a(Vector3d::Zero(), d, Vector3d(4., 0., 0.), Vector3d(0., 2., 0.));
  // the same surface, declared from a tilted section higher up the axis
  const EllipticCylinderSurface b(Vector3d(0., 0., 3.), d, Vector3d(4., 0., 2.), Vector3d(0., -2., 1.));
  CHECK(a.sameAs(b));
  const EllipticCylinderSurface wider(Vector3d::Zero(), d, Vector3d(4.5, 0., 0.), Vector3d(0., 2., 0.));
  CHECK_FALSE(a.sameAs(wider));
}

TEST_CASE("seam: a ruling joins two rims, and must not cross either", "[surface]")
{
  const CylinderSurface cyl(Vector3d::Zero(), Vector3d(0., 0., 1.), 10.);
  std::vector<Vector3d> vertices;
  std::vector<int> bottom, top;
  const int n = 8;
  for (int k = 0; k < n; k++) {
    const double t = 2 * M_PI * k / n;
    bottom.push_back(int(vertices.size()));
    vertices.emplace_back(10. * cos(t), 10. * sin(t), 0.);
  }
  for (int k = 0; k < n; k++) {
    const double t = 2 * M_PI * k / n;
    top.push_back(int(vertices.size()));
    vertices.emplace_back(10. * cos(t), 10. * sin(t), 5.);
  }
  CHECK(recognition::isRuling(cyl, vertices[bottom[0]], vertices[top[0]]));
  CHECK_FALSE(recognition::isRuling(cyl, vertices[bottom[0]], vertices[top[1]]));  // a chord, inside
  CHECK(recognition::windsRound(cyl, bottom, vertices));
  CHECK(recognition::rulingSeams(cyl, bottom, top, vertices).size() == std::size_t(n));

  // A ramp in the top rim, the way a lead-in leaves a wall: along at z = 5 to the
  // third station, back under itself to half past the second at z = 3, and on
  // at that height to the fourth. At the third station the rim is then crossed
  // twice - at 5 by its own vertex and at 3 by the ramp - so the ruling up to 5
  // there would leave the face and come back: it may not be the seam.
  auto at = [&](double k, double z) {
    const double t = 2 * M_PI * k / n;
    vertices.emplace_back(10. * cos(t), 10. * sin(t), z);
    return int(vertices.size()) - 1;
  };
  const std::vector<int> ramp{top[0], top[1], top[2], at(1.5, 3.), at(3., 3.),
                              top[4], top[5], top[6], top[7]};
  const auto seams = recognition::rulingSeams(cyl, bottom, ramp, vertices);
  bool through_ramp = false, clear = false;
  for (const auto& [i, j] : seams) {
    through_ramp = through_ramp || (i == 2 && j == 2);
    clear = clear || (i == 0 && j == 0);
  }
  CHECK_FALSE(through_ramp);
  CHECK(clear);
}
