#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include "geometry/MachinePrecision.h"
#include "geometry/linalg.h"

class Surface
{
public:
  virtual ~Surface() = default;
  Vector3d refpt, normdir;
  /*! The originals that declared this surface: the Manifold original ids
   * minted for the geometry that carried this record when it became a
   * Manifold, sorted and unique. Empty until then.
   *
   * This is provenance as identity. Which original made a facet has always
   * been carried (PolySet::original_ids); which surfaces an original declared
   * was not - booleans merge the operands' records into one list and forget
   * who stated what, and the exporter re-derived it from positions. Stamped
   * where an id is minted (stampOwners), copied by clone() and so by every
   * transform, and unioned wherever two records are deduplicated into one, so
   * it answers "did this facet's maker declare this surface?" without a
   * tolerance. See doc/step-export-wip.md, open item 31.
   *
   * Only originals that declared it as a **face** - a surface their own facets
   * lie on. See `witnesses` for the other kind. */
  std::vector<int32_t> owners;
  /*! Originals that declared this surface as **evidence** rather than as a face
   * of their own: `cylinder(r1, r2)` declares its two rim cylinders so the band
   * pass can identify the frustum from both rims, and its facets lie on
   * neither. Kept apart from `owners` because the same geometric surface can be
   * a face of one original and evidence from another - `cylinder(r = 10)` beside
   * a frustum whose rim is r = 10 - and one list would hand the frustum a face it
   * never had. Measured, it did: see open item 31. */
  std::vector<int32_t> witnesses;
  /*! Set by a generator on a record it offers as evidence, before any original
   * id exists; `stampOwners` reads it to decide which list the minted ids go
   * to. Where two records of one surface are merged the face wins, because a
   * surface some original is made of is a face whoever else points at it. */
  bool evidence_only = false;
  /*! The identity a facet made on this surface carries: minted for each record
   * a generator declares as a face, where its geometry becomes a Manifold
   * (stampTags), and written into every triangle of that geometry lying wholly
   * on the surface, as a vertex property Manifold carries through every
   * boolean. A piece of such a triangle that a boolean cut keeps it, whatever
   * its cut corners - so "which surface was this facet made on" is answered by
   * identity rather than by where its corners are (doc/step-design-checklist.md,
   * B7). Copied by clone() and so by every transform; unioned wherever two
   * records are deduplicated into one. Empty for evidence and until stamped. */
  std::vector<uint32_t> tags;
  /*! Whether this record declares a face: after stamping, whether any original
   * owns it; before, whether its generator did not mark it as evidence. */
  [[nodiscard]] bool declaresFace() const
  {
    return !owners.empty() || (witnesses.empty() && !evidence_only);
  }
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

  // What a surface says about a point. Each kind answers for itself, in closed
  // form where it has one; a consumer asks, and never switches on the kind. The
  // defaults are the answer of a surface that has no closed form to give.

  /*! How far `p` is from this surface, and the bound machine precision puts on
   * computing that (MachinePrecision.h) - what D0 membership asks. Infinite for
   * a surface with no closed-form residual. */
  [[nodiscard]] virtual recognition::Residual residual(const Vector3d& p) const;
  /*! The point of this surface nearest `p`, or false where there is none to give. */
  virtual bool closestPoint(const Vector3d& p, Vector3d& out) const;
  /*! The unit normal where this surface passes nearest `p`, or false. */
  virtual bool normalAt(const Vector3d& p, Vector3d& out) const;
  /*! An implicit form: `f` zero on the surface, and its gradient - algebraic for
   * a plane or a quadric. False for a surface with none. */
  virtual bool implicitAt(const Vector3d& p, double& f, Vector3d& grad) const;
  /*! Whether this surface is a quadric: it states where it is algebraically, and
   * its implicit form is global. */
  [[nodiscard]] virtual bool isQuadric() const { return false; }
  /*! The quadric as (X - origin)' M (X - origin) = constant, where it is one a
   * plane section is taken of - see planeSectionConic in io/StepKernel.cc. */
  virtual bool quadricForm(Matrix3d& M, Vector3d& origin, double& constant) const { return false; }

