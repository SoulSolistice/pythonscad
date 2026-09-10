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
#include <algorithm>
#include <cmath>
#include <string>
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

TEST_CASE("a declared grid hands out the B-spline a STEP file can carry", "[surface][grid]")
{
  const int rows = 24, cols = 4;
  GridSurface grid(rows, cols, helix(rows, cols));

  int du = 0, dv = 0, nu = 0, nv = 0;
  std::vector<Vector3d> ctrl;
  std::vector<double> ku, kv;
  std::vector<int> mu, mv;
  REQUIRE(grid.splineForm(du, dv, nu, nv, ctrl, ku, mu, kv, mv));

  SECTION("the knot vectors are the ones the degrees and the net imply")
  {
    // sum(multiplicities) = control points + degree + 1 is the whole contract
    // between the four lists; a reader that disagrees builds a different
    // surface or none. Both ends clamped, because the patch has to start at the
    // first station and stop at the last rather than somewhere inside them.
    CHECK(du == 3);
    CHECK(dv == 1);
    CHECK(int(ctrl.size()) == nu * nv);

    int sum_u = 0, sum_v = 0;
    for (const int m : mu) sum_u += m;
    for (const int m : mv) sum_v += m;
    CHECK(sum_u == nu + du + 1);
    CHECK(sum_v == nv + dv + 1);
    CHECK(mu.front() == du + 1);
    CHECK(mu.back() == du + 1);
    CHECK(mv.front() == dv + 1);
    CHECK(mv.back() == dv + 1);

    for (std::size_t i = 1; i < ku.size(); i++) CHECK(ku[i] > ku[i - 1]);
    for (std::size_t i = 1; i < kv.size(); i++) CHECK(kv[i] > kv[i - 1]);
  }

  SECTION("it has interior knots, which is why they cannot be left implied")
  {
    // A Bezier's knots follow from its degree, so the exporter used to
    // synthesise them. This grid has 24 stations and 20 interior knots that
    // come from the chord lengths between them; nothing in the file could
    // reconstruct those, and writing Bezier knots here would describe some
    // other surface entirely.
    CHECK(ku.size() > 2);
    CHECK(kv.size() == std::size_t(cols));
  }

  SECTION("evaluating that form gives back the surface it came from")
  {
    // The form is only worth writing if it is the same surface membership was
    // answered against. de Boor over the handed-out knots and poles, compared
    // against evaluate() - if these disagree, a reader gets a surface the
    // exporter never checked the mesh against.
    std::vector<double> full;
    for (std::size_t i = 0; i < ku.size(); i++) {
      for (int m = 0; m < mu[i]; m++) full.push_back(ku[i]);
    }

    auto deboor = [&](double u, int col) {
      int span = du;
      while (span + 1 < nu && full[span + 1] <= u) span++;
      std::vector<Vector3d> d(du + 1);
      for (int i = 0; i <= du; i++) d[i] = ctrl[std::size_t(span - du + i) * nv + col];
      for (int r = 1; r <= du; r++) {
        for (int i = du; i >= r; i--) {
          const int k = span - du + i;
          const double lo = full[k], hi = full[k + du + 1 - r];
          const double a = hi > lo ? (u - lo) / (hi - lo) : 0.0;
          d[i] = d[i - 1] * (1 - a) + d[i] * a;
        }
      }
      return d[du];
    };

    for (int s = 0; s <= 40; s++) {
      const double u = std::min(double(s) / 40, 1.0 - 1e-12);
      for (int col = 0; col < nv; col++) {
        const double v = double(col) / (nv - 1);
        CHECK((deboor(u, col) - grid.evaluate(u, v)).norm() < 1e-9);
      }
    }
  }
}

TEST_CASE("a grid too short for a cubic still reports the surface it is", "[surface][grid]")
{
  // Three stations leave no cubic to fit, and evaluate() walks them linearly.
  // Reporting degree 3 anyway would hand a reader a surface with more control
  // points than it has, so the form comes back at the degree it really is.
  const int rows = 3, cols = 2;
  GridSurface grid(rows, cols, helix(rows, cols));

  int du = 0, dv = 0, nu = 0, nv = 0;
  std::vector<Vector3d> ctrl;
  std::vector<double> ku, kv;
  std::vector<int> mu, mv;
  REQUIRE(grid.splineForm(du, dv, nu, nv, ctrl, ku, mu, kv, mv));
  CHECK(du == 1);
  CHECK(nu == rows);
  CHECK(ctrl.size() == std::size_t(rows * cols));

  int sum_u = 0;
  for (const int m : mu) sum_u += m;
  CHECK(sum_u == nu + du + 1);
  for (int i = 0; i < rows; i++) {
    CHECK((ctrl[std::size_t(i) * cols] - grid.at(i, 0)).norm() < 1e-12);
  }
}

