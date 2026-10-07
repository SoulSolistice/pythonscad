#pragma once

/*! The boolean the model built its solid by, over the solids its generators
 * declared.
 *
 * Identity (Surface::tags) says which surface a facet was made on. It does not
 * say whether the model's solid has a face of that surface there: that has been
 * read off the mesh boolean, which ran on chords, and which is wrong wherever a
 * feature is smaller than a sagitta - a cutter `eps` of 0.1 against a bore whose
 * chords sag 0.107 leaves slivers of a wall the model never shows. Existence
 * has a closed description too: a point of a surface bounds the model's solid
 * where the model's boolean, evaluated over its exact primitives, has material
 * on one side of it and none on the other. This carries what that needs - the
 * boolean, and each primitive's solid as its generator declares it - from where
 * the geometry is made to where it is exported, the way the records travel.
 *
 * A leaf is **declared** or **opaque**. A generator that knows its solid
 * exactly declares it (DeclaredSolid); everything else is opaque, and opaque is
 * an answer - "unknown" - never a guess. A hull and a minkowski sum are leaves
 * of their own: the hull of exact solids is not a boolean of them.
 *
 * Immutable and shared: a boolean makes a new node over its operands' trees, a
 * transform makes a moved copy. Nothing in the evaluator reads it.
 *
 * doc/step-recognition-design.md, "Existence is a D0 question too";
 * doc/step-design-checklist.md, B10.
 */

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "geometry/linalg.h"

class Surface;

/*! A solid its generator declares exactly: the intersection of the insides of
 * `bounds`, the inside of each being the connected part of space, with that
 * surface taken out, in which `interior` lies. Not "the side where its implicit
 * has interior's sign": a cone's surface parts space in three, and a cone to an
 * apex capped at its base would otherwise take in the far nappe as well.
 *
 * Only a generator whose solid is exactly that may declare one - every bound
 * containing the whole solid, so that a point outside any bound is outside the
 * solid: a cylinder or a cone frustum (its wall and its caps), a sphere, a
 * cube. `interior` is the generator's own statement of a point strictly inside,
 * not a measurement. The bounds are this solid's own records, independent of
 * the geometry's list, so that deduplicating that list never changes a solid. */
struct DeclaredSolid {
  std::vector<std::shared_ptr<Surface>> bounds;
  Vector3d interior{0, 0, 0};
  /*! A box the solid lies within, stated by its generator as exactly as it
   * states the solid; empty where it states none. What a hull of this solid
   * can say of where the hull is (SolidTree::extent). */
  BoundingBox extent;

  /*! This solid moved by `mat`, or null where a bound cannot be represented
   * afterwards - the leaf is then opaque, not wrong. */
  [[nodiscard]] std::shared_ptr<const DeclaredSolid> transformed(const Transform3d& mat,
                                                                 const Similarity& similarity) const;
};

/*! A solid its declaration bounds that no intersection of insides describes -
 * the region a closed profile encloses, carried along a sweep, say. Every
 * contribution states its solid, and this is how one that is not convex does:
 * by answering, for a point, where it is - exactly, against the forward error
 * of computing it, or not at all. No tolerance, and never a guess. */
class SolidRegion
{
public:
  enum class Where { In, Out, On, Unknown };
  struct Answer {
    Where where = Where::Unknown;
    /*! Where On: the surfaces of the region's boundary the point is on. */
    std::vector<const Surface *> on;
  };
  virtual ~SolidRegion() = default;
  [[nodiscard]] virtual Answer classify(const Vector3d& p) const = 0;
  /*! The same, for a point that lies on each of `on` by construction - taken
   * onto one of them, say. A region bounded by one of those may answer from
   * that alone; by default it asks as classify() does. */
  [[nodiscard]] virtual Answer classifyOn(const Vector3d& p,
                                          const std::vector<const Surface *>& on) const
  {
    (void)on;
    return classify(p);
  }
  /*! This region moved by `mat`, or null where it cannot be represented
   * afterwards - the leaf is then opaque, not wrong. */
  [[nodiscard]] virtual std::shared_ptr<const SolidRegion> transformed(
    const Transform3d& mat, const Similarity& similarity) const = 0;
  /*! A box the region lies within; false where it states none. */
  virtual bool extent(BoundingBox& out) const = 0;
};

class SolidTree
{
public:
  enum class Op { Opaque, Declared, Region, Faceted, Enveloped, Union, Intersection, Difference };

  // Every leaf carries `what`: a short name its generator gives it - the
  // node's own description, "polyhedron (12 points)", "hull()" - so that a
  // verdict can say which operand decided it, or which stated nothing and left
  // it open (recognition/Existence.h). Leaves carry no ids, and need none
  // elsewhere; this is for the ledger.