  /*! This surface moved with the geometry it describes - possibly a different
   * kind of surface, where the map takes it to one (a cylinder under an uneven
   * scale is elliptic) - or null where the result cannot be represented. The
   * declarers travel with it. */
  [[nodiscard]] virtual std::shared_ptr<Surface> transformed(const Transform3d& mat) const;
};

/*! Every record of `list` moved by `mat` - see Surface::transformed - and those
 * that cannot be represented afterwards dropped rather than kept wrong. Copies:
 * a record may still be shared with another geometry. */
[[nodiscard]] std::vector<std::shared_ptr<Surface>> transformedSurfaces(
  const std::vector<std::shared_ptr<Surface>>& list, const Transform3d& mat);

/*! Whether two records describe one geometric surface - `sameAs`, plus a
 * cylinder or cone declared from two points along its own axis. Two records of
 * one wall are kept apart in a list (see mergeSurfaces), so any question about
 * the *surface* rather than the record - who declared it, above all - has to be
 * asked through this. */
[[nodiscard]] bool describesSameSurface(const Surface& a, const Surface& b);

/*! Whether `list` already holds a record for the same surface. */
[[nodiscard]] bool containsSurface(const std::vector<std::shared_ptr<Surface>>& list,
                                   const std::shared_ptr<Surface>& surface);

/*! Append a surface record unless one describing the same surface is already
 * there, and say which happened. Coaxial cylinders of equal radius count as the
 * same surface, and so do cones on one axis line with one slope through one
 * radius; see the definition.
 *
 * False therefore means "the model already states this surface", which is worth
 * knowing to a caller that has just *fitted* one: the declaration was there all
 * along and something refused it. */
