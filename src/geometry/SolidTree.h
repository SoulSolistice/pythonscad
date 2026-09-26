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

  /*! This solid moved by `mat`, or null where a bound cannot be represented
   * afterwards - the leaf is then opaque, not wrong. */
  [[nodiscard]] std::shared_ptr<const DeclaredSolid> transformed(const Transform3d& mat) const;
};

class SolidTree
{
public:
  enum class Op { Opaque, Declared, Union, Intersection, Difference };

  /*! A leaf that states nothing about its solid. */
  [[nodiscard]] static std::shared_ptr<const SolidTree> opaque();
  /*! A leaf whose solid its generator declared. */
  [[nodiscard]] static std::shared_ptr<const SolidTree> declared(
    std::shared_ptr<const DeclaredSolid> solid);
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
  [[nodiscard]] std::shared_ptr<const SolidTree> transformed(const Transform3d& mat) const;

  [[nodiscard]] Op op() const { return op_; }
  [[nodiscard]] const std::vector<std::shared_ptr<const SolidTree>>& children() const
  {
    return children_;
  }
  /*! The declared solid of a Declared leaf; null otherwise. */
  [[nodiscard]] const DeclaredSolid *solid() const { return solid_.get(); }
  /*! How many leaves, and how many of them declared. */
  [[nodiscard]] std::size_t leaves() const;
  [[nodiscard]] std::size_t declaredLeaves() const;

private:
  SolidTree(Op op, std::vector<std::shared_ptr<const SolidTree>> children,
            std::shared_ptr<const DeclaredSolid> solid)
    : op_(op), children_(std::move(children)), solid_(std::move(solid))
  {
  }

  Op op_;
  std::vector<std::shared_ptr<const SolidTree>> children_;
  std::shared_ptr<const DeclaredSolid> solid_;
};
