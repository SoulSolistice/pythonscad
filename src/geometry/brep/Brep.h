#pragma once

/*! A boundary representation that belongs to no file format.
 *
 * Open item 39 (doc/step-export-wip.md): everything the STEP writer decides -
 * where each corner goes, which exact curve bounds each face, the seams of
 * periodic surfaces, the region each face covers, the evidence behind each - is
 * geometry, and none of it is STEP's; an IGES writer, or an SVG one writing true
 * curves, would need all of it. Until now the only B-rep was the STEP entities
 * `StepKernel::BodyBuild` builds as it decides. This is the neutral one, which a
 * writer translates rather than rebuilds.
 *
 * Three rules shape it.
 *
 * - **Every entity carries its evidence class** (recognition::EvidenceClass):
 *   what decided it, D0 a declaration through D4 the tessellation. A writer
 *   decides from it what it may write exactly - an SVG writer an arc only where
 *   the arc is D0 or D1 - so the class is part of the entity, not a report
 *   about it. `Unstated` is a defect (check()).
 * - **An edge is where its faces' surfaces meet; its curve is how that is
 *   carried.** What an edge *is* is stated once, by topology: it lies on the
 *   surface of every face that uses it - two faces, or one along its own seam.
 *   Its curve is the carrier, exact where the crossing has a closed form a
 *   writer can encode (a circle round a cylinder at a cap, the conic a plane
 *   cuts, a ruling) and otherwise a B-spline fitted to the crossing, and
 *   `carrier_error` says how far it strays: zero, or the file's uncertainty,
 *   stated rather than hidden. A second copy of the surfaces on the curve
 *   would be a second thing to keep true.
 * - **A face is its record; its form is how it is carried.** The record is the
 *   surface the face is exactly on - declared, derived or fitted. The form is
 *   what a writer encodes: a plane or quadric in a frame, or a spline net - and
 *   for a declared sweep the net depends on choices the record does not make,
 *   where the seam goes among them. So a face carries both, as an edge carries
 *   its topology and its curve.
 * - **Topology by index, never by address.** Entities live in the body's
 *   vectors and refer to each other by position, so a body copies, compares and
 *   iterates the same way on every run (checklist F9) - the STEP writer's one
 *   walk in address order once made its output differ run to run (open item 28).
 *
 * Nothing here reads a mesh, a claim or a file. `Face::facets` is the one link
 * back: the mesh facets a face stands for, which is what lets a check ask of
 * every facet whether a face was written over it.
 */

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "geometry/linalg.h"
#include "geometry/recognition/Ledger.h"

class Surface;

