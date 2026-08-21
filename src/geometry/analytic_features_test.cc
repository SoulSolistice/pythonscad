// The recogniser behind analytic STEP export, tested on meshes built by hand.
//
// AnalyticFeatures answers one question - which runs of facets were modelled as
// a surface of revolution, and will every face sharing their edges accept the
// substitution - and it answers it from plain loops and a list of declared
// surfaces. Nothing here needs a geometry backend, an exporter or a binary: the
// whole file links against AnalyticFeatures.cc, Surface.cc and Eigen.
//
// That matters because the only other guard on this code is the STEP export
// sanity suite, which builds the application, exports each fixture three times
// and parses the result with a 1000-line validator. These cases run in
// milliseconds and can construct the shape a defect needs, rather than hoping a
// fixture happens to contain it.
#include "geometry/AnalyticFeatures.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "geometry/Surface.h"

using namespace AnalyticFeatures;

namespace {

/*! A faceted cylinder as mergeTriangles() would hand one over: one quad per
 * segment for the wall, plus a cap at either end.
 *
 * `fan_top` splits the top cap into one triangle per segment instead of a
 * single polygon, which is how a wall ends up with a rim bordering one face per
 * facet - the rule that rejects most of what the recogniser fits. */
struct FacetedCylinder {
  std::vector<Vector3d> vertices;
  std::vector<std::vector<int>> loops;
  std::vector<Vector3d> normals;
  std::vector<char> valid, is_hole;

  FacetedCylinder(int fn, double r, double h, bool fan_top = false)
  {
    for (int i = 0; i < fn; i++) {
      const double a = 2 * M_PI * i / fn;
      vertices.emplace_back(r * cos(a), r * sin(a), 0.0);
    }
    for (int i = 0; i < fn; i++) {
      const double a = 2 * M_PI * i / fn;
      vertices.emplace_back(r * cos(a), r * sin(a), h);
    }

    // The wall, counter clockwise seen from outside, normals away from the axis.
    for (int i = 0; i < fn; i++) {
      const int j = (i + 1) % fn;
      loops.push_back({i, j, j + fn, i + fn});
      const double a = 2 * M_PI * (i + 0.5) / fn;
      normals.emplace_back(cos(a), sin(a), 0.0);
    }

    std::vector<int> bottom;
    for (int i = fn - 1; i >= 0; i--) bottom.push_back(i);
    loops.push_back(bottom);
    normals.emplace_back(0.0, 0.0, -1.0);

    if (fan_top) {
      const int hub = (int)vertices.size();
      vertices.emplace_back(0.0, 0.0, h);
      for (int i = 0; i < fn; i++) {
        const int j = (i + 1) % fn;
        loops.push_back({i + fn, j + fn, hub});
        normals.emplace_back(0.0, 0.0, 1.0);
      }
    } else {
      std::vector<int> top;
      for (int i = 0; i < fn; i++) top.push_back(i + fn);
      loops.push_back(top);
      normals.emplace_back(0.0, 0.0, 1.0);
    }

    valid.assign(loops.size(), 1);
    is_hole.assign(loops.size(), 0);
  }

  [[nodiscard]] Mesh mesh() const
  {
    Mesh m;
    m.vertices = &vertices;
    m.loops = &loops;
    m.valid = &valid;
    m.is_hole = &is_hole;
    m.normals = &normals;
    return m;
  }
};

std::vector<std::shared_ptr<Surface>> declaredCylinder(double r)
{
  std::vector<std::shared_ptr<Surface>> surfaces;
  surfaces.push_back(std::make_shared<CylinderSurface>(Vector3d(0, 0, 0), Vector3d(0, 0, 1), r));
  return surfaces;
}

std::size_t consumedCount(const Result& res)
{
  std::size_t n = 0;
  for (char c : res.consumed) n += c ? 1 : 0;
  return n;
}

bool reportMentions(const Result& res, const std::string& text)
{
  for (const auto& line : res.report) {
    if (line.find(text) != std::string::npos) return true;
  }
  return false;
}

/*! The angle of a vertex about the z axis, which is the ruling it sits on. */
double ruling(const std::vector<Vector3d>& vertices, int id)
{
  return atan2(vertices[id][1], vertices[id][0]);
}

}  // namespace