bool addSurfaceUnique(std::vector<std::shared_ptr<Surface>>& list,
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

/*! The records of `list`, each declared by exactly `ids` - the ids just minted
 * for the geometry that carries them - as a face where the record declares one
 * (`declaresFace`) and as evidence where it does not. Copies rather than
 * mutates: a record may be shared with a cached geometry. Returns `list`
 * unchanged when `ids` is empty, because no id means nothing to say, not
 * "declared by nobody". */
[[nodiscard]] std::vector<std::shared_ptr<Surface>> stampOwners(
  const std::vector<std::shared_ptr<Surface>>& list, const std::set<uint32_t>& ids);

/*! The records of `list`, each face a generator declared given an identity tag
 * of its own - see Surface::tags. Copies rather than mutates, as stampOwners.
 * Called where the geometry that carries them becomes a Manifold, the moment
 * its facets are still the generator's own. */
[[nodiscard]] std::vector<std::shared_ptr<Surface>> stampTags(
  const std::vector<std::shared_ptr<Surface>>& list);

/*! `kept` carrying the declarers of `twin` as well - owners, witnesses, and the
 * face winning over evidence - itself when nothing is new, a copy otherwise.
 * What deduplication has to do with the record it keeps, or the dropped twin's
 * declarers are lost. */
[[nodiscard]] std::shared_ptr<Surface> withDeclarersOf(const std::shared_ptr<Surface>& kept,
                                                       const Surface& twin);

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

  [[nodiscard]] recognition::Residual residual(const Vector3d& p) const override;
  bool closestPoint(const Vector3d& p, Vector3d& out) const override;
  bool normalAt(const Vector3d& p, Vector3d& out) const override;
  bool implicitAt(const Vector3d& p, double& f, Vector3d& grad) const override;
  [[nodiscard]] bool isQuadric() const override { return true; }

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

  [[nodiscard]] recognition::Residual residual(const Vector3d& p) const override;
  bool normalAt(const Vector3d& p, Vector3d& out) const override;

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

/*! A plane, declared by the model that meant a flat face.
 *
 * The one surface a model could not state until now, and the gap was not
 * cosmetic: the corner placement has to tell a face of the part from a chord of
 * some curved surface's tessellation, and with nothing declared it had to infer
 * that from geometry. The inference works and is measured, but it rests on a
 * threshold - see doc/step-export-development.md, *Corner placement*, where the
 * closest pair of cases sits at 0.885 against 0.643. A declaration turns it into
 * a lookup, and gives the inference, still needed wherever nothing declares
 * (hull(), minkowski(), an imported mesh), something to be checked against.
 *
 * Held as the base class's `refpt` and `normdir` and nothing else, so two
 * records of the same plane anchored at different points are the same surface -
 * which is why `sameAs` compares the plane rather than the point, unlike every
 * other subclass here. */
class PlaneSurface : public Surface
{
public:
  PlaneSurface(Vector3d refpt, Vector3d normdir);
  int operator==(const PlaneSurface& other);
  int pointMember(std::vector<Vector3d>& vertices, Vector3d pt) override;
  [[nodiscard]] std::shared_ptr<Surface> clone() const override;
  [[nodiscard]] bool sameAs(const Surface& other) const override;

  [[nodiscard]] recognition::Residual residual(const Vector3d& p) const override;
  bool closestPoint(const Vector3d& p, Vector3d& out) const override;
  bool implicitAt(const Vector3d& p, double& f, Vector3d& grad) const override;

  [[nodiscard]] std::shared_ptr<Surface> transformed(const Transform3d& mat) const override;

private:
  int operator==(const Surface& other) override { return 0; }
};

/*! A surface about an axis: a cylinder, a cone - a surface every plane across
 * its axis cuts in one closed curve round it. The family the trimmed-quadric
 * pass claims on, and each member states what that pass asks of it, so the
 * pass never switches on which member it has. */
class AxialSurface : public Surface
{
public:
  [[nodiscard]] Vector3d axis() const { return normdir.normalized(); }
  /*! Whether this record describes a surface at all - a cylinder of no radius
   * does not. */
  [[nodiscard]] virtual bool usable() const = 0;
  /*! How far the surface leans along its axis: |n . axis| for its unit normal n
   * - zero for a cylinder, the sine of the half angle for a cone. */
  [[nodiscard]] virtual double lean() const = 0;
  /*! The perpendicular distance from `p` to the surface - on a cone that is the
   * radius difference times the cosine of the half angle, and it is the
   * perpendicular distance a tessellation band bounds. */
  [[nodiscard]] virtual double offset(const Vector3d& p) const = 0;
  /*! The direction angles round the axis are measured from, and the one a
   * quarter turn on. */
  virtual void frame(Vector3d& ref, Vector3d& ref2) const;
  /*! The surface's own angle round the axis at `p`, in [0, 2 pi): the parameter
   * a turn is counted in. */
  [[nodiscard]] virtual double angle(const Vector3d& p) const;
  /*! Whether a plane across the axis cuts it in a circle - a cylinder's and a
   * cone's sections are, and a writer reads that off the surface as an arc;
   * any other section is the conic it is. */
  [[nodiscard]] virtual bool circularSections() const { return true; }
  /*! What a report calls it, and the radius it names it by. */
  [[nodiscard]] virtual const char *noun() const = 0;
  [[nodiscard]] virtual double size() const = 0;
};

class CylinderSurface : public AxialSurface
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

  [[nodiscard]] recognition::Residual residual(const Vector3d& p) const override;
  bool closestPoint(const Vector3d& p, Vector3d& out) const override;
  bool normalAt(const Vector3d& p, Vector3d& out) const override;
  bool implicitAt(const Vector3d& p, double& f, Vector3d& grad) const override;
  [[nodiscard]] bool isQuadric() const override { return true; }

  [[nodiscard]] bool usable() const override { return r > 0; }
  [[nodiscard]] double lean() const override { return 0.0; }
  [[nodiscard]] double offset(const Vector3d& p) const override;
  [[nodiscard]] const char *noun() const override { return "cylinder"; }
  [[nodiscard]] double size() const override { return r; }

  bool quadricForm(Matrix3d& M, Vector3d& origin, double& constant) const override;
  [[nodiscard]] std::shared_ptr<Surface> transformed(const Transform3d& mat) const override;

private:
  virtual int operator==(const Surface& other) { return 0; }
};

