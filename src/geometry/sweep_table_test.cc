// The 2D declaration channel and the sweep table, tested on profiles built by
// hand: what each edge of a profile is (Profile2d), and what a sweep of it makes
// (SweptSurfaces). Both are pure functions of the declarations, so nothing here
// needs a node, a mesh or a backend.
#include "geometry/Profile2d.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <vector>

namespace {

/*! A regular polygon sampled from a circle, the way circle() samples one. */
VectorOfVector2d sampled(const Vector2d& centre, double r, int n)
{
  VectorOfVector2d out;
  for (int i = 0; i < n; i++) {
    const double t = 2 * M_PI * i / n;
    out.emplace_back(centre + r * Vector2d(cos(t), sin(t)));
  }
  return out;
}

}  // namespace

TEST_CASE("Profile2d: a sampled circle is chords of its arc, a square is lines", "[sweep-table]")
{
  const Arc2d arc{Vector2d(5., 0.), 2.};
  const VectorOfVector2d square{{0., -1.}, {1., -1.}, {1., 1.}, {0., 1.}};
  const Profile2d p = classifyProfile({sampled(Vector2d(5., 0.), 2., 16), square}, {arc}, {});
  REQUIRE(p.loops.size() == 2);
  for (const auto& e : p.loops[0]) {
    CHECK(e.kind == EdgeKind::Arc);
    CHECK(e.record == 0);
  }
  for (const auto& e : p.loops[1]) CHECK(e.kind == EdgeKind::Line);
}

TEST_CASE("Profile2d: an arc is asked at machine precision, not within a threshold", "[sweep-table]")
{
  // A point 1e-9 off the circle was a chord under the 1e-6 the linear extruder
  // used to allow; it is not on the arc, and the edge is not the arc's chord.
  const Arc2d arc{Vector2d(0., 0.), 1.};
  const Vector2d on(1., 0.);
  const Vector2d off = Vector2d(0., 1.) * (1. + 1e-9);
  CHECK(onArc(on, arc));
  CHECK_FALSE(onArc(off, arc));
  const Profile2d p = classifyProfile({VectorOfVector2d{on, off, Vector2d(-1., 0.)}}, {arc}, {});
  CHECK(p.loops[0][0].kind == EdgeKind::Line);
  CHECK(p.loops[0][2].kind == EdgeKind::Arc);  // (-1, 0) back to (1, 0): a diameter, both ends on it
}

TEST_CASE("Profile2d: every edge is classified, the closing one included", "[sweep-table]")
{
  const VectorOfVector2d triangle{{0., 0.}, {1., 0.}, {0., 1.}};
  const Profile2d p = classifyProfile({triangle}, {}, {});
  REQUIRE(p.loops[0].size() == 3);
  CHECK(p.loops[0][2].a == Vector2d(0., 1.));
  CHECK(p.loops[0][2].b == Vector2d(0., 0.));
}
