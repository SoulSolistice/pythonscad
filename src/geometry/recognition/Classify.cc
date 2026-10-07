/*! Where a point is with respect to the model's solid - see Classify.h. */
#include "geometry/recognition/Classify.h"

#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <vector>

#include "geometry/Surface.h"
#include "geometry/recognition/Support.h"
#include "geometry/MachinePrecision.h"
#include "geometry/recognition/Exact.h"

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

/*! A tangent plane through the point, with every leaf bound on it. */
struct TangentPlane {
  Vector3d m;  // unit, the first bound's outward normal
  std::vector<const Surface *> bounds;
  bool curved = false;
};

/*! Where `p` is, at a point on more than one operand's boundary: each operand
 * keeps the half-space its tangent plane there bounds, and the boolean of
 * those decides (Requicha's neighbourhood classification). The planes through
 * the point - at most three, independent - part the directions into cells;
 * the tree is asked in one direction of each. Every cell in: In; every cell
 * out: Out; else On, and On those planes that part an inside cell from an
 * outside one. Two coinciding planes are one plane - the eps = 0 case,
 * decided exactly - but a curved bound tangent to another is not what its
 * tangent plane says there, and is Unknown; so is more than three planes, or
 * an operand on the point that states no side. */
Classification neighbourhood(const SolidTree& tree, const Vector3d& p)
{
  using Op = SolidTree::Op;
  std::vector<TangentPlane> planes;
  // Each leaf's own answer at p, and the outward normal of each bound it is on.
  struct LeafAt {
    Classification c;
    std::vector<Vector3d> normals;  // parallel to c.on
  };
  std::map<const SolidTree *, LeafAt> leaves;
  bool stated = true;
  std::function<void(const SolidTree&)> gather = [&](const SolidTree& t) {
    if (!stated) return;
    switch (t.op()) {
    case Op::Union:
    case Op::Intersection:
    case Op::Difference:
      for (const auto& child : t.children()) gather(*child);
      return;
    default: break;
    }
    LeafAt at{classify(t, p), {}};
    if (at.c.where == Where::Unknown) {
      stated = false;
      return;
    }
    if (at.c.where == Where::On) {
      // Only a declared leaf states which side of its bound it keeps.
      if (t.op() != Op::Declared) {
        stated = false;
        return;
      }
      for (const Surface *b : at.c.on) {
        // A plane's normal is its own; a curved bound's, its normal at p.
        Vector3d n;
        const auto *plane = dynamic_cast<const PlaneSurface *>(b);
        if (plane != nullptr) n = plane->normdir;
        else if (!b->normalAt(p, n)) n = Vector3d::Zero();
        if (n.norm() == 0) {
          stated = false;
          return;
        }
        n.normalize();
        if (n.dot(t.solid()->interior - p) > 0) n = -n;
        at.normals.push_back(n);
        const bool curved = dynamic_cast<const PlaneSurface *>(b) == nullptr;
        bool placed = false;
        for (auto& plane : planes) {
          if (!parallel(plane.m, n)) continue;
          plane.bounds.push_back(b);
          plane.curved = plane.curved || curved;
          placed = true;
        }
        if (!placed) planes.push_back(TangentPlane{n, {b}, curved});
      }
    }
    leaves.emplace(&t, std::move(at));
  };
  gather(tree);
  if (!stated || planes.empty() || planes.size() > 3) return of(Where::Unknown);
  for (const auto& plane : planes) {
    if (plane.curved && plane.bounds.size() > 1) return of(Where::Unknown);
  }
  // A direction in each cell: m_i . d = s_i, and nothing along the directions
  // the planes leave free.
  const std::size_t k = planes.size();
  Matrix3d rows = Matrix3d::Zero();
  for (std::size_t i = 0; i < k; i++) rows.row(long(i)) = planes[i].m.transpose();
  if (k == 1) {
    const Vector3d e1 = planes[0].m.unitOrthogonal();
    rows.row(1) = e1.transpose();
    rows.row(2) = planes[0].m.cross(e1).transpose();
  } else if (k == 2) {
    const Vector3d e = planes[0].m.cross(planes[1].m);
    if (e.norm() == 0) return of(Where::Unknown);
    rows.row(2) = e.normalized().transpose();
  }
  if (std::fabs(rows.determinant()) <= forwardError(12, 1.0)) return of(Where::Unknown);
  const Matrix3d inverse = rows.inverse();
  // Whether a direction d from p goes into the subtree's solid.
  std::function<bool(const SolidTree&, const Vector3d&)> into = [&](const SolidTree& t,
                                                                    const Vector3d& d) -> bool {
    switch (t.op()) {
    case Op::Union:
      for (const auto& child : t.children()) {
        if (into(*child, d)) return true;
      }
      return false;
    case Op::Intersection:
      for (const auto& child : t.children()) {
        if (!into(*child, d)) return false;
      }
      return true;
    case Op::Difference:
      if (!into(*t.children().front(), d)) return false;
      for (std::size_t c = 1; c < t.children().size(); c++) {
        if (into(*t.children()[c], d)) return false;
      }
      return true;
    default: break;
    }
    const LeafAt& at = leaves.at(&t);
    if (at.c.where != Where::On) return at.c.where == Where::In;
    for (const Vector3d& n : at.normals) {
      if (n.dot(d) >= 0) return false;  // never 0: d is a cell's, off every plane
    }
    return true;
  };
  const std::size_t cells = std::size_t(1) << k;
  std::vector<char> inside(cells, 0);
  for (std::size_t cell = 0; cell < cells; cell++) {
    Vector3d s(0, 0, 0);
    for (std::size_t i = 0; i < k; i++) s[long(i)] = (cell >> i) & 1 ? 1.0 : -1.0;
    inside[cell] = into(tree, inverse * s) ? 1 : 0;
  }
  bool any_in = false, any_out = false;
  for (const char c : inside) (c != 0 ? any_in : any_out) = true;
  if (!any_out) return of(Where::In, &tree);
  if (!any_in) return of(Where::Out, &tree);
  Classification out{Where::On, {}, &tree};
  for (std::size_t i = 0; i < k; i++) {
    bool parts = false;
    for (std::size_t cell = 0; cell < cells && !parts; cell++) {
      parts = inside[cell] != inside[cell ^ (std::size_t(1) << i)];
    }
    if (parts) out.on.insert(out.on.end(), planes[i].bounds.begin(), planes[i].bounds.end());
  }
  return out;
}

/*! The boolean's answer, and - where it is left open by a point on two
 * operands at once - the half-spaces' (neighbourhood). */
Classification settled(const SolidTree& tree, const Vector3d& p, Classification c)
{
  if (c.where == Where::Unknown && c.because == nullptr) return neighbourhood(tree, p);
  return c;
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
    return settled(tree, p, oneOn(parts, Where::Out));
  case Op::Intersection:
    if (const auto *c = first(Where::Out)) return of(Where::Out, c->because);
    if (const auto *c = first(Where::Unknown)) return of(Where::Unknown, c->because);
    return settled(tree, p, oneOn(parts, Where::In));
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
      return cut_on.where == Where::Out ? of(Where::In, head.because) : settled(tree, p, cut_on);
    return cut_on.where == Where::Out ? head : neighbourhood(tree, p);
  }
  default: return of(Where::Unknown);
  }
}

}  // namespace recognition