  /*! A leaf that states nothing about its solid. */
  [[nodiscard]] static std::shared_ptr<const SolidTree> opaque(const std::string& what = "");
  /*! A leaf whose solid its generator declared. */
  [[nodiscard]] static std::shared_ptr<const SolidTree> declared(
    std::shared_ptr<const DeclaredSolid> solid, const std::string& what = "");
  /*! A leaf whose solid its declaration bounds, and which answers for itself:
   * see SolidRegion. */
  [[nodiscard]] static std::shared_ptr<const SolidTree> region(std::shared_ptr<const SolidRegion> region,
                                                               const std::string& what = "");
  /*! A leaf whose generator declares that its faces are its solid's own
   * boundary - each an exact plane, unless a record declares it a piece of a
   * curved surface - as polyhedron() does. It states no solid a point can be
   * classified against; where it becomes a Manifold it is resolved to the
   * envelope its faces span (Enveloped), or opaque. */
  [[nodiscard]] static std::shared_ptr<const SolidTree> faceted(const std::string& what = "");
  /*! A leaf known only to lie within `solid`: a point outside it is outside the
   * leaf, and nothing more follows. What a faceted leaf resolves to - the box
   * its planar faces' corners and its curved records' envelopes span
   * (Surface::envelope). */
  [[nodiscard]] static std::shared_ptr<const SolidTree> enveloped(
    std::shared_ptr<const DeclaredSolid> solid, const std::string& what = "");
  /*! A leaf known only to lie within `box`: enveloped by its six planes, or
   * opaque where the box has no inside to name a point of. */
  [[nodiscard]] static std::shared_ptr<const SolidTree> box(const BoundingBox& box,
                                                            const std::string& what = "");
  /*! A leaf that lies within `box` and contains every solid in `contained`:
   * outside the box it is out, inside any of them it is in, and nothing more
   * follows. What a hull() is of its operands - the hull of solids holds each
   * of them - whatever they are. The contained trees are the leaf's children;
   * they are no operands of a boolean, and nothing but classification reads
   * them. */
  [[nodiscard]] static std::shared_ptr<const SolidTree> enveloping(
    const BoundingBox& box, std::vector<std::shared_ptr<const SolidTree>> contained,
    const std::string& what = "");
  /*! `op` over two operands - null standing for opaque. A union or an
   * intersection absorbs operands of its own kind, and a difference absorbs a
   * difference on its left: the tree says what the model did, not in which
   * order the evaluator paired its children. A Difference is its first child
   * less every other. */
  [[nodiscard]] static std::shared_ptr<const SolidTree> combine(Op op,
                                                                std::shared_ptr<const SolidTree> lhs,
                                                                std::shared_ptr<const SolidTree> rhs);

  /*! This tree moved by `mat`: every declared leaf moved, a leaf whose solid
   * cannot be moved turned opaque. */
  [[nodiscard]] std::shared_ptr<const SolidTree> transformed(const Transform3d& mat,
                                                             const Similarity& similarity) const;

  [[nodiscard]] Op op() const { return op_; }
  [[nodiscard]] const std::vector<std::shared_ptr<const SolidTree>>& children() const
  {
    return children_;
  }
  /*! The declared solid of a Declared leaf, the envelope of an Enveloped one;
   * null otherwise. */
  [[nodiscard]] const DeclaredSolid *solid() const { return solid_.get(); }
  /*! The region of a Region leaf; null otherwise. */
  [[nodiscard]] const SolidRegion *region() const { return region_.get(); }
  /*! What a reader derived from this tree and keeps with it - computed once:
   * the tree is immutable, so the derivation holds for its life. */
  [[nodiscard]] const std::shared_ptr<const void>& derived() const { return derived_; }
  void keepDerived(std::shared_ptr<const void> d) const { derived_ = std::move(d); }
  /*! A leaf's name, from its generator; empty for a node, or where none was given. */
  [[nodiscard]] const std::string& what() const { return what_; }
  /*! How many leaves, and how many of them declared. */
  [[nodiscard]] std::size_t leaves() const;
  /*! A box this tree's solid lies within, where the model states one: a leaf's
   * own extent; a union's operands' joined, all of them needed; an
   * intersection's any operand's; a difference's first operand's. False where
   * an operand that matters states none. */
  bool extent(BoundingBox& out) const;
  [[nodiscard]] std::size_t declaredLeaves() const;

private:
  SolidTree(Op op, std::vector<std::shared_ptr<const SolidTree>> children,
            std::shared_ptr<const DeclaredSolid> solid, std::string what = "",
            std::shared_ptr<const SolidRegion> region = nullptr)
    : op_(op),
      children_(std::move(children)),
      solid_(std::move(solid)),
      region_(std::move(region)),
      what_(std::move(what))
  {
  }

  Op op_;
  std::vector<std::shared_ptr<const SolidTree>> children_;
  std::shared_ptr<const DeclaredSolid> solid_;
  std::shared_ptr<const SolidRegion> region_;
  mutable std::shared_ptr<const void> derived_;
  std::string what_;
};