TEST_CASE("fitCircleCentre finds the axis of an arc, which averaging cannot", "[analytic][fit]")
{
  // The case the header records: a 54 degree arc of a radius 78 wall. Its
  // centroid sits inside the chord, nowhere near the axis, and taking it for the
  // centre put the axis at radius 266 and the wall was then rejected for not
  // fitting itself.
  const double r = 78.0, level = 3.0;
  std::vector<Vector3d> verts;
  std::vector<int> ids;
  for (int i = 0; i <= 9; i++) {
    const double a = (54.0 * i / 9.0) * M_PI / 180.0;
    verts.emplace_back(r * cos(a), r * sin(a), level);
    ids.push_back(i);
  }

  Vector3d centroid(0, 0, 0);
  for (const auto& v : verts) centroid += v / (double)verts.size();
  CHECK(centroid.head<2>().norm() > 70.0);  // the averaged answer, for comparison

  Vector3d centre;
  REQUIRE(fitCircleCentre(verts, ids, Vector3d(0, 0, 1), level, centre));
  CHECK(centre[0] == Catch::Approx(0.0).margin(1e-9));
  CHECK(centre[1] == Catch::Approx(0.0).margin(1e-9));
  CHECK(centre[2] == Catch::Approx(level).margin(1e-9));
  for (const auto& v : verts) {
    CHECK(distanceToAxis(v, centre, Vector3d(0, 0, 1)) == Catch::Approx(r).margin(1e-9));
  }
}

TEST_CASE("a declared faceted cylinder is recognised as one closed band", "[analytic]")
{
  const int fn = 8;
  const double r = 10.0, h = 5.0;
  const FacetedCylinder cyl(fn, r, h);

  const Result res = recogniseSurfacesOfRevolution(cyl.mesh(), declaredCylinder(r), 1e-6);

  REQUIRE(res.bands.size() == 1);
  const Band& band = res.bands[0];
  CHECK(band.alive);
  CHECK(band.dropped == nullptr);
  CHECK(band.closed);
  CHECK(band.outward);
  CHECK_FALSE(band.isCone());
  CHECK(band.walls.size() == (std::size_t)fn);
  CHECK(band.r_bottom == Catch::Approx(r));
  CHECK(band.r_top == Catch::Approx(r));
  CHECK(band.height == Catch::Approx(h));
  CHECK(consumedCount(res) == (std::size_t)fn);

  // Both rims are the complete bound of one cap, which is the case that lets the
  // whole loop be replaced by one circle.
  REQUIRE(res.rims.size() == 1);
  CHECK(res.rims[0].first.kind == RimRef::WHOLE_LOOP);
  CHECK(res.rims[0].second.kind == RimRef::WHOLE_LOOP);

  // The report is data the caller prints, and it is the only signal that a wall
  // which should have been written was. Predict it rather than the exit status.
  CHECK(reportMentions(res, "1 surface recognised"));
  CHECK(reportMentions(res, "8 facets replaced"));
}

