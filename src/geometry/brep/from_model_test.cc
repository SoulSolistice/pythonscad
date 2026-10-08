/*! The B-rep evaluated from the model's declarations - every expectation read
 * off examples/step_test/phantom-sliver.scad by hand. */
#include "geometry/brep/FromModel.h"

#include <catch2/catch_all.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "geometry/SolidTree.h"
#include "geometry/Surface.h"

namespace {

using Op = SolidTree::Op;

/*! cylinder(r, h) from z0 about z, as its generator declares it: the wall and
 * both caps; and, for a slice of `angle` degrees, its two flat sides. */
std::shared_ptr<const SolidTree> cylinder(double r, double z0, double h, double angle = 360)
{
  auto s = std::make_shared<DeclaredSolid>();
  const Vector3d z(0, 0, 1);
  s->bounds.push_back(std::make_shared<CylinderSurface>(Vector3d(0, 0, z0), z, r));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, z0), z));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, z0 + h), z));
  s->interior = Vector3d(0, 0, z0 + h / 2);
  if (angle < 360) {
    const double a = angle * M_PI / 180;
    s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, 0), Vector3d(0, 1, 0)));
    s->bounds.push_back(
      std::make_shared<PlaneSurface>(Vector3d(0, 0, 0), Vector3d(-std::sin(a), std::cos(a), 0)));
    s->interior = Vector3d(r / 2 * std::cos(a / 2), r / 2 * std::sin(a / 2), z0 + h / 2);
  }
  s->extent = BoundingBox(Vector3d(-r, -r, z0), Vector3d(r, r, z0 + h));
  return SolidTree::declared(s);
}

bool on(const brep::Face& f, const Surface& s)
{
  return f.surface != nullptr && f.surface->sameAs(s);
}

}  // namespace

TEST_CASE("the phantom coupon's B-rep is the model's, not the mesh's", "[brep][from-model]")
{
  // A body r = 20, z 0 to 20, less a bore r = 15, less a quarter slot from
  // z = 10 up, r 14.9 to 17. The slot's inner wall is inside the bore's air,
  // so the model has no face on it: eight faces - the outer wall, the bore,
  // two caps, and the slot's floor, outer wall and two flat sides.
  const auto body = cylinder(20, 0, 20);
  const auto bore = cylinder(15, -1, 22);
  const auto slot = SolidTree::combine(Op::Difference, cylinder(17, 10, 11, 90), cylinder(14.9, 9, 13));
  const auto tree =
    SolidTree::combine(Op::Difference, SolidTree::combine(Op::Difference, body, bore), slot);
  std::string why;
  double uncertainty = 0;
  const auto brep = brep::fromModel(*tree, why, uncertainty);
  INFO(why);
  REQUIRE(brep.has_value());
  CHECK(brep::check(*brep).empty());
  CHECK(brep->shells.size() == 1);
  CHECK(brep->faces.size() == 8);
  const CylinderSurface cutter(Vector3d(0, 0, 9), Vector3d(0, 0, 1), 14.9);
  const CylinderSurface bore_wall(Vector3d(0, 0, -1), Vector3d(0, 0, 1), 15);
  const PlaneSurface top(Vector3d(0, 0, 20), Vector3d(0, 0, 1)),
    bottom(Vector3d(0, 0, 0), Vector3d(0, 0, 1));
  int caps = 0;
  for (const brep::Face& f : brep->faces) {
    CHECK_FALSE(on(f, cutter));
    // The caps face out of the solid: the top along its plane's +z, the
    // bottom against it.
    if (on(f, top)) {
      CHECK(f.same_sense);
      caps++;
    }
    if (on(f, bottom)) {
      CHECK_FALSE(f.same_sense);
      caps++;
    }
    // The bore runs all the way round below the slot and is notched by it
    // above: two loops, each round the axis, neither outer.
    if (on(f, bore_wall)) {
      REQUIRE(f.loops.size() == 2);
      CHECK_FALSE(f.loops[0].outer);
      CHECK_FALSE(f.loops[1].outer);
    }
  }
  CHECK(caps == 2);
  // A cutter that is not inside the bore's air - r = 15.5 - has a face: the
  // slot's inner wall, nine faces.
  const auto deeper = SolidTree::combine(
    Op::Difference, SolidTree::combine(Op::Difference, body, bore),
    SolidTree::combine(Op::Difference, cylinder(17, 10, 11, 90), cylinder(15.5, 9, 13)));
  const auto with_wall = brep::fromModel(*deeper, why, uncertainty);
  REQUIRE(with_wall.has_value());
  CHECK(with_wall->faces.size() == 9);
}