TEST_CASE("a profile declared closed has a surface over its closing strip", "[surface][grid]")
{
  // The strip from the last column back to the first is part of the sweep and
  // no column of the net names it. Without it a four sided ridge has a surface
  // over three of its sides: facets on the fourth can be claimed by position,
  // because the generator emitted their corners, and never by projection -
  // which is precisely the half a boolean destroys.
  // A closed profile, so the strip in question is real: the helix() helper
  // sweeps a straight radial segment, whose closing strip retraces the segment
  // it came along and lies on the open surface too.
  const int rows = 24, cols = 4;
  const double R = 20.0, pitch = 6.0, turns = 1.5;
  std::vector<Vector3d> net;
  for (int i = 0; i < rows; i++) {
    const double t = double(i) / (rows - 1);
    const double a = 2 * M_PI * turns * t;
    const double z = pitch * turns * t;
    const double profile[4][2] = {{0.6, -1.2}, {-1.0, -0.4}, {-1.0, 0.4}, {0.6, 1.2}};
    for (const auto& p : profile) {
      net.emplace_back((R + p[0]) * cos(a), (R + p[0]) * sin(a), z + p[1]);
    }
  }
  GridSurface open(rows, cols, net);
  GridSurface closed(rows, cols, net, true);

  SECTION("the midpoint of the closing strip lies on the closed surface only")
  {
    std::vector<Vector3d> scratch;
    const int row = rows / 2;
    const Vector3d mid = (closed.at(row, cols - 1) + closed.at(row, 0)) / 2;
    CHECK(closed.onSurface(mid, closed.tessellationBand()));
    CHECK_FALSE(open.onSurface(mid, open.tessellationBand()));
    // and it is not one of the declared points, so position alone cannot save it
    CHECK_FALSE(closed.isDeclaredPoint(mid));
    CHECK(closed.pointMember(scratch, mid) == 1);
  }

  SECTION("the written form repeats the first column rather than stopping short")
  {
    int du = 0, dv = 0, nu = 0, nv = 0;
    std::vector<Vector3d> ctrl;
    std::vector<double> ku, kv;
    std::vector<int> mu, mv;
    REQUIRE(closed.splineForm(du, dv, nu, nv, ctrl, ku, mu, kv, mv));
    CHECK(nv == cols + 1);
    int sum_v = 0;
    for (const int m : mv) sum_v += m;
    CHECK(sum_v == nv + dv + 1);
    for (int i = 0; i < nu; i++) {
      CHECK((ctrl[std::size_t(i) * nv] - ctrl[std::size_t(i) * nv + cols]).norm() < 1e-12);
    }
  }
}

TEST_CASE("the band covers the facets the grid was built from", "[surface][grid]")
{
  // A twisted wall, the shape step-approximate-report sweeps: stations up z,
  // each rotated a little, so every cell is warped and the sweep curves.
  //
  // The band is what membership is answered at, and a mesh vertex is not the
  // only point that has to fall inside it. A facet's *middle* stands off the
  // smooth surface too, and by more than the chord midpoint does - that is what
  // refused every facet of a recovered grid while its corners passed.
  const int rows = 24, cols = 17;
  std::vector<Vector3d> net;
  for (int i = 0; i < rows; i++) {
    const double t = double(i) / (rows - 1);
    const double a = 0.9 * t;  // the twist
    for (int j = 0; j < cols; j++) {
      const double s = -1.0 + 2.0 * j / (cols - 1);
      net.emplace_back(s * cos(a) - 1.0 * sin(a), s * sin(a) + 1.0 * cos(a), 4.0 * t);
    }
  }
  GridSurface grid(rows, cols, net);
  REQUIRE(grid.interpolated());
  CHECK(grid.tessellationBand() > 0);

  double worst_vertex = 0, worst_middle = 0;
  for (int i = 0; i + 1 < rows; i++) {
    for (int j = 0; j + 1 < cols; j++) {
      const Vector3d corner[4] = {grid.at(i, j), grid.at(i + 1, j), grid.at(i + 1, j + 1),
                                  grid.at(i, j + 1)};
      for (const auto& p : corner) {
        double u = 0, v = 0;
        REQUIRE(grid.project(p, u, v));
        worst_vertex = std::max(worst_vertex, (grid.evaluate(u, v) - p).norm());
      }
      // Both halves of the cell, since that is what a triangulated mesh
      // presents and what membership is asked about.
      const Vector3d middles[2] = {(corner[0] + corner[1] + corner[2]) / 3,
                                   (corner[0] + corner[2] + corner[3]) / 3};
      for (const auto& m : middles) {
        double u = 0, v = 0;
        REQUIRE(grid.project(m, u, v));
        worst_middle = std::max(worst_middle, (grid.evaluate(u, v) - m).norm());
      }
    }
  }
  INFO("band " << grid.tessellationBand() << ", vertices miss " << worst_vertex << ", middles miss "
               << worst_middle);
  CHECK(worst_vertex < 1e-9);
  CHECK(worst_middle <= grid.membershipTolerance());
}

