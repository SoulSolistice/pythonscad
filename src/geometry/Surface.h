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
};

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

  double r;

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

  double r;

private:
  virtual int operator==(const Surface& other) { return 0; }
};
