#pragma once

#include <memory>
#include <vector>
#include "geometry/linalg.h"

class Surface
{
public:
  virtual ~Surface() = default;
  Vector3d refpt, normdir;
  virtual void display(const std::vector<Vector3d>& vertices);
  virtual void reverse(void);
  virtual int operator==(const Surface& other);
  virtual int pointMember(std::vector<Vector3d>& vertices, Vector3d pt);

  /*! Independent copy, so that transforming one geometry never mutates a
   * surface another geometry still shares. */
  [[nodiscard]] virtual std::shared_ptr<Surface> clone() const;

  /*! Move the surface with the geometry it describes.
   *
   * Returns false when the result can no longer be represented - a cylinder
   * under a non uniform scale becomes elliptical - in which case the caller has
   * to drop the surface rather than keep a wrong one. */
  virtual bool transform(const Transform3d& mat);

  /*! Whether two records describe the same surface. For deduplication only.
   *
   * Not `operator==`: that one is declared per subclass, taking that subclass,
   * so it never overrides anything, and the base version always returns 0. It
   * cannot answer this question about two `shared_ptr<Surface>`. This can.
   *
   * Deliberately conservative in one direction only. Two records describing one
   * surface but differing - the same infinite cylinder referred to from two
   * heights - are both kept, which costs one more candidate for a fit that
   * would have accepted either. Two records describing *different* surfaces
   * that compared equal would lose one, so the dynamic type is checked before
   * anything else. */
  [[nodiscard]] virtual bool sameAs(const Surface& other) const;
};

/*! Whether `list` already holds a record for the same surface. */
[[nodiscard]] bool containsSurface(const std::vector<std::shared_ptr<Surface>>& list,
                                   const std::shared_ptr<Surface>& surface);

/*! Append the records of `from` that `into` does not already hold.
 *
 * Every boolean keeps both operands' declarations, including the subtracted
 * one: a bore is declared by the cylinder that cut it, and that record is the
 * only statement that the hole was meant to be round. Keeping a record which no
 * longer matches any facet is harmless, because a record is only ever a hint
 * and the exporter re-checks it against the mesh. Deduplication is here only to
 * stop the list growing without bound through a deep boolean tree. */
void mergeSurfaces(std::vector<std::shared_ptr<Surface>>& into,
                   const std::vector<std::shared_ptr<Surface>>& from);

/*! A sphere, declared by the primitive that drew it.
 *
 * Needed for the same reason a cylinder is: an OpenSCAD sphere is a closed
 * polyhedron inscribed in the sphere, and no measurement of it says whether a
 * sphere or a polyhedron was meant. `normdir` carries the polar axis of the
 * tessellation, which an exporter needs to place the surface's own
 * parameterisation but which says nothing geometric - a sphere looks the same
 * from every direction. */
class SphereSurface : public Surface
{
public:
  SphereSurface(Vector3d center, Vector3d normdir, double r);
  void display(const std::vector<Vector3d>& vertices);
  int operator==(const SphereSurface& other);
  int pointMember(std::vector<Vector3d>& vertices, Vector3d pt) override;
  [[nodiscard]] std::shared_ptr<Surface> clone() const override;
  bool transform(const Transform3d& mat) override;
  [[nodiscard]] bool sameAs(const Surface& other) const override;

  double r;

private:
  int operator==(const Surface& other) override { return 0; }
};

/*! A torus: a circle of radius `r_minor` swept round `normdir` at a distance
 * `r_major` from `refpt`.
 *
 * Declared by `rotate_extrude` when its child is a circle, which it can only
 * know by reading the node tree - a 2D outline carries no record of having been
 * one. */
class TorusSurface : public Surface
{
public:
  TorusSurface(Vector3d center, Vector3d normdir, double r_major, double r_minor);
  void display(const std::vector<Vector3d>& vertices);
  void reverse(void);
  int operator==(const TorusSurface& other);
  int pointMember(std::vector<Vector3d>& vertices, Vector3d pt) override;
  [[nodiscard]] std::shared_ptr<Surface> clone() const override;
  bool transform(const Transform3d& mat) override;
  [[nodiscard]] bool sameAs(const Surface& other) const override;

  double r_major, r_minor;

private:
  int operator==(const Surface& other) override { return 0; }
};

class CylinderSurface : public Surface
{
public:
  CylinderSurface(Vector3d center, Vector3d normdir, double r);
  void display(const std::vector<Vector3d>& vertices);
  void reverse(void);
  int operator==(const CylinderSurface& other);
  virtual int pointMember(std::vector<Vector3d>& vertices, Vector3d pt);
  [[nodiscard]] std::shared_ptr<Surface> clone() const override;
  bool transform(const Transform3d& mat) override;
  [[nodiscard]] bool sameAs(const Surface& other) const override;

  double r;

private:
  virtual int operator==(const Surface& other) { return 0; }
};