/*! A cone, declared by the model that meant one.
 *
 * The recogniser has always been able to *fit* a frustum; what it lacked was
 * anything to check the fit against. Its rule was that both rims had to match a
 * declared cylinder, on the argument that a `hull()` of two coaxial cylinders
 * declares both and never the cone between them - true, and what
 * step-chamfered-cylinder asserts. The rule fails wherever a cone's far rim is
 * only where the cone was *cut*: on the reference lid three chamfers run from a
 * declared radius to one a boolean computed, and asking that trim to declare
 * itself is asking the wrong thing of it. This is the other way to state the
 * intent - name the cone.
 *
 * Stored as a radius and a slope rather than as an apex and a half angle. A
 * cone is infinite and its apex may be far outside the part or, for a nearly
 * cylindrical taper, absurdly far; radius-at-a-point is well conditioned
 * everywhere and is also the form the recogniser asks its question in. The
 * radius at a point `p` on the axis is `r + slope * (p - refpt).dot(normdir)`.
 *
 * `slope` is dimensionless, so a uniform scale leaves it alone and only `r`
 * moves - and reversing the axis negates it, because the radius now grows the
 * other way. A slope of zero is a cylinder and is refused: CylinderSurface
 * already says that, and a cone of no taper has no apex to place. */
class ConeSurface : public AxialSurface
{
public:
  ConeSurface(Vector3d refpt, Vector3d normdir, double r, double slope);
  void reverse(void) override;
  int operator==(const ConeSurface& other);
  int pointMember(std::vector<Vector3d>& vertices, Vector3d pt) override;
  [[nodiscard]] std::shared_ptr<Surface> clone() const override;
  bool transform(const Transform3d& mat) override;
  [[nodiscard]] bool sameAs(const Surface& other) const override;

  /*! The radius this cone has where the axis passes `pt`'s own height. */
  [[nodiscard]] double radiusAt(const Vector3d& pt) const;

  double r, slope;

  [[nodiscard]] recognition::Residual residual(const Vector3d& p) const override;
  bool closestPoint(const Vector3d& p, Vector3d& out) const override;
  bool normalAt(const Vector3d& p, Vector3d& out) const override;
  bool implicitAt(const Vector3d& p, double& f, Vector3d& grad) const override;
  [[nodiscard]] bool isQuadric() const override { return true; }

  [[nodiscard]] bool usable() const override { return true; }
  [[nodiscard]] double lean() const override { return fabs(slope) / sqrt(1 + slope * slope); }
  [[nodiscard]] double offset(const Vector3d& p) const override;
  [[nodiscard]] const char *noun() const override { return "cone"; }
  [[nodiscard]] double size() const override { return r; }

  bool quadricForm(Matrix3d& M, Vector3d& origin, double& constant) const override;

private:
  virtual int operator==(const Surface& other) { return 0; }
};

/*! An ellipse swept along a straight line.
 *
 * What a circular cylinder becomes under any affine map that is not a
 * similarity - an uneven scale, a shear - and what a circle sweeps extruded along
 * a direction that is not its plane's normal. One surface, and a quadric:
 * (X - O)' M (X - O) = 1 with M = e1 e1' / a^2 + e2 e2' / b^2.
 *
 * Held canonically, so that two records of one surface are one whichever section
 * declared them: `refpt` a point of the axis, `normdir` the sweep direction, and
 * `major`, `minor` the principal semi-axes of the section *across* the sweep,
 * |major| >= |minor|, with normdir x major along minor. A circular section, a
 * = b, is a circular cylinder, and says so where it matters (sameAs).
 *
 * Written as the SURFACE_OF_LINEAR_EXTRUSION of that section's ELLIPSE. */
