/*! Where a point is with respect to the model's solid - see Classify.h. */
#include "geometry/recognition/Classify.h"

#include <algorithm>
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

/*! The bounds the point being asked lies on by construction (classifyAt). */
thread_local const std::vector<const Surface *> *constructed_on = nullptr;

bool onByConstruction(const Surface *bound)
{
  return constructed_on != nullptr &&
         std::find(constructed_on->begin(), constructed_on->end(), bound) != constructed_on->end();
}

/*! A bound's normal at p: a plane's its own, a curved bound's its normal
 * there; false where it has none. */
bool normalOf(const Surface *bound, const Vector3d& p, Vector3d& n)
{
  if (const auto *plane = dynamic_cast<const PlaneSurface *>(bound)) n = plane->normdir;
  else if (!bound->normalAt(p, n)) return false;
  return n.norm() > 0;
}

/*! Whether p lies in the convex hull of `at`, all on one plane through p,
 * within `slack`: a point, a segment, or a polygon. */
bool withinHull(const Vector3d& p, std::vector<Vector3d> at, double slack)
{
  // One point each, of those that coincide.
  std::vector<Vector3d> pts;
  for (const Vector3d& x : at) {
    bool seen = false;
    for (const Vector3d& y : pts) seen = seen || (x - y).norm() <= slack;
    if (!seen) pts.push_back(x);
  }
  if (pts.empty()) return false;
  if (pts.size() == 1) return (p - pts[0]).norm() <= slack;
  if (pts.size() == 2) {
    const Vector3d e = pts[1] - pts[0];
    const double s = std::clamp((p - pts[0]).dot(e) / e.squaredNorm(), 0.0, 1.0);
    return (pts[0] + e * s - p).norm() <= slack;
  }
  // A polygon in its plane: p is in their hull where no edge of the hull has
  // it outside - every point on one side of the line through any two of them
  // that the others are all on.
  Vector3d normal(0, 0, 0);
  for (std::size_t i = 1; i + 1 < pts.size(); i++)
    normal += (pts[i] - pts[0]).cross(pts[i + 1] - pts[0]);
  if (normal.norm() == 0) return false;
  normal.normalize();
  for (std::size_t i = 0; i < pts.size(); i++) {
    for (std::size_t j = 0; j < pts.size(); j++) {
      if (i == j) continue;
      const Vector3d edge = normal.cross(pts[j] - pts[i]);  // points to one side of i -> j
      bool all = true;
      for (std::size_t k = 0; k < pts.size() && all; k++)
        all = edge.dot(pts[k] - pts[i]) >= -slack * edge.norm();
      if (all && edge.dot(p - pts[i]) < -slack * edge.norm()) return false;
    }
  }
  return true;
}

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
    if (onByConstruction(bound.get()) || std::fabs(at.value) <= at.bound) {
      out.on.push_back(bound.get());
      continue;
    }
    if ((at.value < 0) != (in.value < 0)) return of(Where::Out, &tree);
  }
  if (!out.on.empty()) out.where = Where::On;
  return out;
}

/*! A region's own answer at p - asked as lying on the bounds it was
 * constructed on, where it was. */
SolidRegion::Answer regionAt(const SolidTree& tree, const Vector3d& p)
{
  return constructed_on != nullptr ? tree.region()->classifyOn(p, *constructed_on)
                                   : tree.region()->classify(p);
}

Classification ofAnswer(const SolidRegion::Answer& a, const SolidTree& tree)
{
  switch (a.where) {
  case SolidRegion::Where::In:  return of(Where::In, &tree);
  case SolidRegion::Where::Out: return of(Where::Out, &tree);
  case SolidRegion::Where::On:  return Classification{Where::On, a.on, &tree, a.sides, a.all};
  default:                      return of(Where::Unknown, &tree);
  }
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
  Vector3d m;                           // unit, the first bound's outward normal
  std::vector<const Surface *> bounds;  // the records on it; a region's own plane face is none
  std::size_t count = 0;                // the bounds on it, one surface counted once
  bool curved = false;
};

/*! Where `p` is, at a point on more than one operand's boundary: each operand
 * keeps the half-space its tangent plane there bounds, and the boolean of
 * those decides (Requicha's neighbourhood classification). The planes through
 * the point - at most three, independent - part the directions into cells;
 * the tree is asked in one direction of each. Every cell in: In; every cell
 * out: Out; else On, and On those planes that part an inside cell from an
 * outside one. Two coinciding planes are one plane - the eps = 0 case,
 * decided exactly. A curved bound tangent to another is not what its tangent
 * plane says there to second order: the cells, open cones of directions, are
 * still right where they disagree - the point is On - but where they all agree
 * a cusp between the two may still touch the point, and that is Unknown. So is
 * more than three planes, or an operand on the point that states no side. A
 * declared leaf keeps the inside of every bound it is on; a region states its
 * side itself (SolidRegion::Answer::sides) - all its bounds' half-spaces at a
 * convex edge, any of them at a concave one - or says nothing. */
