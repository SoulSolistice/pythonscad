// Does BezierPatchSurface accept exactly the vertices FilletNode emits?
//
// Replicates bezier_patch() and the edge-strip rails from src/core/FilletNode.cc
// and checks every vertex they generate against the control net the patch is
// declared with. That net is derived by algebra rather than fitted, so the
// residual should be at machine precision - if it is not, the declaration is
// describing a different surface from the one that was drawn, and every fillet
// would be silently left faceted.
//
// The corner half below is a transcription of bezier_patch(). To check it has
// not drifted, extract the real function and run it against a stub builder that
// records what it emits:
//
//   sed -n '/^Vector3d Bezier(double t/,/^}$/p;/^void bezier_patch/,/^}$/p' \
//       src/core/FilletNode.cc > /tmp/bp.inc
//
// then compile that with a PolySetBuilder providing vertexIndex(), addSurface()
// and appendPolygon(), and call pointMember() on every vertex it records. Doing
// that over 80 configurations - including the handedness swap and a rotated
// frame - checked 6432 emitted vertices with none off the declared surface.
#include "geometry/Surface.h"

#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <random>
#include <vector>

namespace {

/*! Distance to the patch by brute force, so the test never depends on the
 * routine it is testing (nor on a finite-difference normal, which on a ruled
 * patch can come out tangent). */
double brute(const BezierPatchSurface& s, const Vector3d& p)
{
  double best = 1e30;
  for (int i = 0; i <= 300; i++) {
    for (int j = 0; j <= 300; j++) {
      best = std::min(best, (s.evaluate(i / 300.0, j / 300.0) - p).norm());
    }
  }
  return best;
}

Vector3d bez(double t, Vector3d a, Vector3d b, Vector3d c)
{
  return (a * (1 - t) + b * t) * (1 - t) + (b * (1 - t) + c * t) * t;
}

}  // namespace

TEST_CASE("A corner patch accepts every vertex bezier_patch() draws on it", "[geometry][bezier]")
{
  std::vector<Vector3d> dummy;
  int accepted = 0, missed = 0, rejected = 0, wrongly_accepted = 0;

  for (int N : {2, 3, 5, 12, 24}) {
    for (const auto& cfg : std::vector<std::vector<double>>{
           {1, 1, 1, 0}, {2, 3, 1.5, 0}, {1, 1, 1, 2}, {0.7, 2.2, 1.1, 4}}) {
      const double dx = cfg[0], dy = cfg[1], dz = cfg[2], g = cfg[3];
      const Vector3d xdir(dx, 0, 0), ydir(0, dy, 0), zdir(0, 0, dz);
      const Vector3d apex = zdir + g * (xdir + ydir);
      // A corner patch is degree (2,2) with the apex three times over as its
      // last row - the singular point a rounded corner is written with.
      BezierPatchSurface patch(
        2, 2,
        {Vector3d(dx, 0, 0), Vector3d(dx, dy, 0), Vector3d(0, dy, 0), Vector3d(dx, 0, dz),
         Vector3d(dx, dy, dz), Vector3d(0, dy, dz), apex, apex, apex});
      std::vector<Vector3d> pxz, pyz;
      for (int i = 0; i < N; i++) {
        const double t = (double)i / (N - 1);
        pxz.push_back(bez(t, xdir, xdir + zdir, apex));
        pyz.push_back(bez(t, ydir, ydir + zdir, apex));
      }
      for (int i = 0; i < N; i++) {
        if (i == N - 1) {
          (patch.pointMember(dummy, apex) ? accepted : missed)++;
          continue;
        }
        const int M = N - i;
        for (int k = 0; k < M; k++) {
          const double t2 = (double)k / (M - 1);
          const Vector3d mid(pxz[i][0], pyz[i][1], pxz[i][2]);
          (patch.pointMember(dummy, bez(t2, pxz[i], mid, pyz[i])) ? accepted : missed)++;
        }
      }
      // Points deliberately off the surface, counted only once brute force
      // confirms they really are off it.
      for (double u : {0.0, 0.3, 0.7}) {
        for (double v : {0.1, 0.5, 0.9}) {
          const Vector3d p = patch.evaluate(u, v) + Vector3d(0, 0, 2e-2);
          if (brute(patch, p) < 1e-3) continue;
          (patch.pointMember(dummy, p) ? wrongly_accepted : rejected)++;
        }
      }
    }
  }

  // The counts are stated rather than merely required to be non-zero: a patch
  // that silently stops being recognised, or a tessellation that changes shape,
  // both show up here as a different number of vertices tested.
  CHECK(accepted == 1608);
  CHECK(missed == 0);
  CHECK(rejected == 135);
  CHECK(wrongly_accepted == 0);
}