class EllipticCylinderSurface : public AxialSurface
{
public:
  /*! From any section: its `centre` and two conjugate semi-diameters `u` and `v`
   * - the section is centre + u cos t + v sin t - in any plane the sweep
   * `direction` crosses. */
  EllipticCylinderSurface(const Vector3d& centre, const Vector3d& direction, const Vector3d& u,
                          const Vector3d& v);

  [[nodiscard]] std::shared_ptr<Surface> clone() const override;
  bool transform(const Transform3d& mat) override;
  [[nodiscard]] bool sameAs(const Surface& other) const override;

  [[nodiscard]] bool usable() const override;
  [[nodiscard]] double lean() const override { return 0.0; }
  [[nodiscard]] double offset(const Vector3d& p) const override;
  void frame(Vector3d& ref, Vector3d& ref2) const override;
  [[nodiscard]] double angle(const Vector3d& p) const override;
  [[nodiscard]] bool circularSections() const override { return false; }
  [[nodiscard]] const char *noun() const override { return "elliptic cylinder"; }
  [[nodiscard]] double size() const override { return a(); }

  [[nodiscard]] recognition::Residual residual(const Vector3d& p) const override;
  bool normalAt(const Vector3d& p, Vector3d& out) const override;
  bool implicitAt(const Vector3d& p, double& f, Vector3d& grad) const override;
  [[nodiscard]] bool isQuadric() const override { return true; }
  bool quadricForm(Matrix3d& M, Vector3d& origin, double& constant) const override;

  [[nodiscard]] double a() const { return major.norm(); }
  [[nodiscard]] double b() const { return minor.norm(); }

  Vector3d major, minor;

private:
  /*! Where `p` is in the section across the sweep, in the principal frame. */
  void section(const Vector3d& p, double& x, double& y) const;
  int operator==(const Surface& other) override { return 0; }
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

  /*! Build one from a declared grid of rows, or say why the grid is not one.
   *
   * The rules live here rather than in either front end because both languages
   * declare the same thing and must reject the same grids for the same reasons.
   * They were written twice once - against the Python C API and again against
   * OpenSCAD's Value - and two copies of a validation rule drift, which on a
   * declaration channel is the worst place for it: a grid wrongly accepted is a
   * surface the exporter then measures facets against.
   *
   * Returns nullptr and fills `why` on failure. `why` is a complete sentence
   * naming the offending row, so a caller can put it straight in front of a
   * user. */
  static std::shared_ptr<GridSurface> fromRows(const std::vector<std::vector<Vector3d>>& rows,
                                               bool closed_v, std::string& why);
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
  [[nodiscard]] virtual Vector3d evaluate(double u, double v) const;

  /*! Closest point on the swept surface, by Newton from a coarse sample.
   *
   * Returns false when it does not converge, which the caller must treat as
   * "not on this surface" rather than as an answer. */
  virtual bool project(const Vector3d& pt, double& u, double& v) const;

  /*! Whether `pt` lies on the swept surface, as opposed to being one of the
   * points it was declared with. `tol` is absolute. */
  [[nodiscard]] virtual bool onSurface(const Vector3d& pt, double tol) const;

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
  /*! Whether (u, v) is on the seam of a profile declared closed: the column it
   * starts and ends at, which is v = 0 and v = 1 at once. Always false for an
   * open profile, which has edges there rather than a seam. */
  [[nodiscard]] bool onSeam(double u, double v) const;

  /*! Where a closed profile's seam falls, as a parameter shift.
   *
   * A face on a closed sweep is written over the rectangle 0 <= v <= 1, so a
   * boundary that crosses the seam has nowhere to go but the long way round -
   * and which column the seam sits at is free. Writing the surface from column
   * `seam_column` puts it there, and this is what that does to a `v` this
   * surface reports: v' = frac(v - seam_column / spans). A face whose claim
   * leaves one span of the profile unused can always be written without crossing
   * a seam by putting it in that span, which costs no face, no refused facet and
   * no cut. */
  [[nodiscard]] double seamShift(int seam_column) const;