namespace brep {

using recognition::EvidenceClass;

/*! A right-handed frame: where a curve or a surface is measured from. `axis`
 * is the normal of a conic's plane or the axis of a surface of revolution;
 * `ref` is the direction its parameter starts from - a conic's major axis, the
 * radius a cylinder's angle is measured from. Both unit, `ref` perpendicular to
 * `axis`. */
struct Frame {
  Vector3d origin = Vector3d::Zero();
  Vector3d axis = Vector3d::UnitZ();
  Vector3d ref = Vector3d::UnitX();
};

/*! A straight line through `origin` along `direction` (unit). */
struct Line {
  Vector3d origin = Vector3d::Zero();
  Vector3d direction = Vector3d::UnitX();
};

/*! A conic in the plane of `frame`, centred at its origin with its major axis
 * along `frame.ref`. A circle has `a` its radius and `b` equal to it; an
 * ellipse and a hyperbola their two semi-axes, `a` along `ref`; a parabola has
 * `a` its focal distance and `b` unused - the order ISO 10303-42 and IGES 104
 * both give them. */
struct Conic {
  enum class Kind { Circle, Ellipse, Hyperbola, Parabola };
  Kind kind = Kind::Circle;
  Frame frame;
  double a = 0, b = 0;
};

/*! A clamped B-spline: its degree, control points, and distinct knots with
 * their multiplicities. `weights` empty is a polynomial spline; otherwise one
 * per control point, and the curve is rational. */
struct BSpline {
  int degree = 0;
  std::vector<Vector3d> points;
  std::vector<double> weights;
  std::vector<double> knots;
  std::vector<int> mults;
};

/*! How a curve is carried in space. `std::monostate` is no curve at all: an
 * edge nobody gave a curve, which check() names rather than letting a default
 * stand in for one. */
using CurveForm = std::variant<std::monostate, Line, Conic, BSpline>;

/*! An edge's carrier, and how far it strays from the surfaces of the faces
 * that use the edge: zero for a carrier that is the crossing exactly, a fit's
 * worst otherwise (the writer's `crossing_fit`). Which evidence class the
 * carrier has follows from that - the edge's own where zero, D2 at best where
 * not, a fit to declarations at a stated tolerance - so it is not stated
 * twice. */
struct Curve {
  CurveForm form;
  double carrier_error = 0;
  // Set by a maker that built the carrier from its face's own record - a row
  // of a Bezier net is that patch's boundary - where no algebraic certificate
  // can ask it (stateClasses). A statement, not a measurement.
  bool stated_exact = false;
};

/*! A curve in a face's parameter plane: a line or a B-spline in (u, v), as
 * `Frame` and the surface's own parameterisation measure them. */
struct Line2 {
  Vector2d origin = Vector2d::Zero();
  Vector2d direction = Vector2d::UnitX();
  // The line's parameter scale: how far along (u, v) one unit of its parameter
  // runs - a pcurve's span, which the direction alone does not state.
  double length = 1;
};
struct BSpline2 {
  int degree = 0;
  VectorOfVector2d points;
  std::vector<double> knots;
  std::vector<int> mults;
};
using PCurve = std::variant<Line2, BSpline2>;

/*! How a face's surface is carried. The elementary ones in `frame`: a plane
 * through its origin normal to its axis; a cylinder, cone, sphere or torus
 * about its axis, with the parameter's zero along `ref`. A cone's `r` is its
 * radius in the plane of the frame's origin and `half_angle` its opening; a
 * torus's `r` is the major radius and `r_minor` the tube's. */
struct Plane {
  Frame frame;
};
struct Cylinder {
  Frame frame;
  double r = 0;
};
struct Cone {
  Frame frame;
  double r = 0, half_angle = 0;
};
struct Sphere {
  Frame frame;
  double r = 0;
};
struct Torus {
  Frame frame;
  double r = 0, r_minor = 0;
};
/*! A tensor-product B-spline: `points[u][v]`, `weights` empty or one per
 * point, and each direction's distinct knots with their multiplicities. */
struct SplineSurface {
  int degree_u = 0, degree_v = 0;
  std::vector<std::vector<Vector3d>> points;
  std::vector<std::vector<double>> weights;
  std::vector<double> knots_u, knots_v;
  std::vector<int> mults_u, mults_v;
};
/*! A curve swept along a straight line: an elliptic cylinder is its ellipse
 * swept along its axis. */
struct Extrusion {
  CurveForm curve;
  Vector3d direction = Vector3d::UnitZ();  // unit, as stated
  double length = 1;                       // how far one unit of the sweep's parameter runs
};
/*! `std::monostate` is no surface at all, which check() names. */
using SurfaceForm =
  std::variant<std::monostate, Plane, Cylinder, Cone, Sphere, Torus, SplineSurface, Extrusion>;

/*! A point of the solid's boundary. `mesh_vertex` is the mesh's vertex it was
 * placed from, or -1 for a point the mesh never had - a seam's end made where a
 * ruling meets a rim. */
struct Vertex {
  Vector3d point = Vector3d::Zero();
  EvidenceClass evidence = EvidenceClass::Unstated;
  int mesh_vertex = -1;
};

/*! A bounded piece of a curve from `start` to `end`, indices into
 * `Body::vertices`. A closed curve - a full circle - starts and ends at one
 * vertex. `same_sense` says whether the curve's own direction runs from start
 * to end. */
struct Edge {
  std::size_t start = 0, end = 0;
  Curve curve;
  bool same_sense = true;
  EvidenceClass evidence = EvidenceClass::Unstated;
};

/*! One use of an edge by a loop: forward runs it start to end. Where a writer
 * carries curves in the face's own parameters too, `pcurve` is this use's -
 * and a seam, which a face uses twice, has one for each. */
struct Coedge {
  std::size_t edge = 0;
  bool forward = true;
  std::optional<PCurve> pcurve;
};

/*! A closed chain of coedges, each ending where the next starts. `outer` marks
 * a face's outer boundary, and a face has at most one: a face round a periodic
 * surface between two rims has two loops and neither is outer - ISO 10303
 * writes it with two plain face bounds - so no loop is the rule there, not a
 * defect. */
struct Loop {
  std::vector<Coedge> coedges;
  bool outer = true;
};

/*! A bounded piece of a surface. `surface` is the record it lies on - declared,
 * derived or fitted, as its evidence class says - and `same_sense` whether its
 * outward normal is the surface's own. `form` is how it is carried, and the
 * parameterisation its pcurves are measured in; `carrier_error` how far the
 * form strays from the record - zero where the form is the record's own, a
 * fit's worst where the record has no form a writer can encode.
 * `evidence` is what decided that the face stands on its surface: the weakest
 * class among its members, so a patch of D0 facets and D4 additions is D4 - the
 * honest answer, and the one a writer choosing what to write exactly needs.
 * `record_evidence` is a different fact: what the surface itself is - D0 a
 * maker's record, D1 a derived one, D3 a fit, a plane from corners as good as
 * they are. A vertex or an edge on the face rests on the record, never on which
 * facets a band admitted to it; the two are one fact each.
 * `facets` are the mesh facets the face was written over. */
struct Face {
  std::shared_ptr<const Surface> surface;
  bool same_sense = true;
  SurfaceForm form;
  double carrier_error = 0;
  std::vector<Loop> loops;
  EvidenceClass evidence = EvidenceClass::Unstated;
  EvidenceClass record_evidence = EvidenceClass::Unstated;
  std::vector<std::size_t> facets;
  // Set where `surface` is the plane through the face's own corners rather
  // than a record anyone stated: then the face is as good as the corners that
  // span it, and stateClasses decides how good that is.
  bool plane_from_corners = false;
  // The exact surface `form` writes, as the face's maker built it - what a
  // consumer asking "which surface is this face on?" reads, never the form read
  // back, which is the carrier rounded (open item 39, R10). It is `surface`, or
  // one surface with it (Surface::sameAs), or - where `carrier_derived` says so
  // - a closed form its maker derived from it: a Bezier patch's quadric.
  // Anything else is a face written on a surface it does not stand for, which
  // checkAttribution names.
  std::shared_ptr<const Surface> carrier;
  bool carrier_derived = false;
};

/*! A connected set of faces, indices into `Body::faces`. Closed, it bounds a
 * solid: every edge is used twice, once in each direction. */
struct Shell {
  std::vector<std::size_t> faces;
  bool closed = true;
};

/*! A solid's boundary: its shells, and every entity they refer to. */
struct Body {
  std::vector<Vertex> vertices;
  std::vector<Edge> edges;
  std::vector<Face> faces;
  std::vector<Shell> shells;
};

/*! Every face written on a surface its record is not: a carrier of another
 * kind than its form, or one that is neither the record, one surface with it,
 * nor stated derived from it. A separate question from check(): attribution,
 * not topology - a face can close perfectly on the wrong surface. */
std::vector<std::string> checkAttribution(const Body& body);

/*! Every end of an edge further off that edge's own curve than `uncertainty`,
 * the uncertainty the file declares - what a reader rejects an edge on
 * (SOLIDWORKS fault code 7, "vertex does not lie on edge's curve"). Apart from
 * check() because it needs that figure, which the topology does not. */
std::vector<std::string> checkVerticesOnCurves(const Body& body, double uncertainty);

/*! Whether the curve carried between `start` and `end` lies on `surface`
 * exactly - a closed-form certificate, no tolerance. A surface of algebraic
 * degree d (a plane 1, a quadric 2, a torus 4) meets a line in at most d points
 * and a conic in at most 2d unless it contains it, so d + 1 points of a line
 * and 2d + 1 of a conic, each on the surface at machine precision, prove the
 * whole curve is. Each point is asked against the surface's own residual bound
 * plus the forward error of computing the point from the carrier, which the
 * residual's bound does not state (recognition/Exact.h). False where no
 * certificate exists: a B-spline, or a surface with no algebraic degree - a
 * grid, a Bezier patch. */
bool curveOn(const CurveForm& form, const Vector3d& start, const Vector3d& end, const Surface *surface);

/*! How far `p` is from the curve `form` carries - what a reader asks of every
 * vertex against its own edge's curve (SOLIDWORKS fault code 7). Exact for a
 * line and a circle; an ellipse, a hyperbola and a parabola are sampled and
 * refined, which bounds the true distance from above - the safe direction for
 * a check that has to be believed; a clamped B-spline is its end control
 * points. No curve, 0: not a finding. */
double distanceToCurve(const CurveForm& form, const Vector3d& p);

/*! Whether curveOn can answer at all: a line or a conic, on a record of some
 * algebraic degree. Where it cannot, "not certified" means "not asked", never
 * "off". */
bool certifiable(const CurveForm& form, const Surface *surface);

/*! How a body's vertices and edges came out of stateClasses: how many of each
 * class; how many are shown off the record of a face they bound; and how many
 * sit on a record nothing can ask - a grid, a Bezier patch, a spline carrier -
 * which is not the same as off, and is counted apart. */
struct ClassTally {
  std::array<std::size_t, 6> vertices{};  // indexed by EvidenceClass
  std::array<std::size_t, 6> edges{};
  std::size_t vertices_off = 0, edges_off = 0;
  std::size_t vertices_unasked = 0, edges_unasked = 0;
};

/*! State every vertex's and edge's class from the faces that meet there - the
 * one truth an edge and a vertex have (see the file comment) - by the
 * determinacy rule: an entity is as good as the weakest class it rests on and
 * the freedom it has left (doc/step-rungs.md).
 *
 * An edge whose carrier is certified on the record of every face using it
 * (curveOn), or stated exact by its maker, is D1 - a closed-form consequence -
 * or its records' weakest class if that is weaker; one whose carrier is a stated
 * fit (`carrier_error` > 0) is D2 at best; any other is D4, a carrier off a face
 * it bounds, with `carrier_error` NaN - not measured.
 *
 * A vertex a maker declared - a point of a declared grid's net - is D0. One
 * exactly on three records with normals independent there is determined, and
 * as good as the best such three - the weakest class it *needs*, not every face
 * that touches it (doc/step-rungs.md, determinacy): D1 at best. One fixed by no
 * three has freedom the tessellation decided: D4. A face that does not hold a
 * vertex it bounds is counted off; the vertex keeps the class what fixes it
 * gives it.
 *
 * A face whose plane is its own corners' (`plane_from_corners`) is as good as
 * the best three non-collinear corners spanning it, provided every corner is
 * on that plane - and its corners may rest on it in turn. So the classes are
 * the least fixpoint from D4 upward: a derived plane starts D4, nothing it
 * props up can lift it, and classes only ever improve. It is the record's
 * class every rule here reads (`record_evidence`), never the face's membership
 * class; every other record's is what its maker stated, read and never written. */
ClassTally stateClasses(Body& body);

/*! What is wrong with a body's structure, one line per defect, empty when
 * nothing is: an index out of range; an edge with no curve, or a B-spline whose
 * knots do not fit its control points; a loop that does not close, a coedge
 * ending where the next does not start; a face on no surface, carried by no
 * form, with more than one outer loop, or stating no class for its record; a spline surface whose knots
 * do not fit its net; in a closed shell an edge not used exactly twice in opposite directions; a face in
 * no shell or in two; and any entity whose evidence class is unstated. Structure only - whether a vertex
 * lies on its edges' curves and an edge on its faces' surfaces is geometry, and asked elsewhere. */
std::vector<std::string> check(const Body& body);

/*! The body's faces as shells: one per connected component, faces that share
 * an edge being one, since a closed shell has to be connected. Read off the
 * coedges - the topology the file asserts, not a list kept beside it. Each
 * shell lists its faces in order, and the shells come in the order of their
 * first face. */
std::vector<Shell> connectedShells(const Body& body);

}  // namespace brep
