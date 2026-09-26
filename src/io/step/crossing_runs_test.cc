// The curve two declared surfaces cross along, fitted over a run of mesh edges
// (io/step/CrossingRuns). What is tested is what a file relies on and no
// distance measures: that the curve runs along the crossing rather than back
// over it, and that a run whose vertices are unevenly spaced is still one curve.
#include "io/step/CrossingRuns.h"

#include <catch2/catch_all.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "geometry/Surface.h"

using namespace step;

namespace {

/*! Points of a helix of radius `r` and rise `rise` per turn, at `turns` of it. */
std::vector<Vector3d> helix(double r, double rise, const std::vector<double>& turns)
{
  std::vector<Vector3d> out;
  for (const double t : turns) {
    out.emplace_back(r * cos(2 * M_PI * t), r * sin(2 * M_PI * t), rise * t);
  }
  return out;
}

}  // namespace

TEST_CASE("a curve fitted through evenly spaced points runs along them", "[step][crossing]")
{
  std::vector<double> at;
  for (int i = 0; i <= 40; i++) at.push_back(i / 32.0);
  CurveFit fit;
  REQUIRE(interpolateCubic(helix(20, 9.6, at), fit));
  CHECK(intervalsTurningBack(fit) == 0);
}

TEST_CASE("a curve through points out of order turns back, and is caught", "[step][crossing]")
{
  // On one line, the fifth point behind the fourth: whatever the cubic does, it
  // has to go back to reach it, and it is on the line all the way - so no
  // distance from the line can tell.
  const std::vector<Vector3d> points{{0, 0, 0},   {1, 0, 0}, {2, 0, 0}, {3, 0, 0},
                                     {2.5, 0, 0}, {4, 0, 0}, {5, 0, 0}, {6, 0, 0}};
  CurveFit fit;
  REQUIRE(interpolateCubic(points, fit));
  CHECK(intervalsTurningBack(fit) > 0);
}

TEST_CASE("a crossing run with unevenly spaced vertices is one curve", "[step][crossing]")
{
  // step-band-family's ridge on its bore: a sweep's flank crossing a coaxial
  // cylinder, which is a helix. The mesh's vertices along it are where the
  // bore's 32 facets a turn and the sweep's 32 stations a turn cross each
  // other, so they come in pairs a hundredth of a station apart. Interpolating
  // those vertices, the fit stalled at 2.3e-7 against the 1e-7 it asks for and
  // the run was halved into pieces nothing meets.
  const double r = 20, back = 0.3, depth = 2, turns = 10.0 / 3.0, pitch = 32.0 / turns;
  const std::vector<Vector2d> profile{{back, -4}, {-depth, -1.5}, {-depth, 1.5}, {back, 4}};
  std::string why;
  const auto sweep = SweepSurface::make(Vector3d(0, 0, 4), Vector3d(0, 0, 1), Vector3d(1, 0, 0),
                                        SweepLaws::constant(r, profile), pitch, turns, 108, why);
  REQUIRE(sweep != nullptr);
  const CylinderSurface bore(Vector3d(0, 0, 0), Vector3d(0, 0, 1), r);

  // Where the first flank reaches the bore, at station t: in the meridian plane
  // a line meeting a line.
  const double s = back / (back + depth);
  auto crossing = [&](double t) { return sweep->evaluate(t, s / 4); };
  // The mesh's own vertices along it: the sweep's 107 stations, and the bore's
  // 32 facet edges a turn, which drift in and out of step with them.
  std::vector<double> at;
  for (int i = 0; i <= 107; i++) at.push_back(i / 107.0);
  for (int k = 0; k <= 32 * turns; k++) at.push_back(k / (32 * turns));
  std::sort(at.begin(), at.end());
  at.erase(std::unique(at.begin(), at.end(), [](double x, double y) { return y - x < 1e-12; }),
           at.end());
  std::vector<Vector3d> run;
  for (const double t : at) {
    if (t >= 0.1 && t <= 0.9) run.push_back(crossing(t));
  }
  for (const auto& p : run) REQUIRE(fabs(Vector2d(p.x(), p.y()).norm() - r) < 1e-12);

  CurveFit fit;
  REQUIRE(fitCrossingRun(sweep.get(), &bore, run, 1e-7, fit));
  CHECK(intervalsTurningBack(fit) == 0);
  CHECK((evaluate(fit, 0.0) - run.front()).norm() < 1e-12);
  CHECK((evaluate(fit, 1.0) - run.back()).norm() < 1e-12);
}
