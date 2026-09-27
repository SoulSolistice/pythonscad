/*! Where a point is with respect to the model's solid - see Classify.h. */
#include "geometry/recognition/Classify.h"

#include <cmath>
#include <cstddef>

#include "geometry/Surface.h"

namespace recognition {

namespace {

Classification of(Where where)
{
  return Classification{where, {}};
}

/*! A declared solid: the intersection of the insides of its bounds, the inside
 * of each being the side its interior is on. */
Classification leaf(const DeclaredSolid& solid, const Vector3d& p)
{
  Classification out{Where::In, {}};
  for (const auto& bound : solid.bounds) {
    const Residual at = bound->signedResidual(p);
    const Residual in = bound->signedResidual(solid.interior);
    if (!std::isfinite(at.value) || !std::isfinite(in.value)) return of(Where::Unknown);
    // An interior the declaration's own bound passes through is no interior:
    // the declaration is wrong, and says nothing.
    if (std::fabs(in.value) <= in.bound) return of(Where::Unknown);
    if (std::fabs(at.value) <= at.bound) {
      out.on.push_back(bound.get());
      continue;
    }
    if ((at.value < 0) != (in.value < 0)) return of(Where::Out);
  }
  if (!out.on.empty()) out.where = Where::On;
  return out;
}

/*! The one operand that is On, where every other is `rest` - or Unknown where
 * more than one is On, which needs the half-space each keeps. */
Classification oneOn(const std::vector<Classification>& parts, Where rest)
{
  const Classification *on = nullptr;
  for (const auto& c : parts) {
    if (c.where == rest) continue;
    if (c.where != Where::On || on != nullptr) return of(Where::Unknown);
    on = &c;
  }
  return on != nullptr ? *on : of(rest);
}

}  // namespace

Classification classify(const SolidTree& tree, const Vector3d& p)
{
  using Op = SolidTree::Op;
  switch (tree.op()) {
  case Op::Opaque:   return of(Where::Unknown);
  case Op::Declared: return leaf(*tree.solid(), p);
  case Op::Faceted:
    return of(Where::Unknown);  // resolved where it became a Manifold
  // Known only to lie within its envelope: outside it, outside the leaf.
  case Op::Enveloped:
    return leaf(*tree.solid(), p).where == Where::Out ? of(Where::Out) : of(Where::Unknown);
  default: break;
  }
  std::vector<Classification> parts;
  parts.reserve(tree.children().size());
  for (const auto& child : tree.children()) parts.push_back(classify(*child, p));

  auto any = [&](Where w) {
    for (const auto& c : parts) {
      if (c.where == w) return true;
    }
    return false;
  };
  switch (tree.op()) {
  case Op::Union:
    if (any(Where::In)) return of(Where::In);
    if (any(Where::Unknown)) return of(Where::Unknown);
    return oneOn(parts, Where::Out);
  case Op::Intersection:
    if (any(Where::Out)) return of(Where::Out);
    if (any(Where::Unknown)) return of(Where::Unknown);
    return oneOn(parts, Where::In);
  case Op::Difference: {
    // The first operand less every other: a subtracted operand holding the
    // point takes it out, whatever the first one is.
    for (std::size_t k = 1; k < parts.size(); k++) {
      if (parts[k].where == Where::In) return of(Where::Out);
    }
    const Classification& first = parts.front();
    if (first.where == Where::Out) return of(Where::Out);
    if (any(Where::Unknown)) return of(Where::Unknown);
    // Now the first is In or On and every subtracted one Out or On.
    const std::vector<Classification> cut(parts.begin() + 1, parts.end());
    const Classification cut_on = oneOn(cut, Where::Out);  // Out: none On
    if (first.where == Where::In) return cut_on.where == Where::Out ? of(Where::In) : cut_on;
    return cut_on.where == Where::Out ? first : of(Where::Unknown);
  }
  default: return of(Where::Unknown);
  }
}

}  // namespace recognition