TEST_CASE("the seam's two ends are on one ruling across atan2's branch cut", "[analytic][seam]")
{
  // A periodic face is closed by a seam running up one ruling, and both of its
  // ends have to be on that same ruling. Choosing each end independently as the
  // rim vertex of smallest angle is what this used to do, and it is wrong
  // exactly here: atan2's cut is at pi, an even sided polygon has a vertex
  // sitting on it, and which side that vertex falls is decided by the sign of a
  // coordinate that is zero to fifteen digits. The two rims of one wall
  // disagreed and the wall was dropped while its neighbours were kept.
  for (const int fn : {4, 6, 8, 12, 32}) {
    const double r = 10.0, h = 5.0;
    const FacetedCylinder cyl(fn, r, h);
    const Result res = recogniseSurfacesOfRevolution(cyl.mesh(), declaredCylinder(r), 1e-6);

    CAPTURE(fn);
    REQUIRE(res.bands.size() == 1);
    const Band& band = res.bands[0];
    REQUIRE(band.alive);
    REQUIRE(band.seam_bottom >= 0);
    REQUIRE(band.seam_top >= 0);
    // Same ruling, and the seam really is vertical: one end on each rim.
    CHECK(ruling(cyl.vertices, band.seam_bottom) ==
          Catch::Approx(ruling(cyl.vertices, band.seam_top)).margin(1e-12));
    CHECK(cyl.vertices[band.seam_bottom][2] == Catch::Approx(0.0));
    CHECK(cyl.vertices[band.seam_top][2] == Catch::Approx(h));
  }
}

TEST_CASE("an undeclared cylinder stays faceted, because it is also a prism", "[analytic][intent]")
{
  // A ring of N quads is exactly the mesh of an N-sided prism, and a cube's four
  // sides fit a cylinder through its corners with zero residual. No measurement
  // of the geometry can tell the two apart, so the fit alone must never decide:
  // geometry comes from the mesh, intent from the primitive.
  const FacetedCylinder cyl(8, 10.0, 5.0);

  const Result res = recogniseSurfacesOfRevolution(cyl.mesh(), {}, 1e-6);

  CHECK(res.bands.empty());
  CHECK(consumedCount(res) == 0);
}

TEST_CASE("a wrongly declared radius leaves the wall faceted", "[analytic][intent]")
{
  // A record is only ever a hint, re-checked against the mesh before it is acted
  // on, which is what makes a loose declaration - including one a model makes
  // for itself through declare_cylinder() - safe to offer.
  const FacetedCylinder cyl(8, 10.0, 5.0);

  const Result res = recogniseSurfacesOfRevolution(cyl.mesh(), declaredCylinder(9.0), 1e-6);

  CHECK(res.bands.empty());
  CHECK(consumedCount(res) == 0);
}

TEST_CASE("a rim bordering one face per facet rejects the band, and says so", "[analytic][rims]")
{
  // The topology gate, which is where the losses are: the bayonet lid had 11
  // exact cylinder fits, every one declared, and produced no analytic surface at
  // all because every rim bordered a taper one facet at a time. Here the top cap
  // is a triangle fan, so each wall facet's top edge borders a different
  // triangle and there is no single loop to rewrite.
  const int fn = 8;
  const double r = 10.0, h = 5.0;
  const FacetedCylinder cyl(fn, r, h, /* fan_top */ true);

  const Result res = recogniseSurfacesOfRevolution(cyl.mesh(), declaredCylinder(r), 1e-6);

  CHECK(consumedCount(res) == 0);
  for (const auto& band : res.bands) CHECK_FALSE(band.alive);
  // A rejected surface is invisible in the output - a band that was never
  // recognised looks exactly like one that was never there - so the rule that
  // rejected it has to reach the report.
  CHECK(reportMentions(res, "the rim borders one face per facet"));
}