TEST_CASE("two declared planes at one height are one level of the grid", "[brep][from-model]")
{
  // A ring r 5 to 10, z 0 to 10, and on it a ring r 5 to 7, z 10 to 20: the
  // first's top and the second's bottom are one plane, z = 10. Six faces:
  // the outer walls r = 10 and r = 7, the bore r = 5 all the way up - one
  // cylinder, declared by both - the bottom, the step at z = 10 from r 7 to
  // 10, and the top.
  const auto lower = SolidTree::combine(Op::Difference, cylinder(10, 0, 10), cylinder(5, -1, 12));
  const auto upper = SolidTree::combine(Op::Difference, cylinder(7, 10, 10), cylinder(5, 9, 12));
  std::string why;
  double uncertainty = 0;
  const auto brep = brep::fromModel(*SolidTree::combine(Op::Union, lower, upper), why, uncertainty);
  INFO(why);
  REQUIRE(brep.has_value());
  CHECK(brep::check(*brep).empty());
  CHECK(brep->faces.size() == 6);
}

namespace {

/*! cylinder(r = 10, h = 20) less everything above the plane through (0, 0,
 * 10) across unit n: a cube 40 across, its bottom on that plane, its sides 20
 * either way along the unit `side` and `across` it. */
std::shared_ptr<const SolidTree> obliqueCut(const Vector3d& n, const Vector3d& side,
                                            const Vector3d& across)
{
  const Vector3d p0(0, 0, 10);
  auto s = std::make_shared<DeclaredSolid>();
  s->bounds.push_back(std::make_shared<PlaneSurface>(p0, n));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(p0 + n * 40), n));
  for (const Vector3d& d : {side, across}) {
    s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(p0 - d * 20), d));
    s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(p0 + d * 20), d));
  }
  s->interior = p0 + n * 20;
  s->extent = BoundingBox(Vector3d(-60, -60, -60), Vector3d(60, 60, 60));
  return SolidTree::combine(Op::Difference, cylinder(10, 0, 20), SolidTree::declared(s));
}

/*! Where a coedge's pcurve starts and ends in the angle, along its use: a
 * ruling's angle at both; a circle's line from its start vertex's parameter
 * on the written circle, a turn on where it closes; a spline's end points. */
std::pair<double, double> angles(const brep::Body& body, const brep::Coedge& c)
{
  const brep::Edge& e = body.edges[c.edge];
  double from = 0, to = 0;
  if (const auto *line = std::get_if<brep::Line2>(&*c.pcurve)) {
    if (line->direction.x() == 0) {
      from = to = line->origin.x();
    } else {
      const auto& circle = std::get<brep::Conic>(e.curve.form);
      auto param = [&](const Vector3d& p) {
        const Vector3d rel = p - circle.frame.origin;
        const double t =
          std::atan2(rel.dot(circle.frame.axis.cross(circle.frame.ref)), rel.dot(circle.frame.ref));
        return t < 0 ? t + 2 * M_PI : t;
      };
      const double s0 = param(body.vertices[e.start].point);
      double s1 = param(body.vertices[e.end].point);
      if (s1 <= s0) s1 += 2 * M_PI;
      from = line->origin.x() + s0;
      to = line->origin.x() + s1;
    }
  } else {
    const auto& spline = std::get<brep::BSpline2>(*c.pcurve);
    from = spline.points.front().x();
    to = spline.points.back().x();
  }
  return c.forward ? std::pair(from, to) : std::pair(to, from);
}

}  // namespace

