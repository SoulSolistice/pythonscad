// The boolean the model built its solid by, carried with the geometry - see
// SolidTree.h and doc/step-recognition-design.md, "Existence is a D0 question
// too". Nothing reads the tree yet; these cases pin what it says, so that the
// classification built on it later has a structure it can trust.
#include "geometry/SolidTree.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

#include "core/enums.h"
#include "geometry/PolySet.h"
#include "geometry/Surface.h"
#include "geometry/manifold/ManifoldGeometry.h"
#include "geometry/manifold/manifoldutils.h"

namespace {

std::shared_ptr<const SolidTree> sphereLeaf(const Vector3d& centre, double r)
{
  auto solid = std::make_shared<DeclaredSolid>();
  solid->bounds.push_back(std::make_shared<SphereSurface>(centre, Vector3d(0, 0, 1), r));
  solid->interior = centre;
  return SolidTree::declared(solid);
}

/*! A cube from `lo` to `hi` as a generator hands one over: its six faces as
 * triangles, its six planes as records, and its solid declared by them. */
std::unique_ptr<PolySet> cube(const Vector3d& lo, const Vector3d& hi)
{
  auto ps = std::make_unique<PolySet>(3, /*convex*/ true);
  for (int i = 0; i < 8; i++) {
    ps->vertices.emplace_back(i & 1 ? hi[0] : lo[0], i & 2 ? hi[1] : lo[1], i & 4 ? hi[2] : lo[2]);
  }
  ps->indices = {{4, 5, 7}, {4, 7, 6}, {2, 3, 1}, {2, 1, 0}, {0, 1, 5}, {0, 5, 4},
                 {1, 3, 7}, {1, 7, 5}, {3, 2, 6}, {3, 6, 7}, {2, 0, 4}, {2, 4, 6}};
  auto solid = std::make_shared<DeclaredSolid>();
  for (int i = 0; i < 3; i++) {
    Vector3d n(0, 0, 0);
    n[i] = 1;
    for (const Vector3d& at : {lo, hi}) {
      auto plane = std::make_shared<PlaneSurface>(at, n);
      ps->surfaces.push_back(plane);
      solid->bounds.push_back(plane);
    }
  }
  solid->interior = (lo + hi) / 2;
  solid->extent = BoundingBox(lo, hi);
  ps->solid_tree = SolidTree::declared(solid);
  return ps;
}

std::shared_ptr<const ManifoldGeometry> manifoldOf(const PolySet& ps)
{
  auto m = ManifoldUtils::createManifoldFromPolySet(ps);
  REQUIRE(m != nullptr);
  return m;
}

}  // namespace

TEST_CASE("a chain of differences flattens, a nested one keeps its nesting", "[solidtree]")
{
  const auto a = sphereLeaf({0, 0, 0}, 3), b = sphereLeaf({1, 0, 0}, 1), c = sphereLeaf({2, 0, 0}, 1);
  using Op = SolidTree::Op;

  // (a - b) - c is a less b less c
  const auto chain = SolidTree::combine(Op::Difference, SolidTree::combine(Op::Difference, a, b), c);
  REQUIRE(chain->op() == Op::Difference);
  CHECK(chain->children().size() == 3);
  CHECK(chain->children()[0] == a);

  // a - (b - c) is not: c puts material back inside b
  const auto nested = SolidTree::combine(Op::Difference, a, SolidTree::combine(Op::Difference, b, c));
  REQUIRE(nested->children().size() == 2);
  CHECK(nested->children()[1]->op() == Op::Difference);

  // a union absorbs unions on either side
  const auto u = SolidTree::combine(Op::Union, SolidTree::combine(Op::Union, a, b),
                                    SolidTree::combine(Op::Union, c, a));
  CHECK(u->children().size() == 4);
  CHECK(u->leaves() == 4);
  CHECK(u->declaredLeaves() == 4);

  // nothing stated is opaque, and an intersection with it keeps it as a leaf
  const auto x = SolidTree::combine(Op::Intersection, a, nullptr);
  REQUIRE(x->children().size() == 2);
  CHECK(x->children()[1]->op() == Op::Opaque);
  CHECK(x->declaredLeaves() == 1);
}

TEST_CASE("a transform moves every declared solid, and one it cannot carry turns opaque", "[solidtree]")
{
  const auto leaf = sphereLeaf({1, 2, 3}, 2);

  Transform3d move = Transform3d::Identity();
  move.translate(Vector3d(10, 0, 0));
  move.rotate(Eigen::AngleAxisd(0.5, Vector3d(0, 0, 1)));
  const auto moved = leaf->transformed(move);
  REQUIRE(moved->op() == SolidTree::Op::Declared);
  CHECK((moved->solid()->interior - move * Vector3d(1, 2, 3)).norm() < 1e-12);
  CHECK((moved->solid()->bounds[0]->refpt - move * Vector3d(1, 2, 3)).norm() < 1e-12);
  // the original is untouched: a tree is shared
  CHECK((leaf->solid()->interior - Vector3d(1, 2, 3)).norm() == 0);

  // A sphere under an uneven scale is no sphere, and no record of one is kept.
  Transform3d squash = Transform3d::Identity();
  squash.scale(Vector3d(1, 1, 2));
  CHECK(leaf->transformed(squash)->op() == SolidTree::Op::Opaque);

  // A cube under a shear is still six planes, so it stays declared.
  const auto ps = cube({0, 0, 0}, {1, 1, 1});
  Transform3d shear = Transform3d::Identity();
  shear.linear()(0, 2) = 0.5;
  const auto sheared = ps->solid_tree->transformed(shear);
  REQUIRE(sheared->op() == SolidTree::Op::Declared);
  CHECK((sheared->solid()->interior - shear * Vector3d(0.5, 0.5, 0.5)).norm() < 1e-12);
}

