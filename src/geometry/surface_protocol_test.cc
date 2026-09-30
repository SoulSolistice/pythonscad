// What a surface says about itself: the protocol every kind answers for itself
// (Surface.h), and the elliptic cylinder, which answers it in closed form too.
#include "geometry/Surface.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "geometry/PolySet.h"
#include "geometry/PolySetUtils.h"
#include "geometry/recognition/Exact.h"
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
  const auto moved = cyl->transformed(scaling(2., 1., 1.), scaleSimilarity(Vector3d(2., 1., 1.)));
  REQUIRE(moved != nullptr);
  const auto *e = dynamic_cast<const EllipticCylinderSurface *>(moved.get());
  REQUIRE(e != nullptr);
  CHECK(e->a() == Catch::Approx(10.));
  CHECK(e->b() == Catch::Approx(5.));
  CHECK(e->owners == std::vector<int32_t>{7});
  // a similarity keeps it circular
  const auto even = cyl->transformed(scaling(2., 2., 2.), scaleSimilarity(Vector3d(2., 2., 2.)));
  REQUIRE(dynamic_cast<const CylinderSurface *>(even.get()) != nullptr);
}

TEST_CASE("plane: a plane under any affine map", "[surface]")
{
  const PlaneSurface diagonal(Vector3d(1., 0., 0.), Vector3d(1., 1., 0.).normalized());
  const auto moved = diagonal.transformed(scaling(2., 1., 1.), scaleSimilarity(Vector3d(2., 1., 1.)));
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

TEST_CASE("a record moves by the similarity its transform's maker states", "[surface][identity]")
{
  // Until 2026-09-28 whether a map was a similarity was read off its matrix to
  // 1e-9, and the scale was a column's norm. Now the maker states both: moved
  // by a stated similarity a record takes exactly the stated scale; moved by a
  // matrix nobody stated anything of, a sphere has no image to be, and a
  // cylinder becomes the elliptic one it is under any affine map.
  Transform3d m = Transform3d::Identity();
  m.rotate(Eigen::AngleAxisd(0.7, Vector3d(1, 2, 3).normalized()));
  m.scale(2.5);
  const SphereSurface sphere(Vector3d(1, 2, 3), Vector3d(0, 0, 1), 1.3);
  const auto stated = sphere.transformed(m, Similarity::by(2.5));
  REQUIRE(stated != nullptr);
  CHECK(static_cast<const SphereSurface&>(*stated).r == 1.3 * 2.5);
  CHECK(sphere.transformed(m, Similarity::unstated()) == nullptr);

  const CylinderSurface cyl(Vector3d(0, 0, 0), Vector3d(0, 0, 1), 1.3);
  const auto circular = cyl.transformed(m, Similarity::by(2.5));
  REQUIRE(dynamic_cast<const CylinderSurface *>(circular.get()) != nullptr);
  CHECK(static_cast<const CylinderSurface&>(*circular).r == 1.3 * 2.5);
  CHECK(dynamic_cast<const EllipticCylinderSurface *>(
          cyl.transformed(m, Similarity::unstated()).get()) != nullptr);

  // the statement composes: a uniform scale then a rotation; a mirror twice is none
  CHECK(Similarity::by(2).then(Similarity::by(1)).scale == 2);
  CHECK_FALSE(Similarity::by(1, true).then(Similarity::by(1, true)).mirrors);
  CHECK_FALSE(Similarity::by(2).then(Similarity::unstated()).stated);
  CHECK(scaleSimilarity(Vector3d(-2, -2, -2)).mirrors);
  CHECK_FALSE(scaleSimilarity(Vector3d(2, 2, 2.0000001)).stated);
}

TEST_CASE("sameAs: one surface at machine precision, whichever record states it", "[surface][identity]")
{
  // Until 2026-09-28 every kind compared its fields to an absolute 1e-9, and a
  // plane or a sphere anchored elsewhere was another surface. Now: the
  // geometric surface, and nothing chosen - a record 5e-10 off at radius 10 is
  // tens of thousands of roundings away, and another surface.
  const Vector3d z(0, 0, 1);

  // a cylinder declared at two heights, and one turned about its own axis line
  const CylinderSurface low(Vector3d(0, 0, 0), z, 10), high(Vector3d(0, 0, 37), -z, 10);
  CHECK(low.sameAs(high));
  CHECK_FALSE(low.sameAs(CylinderSurface(Vector3d(0, 0, 0), z, 10 + 5e-10)));
  CHECK_FALSE(low.sameAs(CylinderSurface(Vector3d(5e-10, 0, 0), z, 10)));
  // the same wall reached by two chains of arithmetic: a rotation of the record
  // at the origin, and of one declared along the axis
  Transform3d turn = Transform3d::Identity();
  turn.rotate(Eigen::AngleAxisd(0.3, Vector3d(1, 2, 3).normalized()));
  CylinderSurface a = low, b(Vector3d(0, 0, 11), z, 10);
  REQUIRE(a.transform(turn, Similarity::by(1.0)));
  REQUIRE(b.transform(turn, Similarity::by(1.0)));
  CHECK(a.sameAs(b));

  // a plane anchored at two of its points, and one a hair off it
  const PlaneSurface p(Vector3d(1, 2, 3), Vector3d(1, 1, 0)),
    q(Vector3d(1 - 50, 2 + 50, 3 + 9), -Vector3d(1, 1, 0));
  CHECK(p.sameAs(q));
  CHECK_FALSE(p.sameAs(PlaneSurface(Vector3d(1 + 5e-10, 2, 3), Vector3d(1, 1, 0))));

  // a cone from two levels, and traversed the other way: radius 10 at z 0,
  // 8 at z 4, so from z 4 down it is radius 8 growing by 0.5 per unit
  const ConeSurface c1(Vector3d(0, 0, 0), z, 10, -0.5), c2(Vector3d(0, 0, 4), -z, 8, 0.5);
  CHECK(c1.sameAs(c2));
  CHECK_FALSE(c1.sameAs(ConeSurface(Vector3d(0, 0, 0), z, 10, -0.5 - 1e-10)));

  // a sphere and a torus: the centre, and each radius
  const SphereSurface s(Vector3d(1, 2, 3), z, 5);
  CHECK(s.sameAs(SphereSurface(Vector3d(1, 2, 3), Vector3d(1, 0, 0), 5)));
  CHECK_FALSE(s.sameAs(SphereSurface(Vector3d(1, 2, 3 + 5e-10), z, 5)));
  const TorusSurface t(Vector3d(0, 0, 2), z, 20, 3);
  CHECK(t.sameAs(TorusSurface(Vector3d(0, 0, 2), -z, 20, 3)));
  CHECK_FALSE(t.sameAs(TorusSurface(Vector3d(0, 0, 2), z, 20, 3 + 5e-10)));
  CHECK_FALSE(t.sameAs(SphereSurface(Vector3d(0, 0, 2), z, 20)));  // the kind first
}

TEST_CASE("classifications: each asked in one place, at machine precision", "[surface][identity]")
{
  // A rim of a turned cylinder: every point of it at one level along the turned
  // axis, and a point 1e-9 above it not - the band pass asked this against an
  // absolute 1e-5 until 2026-09-28.
  Transform3d turn = Transform3d::Identity();
  turn.rotate(Eigen::AngleAxisd(0.3, Vector3d(1, 2, 3).normalized()));
  turn.pretranslate(Vector3d(40, -7, 12));
  const Vector3d axis = turn.linear() * Vector3d(0, 0, 1);
  std::vector<Vector3d> rim;
  for (int i = 0; i < 32; i++) {
    const double a = 2 * M_PI * i / 32;
    rim.push_back(turn * Vector3d(10 * cos(a), 10 * sin(a), 5));
  }
  for (const Vector3d& v : rim) CHECK(recognition::onPlane(rim[0], axis, v));
  CHECK_FALSE(recognition::onPlane(rim[0], axis, turn * Vector3d(10, 0, 5 + 1e-9)));

  // Asked of the ends, not of the direction between them: a chord a hundredth
  // long, a thousand out, is in the rim though its normalised direction is off
  // by the rounding of both ends over that hundredth.
  const Vector3d far = turn * Vector3d(1000, 0, 5), near = turn * Vector3d(1000, 0.01, 5);
  CHECK(recognition::onPlane(far, axis, near));

  // one point reached by two chains of arithmetic, and a hair beside it
  const Vector3d far_by_parts = turn.linear() * Vector3d(1000, 0, 5) + turn.translation();
  CHECK(recognition::samePoint(far, far_by_parts));
  CHECK_FALSE(recognition::samePoint(far, far + Vector3d(0, 1e-10, 0)));

  // directions at right angles, and a hair off them
  const Vector3d across = turn.linear() * Vector3d(1, 0, 0);
  CHECK(recognition::perpendicular(axis, across));
  CHECK_FALSE(recognition::perpendicular(axis, turn.linear() * Vector3d(1, 0, 1e-12)));
  CHECK(recognition::parallel(axis, -axis));
  CHECK_FALSE(recognition::parallel(axis, turn.linear() * Vector3d(1e-12, 0, 1)));

  // a point on the turned axis line, and one off it
  const Vector3d origin = turn * Vector3d(0, 0, 0);
  CHECK(recognition::onLine(origin, axis, turn * Vector3d(0, 0, 37)));
  CHECK_FALSE(recognition::onLine(origin, axis, turn * Vector3d(1e-10, 0, 37)));
  CHECK(recognition::sameLine(origin, axis, turn * Vector3d(0, 0, 37), -axis));

  // four points of one turned plane, one a hair off it, and three on a line
  // that state no plane at all
  CHECK(recognition::coplanar(rim[0], rim[9], rim[21], rim[30]));
  CHECK(recognition::coplanar(rim[0], rim[9], rim[21], turn * Vector3d(3, -4, 5)));
  CHECK_FALSE(recognition::coplanar(rim[0], rim[9], rim[21], turn * Vector3d(3, -4, 5 + 1e-9)));
  CHECK_FALSE(recognition::coplanar(origin, turn * Vector3d(0, 0, 1), turn * Vector3d(0, 0, 2), rim[0]));

  // one plane anchored at two of its points, and one a hair above
  CHECK(recognition::samePlane(rim[0], axis, rim[17], -axis));
  CHECK_FALSE(recognition::samePlane(rim[0], axis, turn * Vector3d(0, 0, 5 + 1e-9), axis));
}

TEST_CASE("a face's corners are on one plane, or one is off it, at machine precision",
          "[surface][identity]")
{
  // A face of a turned solid 100 mm out: a pentagon in the plane z = 5 of a
  // turned frame, and a hole in it. Every corner is on one plane. The writer
  // asked this within 1e-8 of the model's size until 2026-09-30 - which lets a
  // corner 1e-10 off through at any size above a centimetre (open item 39, R2).
  Transform3d turn = Transform3d::Identity();
  turn.rotate(Eigen::AngleAxisd(0.3, Vector3d(1, 2, 3).normalized()));
  turn.pretranslate(Vector3d(100, -70, 120));
  std::vector<Vector3d> corners;
  for (int i = 0; i < 5; i++) {
    const double a = 2 * M_PI * i / 5;
    corners.push_back(turn * Vector3d(100 * cos(a), 100 * sin(a), 5));
  }
  for (int i = 0; i < 4; i++) {
    const double a = 2 * M_PI * i / 4;
    corners.push_back(turn * Vector3d(20 * cos(a), 20 * sin(a), 5));
  }
  CHECK(recognition::allCoplanar(corners));

  // a hole corner 1e-10 off the plane is off it
  std::vector<Vector3d> lifted = corners;
  lifted[7] = turn * Vector3d(20 * cos(M_PI), 20 * sin(M_PI), 5 + 1e-10);
  CHECK_FALSE(recognition::allCoplanar(lifted));

  // and one moved by the last bit of one coordinate - within what the
  // arithmetic that put it there can promise - is on it
  std::vector<Vector3d> nudged = corners;
  nudged[3][0] = std::nextafter(nudged[3][0], 1e300);
  CHECK(recognition::allCoplanar(nudged));

  // three points, and points on one line, state no plane to be off
  CHECK(recognition::allCoplanar({corners[0], corners[1], corners[2]}));
  CHECK(recognition::allCoplanar(
    {Vector3d(0, 0, 0), Vector3d(1, 1, 1), Vector3d(2, 2, 2), Vector3d(5, 5, 5)}));
}

TEST_CASE("a maker's names ride with its polygons into triangles, in step", "[surface][identity]")
{
  // Identity said by the maker (checklist E8, open item 40): each polygon names
  // the record it was made on, and every triangle made from it keeps the name.
  // Out of step, every triangle after the first polygon lost would name the
  // wrong surface - so it is carried whole, or not at all.
  PolySet ps(3);
  ps.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
  ps.indices = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}};  // bottom, top, front: quads
  ps.surface_tags = {11, 22, 33};
  const auto tris = PolySetUtils::tessellate_faces(ps);
  REQUIRE(tris->indices.size() == 6);
  REQUIRE(tris->surface_tags.size() == tris->indices.size());
  for (std::size_t t = 0; t < tris->indices.size(); t++) {
    // every corner of a triangle is on the face its tag names
    bool bottom = true, top = true, front = true;
    for (const int v : tris->indices[t]) {
      bottom = bottom && tris->vertices[std::size_t(v)][2] == 0;
      top = top && tris->vertices[std::size_t(v)][2] == 1;
      front = front && tris->vertices[std::size_t(v)][1] == 0;
    }
    const uint32_t tag = tris->surface_tags[t];
    CHECK(((tag == 11 && bottom) || (tag == 22 && top) || (tag == 33 && front)));
  }

  // a polygon that collapses is removed with its name, and the rest stay in step
  PolySet collapsing(3);
  collapsing.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {1e-12, 0, 0}, {0, 0, 5}, {0, 1, 5}};
  collapsing.indices = {{0, 3, 1}, {0, 1, 2}, {0, 4, 5}};
  collapsing.surface_tags = {7, 8, 9};
  collapsing.quantizeVertices();
  REQUIRE(collapsing.indices.size() == 2);
  CHECK(collapsing.surface_tags == std::vector<uint32_t>{8, 9});

  // and names already out of step are dropped, not carried shifted
  PolySet stale(3);
  stale.vertices = collapsing.vertices;
  stale.indices = {{0, 1, 2}, {0, 4, 5}};
  stale.surface_tags = {1};
  stale.quantizeVertices();
  CHECK(stale.surface_tags.empty());
}