TEST_CASE("a conic on a cylinder carries its pcurve, certified within the file's uncertainty",
          "[brep][from-model][pcurve]")
{
  // The oblique cut, tilted 30 degrees about x and about y. On the wall, the
  // bottom circle is the line h = 0 in (angle, height); the cut's ellipse is
  // h = 10 - (n_x X + n_y Y) / n_z at the cylinder's point (X, Y) of angle
  // theta - which no rational B-spline is, so cubic Hermite pieces exact at
  // their joints, and within the uncertainty at their middles. About y the
  // ellipse's principal axes are not the boundary's u and v, and the written
  // conic's parameter is a quarter turn from theta: the pcurve must still be
  // in theta.
  const double a = M_PI / 6;
  const struct {
    Vector3d n, side, across;
  } tilts[] = {{{0, -std::sin(a), std::cos(a)}, {1, 0, 0}, {0, std::cos(a), std::sin(a)}},
               {{std::sin(a), 0, std::cos(a)}, {std::cos(a), 0, -std::sin(a)}, {0, 1, 0}}};
  for (const auto& tilt : tilts) {
    std::string why;
    double uncertainty = 0;
    const auto brep = brep::fromModel(*obliqueCut(tilt.n, tilt.side, tilt.across), why, uncertainty);
    INFO(why);
    REQUIRE(brep.has_value());
    const Vector3d axis(0, 0, 1), e1 = axis.unitOrthogonal(), e2 = axis.cross(e1);
    auto heightAt = [&](double theta) {
      const Vector3d p = (e1 * std::cos(theta) + e2 * std::sin(theta)) * 10;
      return 10 - (tilt.n.x() * p.x() + tilt.n.y() * p.y()) / tilt.n.z();
    };
    int walls = 0;
    for (const brep::Face& f : brep->faces) {
      if (!std::holds_alternative<brep::Cylinder>(f.form)) continue;
      walls++;
      for (const brep::Loop& loop : f.loops) {
        REQUIRE(loop.coedges.size() == 1);  // each a whole conic
        REQUIRE(loop.coedges.front().pcurve.has_value());
        const brep::PCurve& pc = *loop.coedges.front().pcurve;
        if (const auto *line = std::get_if<brep::Line2>(&pc)) {
          CHECK(line->origin.y() == Catch::Approx(0).margin(1e-12));  // the bottom, h = 0
          CHECK(line->direction.y() == 0);
          continue;
        }
        const auto& spline = std::get<brep::BSpline2>(pc);
        REQUIRE(spline.degree == 3);
        REQUIRE((spline.points.size() - 1) % 3 == 0);
        // Round the cylinder once, and closed in height.
        CHECK(std::fabs(spline.points.back().x() - spline.points.front().x()) ==
              Catch::Approx(2 * M_PI));
        CHECK(spline.points.back().y() == Catch::Approx(spline.points.front().y()).margin(1e-12));
        double worst = 0;
        for (std::size_t i = 0; i + 3 < spline.points.size(); i += 3) {
          const Vector2d& b0 = spline.points[i];
          const Vector2d& b3 = spline.points[i + 3];
          CHECK(b0.y() == Catch::Approx(heightAt(b0.x())).margin(1e-12));
          const Vector2d mid = (b0 + 3 * spline.points[i + 1] + 3 * spline.points[i + 2] + b3) / 8;
          CHECK(mid.x() == Catch::Approx((b0.x() + b3.x()) / 2).margin(1e-12));
          worst = std::max(worst, std::fabs(mid.y() - heightAt(mid.x())));
        }
        CHECK(worst <= uncertainty);
      }
    }
    CHECK(walls == 1);
  }
}

TEST_CASE("a loop round a cylinder is one chain in its parameter plane", "[brep][from-model][pcurve]")
{
  // The phantom coupon's bore: below the slot a band, above it notched - its
  // upper loop arcs and rulings in turn. Each use's pcurve starts, in the
  // angle, where the one before it ended - not a turn away, which the
  // surface's points alone would allow.
  const auto body = cylinder(20, 0, 20);
  const auto bore = cylinder(15, -1, 22);
  const auto slot = SolidTree::combine(Op::Difference, cylinder(17, 10, 11, 90), cylinder(14.9, 9, 13));
  const auto tree =
    SolidTree::combine(Op::Difference, SolidTree::combine(Op::Difference, body, bore), slot);
  std::string why;
  double uncertainty = 0;
  const auto brep = brep::fromModel(*tree, why, uncertainty);
  INFO(why);
  REQUIRE(brep.has_value());
  const CylinderSurface bore_wall(Vector3d(0, 0, -1), Vector3d(0, 0, 1), 15);
  int chained = 0;
  for (const brep::Face& f : brep->faces) {
    if (!on(f, bore_wall)) continue;
    for (const brep::Loop& loop : f.loops) {
      for (std::size_t i = 0; i < loop.coedges.size(); i++) {
        REQUIRE(loop.coedges[i].pcurve.has_value());
        const auto here = angles(*brep, loop.coedges[i]);
        const auto next = angles(*brep, loop.coedges[(i + 1) % loop.coedges.size()]);
        if (i + 1 < loop.coedges.size()) {
          CHECK(next.first == Catch::Approx(here.second).margin(1e-12));
          chained++;
        }
      }
    }
  }
  CHECK(chained >= 3);  // the notched loop's rulings and arcs
}
