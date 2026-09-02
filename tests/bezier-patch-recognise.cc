// Feed the real bezier_patch() output, and a real edge strip, to the patch
// recogniser and check the regions and boundary runs come out right.
//
// A corner of N rows is (N-1)^2 triangles bounded by three runs of N vertices -
// its base row and its two rails, meeting at the apex. An edge strip of bn
// stations is bn-1 quads bounded by two curved runs of bn (the rails, which are
// what a neighbouring face sees as a row of short segments) and two straight
// runs of 2 (the rulings, which are single edges already). Those counts are the
// substitution the emitter has to perform, so getting them wrong is the whole
// feature failing quietly.
//
// Needs bezier_patch() extracted from src/core/FilletNode.cc, as
// tests/bezier-patch-check.cc describes:
//
//   sed -n '/^Vector3d Bezier(double t/,/^}$/p;/^void bezier_patch/,/^}$/p' \
//       src/core/FilletNode.cc > bezier_patch_body.inc
//   g++ -O2 -std=c++17 -I src -I /usr/include/eigen3 -o /tmp/recog \
//       tests/bezier-patch-recognise.cc src/geometry/Surface.cc \
//       src/geometry/AnalyticFeatures.cc
//
// Exits non-zero on any mismatch.
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "geometry/Surface.h"

struct PolySetBuilder {
  std::vector<Vector3d> verts;
  std::vector<std::vector<int>> polys;
  std::vector<std::shared_ptr<Surface>> surfaces;
  int vertexIndex(const Vector3d& v)
  {
    for (size_t i = 0; i < verts.size(); i++)
      if ((verts[i] - v).norm() < 1e-12) return (int)i;
    verts.push_back(v);
    return (int)verts.size() - 1;
  }
  void appendPolygon(const std::vector<int>& p) { polys.push_back(p); }
  void addSurface(std::shared_ptr<Surface> s) { surfaces.push_back(std::move(s)); }
};

#include "bezier_patch_body.inc"

static Vector3d newell(const std::vector<Vector3d>& v, const std::vector<int>& loop)
{
  Vector3d n = Vector3d::Zero();
  for (size_t i = 0; i < loop.size(); i++) n += v[loop[i]].cross(v[loop[(i + 1) % loop.size()]]);
  return n.norm() > 0 ? n.normalized() : Vector3d(0, 0, 1);
}

static int run_case(const char *what, PolySetBuilder& b, size_t want_facets, size_t want_runs)
{
  std::vector<char> valid(b.polys.size(), 1), hole(b.polys.size(), 0), consumed(b.polys.size(), 0);
  std::vector<Vector3d> normals;
  for (auto& p : b.polys) normals.push_back(newell(b.verts, p));
  AnalyticFeatures::Mesh mesh;
  mesh.vertices = &b.verts;
  mesh.loops = &b.polys;
  mesh.valid = &valid;
  mesh.is_hole = &hole;
  mesh.normals = &normals;
  std::vector<std::string> report;
  auto patches = AnalyticFeatures::recogniseBezierPatches(mesh, b.surfaces, consumed, report);

  size_t live = 0, facets = 0, runs = 0;
  const char *why = "";
  for (auto& p : patches) {
    if (!p.alive) {
      why = p.dropped;
      continue;
    }
    live++;
    facets += p.facets.size();
    runs = p.runs.size();
  }
  const bool ok = live == 1 && facets == want_facets && runs == want_runs;
  printf("%-34s facets %3zu/%-3zu runs %zu/%zu  %s%s\n", what, facets, want_facets, runs, want_runs,
         ok ? "ok" : "MISMATCH ", ok ? "" : why);
  if (ok) {
    for (auto& p : patches)
      if (p.alive)
        for (auto& r : p.runs)
          printf("      edge %d %-8s %2zu vertices\n", r.edge, r.straight ? "straight" : "curved",
                 r.verts.size());
  }
  return ok ? 0 : 1;
}

int main()
{
  int bad = 0;
  for (int N : {3, 5, 12}) {
    PolySetBuilder b;
    Vector3d dir[3] = {Vector3d(1, 0, 0), Vector3d(0, 1, 0), Vector3d(0, 0, 1)};
    bezier_patch(b, Vector3d(0, 0, 0), dir, 0, 0, 0, N);
    char buf[64];
    snprintf(buf, sizeof buf, "corner patch, N=%d", N);
    bad += run_case(buf, b, (size_t)(N - 1) * (N - 1), 3);
  }
  for (int bn : {3, 5, 12}) {
    PolySetBuilder b;
    const Vector3d p1(0, 0, 0), p2(0, 0, 10);
    const Vector3d a1(1, 0, 0), b1v(0, 1, 0), a2(1, 0, 0), b2v(0, 1, 0);
    std::vector<int> r1, r2;
    for (int i = 0; i < bn; i++) {
      const double f = (double)i / (bn - 1);
      r1.push_back(b.vertexIndex(p1 + a1 - 2 * f * a1 + f * f * (a1 + b1v)));
      r2.push_back(b.vertexIndex(p2 + a2 - 2 * f * a2 + f * f * (a2 + b2v)));
    }
    for (int i = 0; i + 1 < bn; i++) b.appendPolygon({r1[i], r1[i + 1], r2[i + 1], r2[i]});
    b.addSurface(std::make_shared<BezierPatchSurface>(
      2, 1, std::vector<Vector3d>{p1 + a1, p2 + a2, p1, p2, p1 + b1v, p2 + b2v}));
    char buf[64];
    snprintf(buf, sizeof buf, "edge strip, bn=%d", bn);
    bad += run_case(buf, b, (size_t)(bn - 1), 4);
  }
  return bad;
}