/*! `project` clamps to the patch, so the normal component is not the distance.
 *
 * This pins the premise behind a defect in the STEP exporter's crossing-curve
 * solver, found in review on 2026-09-10 and fixed the same day. That solver
 * needs an implicit `f` for a surface that has no algebraic one, and takes the
 * foot-point plane: project, and use `n . (p - foot)`. For a point *inside* the
 * patch that is the distance. For one off the end of it, it is not, and the
 * difference is unbounded.
 *
 * `evaluate` clamps both parameters and `project` returns the nearest point of
 * the bounded rectangle, so a point beyond the patch gets a foot on the
 * boundary and `p - foot` lies almost entirely *in* the tangent plane. Anything
 * accepting `n . (p - foot)` as proof of membership then believes a point is on
 * a surface it has run off the end of.
 *
 * A flat grid over the unit square makes it exact rather than approximate: the
 * normal is z everywhere, so the normal component of a purely in-plane offset
 * is zero to the last bit.
 */
TEST_CASE("a point off the end of a grid projects to its boundary", "[gridsurface]")
{
  // z = 0 over 0 <= x <= 1, 0 <= y <= 1, as three rows of two columns.
  std::vector<std::vector<Vector3d>> rows = {
    {Vector3d(0, 0, 0), Vector3d(0, 1, 0)},
    {Vector3d(0.5, 0, 0), Vector3d(0.5, 1, 0)},
    {Vector3d(1, 0, 0), Vector3d(1, 1, 0)},
  };
  std::string why;
  const auto grid = GridSurface::fromRows(rows, false, why);
  REQUIRE(grid != nullptr);

  const Vector3d outside(2.0, 0.5, 0.0);
  double u = 0, v = 0;
  REQUIRE(grid->project(outside, u, v));
  const Vector3d foot = grid->evaluate(u, v);

  // The foot is on the patch, a whole unit away.
  CHECK((outside - foot).norm() > 0.9);
  // And the offset is in the plane, so the normal component says zero.
  CHECK(std::fabs(foot.z()) < 1e-9);
  CHECK(std::fabs(Vector3d(0, 0, 1).dot(outside - foot)) < 1e-12);
}

/*! A mirrored sweep declares a different surface from the mirrored mesh.
 *
 * `SweepSurface::transform` accepts any map whose `m^T m` is `scale^2 I`. Every
 * *orthogonal* matrix satisfies that, reflections included, because nothing
 * checks the determinant's sign. The record it then writes keeps `turns` and the
 * sign of `pitch` and transforms `ref` and `normdir` - but `evaluate` builds the
 * second radial direction as `normdir x ref`, and a cross product is not
 * preserved by a reflection. So the declaration comes out with the helix turning
 * the other way while `PolySet::transform` mirrors the mesh correctly, and the
 * two no longer describe the same solid.
 *
 * Found in external review 2026-09-10, where the declaration came out 20.0 mm
 * from the mesh - the point on the far side of the axis. `transform` now refuses
 * a reflection, so the declaration is dropped and the mesh is exported faceted:
 * a recognition loss, never a wrong solid.
 */
TEST_CASE("a mirrored sweep is refused rather than declared wrongly", "[sweepsurface]")
{
  const std::vector<Vector2d> profile = {Vector2d(0, 0), Vector2d(1, 0), Vector2d(0, 1)};
  SweepSurface sweep(Vector3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0), 10.0, 4.0, 1.0, profile,
                     16);

  // A quarter turn along the helix, on the profile's first vertex.
  const Vector3d before = sweep.evaluate(0.25, 0.0);

  // Mirror in x. The mesh point goes to (-x, y, z).
  Transform3d mirror = Transform3d::Identity();
  mirror.linear() = Eigen::Vector3d(-1, 1, 1).asDiagonal();
  const Vector3d mesh_says = mirror * before;

  auto moved = sweep.clone();
  // Refused, so no declaration survives to disagree with `mesh_says`.
  CHECK_FALSE(moved->transform(mirror));

  // A rotation of the same magnitude is still accepted, so the guard is on the
  // determinant and not on orthogonal maps in general.
  Transform3d turn = Transform3d::Identity();
  turn.linear() = Eigen::AngleAxisd(M_PI / 2, Vector3d::UnitZ()).toRotationMatrix();
  auto turned = sweep.clone();
  REQUIRE(turned->transform(turn));
  const auto *ok = dynamic_cast<const SweepSurface *>(turned.get());
  REQUIRE(ok != nullptr);
  CHECK((ok->evaluate(0.25, 0.0) - turn * before).norm() < 1e-9);
}

