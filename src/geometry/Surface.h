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

/*! A tensor-product Bezier patch, declared by the generator that drew it.
 *
 * `FilletNode` builds its surfaces from explicit quadratic Bezier control
 * points and then tessellates them, so the exact surface is known before the
 * mesh exists and there is nothing to fit. That is the general rule for
 * splines: there is no unique spline underlying a triangle mesh, so never
 * recover one - have the generator say what it drew.
 *
 * Two shapes occur, and both come out of `FilletNode` by algebra:
 *
 *   an edge fillet   degree (2,1), a 3x2 net. The rail along one end is the
 *                    quadratic Bezier (p + e_fa, p, p + e_fb) - it starts on
 *                    one of the two faces meeting at the edge, is controlled by
 *                    the original edge vertex, and ends on the other. The strip
 *                    is the ruled surface between the two rails.
 *   a corner fillet  degree (2,2), a 3x3 net whose last row is the apex three
 *                    times. It looks like a triangular patch, but every row is
 *                    a quadratic Bezier between the two rails through a control
 *                    point mixing their coordinates, and all three of those are
 *                    quadratic in the row parameter. The degenerate row is a
 *                    singular point, which is how a rounded corner is normally
 *                    written in STEP.
 *
 * The boundary curves need no separate declaration: each is a row or a column
 * of the net, so an exporter reads them off the record the way it reads a
 * torus's two seam circles off theirs. */
class BezierPatchSurface : public Surface
{
public:
  /*! `net` holds (degree_u + 1) * (degree_v + 1) control points, v varying
   * fastest.
   *
   * `weights` is parallel to `net` and may be empty, which means all of them are
   * 1 and the patch is polynomial. A quadratic *polynomial* Bezier through three
   * points is a parabola; the same three points with the middle weight at
   * cos(theta/2), theta being the turn between the end tangents, is exactly a
   * circular arc. That is how a fillet describes the surface it drew - see
   * Bezier() in core/FilletNode.cc - so the weights have to travel with the net
   * or the declaration describes a different surface from the mesh. */
  BezierPatchSurface(int degree_u, int degree_v, std::vector<Vector3d> net,
                     std::vector<double> weights = {});
  void display(const std::vector<Vector3d>& vertices) override;
  int pointMember(std::vector<Vector3d>& vertices, Vector3d pt) override;
  [[nodiscard]] std::shared_ptr<Surface> clone() const override;
  bool transform(const Transform3d& mat) override;
  [[nodiscard]] bool sameAs(const Surface& other) const override;

  [[nodiscard]] const Vector3d& control(int i, int j) const { return net[i * (degree_v + 1) + j]; }
  [[nodiscard]] double weight(int i, int j) const
  {
    return weights.empty() ? 1.0 : weights[i * (degree_v + 1) + j];
  }
  /*! False when every weight is 1, in which case the polynomial arithmetic is
   * used unchanged. */
  [[nodiscard]] bool isRational() const { return !weights.empty(); }
  [[nodiscard]] Vector3d evaluate(double u, double v) const;

  /*! One row (`along_u` false) or one column (true) of the net, which is the
   * boundary curve along that edge of the patch. */
  [[nodiscard]] std::vector<Vector3d> boundary(bool along_u, bool far) const;

  /*! The weights of that same boundary curve, empty when the patch is
   * polynomial. A boundary of a rational patch is a rational curve of the same
   * weights, so an exporter writing the curve needs them too. */
  [[nodiscard]] std::vector<double> boundaryWeights(bool along_u, bool far) const;

  /*! Closest point on the patch to `pt`, by Newton from a grid of starts.
   * Returns false when it does not converge. */
  bool project(const Vector3d& pt, double& u, double& v) const;

  /*! The patch has collapsed to a point along one edge, as a corner fillet's
   * apex row has. */
  [[nodiscard]] bool degenerateAt(bool along_u, bool far) const;

  int degree_u, degree_v;
  std::vector<Vector3d> net;
  std::vector<double> weights;  // empty, or parallel to net

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