TEST_CASE("a rational fillet strip is recognised, boundary and all", "[analytic][bezier]")
{
  // The patch pass has two halves: finding the facets that lie on a declared
  // patch, and deciding which edge of the patch each boundary segment belongs
  // to. Both are geometric, and both have to know the patch is rational - a
  // fillet's arc is a circle, and the parabola through the same control points
  // is 6% of the radius away from it, which is enormous next to the 1e-7
  // tolerance the boundary test uses. Getting that wrong drops every patch of
  // every fillet and reports nothing beyond one line per patch, so the file is
  // silently faceted and still perfectly valid.
  const int bn = 12;
  const Vector3d p1(0, 0, 0), p2(0, 0, 5);  // the two ends of a filleted edge
  const Vector3d ea(1, 0, 0), eb(0, 1, 0);  // towards the two faces, radius 1

  const double w = std::sqrt(0.5);  // cos 45, the weight for a quarter circle
  auto rail = [&](const Vector3d& p, double t) {
    const Vector3d a = p + ea, b = p, c = p + eb;
    const double b0 = (1 - t) * (1 - t), b1 = 2 * t * (1 - t) * w, b2 = t * t;
    return Vector3d((a * b0 + b * b1 + c * b2) / (b0 + b1 + b2));
  };

  std::vector<Vector3d> vertices;
  for (int i = 0; i < bn; i++) vertices.push_back(rail(p1, (double)i / (bn - 1)));
  for (int i = 0; i < bn; i++) vertices.push_back(rail(p2, (double)i / (bn - 1)));

  std::vector<std::vector<int>> loops;
  std::vector<Vector3d> normals;
  for (int i = 0; i + 1 < bn; i++) {
    loops.push_back({i, i + 1, i + 1 + bn, i + bn});
    const Vector3d& a = vertices[loops.back()[0]];
    const Vector3d& b = vertices[loops.back()[1]];
    const Vector3d& c = vertices[loops.back()[2]];
    normals.push_back((b - a).cross(c - b).normalized());
  }
  std::vector<char> valid(loops.size(), 1), is_hole(loops.size(), 0);

  Mesh mesh;
  mesh.vertices = &vertices;
  mesh.loops = &loops;
  mesh.valid = &valid;
  mesh.is_hole = &is_hole;
  mesh.normals = &normals;

  std::vector<std::shared_ptr<Surface>> surfaces;
  surfaces.push_back(std::make_shared<BezierPatchSurface>(
    2, 1, std::vector<Vector3d>{p1 + ea, p2 + ea, p1, p2, p1 + eb, p2 + eb},
    std::vector<double>{1.0, 1.0, w, w, 1.0, 1.0}));

  std::vector<char> consumed(loops.size(), 0);
  std::vector<std::string> report;
  const std::vector<Patch> patches = recogniseBezierPatches(mesh, surfaces, consumed, report);

  for (const auto& line : report) WARN(line);
  REQUIRE(patches.size() == 1);
  const Patch& patch = patches[0];
  // Half one: every facet of the strip lies on the declared patch.
  CHECK(patch.facets.size() == (std::size_t)(bn - 1));
  // Half two: the boundary is classified, so the patch survives and its runs
  // are the two rails and the two straight ends.
  INFO("dropped: " << (patch.dropped != nullptr ? patch.dropped : "not dropped"));
  CHECK(patch.alive);
  CHECK(patch.dropped == nullptr);
  CHECK(patch.runs.size() == 4);
  int curved = 0, straight = 0;
  for (const auto& run : patch.runs) (run.straight ? straight : curved)++;
  CHECK(curved == 2);
  CHECK(straight == 2);
}

