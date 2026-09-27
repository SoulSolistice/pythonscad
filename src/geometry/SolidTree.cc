/*! The boolean and the declared solids - see SolidTree.h. */
#include "geometry/SolidTree.h"

#include <utility>

#include "geometry/Surface.h"

std::shared_ptr<const DeclaredSolid> DeclaredSolid::transformed(const Transform3d& mat) const
{
  auto out = std::make_shared<DeclaredSolid>();
  out->bounds = transformedSurfaces(bounds, mat);
  if (out->bounds.size() != bounds.size()) return nullptr;
  out->interior = mat * interior;
  if (!extent.isEmpty()) out->extent = mat * extent;
  return out;
}

std::shared_ptr<const SolidTree> SolidTree::opaque()
{
  static const std::shared_ptr<const SolidTree> leaf(new SolidTree(Op::Opaque, {}, nullptr));
  return leaf;
}

std::shared_ptr<const SolidTree> SolidTree::declared(std::shared_ptr<const DeclaredSolid> solid)
{
  if (solid == nullptr || solid->bounds.empty()) return opaque();
  return std::shared_ptr<const SolidTree>(new SolidTree(Op::Declared, {}, std::move(solid)));
}

std::shared_ptr<const SolidTree> SolidTree::faceted()
{
  static const std::shared_ptr<const SolidTree> leaf(new SolidTree(Op::Faceted, {}, nullptr));
  return leaf;
}

std::shared_ptr<const SolidTree> SolidTree::enveloped(std::shared_ptr<const DeclaredSolid> solid)
{
  if (solid == nullptr || solid->bounds.empty()) return opaque();
  return std::shared_ptr<const SolidTree>(new SolidTree(Op::Enveloped, {}, std::move(solid)));
}

std::shared_ptr<const SolidTree> SolidTree::box(const BoundingBox& b)
{
  if (b.isEmpty() || (b.sizes().array() <= 0).any()) return opaque();
  auto solid = std::make_shared<DeclaredSolid>();
  for (int k = 0; k < 3; k++) {
    Vector3d n(0, 0, 0);
    n[k] = 1;
    solid->bounds.push_back(std::make_shared<PlaneSurface>(b.min(), n));
    solid->bounds.push_back(std::make_shared<PlaneSurface>(b.max(), n));
  }
  solid->interior = b.center();
  solid->extent = b;
  return enveloped(solid);
}

bool SolidTree::extent(BoundingBox& out) const
{
  switch (op_) {
  case Op::Declared:
  case Op::Enveloped:
    if (solid_->extent.isEmpty()) return false;
    out = solid_->extent;
    return true;
  case Op::Union: {
    BoundingBox joined;
    for (const auto& child : children_) {
      BoundingBox b;
      if (!child->extent(b)) return false;
      joined.extend(b);
    }
    out = joined;
    return !joined.isEmpty();
  }
  case Op::Intersection: {
    bool any = false;
    for (const auto& child : children_) {
      BoundingBox b;
      if (!child->extent(b)) continue;
      out = any ? out.intersection(b) : b;
      any = true;
    }
    return any;
  }
  case Op::Difference: return !children_.empty() && children_.front()->extent(out);
  default:             return false;
  }
}

std::shared_ptr<const SolidTree> SolidTree::combine(Op op, std::shared_ptr<const SolidTree> lhs,
                                                    std::shared_ptr<const SolidTree> rhs)
{
  if (lhs == nullptr) lhs = opaque();
  if (rhs == nullptr) rhs = opaque();
  std::vector<std::shared_ptr<const SolidTree>> children;
  // A union and an intersection are associative and commutative, so an operand
  // of the same kind is absorbed whichever side it is on; a difference only on
  // its left, (a - b) - c being a - b - c and a - (b - c) not.
  auto absorb = [&](const std::shared_ptr<const SolidTree>& t, bool may) {
    if (may && t->op_ == op) {
      children.insert(children.end(), t->children_.begin(), t->children_.end());
    } else {
      children.push_back(t);
    }
  };
  const bool symmetric = op == Op::Union || op == Op::Intersection;
  absorb(lhs, symmetric || op == Op::Difference);
  absorb(rhs, symmetric);
  return std::shared_ptr<const SolidTree>(new SolidTree(op, std::move(children), nullptr));
}

std::shared_ptr<const SolidTree> SolidTree::transformed(const Transform3d& mat) const
{
  switch (op_) {
  case Op::Opaque:    return opaque();
  case Op::Declared:  return declared(solid_->transformed(mat));
  case Op::Faceted:   return faceted();
  case Op::Enveloped: return enveloped(solid_->transformed(mat));
  default:            break;
  }
  std::vector<std::shared_ptr<const SolidTree>> moved;
  moved.reserve(children_.size());
  for (const auto& child : children_) moved.push_back(child->transformed(mat));
  return std::shared_ptr<const SolidTree>(new SolidTree(op_, std::move(moved), nullptr));
}

std::size_t SolidTree::leaves() const
{
  if (children_.empty()) return 1;
  std::size_t n = 0;
  for (const auto& child : children_) n += child->leaves();
  return n;
}

std::size_t SolidTree::declaredLeaves() const
{
  if (op_ == Op::Declared) return 1;
  std::size_t n = 0;
  for (const auto& child : children_) n += child->declaredLeaves();
  return n;
}