TEST_CASE("the boolean is carried through Manifold to the exported mesh", "[solidtree]")
{
  using Op = SolidTree::Op;
  const auto block = manifoldOf(*cube({0, 0, 0}, {4, 4, 4}));
  const auto bite = manifoldOf(*cube({3, 3, 3}, {5, 5, 5}));
  const auto notch = manifoldOf(*cube({-1, 1, 1}, {1, 2, 2}));

  const ManifoldGeometry cut = (*block - *bite) - *notch;
  const auto ps = cut.toPolySet();
  REQUIRE(ps->solid_tree != nullptr);
  REQUIRE(ps->solid_tree->op() == Op::Difference);
  CHECK(ps->solid_tree->children().size() == 3);
  CHECK(ps->solid_tree->declaredLeaves() == 3);
  // the first operand is the block, whose interior its generator stated
  CHECK((ps->solid_tree->children()[0]->solid()->interior - Vector3d(2, 2, 2)).norm() == 0);

  // A transform of the result moves the solids with the mesh.
  ManifoldGeometry moved = cut;
  Transform3d up = Transform3d::Identity();
  up.translate(Vector3d(0, 0, 7));
  moved.transform(up);
  CHECK((moved.toPolySet()->solid_tree->children()[0]->solid()->interior - Vector3d(2, 2, 9)).norm() <
        1e-12);

  // A mesh made by something that declares no solid is an opaque leaf.
  PolySet plain = *cube({0, 0, 0}, {1, 1, 1});
  plain.solid_tree = nullptr;
  const ManifoldGeometry mixed = *block + *manifoldOf(plain);
  const auto tree = mixed.toPolySet()->solid_tree;
  REQUIRE(tree->op() == Op::Union);
  CHECK(tree->leaves() == 2);
  CHECK(tree->declaredLeaves() == 1);
}

TEST_CASE("a faceted leaf becomes the envelope its faces span", "[solidtree][envelope]")
{
  // A prism over a square inscribed in r = 10, declared a cylinder: its corners
  // reach only 10 cos 45 = 7.07 along x, but the cylinder its sides are chords
  // of reaches 10, and so does the envelope - a face taken as exact would put
  // the solid's edge at 7.07 and rule out points the model has.
  auto ps = std::make_unique<PolySet>(3);
  for (const double z : {0.0, 5.0}) {
    for (int k = 0; k < 4; k++) {
      const double a = M_PI / 4 + k * M_PI / 2;
      ps->vertices.emplace_back(10 * std::cos(a), 10 * std::sin(a), z);
    }
  }
  ps->indices = {{0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6},
                 {3, 0, 4}, {3, 4, 7}, {0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}};
  ps->surfaces.push_back(std::make_shared<CylinderSurface>(Vector3d(0, 0, 0), Vector3d(0, 0, 1), 10));
  ps->solid_tree = SolidTree::faceted();
  const auto tree = manifoldOf(*ps)->toPolySet()->solid_tree;
  REQUIRE(tree->op() == SolidTree::Op::Enveloped);
  BoundingBox box;
  for (const auto& b : tree->solid()->bounds) box.extend(b->refpt);
  CHECK(box.max()[0] == Catch::Approx(10).margin(1e-12));
  CHECK(box.min()[1] == Catch::Approx(-10).margin(1e-12));
  CHECK(box.max()[2] == Catch::Approx(5).margin(1e-12));

  // With nothing declared over its faces, a faceted solid lies within its
  // corners' box: a tetrahedron's.
  auto tet = std::make_unique<PolySet>(3);
  tet->vertices = {{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {0, 0, 4}};
  tet->indices = {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}};
  tet->solid_tree = SolidTree::faceted();
  const auto t = manifoldOf(*tet)->toPolySet()->solid_tree;
  REQUIRE(t->op() == SolidTree::Op::Enveloped);
  BoundingBox tb;
  for (const auto& b : t->solid()->bounds) tb.extend(b->refpt);
  CHECK((tb.max() - Vector3d(2, 3, 4)).norm() == 0);
  CHECK(tb.min().norm() == 0);
  // and a transform keeps it faceted until it becomes a Manifold
  CHECK(SolidTree::faceted()->transformed(Transform3d::Identity())->op() == SolidTree::Op::Faceted);
}