TEST_CASE("a rational strip is an exact cylinder and a corner an exact sphere",
          "[analytic][bezier][quadric]")
{
  // Since the fillet's rails went rational these two nets are not splines that
  // happen to fit a quadric closely - they are the quadric, and the whole point
  // of recovering it is that a CAD kernel can offset, thread and pattern a
  // CYLINDRICAL_SURFACE while it merely tolerates a B-spline. The axis and the
  // centre have to come out of the control net exactly, in world coordinates,
  // with nothing assumed about the frame.
  const double w = std::sqrt(0.5);  // cos 45, a quarter circle
  const double tol = 1e-9;

  SECTION("an edge strip is a cylinder quadrant")
  {
    // A quarter round along the z axis at radius 1, from +x to +y.
    const Vector3d p1(0, 0, 0), p2(0, 0, 5);
    const Vector3d ea(1, 0, 0), eb(0, 1, 0);
    const BezierPatchSurface bez(2, 1, std::vector<Vector3d>{p1 + ea, p2 + ea, p1, p2, p1 + eb, p2 + eb},
                                 std::vector<double>{1.0, 1.0, w, w, 1.0, 1.0});

    const auto quadric = quadricOfPatch(bez, tol);
    REQUIRE(quadric != nullptr);
    const auto *cyl = dynamic_cast<const CylinderSurface *>(quadric.get());
    REQUIRE(cyl != nullptr);
    CHECK(cyl->r == Catch::Approx(1.0).margin(1e-12));
    // The axis is *not* the filleted edge: the middle control point is the edge
    // itself, so the arc bulges away from it and turns about the point at
    // distance r from both faces - (1, 1, z) here, which is what a fillet's
    // axis is. Getting this wrong would put the cylinder one radius off in two
    // directions and still fit a circle, so it is worth stating.
    CHECK(distanceToAxis(Vector3d(1, 1, 3), cyl->refpt, cyl->normdir.normalized()) ==
          Catch::Approx(0.0).margin(1e-12));
    // The axis is oriented so the rail sweeps counter clockwise about it, which
    // for this arc - from (0,-1) to (-1,0) about the centre - is -z.
    CHECK(cyl->normdir.normalized().dot(Vector3d(0, 0, 1)) == Catch::Approx(-1.0).margin(1e-12));
  }

  SECTION("a corner is a sphere octant, apex and all")
  {
    // FilletNode's own corner net, translated off the origin so that a
    // recovery which quietly assumed the centre was at zero would fail.
    const Vector3d c(3, -7, 2);
    const Vector3d x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
    const BezierPatchSurface bez(2, 2,
                                 std::vector<Vector3d>{c + x, c + x + y, c + y, c + x + z, c + x + y + z,
                                                       c + y + z, c + z, c + z, c + z},
                                 std::vector<double>{1.0, w, 1.0, w, w * w, w, 1.0, w, 1.0});

    const auto quadric = quadricOfPatch(bez, tol);
    REQUIRE(quadric != nullptr);
    const auto *sph = dynamic_cast<const SphereSurface *>(quadric.get());
    REQUIRE(sph != nullptr);
    CHECK(sph->r == Catch::Approx(1.0).margin(1e-12));
    CHECK((sph->refpt - c).norm() == Catch::Approx(0.0).margin(1e-12));
    // The polar axis is the apex, which is what keeps the octant inside one
    // (theta, phi) rectangle instead of straddling the surface's seam.
    CHECK(sph->normdir.normalized().dot(z) == Catch::Approx(1.0).margin(1e-12));
  }

  SECTION("the same net without weights is a parabola, and is refused")
  {
    // The polynomial patch through these points is 6% of the radius off the
    // cylinder. Refusing it is the difference between writing the surface the
    // mesh is on and writing one it is not.
    const Vector3d p1(0, 0, 0), p2(0, 0, 5);
    const Vector3d ea(1, 0, 0), eb(0, 1, 0);
    const BezierPatchSurface bez(2, 1,
                                 std::vector<Vector3d>{p1 + ea, p2 + ea, p1, p2, p1 + eb, p2 + eb});
    CHECK(quadricOfPatch(bez, tol) == nullptr);
  }

  SECTION("a rail turned against its partner rules a hyperboloid, not a cylinder")
  {
    // This is the mutation the control net alone cannot catch, and the reason
    // quadricOfPatch measures the surface rather than reading its boundary.
    // Both rails are still circles of radius 1 on the same axis - every test
    // made of centres, radii and plane normals passes - but the second is
    // turned a quarter turn against the first, so the ruled surface between
    // them is a hyperboloid that touches the cylinder only at its two ends.
    const Vector3d p1(0, 0, 0), p2(0, 0, 5);
    const Vector3d ea(1, 0, 0), eb(0, 1, 0);
    const Vector3d fa(0, 1, 0), fb(-1, 0, 0);  // the same arc, rotated 90 degrees
    const BezierPatchSurface bez(2, 1, std::vector<Vector3d>{p1 + ea, p2 + fa, p1, p2, p1 + eb, p2 + fb},
                                 std::vector<double>{1.0, 1.0, w, w, 1.0, 1.0});
    CHECK(quadricOfPatch(bez, tol) == nullptr);
  }

  SECTION("a corner whose faces are not perpendicular is not a sphere")
  {
    // A 60 degree dihedral. The rails are still exact circular arcs - that is
    // what the rational weight guarantees - but they are arcs of different
    // circles, so the patch is a genuine spline and stays one.
    const Vector3d c(0, 0, 0);
    const Vector3d x(1, 0, 0), y(0.5, std::sqrt(3.0) / 2, 0), z(0, 0, 1);
    const double wu = std::sqrt(0.5), wv = std::sqrt((1.0 + x.dot(y)) / 2.0);
    const BezierPatchSurface bez(2, 2,
                                 std::vector<Vector3d>{c + x, c + x + y, c + y, c + x + z, c + x + y + z,
                                                       c + y + z, c + z, c + z, c + z},
                                 std::vector<double>{1.0, wv, 1.0, wu, wu * wv, wu, 1.0, wv, 1.0});
    CHECK(quadricOfPatch(bez, tol) == nullptr);
  }
}