TEST_CASE("An edge strip accepts the two rails FilletNode draws", "[geometry][bezier]")
{
  std::vector<Vector3d> dummy;
  int accepted = 0, missed = 0, rejected = 0, wrongly_accepted = 0;

  // A fixed generator rather than rand(), so the cases are the same set on
  // every platform - rand() is not.
  std::mt19937 gen(7);
  std::uniform_real_distribution<double> unit(-1.0, 1.0);
  auto rnd = [&] { return unit(gen); };

  for (int t = 0; t < 60; t++) {
    const Vector3d p1(rnd(), rnd(), rnd()), p2(rnd(), rnd(), rnd());
    const Vector3d a1(rnd(), rnd(), rnd()), b1(rnd(), rnd(), rnd());
    const Vector3d a2(rnd(), rnd(), rnd()), b2(rnd(), rnd(), rnd());
    const int bn = 2 + (int)(gen() % 30);
    // Expanding the rail p + e_fa - 2f*e_fa + f^2*(e_fa + e_fb) gives the
    // quadratic Bezier control points (p + e_fa, p, p + e_fb); the strip is the
    // ruled surface between two of those, so its net is degree (2,1).
    BezierPatchSurface patch(2, 1, {p1 + a1, p2 + a2, p1, p2, p1 + b1, p2 + b2});
    for (int i = 0; i < bn; i++) {
      const double f = (double)i / (bn - 1);
      (patch.pointMember(dummy, p1 + a1 - 2 * f * a1 + f * f * (a1 + b1)) ? accepted : missed)++;
      (patch.pointMember(dummy, p2 + a2 - 2 * f * a2 + f * f * (a2 + b2)) ? accepted : missed)++;
    }
    for (const auto& dir : {Vector3d(1, 0, 0), Vector3d(0, 1, 0), Vector3d(0, 0, 1)}) {
      const Vector3d p = patch.evaluate(0.5, 0.5) + dir * 2e-2;
      if (brute(patch, p) < 1e-3) continue;  // the offset slid along the surface
      (patch.pointMember(dummy, p) ? wrongly_accepted : rejected)++;
    }
  }

  CHECK(accepted == 1894);
  CHECK(missed == 0);
  CHECK(rejected == 178);
  CHECK(wrongly_accepted == 0);
}

TEST_CASE("A patch's boundary curves come off its control net", "[geometry][bezier]")
{
  const BezierPatchSurface strip(2, 1,
                                 {Vector3d(1, 0, 0), Vector3d(1, 0, 5), Vector3d(0, 0, 0),
                                  Vector3d(0, 0, 5), Vector3d(0, 1, 0), Vector3d(0, 1, 5)});

  const auto rail = strip.boundary(true, false);
  REQUIRE(rail.size() == 3);
  CHECK(rail[0] == Vector3d(1, 0, 0));
  CHECK(rail[1] == Vector3d(0, 0, 0));
  CHECK(rail[2] == Vector3d(0, 1, 0));

  // A strip's ruling is a real edge; only a corner's apex row collapses.
  CHECK_FALSE(strip.degenerateAt(false, false));

  const BezierPatchSurface corner(
    2, 2,
    {Vector3d(1, 0, 0), Vector3d(1, 1, 0), Vector3d(0, 1, 0), Vector3d(1, 0, 1), Vector3d(1, 1, 1),
     Vector3d(0, 1, 1), Vector3d(0, 0, 1), Vector3d(0, 0, 1), Vector3d(0, 0, 1)});
  CHECK(corner.degenerateAt(false, true));
}