TEST_CASE("a sweep's envelope is the finite cylinder its laws span", "[envelope]")
{
  // radius 20 throughout, a square profile of half width 1, pitch 5, 3 turns
  // about z from the origin: every point is within 21 of the axis and between
  // z = -1 and 15 + 1.
  const auto sweep = std::make_shared<SweepSurface>(
    Vector3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0),
    SweepLaws::constant(20, {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}), 5.0, 3.0, 0);
  BoundingBox box;
  REQUIRE(sweep->envelope({}, box));
  CHECK(box.max()[0] == Catch::Approx(21).margin(1e-12));
  CHECK(box.min()[1] == Catch::Approx(-21).margin(1e-12));
  CHECK(box.min()[2] == Catch::Approx(-1).margin(1e-12));
  CHECK(box.max()[2] == Catch::Approx(16).margin(1e-12));
  for (int i = 0; i <= 60; i++) {
    for (int j = 0; j <= 16; j++) {
      const Vector3d p = sweep->evaluate(i / 60.0, j / 16.0);
      CHECK(box.contains(p));
    }
  }
}

TEST_CASE("a hull lies within the box its operands state they lie in", "[solidtree][envelope]")
{
  // hull() of two declared cubes, [0,1]^3 and [4,5] x [0,1] x [0,1]: the hull is
  // no boolean of them, but it lies within the box that holds both - x from 0
  // to 5 - and a point beyond it is outside the hull.
  Geometry::Geometries two;
  two.emplace_back(nullptr, manifoldOf(*cube({0, 0, 0}, {1, 1, 1})));
  two.emplace_back(nullptr, manifoldOf(*cube({4, 0, 0}, {5, 1, 1})));
  const auto hull = ManifoldUtils::applyOperator3DManifold(two, OpenSCADOperator::HULL);
  REQUIRE(hull != nullptr);
  const auto tree = hull->getSolidTree();
  REQUIRE(tree->op() == SolidTree::Op::Enveloped);
  BoundingBox box;
  REQUIRE(tree->extent(box));
  CHECK((box.min() - Vector3d(0, 0, 0)).norm() == 0);
  CHECK((box.max() - Vector3d(5, 1, 1)).norm() == 0);

  // An operand stating nothing leaves the hull opaque.
  PolySet plain = *cube({0, 0, 0}, {1, 1, 1});
  plain.solid_tree = nullptr;
  Geometry::Geometries mixed;
  mixed.emplace_back(nullptr, manifoldOf(*cube({4, 0, 0}, {5, 1, 1})));
  mixed.emplace_back(nullptr, manifoldOf(plain));
  CHECK(ManifoldUtils::applyOperator3DManifold(mixed, OpenSCADOperator::HULL)->getSolidTree()->op() ==
        SolidTree::Op::Opaque);

  // Extents through the tree: a union's joined, a difference's first operand's.
  using Op = SolidTree::Op;
  BoundingBox u, d;
  const auto a = manifoldOf(*cube({0, 0, 0}, {1, 1, 1}))->getSolidTree();
  const auto b = manifoldOf(*cube({2, 0, 0}, {3, 1, 1}))->getSolidTree();
  REQUIRE(SolidTree::combine(Op::Union, a, b)->extent(u));
  CHECK((u.max() - Vector3d(3, 1, 1)).norm() == 0);
  REQUIRE(SolidTree::combine(Op::Difference, a, b)->extent(d));
  CHECK((d.max() - Vector3d(1, 1, 1)).norm() == 0);
  CHECK_FALSE(SolidTree::combine(Op::Union, a, SolidTree::opaque())->extent(u));
}

TEST_CASE("copies of one geometry keep a tag each where they meet", "[identity]")
{
  // One cube, evaluated once and placed twice - its tags are the same in both
  // copies, and each copy's planes stand elsewhere. United, every triangle has
  // to name the record of its own copy: the one whose plane holds it.
  const auto once = manifoldOf(*cube({0, 0, 0}, {1, 1, 1}));
  ManifoldGeometry moved = *once;
  Transform3d shift = Transform3d::Identity();
  shift.translate(Vector3d(3, 0, 0));
  moved.transform(shift);
  const auto ps = (*once + moved).toPolySet();
  REQUIRE(ps->surface_tags.size() == ps->indices.size());
  std::map<uint32_t, std::vector<const Surface *>> of_tag;
  for (const auto& r : ps->surfaces) {
    for (const uint32_t t : r->tags) of_tag[t].push_back(r.get());
  }
  std::size_t checked = 0;
  for (std::size_t f = 0; f < ps->indices.size(); f++) {
    const uint32_t t = ps->surface_tags[f];
    REQUIRE(t != 0);
    REQUIRE(of_tag[t].size() == 1);
    for (const int v : ps->indices[f]) CHECK(of_tag[t].front()->residual(ps->vertices[v]).exact());
    checked++;
  }
  CHECK(checked == 24);  // two cubes, twelve triangles each
}
