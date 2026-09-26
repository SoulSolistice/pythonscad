// The boolean the model built its solid by, carried with the geometry - see
// SolidTree.h and doc/step-recognition-design.md, "Existence is a D0 question
// too". Nothing reads the tree yet; these cases pin what it says, so that the
// classification built on it later has a structure it can trust.
#include "geometry/SolidTree.h"

#include <catch2/catch_all.hpp>
#include <memory>
#include <vector>

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