/*! A ring - a sweep of zero pitch - rejects points its own evaluator made.
 *
 * `make` rejects `turns == 0` and accepts `pitch == 0`, so a closed ring is a
 * legal declaration. Membership is documented as an inversion rather than a
 * search, and for a ring the inversion loses a turn: `localCoords` recovers the
 * angle with `atan2`, which lands in `(-pi, pi]`, and then chooses the turn from
 * the *height*, which on a ring carries no information at all - `k` is forced to
 * zero. Any point past the half turn therefore inverts to a negative `t` and is
 * refused by the surface that generated it.
 *
 * Found in external review 2026-09-10, stated here rather than fixed: the repair
 * is a decision about what `declare_sweep` should accept, not a local edit.
 */
TEST_CASE("a zero-pitch sweep inverts its own points", "[sweepsurface][!shouldfail]")
{
  const std::vector<Vector2d> profile = {Vector2d(0, 0), Vector2d(1, 0), Vector2d(0, 1)};
  std::string why;
  const auto ring = SweepSurface::make(Vector3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0), 10.0,
                                       0.0, 1.0, profile, 16, why);
  REQUIRE(ring != nullptr);  // zero pitch is accepted

  // Three quarters of the way round, on a point the evaluator itself produced.
  const Vector3d generated = ring->evaluate(0.75, 0.0);
  CHECK(ring->onSurface(generated, 1e-6));
}

// A point the surface produced is a point the surface can find again. That
// sounds like a tautology and it is not: `project` descends on the *squared*
// distance, which in double precision stops changing about sqrt(eps) from the
// minimum, and it takes its Jacobian by central differences at h = 1e-6, whose
// own error is around eps/h. Both floors sit far above the 1e-7 that
// `intersectionArc` holds a fitted boundary curve to.
//
// This is the measurement behind item 10 A3 of doc/step-export-wip.md: on the
// band family the crossing arc lies on the *quadric* it bounds to 5.5e-06 and
// is reported 1.7e-02 off the declared sweep, and that number has to come from
// somewhere. `evaluate` then `project` is the shortest way to ask whether it is
// the curve missing the surface or the surface failing to locate itself.
TEST_CASE("a declared grid can find a point it evaluated itself", "[surface][grid][projection]")
{
  // The band family's own ridge, station for station: a trapezoidal profile
  // swept along 10/3 turns, declared closed. A smooth ramp is not a substitute -
  // the profile here is a polyline with re-entrant corners and it is the shape
  // the 1.7e-02 was measured on.
  const int steps = 107;  // max(24, round(32 * 40/12))
  const int rows = steps + 1, cols = 4;
  std::vector<Vector3d> net;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps;
    const double a = 2 * M_PI * (40.0 / 12.0) * t;
    const double z = 8.0 / 2 + (40.0 - 8.0) * t;
    const double f = std::max(0.0, std::min(1.0, std::min(t / 0.2, (1 - t) / 0.2)));
    const double profile[4][2] = {{0.3, -4.0}, {-2.0 * f, -1.5}, {-2.0 * f, 1.5}, {0.3, 4.0}};
    for (const auto& pr : profile) {
      net.emplace_back((20.0 + pr[0]) * cos(a), (20.0 + pr[0]) * sin(a), z + pr[1]);
    }
  }
  GridSurface grid(rows, cols, net, true);

  double worst = 0, worst_u = 0, worst_v = 0;
  for (int i = 1; i < 20; i++) {
    for (int j = 1; j < 8; j++) {
      const double u = double(i) / 20.0, v = double(j) / 8.0;
      const Vector3d on = grid.evaluate(u, v);
      double fu = 0, fv = 0;
      REQUIRE(grid.project(on, fu, fv));
      const double miss = (grid.evaluate(fu, fv) - on).norm();
      if (miss > worst) {
        worst = miss;
        worst_u = u;
        worst_v = v;
      }
    }
  }
  INFO("worst at u=" << worst_u << " v=" << worst_v << " miss=" << worst);
  // A boundary curve is held to 1e-7 of this surface, so the surface has to be
  // able to place a point on itself at least that well.
  CHECK(worst <= 1e-7);
}
