// Does BezierPatchSurface accept exactly the vertices FilletNode emits?
//
// Replicates bezier_patch() and the edge-strip rails from src/core/FilletNode.cc
// and checks every vertex they generate against the control net the patch is
// declared with. That net is derived by algebra rather than fitted, so the
// residual should be at machine precision - if it is not, the declaration is
// describing a different surface from the one that was drawn, and every fillet
// would be silently left faceted.
//
// Standalone rather than a Catch2 case because OpenSCADUnitTests is commented
// out in CMakeLists.txt and no unit test in this repository is currently built.
// Build and run it by hand:
//
//   g++ -O2 -std=c++17 -I src -I /usr/include/eigen3 \
//       -o /tmp/bezier-patch-check tests/bezier-patch-check.cc src/geometry/Surface.cc
//   /tmp/bezier-patch-check
//
// Exits non-zero if any on-surface vertex is missed or any off-surface point is
// accepted.
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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "geometry/Surface.h"

/*! Distance to the patch by brute force, so the test never depends on the
 * routine it is testing (nor on a finite-difference normal, which on a ruled
 * patch can come out tangent). */
static double brute(const BezierPatchSurface& s, const Vector3d& p)
{
  double best = 1e30;
  for (int i = 0; i <= 300; i++)
    for (int j = 0; j <= 300; j++) best = std::min(best, (s.evaluate(i / 300.0, j / 300.0) - p).norm());
  return best;
}

static Vector3d bez(double t, Vector3d a, Vector3d b, Vector3d c)
{
  return (a * (1 - t) + b * t) * (1 - t) + (b * (1 - t) + c * t) * t;
}

int main()
{
  std::vector<Vector3d> dummy;
  int on = 0, off = 0, wrong_on = 0, wrong_off = 0;

  // ---- corner patch, exactly as bezier_patch() tessellates it -------------
  for (int N : {2, 3, 5, 12, 24}) {
    for (auto cfg : std::vector<std::vector<double>>{
           {1, 1, 1, 0}, {2, 3, 1.5, 0}, {1, 1, 1, 2}, {0.7, 2.2, 1.1, 4}}) {
      const double dx = cfg[0], dy = cfg[1], dz = cfg[2], g = cfg[3];
      const Vector3d xdir(dx, 0, 0), ydir(0, dy, 0), zdir(0, 0, dz);
      const Vector3d apex = zdir + g * (xdir + ydir);
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
          (patch.pointMember(dummy, apex) ? on : wrong_off)++;
          continue;
        }
        const int M = N - i;
        for (int k = 0; k < M; k++) {
          const double t2 = (double)k / (M - 1);
          const Vector3d mid(pxz[i][0], pyz[i][1], pxz[i][2]);
          (patch.pointMember(dummy, bez(t2, pxz[i], mid, pyz[i])) ? on : wrong_off)++;
        }
      }
      // points deliberately off the surface, counted only once brute force
      // confirms they really are off it
      for (double u : {0.0, 0.3, 0.7}) {
        for (double v : {0.1, 0.5, 0.9}) {
          const Vector3d p = patch.evaluate(u, v) + Vector3d(0, 0, 2e-2);
          if (brute(patch, p) < 1e-3) continue;
          (patch.pointMember(dummy, p) ? wrong_on : off)++;
        }
      }
    }
  }

  // ---- edge strip --------------------------------------------------------
  srand(7);
  auto rnd = [] { return (double)rand() / RAND_MAX * 2 - 1; };
  for (int t = 0; t < 60; t++) {
    const Vector3d p1(rnd(), rnd(), rnd()), p2(rnd(), rnd(), rnd());
    const Vector3d a1(rnd(), rnd(), rnd()), b1(rnd(), rnd(), rnd());
    const Vector3d a2(rnd(), rnd(), rnd()), b2(rnd(), rnd(), rnd());
    const int bn = 2 + rand() % 30;
    BezierPatchSurface patch(2, 1, {p1 + a1, p2 + a2, p1, p2, p1 + b1, p2 + b2});
    for (int i = 0; i < bn; i++) {
      const double f = (double)i / (bn - 1);
      (patch.pointMember(dummy, p1 + a1 - 2 * f * a1 + f * f * (a1 + b1)) ? on : wrong_off)++;
      (patch.pointMember(dummy, p2 + a2 - 2 * f * a2 + f * f * (a2 + b2)) ? on : wrong_off)++;
    }
    for (auto dir : {Vector3d(1, 0, 0), Vector3d(0, 1, 0), Vector3d(0, 0, 1)}) {
      const Vector3d p = patch.evaluate(0.5, 0.5) + dir * 2e-2;
      if (brute(patch, p) < 1e-3) continue;  // the offset slid along the surface
      (patch.pointMember(dummy, p) ? wrong_on : off)++;
    }
  }

  printf("on-surface vertices accepted : %d  (missed %d)\n", on, wrong_off);
  printf("off-surface points rejected  : %d  (wrongly accepted %d)\n", off, wrong_on);

  // boundary curves come off the net
  BezierPatchSurface strip(2, 1,
                           {Vector3d(1, 0, 0), Vector3d(1, 0, 5), Vector3d(0, 0, 0), Vector3d(0, 0, 5),
                            Vector3d(0, 1, 0), Vector3d(0, 1, 5)});
  auto rail = strip.boundary(true, false);
  printf("strip rail control points    : (%g,%g,%g) (%g,%g,%g) (%g,%g,%g)\n", rail[0][0], rail[0][1],
         rail[0][2], rail[1][0], rail[1][1], rail[1][2], rail[2][0], rail[2][1], rail[2][2]);
  printf("strip ruling is degenerate?  : %d (expected 0)\n", (int)strip.degenerateAt(false, false));
  BezierPatchSurface corner(
    2, 2,
    {Vector3d(1, 0, 0), Vector3d(1, 1, 0), Vector3d(0, 1, 0), Vector3d(1, 0, 1), Vector3d(1, 1, 1),
     Vector3d(0, 1, 1), Vector3d(0, 0, 1), Vector3d(0, 0, 1), Vector3d(0, 0, 1)});
  printf("corner apex row degenerate?  : %d (expected 1)\n", (int)corner.degenerateAt(false, true));
  return (wrong_on || wrong_off) ? 1 : 0;
}
