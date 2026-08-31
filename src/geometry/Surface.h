#pragma once

#include <map>
#include <memory>
#include <tuple>
#include <vector>
#include "geometry/linalg.h"

class Surface
{
public:
  virtual ~Surface() = default;
  Vector3d refpt, normdir;
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

/*! Append a surface record unless one describing the same surface is already
 * there. Coaxial cylinders of equal radius count as the same surface; see the
 * definition. */
void addSurfaceUnique(std::vector<std::shared_ptr<Surface>>& list,
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

/*! An ordered grid of points a generator swept, declared by that generator.
 *
 * This is the channel for the geometry OpenSCAD has no primitive for: a helical
 * thread, a cam ramp, anything a model builds with `polyhedron()` over a
 * computed point list. Every other record here says *what* surface the facets
 * lie on - a cylinder of this radius about that axis. This one cannot, because
 * there is no name for the surface. What it says instead is the one thing the
 * mesh loses and the generator still has: the **ordering**.
 *
 * That distinction is measured rather than assumed. Fitting a surface to a run
 * of facets needs to know which follows which along the sweep, and nothing
 * recovers that from an unordered set - a swept quad grid straight from a
 * generator puts every interior vertex at valence 6, but after a boolean has
 * trimmed it against a wall the valence spreads and the ordering is gone. On
 * the reference part the untouched case measures 100% regular and the trimmed
 * one 36%. See uncoveredRegions() in AnalyticFeatures.
 *
 * So the generator hands over its own grid, `rows` stations by `cols` profile
 * points, in the order it generated them. `closed_v` marks a profile which
 * wraps, as a swept tube's does.
 *
 * Membership is by position rather than by projection, and that is deliberate.
 * These points *are* mesh vertices - the generator emitted them - so a facet
 * belongs to the sweep exactly when all its corners are grid points. A boolean
 * which cuts the sweep introduces vertices that are not in the grid, and those
 * facets are correctly excluded rather than approximated. No tolerance to tune
 * and no projection to converge. */
class GridSurface : public Surface
{
public:
  GridSurface(int rows, int cols, std::vector<Vector3d> net, bool closed_v = false);
  int pointMember(std::vector<Vector3d>& vertices, Vector3d pt) override;
  [[nodiscard]] std::shared_ptr<Surface> clone() const override;
  bool transform(const Transform3d& mat) override;
  [[nodiscard]] bool sameAs(const Surface& other) const override;

  [[nodiscard]] const Vector3d& at(int row, int col) const { return net[row * cols + col]; }

  /*! The swept surface itself, not just the points it was declared with.
   *
   * A cubic B-spline interpolating the stations along the sweep, ruled across
   * the profile. Cubic along u because that is the direction the sweep curves
   * in and the stations are dense there; degree 1 across v because the profile
   * is a polyline with corners the model means to keep - a thread's flanks meet
   * at an angle that resists the hose pulling out, and smoothing across them
   * would round away the feature. The same reason the fillet mockup fitted one
   * surface per flank rather than one around the tube.
   *
   * `u` and `v` run 0..1. Available because membership by position alone claims
   * only the facets a boolean never touched - 63 of 300 on a ridge cut at its
   * base - while the other 237 still lie on this surface and only lost their
   * original corners. */
  [[nodiscard]] Vector3d evaluate(double u, double v) const;

  /*! Closest point on the swept surface, by Newton from a coarse sample.
   *
   * Returns false when it does not converge, which the caller must treat as
   * "not on this surface" rather than as an answer. */
  bool project(const Vector3d& pt, double& u, double& v) const;

  /*! Whether `pt` lies on the swept surface, as opposed to being one of the
   * points it was declared with. `tol` is absolute. */
  [[nodiscard]] bool onSurface(const Vector3d& pt, double tol) const;

  /*! How far the swept surface departs from the facets that approximate it.
   *
   * The tolerance membership has to use, and it cannot be a constant. The
   * declared grid describes the smooth surface the generator meant; the mesh is
   * its tessellation, and a boolean cuts the *tessellation* - so the vertices it
   * creates lie on the facets, which stand off the smooth surface by up to the
   * sagitta of a station. At 1e-7 every one of them is refused, which is why
   * declaring the grid alone claimed 63 facets of 300 and projection alone
   * moved that only to 66.
   *
   * So the tolerance is the grid's own: the widest gap between the interpolant
   * and the chords joining the points it was built from. A point within it is
   * indistinguishable from the declared surface at the resolution the model was
   * tessellated at, which is the most any mesh can say. */
  [[nodiscard]] double tessellationBand() const { return band; }

  /*! The tolerance membership is answered at: the tessellation band, floored
   * so that a grid whose interpolant is exact - a sweep along a straight line,
   * where the cubic reproduces the stations and the band is zero - does not end
   * up asking for equality to the last bit. Every caller deciding whether a
   * point is on this surface uses this, or two of them disagree about the same
   * point. */
  /*! Whether a cubic was actually fitted along the sweep. False when the grid
   * was too short for one, or when its stations repeat and leave no parameter -
   * in which case `evaluate` walks the declared points linearly, which is the
   * mesh again. */
  [[nodiscard]] bool interpolated() const { return !poles.empty(); }

  [[nodiscard]] double membershipTolerance() const { return std::max(band, 1e-7); }

  /*! Whether `pt` is one of the points the generator emitted. Exact, by
   * position - no projection, no tolerance to choose. */
  [[nodiscard]] bool isDeclaredPoint(const Vector3d& pt) const;
  /*! The same surface `evaluate` describes, in the form a STEP file wants.
   *
   * `ctrl` is row major over `rows_out` x `cols_out`, and each direction gets
   * its distinct knots with their multiplicities - which is the whole reason
   * this exists separately from `at()`. A fillet's patch is a Bezier and its
   * knots follow from its degree; an interpolated grid has interior knots that
   * come from the chord lengths and cannot be derived from anything the file
   * already carries, so they have to be written out.
   *
   * Returns false for a grid too small to describe (fewer than two rows or
   * columns). A grid of fewer than four rows has no cubic fitted and comes back
   * as the bilinear surface `evaluate` falls back to, which is the same
   * surface, honestly reported at degree 1. */
  [[nodiscard]] bool splineForm(int& degree_u, int& degree_v, int& rows_out, int& cols_out,
                                std::vector<Vector3d>& ctrl, std::vector<double>& knots_u,
                                std::vector<int>& mults_u, std::vector<double>& knots_v,
                                std::vector<int>& mults_v) const;

  int rows = 0, cols = 0;
  bool closed_v = false;
  std::vector<Vector3d> net;

private:
  /*! Positions rounded to a grid, so membership is a lookup rather than a scan
   * over every declared point for every vertex of the mesh. Rebuilt whenever
   * the points move. 1e-9 is far below anything measured and far above one ulp
   * at any plausible model size - the same reasoning as VertexSnapper in
   * core/FilletNode.cc, which exists for the same kind of problem. */
  void reindex();
  /*! How many spans the profile has across it. One more than the number of
   * columns has gaps between them when the profile is declared closed, because
   * the strip from the last column back to the first is part of the sweep. */
  [[nodiscard]] int vspans() const { return closed_v ? cols : cols - 1; }
  /*! A net with its first column repeated at the end, when the profile is
   * closed - the columns the closing strip needs and the declaration does not
   * carry. Returns the net unchanged otherwise. */
  [[nodiscard]] std::vector<Vector3d> withClosingColumn(const std::vector<Vector3d>& src) const;
  /*! Interpolate the stations, so that `evaluate` describes the sweep and not
   * only the points it passes through. Runs on construction and after a move. */
  void buildSpline();
  std::map<std::tuple<int64_t, int64_t, int64_t>, int> lookup;
  std::vector<double> uknots;   // clamped, averaged; empty when rows < 4
  std::vector<Vector3d> poles;  // rows * cols, parallel to net
  double band = 0;              // see tessellationBand()
  int operator==(const Surface& other) override { return 0; }
};