Classification neighbourhood(const SolidTree& tree, const Vector3d& p)
{
  using Op = SolidTree::Op;
  std::vector<TangentPlane> planes;
  // Each leaf's own answer at p, and the outward normal of each bound it is on.
  struct LeafAt {
    Classification c;
    std::vector<Vector3d> normals;  // outward, of the bounds it is on
    bool all = true;                // in every half-space they bound, or in any
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
    // A bound through the point, by its outward normal: on the tangent plane
    // it shares with others, one surface counted once.
    auto place = [&](const Vector3d& n, const Surface *b) {
      const bool curved = b != nullptr && dynamic_cast<const PlaneSurface *>(b) == nullptr;
      bool placed = false;
      for (auto& plane : planes) {
        if (!parallel(plane.m, n)) continue;
        placed = true;
        const bool seen = b != nullptr && std::any_of(plane.bounds.begin(), plane.bounds.end(),
                                                      [&](const Surface *x) { return x->sameAs(*b); });
        if (seen) continue;
        if (b != nullptr) plane.bounds.push_back(b);
        plane.count++;
        plane.curved = plane.curved || curved;
      }
      if (!placed) {
        TangentPlane plane{n, {}, 1, curved};
        if (b != nullptr) plane.bounds.push_back(b);
        planes.push_back(std::move(plane));
      }
    };
    LeafAt at;
    at.c = classify(t, p);
    if (at.c.where == Where::Unknown) {
      stated = false;
      return;
    }
    if (at.c.where == Where::On) {
      if (t.op() != Op::Declared) {
        // A region or a hull states the side it keeps, where it can.
        if (at.c.sides.empty()) {
          stated = false;
          return;
        }
        at.all = at.c.all;
        for (const auto& side : at.c.sides) {
          const Vector3d n = side.outward.normalized();
          at.normals.push_back(n);
          place(n, side.bound);
        }
      } else {
        for (const Surface *b : at.c.on) {
          Vector3d n;
          if (!normalOf(b, p, n)) {
            stated = false;
            return;
          }
          n.normalize();
          if (n.dot(t.solid()->interior - p) > 0) n = -n;
          at.normals.push_back(n);
          place(n, b);
        }
      }
    }
    leaves.emplace(&t, std::move(at));
  };
  gather(tree);
  if (!stated || planes.empty() || planes.size() > 3) return of(Where::Unknown);
  bool curved_tangency = false;
  for (const auto& plane : planes)
    curved_tangency = curved_tangency || (plane.curved && plane.count > 1);
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
    // Never 0: d is a cell's, off every plane.
    for (const Vector3d& n : at.normals) {
      if ((n.dot(d) < 0) != at.all) return !at.all;
    }
    return at.all;
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
  if ((!any_out || !any_in) && curved_tangency) return of(Where::Unknown);
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
  case Op::Region:   return ofAnswer(regionAt(tree, p), tree);
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
      if (!normalOf(c.on.front(), p, n)) continue;
      n.normalize();
      if (n.dot(c.because->solid()->interior - p) > 0) n = -n;
      double reach = 0, reach_err = 0;
      if (!supportValue(tree, n, reach, reach_err)) break;
      const double slack = reach_err + forwardError(32, p.norm() + n.norm() + std::fabs(reach));
      // On it, the hull is smooth there and keeps the operand's side: it holds
      // the operand, so at a point on both boundaries its normal cone lies
      // within the operand's - n alone - and it is convex.
      if (reach <= n.dot(p) + slack)
        return Classification{Where::On, c.on, &tree, {SolidRegion::Side{n, c.on.front()}}, true};
      return of(Where::In, &tree);
    }
    // On a surface the hull itself makes - the cone between two rims - with
    // normal n there: out where the hull reaches less far along n or -n than
    // the point, a plane separating them; on it where the hull reaches exactly
    // that far and the point lies in the convex hull of where it does - the
    // hull's face there; out beyond that face.
    if (constructed_on != nullptr) {
      for (const Surface *s : *constructed_on) {
        Vector3d n;
        if (!normalOf(s, p, n)) continue;
        n.normalize();
        for (const double sign : {1.0, -1.0}) {
          const Vector3d m = n * sign;
          Reach r;
          if (!reachOf(tree, m, r)) return of(Where::Unknown, &tree);
          const double slack = 2 * r.err + forwardError(32, p.norm() + std::fabs(r.h));
          if (r.h < m.dot(p) - slack) return of(Where::Out, &tree);
          if (r.h > m.dot(p) + slack) continue;
          if (!r.finite) return of(Where::Unknown, &tree);
          return withinHull(p, r.at, slack) ? Classification{Where::On, {s}, &tree}
                                            : of(Where::Out, &tree);
        }
      }
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

Classification classifyAt(const SolidTree& tree, const Vector3d& p,
                          const std::vector<const Surface *>& on)
{
  const auto *outer = constructed_on;
  constructed_on = &on;
  Classification c = classify(tree, p);
  constructed_on = outer;
  return c;
}

}  // namespace recognition