namespace {

/*! A tube swept along z, capped at both ends by a fan to a hub on the axis.
 *
 * The caps matter, and not for the geometry. A cap made of the rim's own
 * vertices is a face every one of whose corners lies on the declared sweep, so
 * claiming by corners alone takes it too and the region closes into a shell
 * with no boundary at all. Fanning to a hub off the sweep keeps the caps out of
 * the region by both tests - the hub is not a declared point, and the triangle's
 * middle is not on the sweep either.
 *
 * `dimple` replaces one wall quad with four triangles to an apex pushed out
 * radially, which is a hole in the region that is still a closed mesh: the
 * quad's edges keep exactly two users, and the triangles are off the sweep. */
struct FacetedTube {
  std::vector<Vector3d> vertices;
  std::vector<std::vector<int>> loops;
  std::vector<Vector3d> normals;
  std::vector<char> valid, is_hole;
  int rings, around;

  FacetedTube(int rings_in, int around_in, bool dimple = false, double r = 5.0, double h = 3.0)
    : rings(rings_in), around(around_in)
  {
    for (int i = 0; i < rings; i++) {
      const double z = h * i / (rings - 1);
      for (int j = 0; j < around; j++) {
        const double a = 2 * M_PI * j / around;
        vertices.emplace_back(r * cos(a), r * sin(a), z);
      }
    }
    const int dimple_ring = rings / 2, dimple_col = around / 3;
    for (int i = 0; i + 1 < rings; i++) {
      for (int j = 0; j < around; j++) {
        const int k = (j + 1) % around;
        const int a0 = i * around + j, b0 = i * around + k;
        const int c0 = (i + 1) * around + k, d0 = (i + 1) * around + j;
        const double a = 2 * M_PI * (j + 0.5) / around;
        if (dimple && i == dimple_ring && j == dimple_col) {
          const int apex = (int)vertices.size();
          const Vector3d mid = (vertices[a0] + vertices[b0] + vertices[c0] + vertices[d0]) / 4;
          vertices.emplace_back(mid + Vector3d(cos(a), sin(a), 0.0));
          const int corner[4] = {a0, b0, c0, d0};
          for (int e = 0; e < 4; e++) {
            loops.push_back({corner[e], corner[(e + 1) % 4], apex});
            normals.emplace_back(cos(a), sin(a), 0.0);
          }
          continue;
        }
        loops.push_back({a0, b0, c0, d0});
        normals.emplace_back(cos(a), sin(a), 0.0);
      }
    }
    for (int end = 0; end < 2; end++) {
      const int base = end == 0 ? 0 : (rings - 1) * around;
      const int hub = (int)vertices.size();
      vertices.emplace_back(0.0, 0.0, end == 0 ? 0.0 : h);
      for (int j = 0; j < around; j++) {
        const int k = (j + 1) % around;
        if (end == 0) loops.push_back({base + k, base + j, hub});
        else loops.push_back({base + j, base + k, hub});
        normals.emplace_back(0.0, 0.0, end == 0 ? -1.0 : 1.0);
      }
    }
    valid.assign(loops.size(), 1);
    is_hole.assign(loops.size(), 0);
  }