  [[nodiscard]] virtual bool splineForm(int& degree_u, int& degree_v, int& rows_out, int& cols_out,
                                        std::vector<Vector3d>& ctrl, std::vector<double>& knots_u,
                                        std::vector<int>& mults_u, std::vector<double>& knots_v,
                                        std::vector<int>& mults_v, int seam_column = 0) const;

  int rows = 0, cols = 0;
  bool closed_v = false;
  std::vector<Vector3d> net;

protected:
  /*! How many spans the profile has across it. See vspans() below. */
  [[nodiscard]] int vspansCount() const { return vspans(); }

protected:
  // What a closed form deriving from the grid renders itself with - see
  // SweepSurface::splineForm.
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
  [[nodiscard]] std::vector<Vector3d> withClosingColumn(const std::vector<Vector3d>& src,
                                                        int seam_column = 0) const;
  /*! Interpolate the stations, so that `evaluate` describes the sweep and not
   * only the points it passes through. Runs on construction and after a move. */
  void buildSpline();
  std::map<std::tuple<int64_t, int64_t, int64_t>, int> lookup;
  std::vector<double> uknots;   // clamped, averaged; empty when rows < 4
  std::vector<Vector3d> poles;  // rows * cols, parallel to net
  double band = 0;              // see tessellationBand()
  int operator==(const Surface& other) override { return 0; }
};

/*! A profile swept along a helix, declared as the shape rather than as points.
 *
 * The one declaration on this channel that needs no projection to answer
 * anything. `GridSurface` carries a grid of *stations*, interpolates them, and
 * answers membership by Gauss-Newton onto that interpolant - so a tessellation
 * band has to be trusted, a coarse sample has to find the right basin, and a
 * profile corner is a place where the derivative does not exist. Each of those
 * has cost a defect: a projection landing 11.9 mm from a 2 mm ridge, and half a
 * sweep written as facets because the descent stalled on a corner.
 *
 * None of it is a property of the model. A profile swept along a helix has a
 * closed form, and membership in it is an *inversion*: take the point's angle
 * about the axis, work out which turn it belongs to, subtract that station's
 * height, and ask whether what is left lies on the profile. Exact, O(profile),
 * no tolerance beyond the one the caller states, and no basin to fall the wrong
 * side of.
 *
 * It derives from GridSurface so that everything downstream - the recogniser,
 * the emitter, `splineForm` - keeps working unchanged. The inherited net is a
 * *rendering* of this surface for those consumers, sampled from the closed form
 * rather than handed in; the declaration is the closed form, and the overrides
 * below are what membership actually asks.
 *
 * The radius and the profile may change along the sweep, each by its own law -
 * SweepLaws: piecewise linear in the sweep's parameter, which is how a thread
 * fades in and out and follows a tapered socket. What stays the same is the
 * path: a helix about the axis, the angle and the rise both linear in t. That is
 * what keeps membership an inversion - the angle and the height still say which
 * station a point belongs to, and the laws say what the profile is there. */
/*! How a sweep's radius and profile change along it: each a table of values at
 * points of the sweep's own parameter t, linear between them and constant past
 * the first and the last. One entry is a constant law. The corners of the
 * laws - where a table turns - are corners of the surface too, and whatever
 * renders it has to keep them. */
struct SweepLaws {
  std::vector<double> radius_at;               // t of each radius key, increasing
  std::vector<double> radius;                  // the radius there
  std::vector<double> profile_at;              // t of each profile key, increasing
  std::vector<std::vector<Vector2d>> profile;  // the closed profile there, one point count throughout

