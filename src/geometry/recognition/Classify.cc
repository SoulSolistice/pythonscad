/*! Where a point is with respect to the model's solid - see Classify.h. */
#include "geometry/recognition/Classify.h"

#include <cmath>
#include <cstddef>

#include "geometry/Surface.h"
#include "geometry/recognition/Support.h"
#include "geometry/MachinePrecision.h"

namespace recognition {

namespace {

Classification of(Where where, const SolidTree *because = nullptr)
{
  return Classification{where, {}, because};
}

/*! A declared solid: the intersection of the insides of its bounds, the inside
 * of each being the side its interior is on. */
Classification leaf(const SolidTree& tree, const Vector3d& p)
{
  const DeclaredSolid& solid = *tree.solid();
  Classification out{Where::In, {}, &tree};
  for (const auto& bound : solid.bounds) {
    const Residual at = bound->signedResidual(p);
    const Residual in = bound->signedResidual(solid.interior);
    if (!std::isfinite(at.value) || !std::isfinite(in.value)) return of(Where::Unknown, &tree);
    // An interior the declaration's own bound passes through is no interior:
    // the declaration is wrong, and says nothing.
    if (std::fabs(in.value) <= in.bound) return of(Where::Unknown, &tree);
    if (std::fabs(at.value) <= at.bound) {
      out.on.push_back(bound.get());
      continue;
    }
    if ((at.value < 0) != (in.value < 0)) return of(Where::Out, &tree);
  }
  if (!out.on.empty()) out.where = Where::On;
  return out;
}

/*! The one operand that is On, where every other is `rest` - or Unknown where
 * more than one is On, which needs the half-space each keeps, and which no
 * one operand is the reason for. */
Classification oneOn(const std::vector<Classification>& parts, Where rest)
{
  const Classification *on = nullptr;
  for (const auto& c : parts) {
    if (c.where == rest) continue;
    if (c.where != Where::On || on != nullptr) return of(Where::Unknown);
    on = &c;
  }
  return on != nullptr ? *on : of(rest, parts.empty() ? nullptr : parts.front().because);
}

}  // namespace

Classification classify(const SolidTree& tree, const Vector3d& p)
{
  using Op = SolidTree::Op;
  switch (tree.op()) {
  case Op::Opaque:   return of(Where::Unknown, &tree);
  case Op::Declared: return leaf(tree, p);
  // A region its declaration bounds answers for itself, as exactly.
  case Op::Region:   {
    const SolidRegion::Answer a = tree.region()->classify(p);
    switch (a.where) {
    case SolidRegion::Where::In:  return of(Where::In, &tree);
    case SolidRegion::Where::Out: return of(Where::Out, &tree);
    case SolidRegion::Where::On:  return Classification{Where::On, a.on, &tree};
    default:                      return of(Where::Unknown, &tree);
    }
  }
  case Op::Faceted:
    return of(Where::Unknown, &tree);  // resolved where it became a Manifold
  // Known to lie within its envelope, and to contain every solid it holds:
  // outside the one it is out, inside any of the others it is in. On a held
  // solid's boundary says nothing - the leaf may have material beyond it.
  case Op::Enveloped: {
    if (leaf(tree, p).where == Where::Out) return of(Where::Out, &tree);
    std::vector<Classification> held;
    for (const auto& h : tree.children()) {
      held.push_back(classify(*h, p));
      if (held.back().where == Where::In) return of(Where::In, &tree);
    }
    // On an operand's smooth boundary, with outward normal n: on the hull's
    // where no operand reaches further along n than the point - the hull's
    // supporting plane there would support the operand too, so it is the
    // tangent plane - and inside it where one does (Caratheodory; see
    // Support.h). A hull of solids that state no support value says nothing.
    for (const Classification& c : held) {
      if (c.where != Where::On || c.on.size() != 1 || c.because == nullptr ||
          c.because->solid() == nullptr)
        continue;
      Vector3d n;
      if (!c.on.front()->normalAt(p, n)) continue;
      if (n.dot(c.because->solid()->interior - p) > 0) n = -n;
      double reach = 0, reach_err = 0;
      if (!supportValue(tree, n, reach, reach_err)) break;
      const double slack = reach_err + forwardError(32, p.norm() + n.norm() + std::fabs(reach));
      if (reach <= n.dot(p) + slack) return Classification{Where::On, c.on, &tree};
      return of(Where::In, &tree);
    }
    return of(Where::Unknown, &tree);
  }
  default: break;
  }
  std::vector<Classification> parts;
  parts.reserve(tree.children().size());
  for (const auto& child : tree.children()) parts.push_back(classify(*child, p));

  // The first operand with this answer - the reason given for it.
  auto first = [&](Where w) -> const Classification * {
    for (const auto& c : parts) {
      if (c.where == w) return &c;
    }
    return nullptr;
  };
  switch (tree.op()) {
  case Op::Union:
    if (const auto *c = first(Where::In)) return of(Where::In, c->because);
    if (const auto *c = first(Where::Unknown)) return of(Where::Unknown, c->because);
    return oneOn(parts, Where::Out);
  case Op::Intersection:
    if (const auto *c = first(Where::Out)) return of(Where::Out, c->because);
    if (const auto *c = first(Where::Unknown)) return of(Where::Unknown, c->because);
    return oneOn(parts, Where::In);
  case Op::Difference: {
    // The first operand less every other: a subtracted operand holding the
    // point takes it out, whatever the first one is - and is the reason.
    for (std::size_t k = 1; k < parts.size(); k++) {
      if (parts[k].where == Where::In) return of(Where::Out, parts[k].because);
    }
    const Classification& head = parts.front();
    if (head.where == Where::Out) return of(Where::Out, head.because);
    if (const auto *c = first(Where::Unknown)) return of(Where::Unknown, c->because);
    // Now the first is In or On and every subtracted one Out or On.
    const std::vector<Classification> cut(parts.begin() + 1, parts.end());
    const Classification cut_on = oneOn(cut, Where::Out);  // Out: none On
    if (head.where == Where::In)
      return cut_on.where == Where::Out ? of(Where::In, head.because) : cut_on;
    return cut_on.where == Where::Out ? head : of(Where::Unknown);
  }
  default: return of(Where::Unknown);
  }
}

}  // namespace recognition