  [[nodiscard]] Mesh mesh() const
  {
    Mesh m;
    m.vertices = &vertices;
    m.loops = &loops;
    m.valid = &valid;
    m.is_hole = &is_hole;
    m.normals = &normals;
    return m;
  }

  [[nodiscard]] std::vector<std::shared_ptr<Surface>> declared(bool closed) const
  {
    std::vector<Vector3d> net(vertices.begin(), vertices.begin() + rings * around);
    std::vector<std::shared_ptr<Surface>> surfaces;
    surfaces.push_back(std::make_shared<GridSurface>(rings, around, net, closed));
    return surfaces;
  }
};

}  // namespace

TEST_CASE("a sweep closing around its profile is cut rather than seamed", "[analytic][grid]")
{
  // A face on a surface written as an open rectangle cannot be bounded across
  // its own seam. Writing the surface as closed and carrying a seam edge is the
  // alternative, and it depends on the region being the whole tube; cutting
  // does not depend on the trim boundary at all, and the cut runs along mesh
  // edges the two arcs already share.
  const int rings = 5, around = 12;
  FacetedTube tube(rings, around);
  const std::vector<char> consumed(tube.loops.size(), 0);
  std::vector<std::string> report;
  const std::vector<Patch> patches =
    recogniseGridPatches(tube.mesh(), tube.declared(true), consumed, report);

  REQUIRE(patches.size() == 2);
  std::set<std::size_t> seen;
  std::size_t facets = 0;
  for (const auto& patch : patches) {
    REQUIRE(patch.alive);
    facets += patch.facets.size();
    for (const std::size_t f : patch.facets) seen.insert(f);
    // Each arc is a sheet: one boundary, and every run with a single
    // neighbouring face behind it.
    std::set<std::size_t> bounds;
    for (const auto& run : patch.runs) {
      bounds.insert(run.bound);
      CHECK(run.kind != Patch::Run::UNRESOLVED);
    }
    CHECK(bounds.size() == 1);
  }
  // Between them they cover the whole wall exactly once - a cut, not a
  // reduction, and not an overlap either.
  CHECK(facets == std::size_t((rings - 1) * around));
  CHECK(seen.size() == facets);
  CHECK(tube.loops[*seen.begin()].size() == 4);
}

TEST_CASE("a declared sweep with a hole in it keeps both its boundaries", "[analytic][grid]")
{
  // Two boundary cycles, which is what the walk had to learn: it used to stop
  // at the first cycle it closed and compare its length against every boundary
  // edge, so a region with a hole reported that its boundary did not close -
  // true of the walk, not of the region.
  const int rings = 5, around = 12;
  FacetedTube tube(rings, around, /* dimple */ true);
  const std::vector<char> consumed(tube.loops.size(), 0);
  std::vector<std::string> report;
  // Declared open, so the sweep is a ribbon rather than a tube and nothing is
  // cut; the strip closing the profile is not part of it.
  const std::vector<Patch> patches =
    recogniseGridPatches(tube.mesh(), tube.declared(false), consumed, report);

  REQUIRE(patches.size() == 1);
  const Patch& patch = patches.front();
  REQUIRE(patch.alive);
  // The wall, less the strip that closes the profile and less the quad the
  // dimple replaced.
  CHECK(patch.facets.size() == std::size_t((rings - 1) * (around - 1) - 1));

  std::set<std::size_t> bounds;
  for (const auto& run : patch.runs) {
    bounds.insert(run.bound);
    CHECK(run.kind != Patch::Run::UNRESOLVED);
  }
  CHECK(bounds.size() == 2);
}