  /*! One radius and one profile, the whole way. */
  static SweepLaws constant(double r, std::vector<Vector2d> p);
  [[nodiscard]] double radiusAt(double t) const;
  /*! Profile point `i` at `t`. */
  [[nodiscard]] Vector2d pointAt(std::size_t i, double t) const;
  [[nodiscard]] std::size_t points() const { return profile.empty() ? 0 : profile.front().size(); }
  /*! The keys of both laws strictly inside (0, 1), merged: where the surface
   * may turn a corner along the sweep. */
  [[nodiscard]] std::vector<double> corners() const;
  [[nodiscard]] double largestRadius() const;
  /*! Why these are not laws a sweep can follow, or empty. */
  [[nodiscard]] std::string invalid() const;
  /*! Both tables scaled by `s`, as a uniform scale of the sweep scales them. */
  [[nodiscard]] SweepLaws scaled(double s) const;
  [[nodiscard]] bool sameAs(const SweepLaws& o, double tol) const;
};

class SweepSurface : public GridSurface
{
public:
  SweepSurface(const Vector3d& origin, const Vector3d& axis, const Vector3d& ref, SweepLaws laws,
               double pitch, double turns, int stations_in);

  /*! Build one, or say why the arguments do not describe a sweep. Both front
   * ends validate here, for the reason `GridSurface::fromRows` does. */
  static std::shared_ptr<SweepSurface> make(const Vector3d& origin, const Vector3d& axis,
                                            const Vector3d& ref, const SweepLaws& laws, double pitch,
                                            double turns, int stations_in, std::string& why);

  int pointMember(std::vector<Vector3d>& vertices, Vector3d pt) override;
  [[nodiscard]] Vector3d evaluate(double u, double v) const override;
  bool project(const Vector3d& pt, double& u, double& v) const override;
  [[nodiscard]] bool onSurface(const Vector3d& pt, double tol) const override;
  [[nodiscard]] std::shared_ptr<Surface> clone() const override;
  bool transform(const Transform3d& mat) override;
  [[nodiscard]] bool sameAs(const Surface& other) const override;

  /*! Where the point sits in the sweep: how far along, and where in the profile
   * plane belonging to that station. The whole of the exactness is here - a
   * point is on the surface exactly when (`dr`, `dz`) is on the profile. */
  void localCoords(const Vector3d& pt, double& t, double& dr, double& dz) const;

  /*! Distance from (dr, dz) to the closed profile at station `t`, and where
   * along it. */
  [[nodiscard]] double profileDistance(double t, double dr, double dz, double *at = nullptr) const;

  /*! The B-spline this sweep is written as: the closed form rendered, not the
   * inherited grid's interpolant of a few stations. Parametrised by the sweep's
   * own t along u - which is what `project` answers, and so what every pcurve
   * on a face of it means - and held to kRenderTolerance of the closed form,
   * the tolerance the curves on it are held to: along each column a clamped
   * cubic in t, its stations doubled wherever a point between them misses, and
   * a knot of multiplicity 3 at every corner of the laws, where the surface is
   * only C0. Across the profile it is exact, the closed form being linear along
   * each profile edge at a station. */
  [[nodiscard]] bool splineForm(int& degree_u, int& degree_v, int& rows_out, int& cols_out,
                                std::vector<Vector3d>& ctrl, std::vector<double>& knots_u,
                                std::vector<int>& mults_u, std::vector<double>& knots_v,
                                std::vector<int>& mults_v, int seam_column = 0) const override;
  static constexpr double kRenderTolerance = 1e-7;

  SweepLaws laws;
  double pitch = 0, turns = 0;
  Vector3d ref;  // where theta is measured from; unit, perpendicular to normdir
  int stations = 0;

private:
  /*! The rendering along u, made once: rows * cols control points, and u's
   * distinct knots with their multiplicities. */
  bool renderAlongU() const;
  mutable bool rendered_ = false, render_ok_ = false;
  mutable int render_rows_ = 0;
  mutable std::vector<Vector3d> render_ctrl_;
  mutable std::vector<double> render_knots_;
  mutable std::vector<int> render_mults_;
};
