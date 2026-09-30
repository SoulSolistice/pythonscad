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
 * - **Topology by index, never by address.** Entities live in the body's
 *   vectors and refer to each other by position, so a body copies, compares and
 *   iterates the same way on every run (checklist F9) - the STEP writer's one
 *   walk in address order once made its output differ run to run (open item 28).
 *
 * Nothing here reads a mesh, a claim or a file. `Face::facets` is the one link
 * back: the mesh facets a face stands for, which is what lets a check ask of
 * every facet whether a face was written over it.
 */

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
};

/*! A curve in a face's parameter plane: a line or a B-spline in (u, v), as
 * `Frame` and the surface's own parameterisation measure them. */
struct Line2 {
  Vector2d origin = Vector2d::Zero();
  Vector2d direction = Vector2d::UnitX();
};
struct BSpline2 {
  int degree = 0;
  VectorOfVector2d points;
  std::vector<double> knots;
  std::vector<int> mults;
};
using PCurve = std::variant<Line2, BSpline2>;

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
 * outward normal is the surface's own. `frame` is the parameterisation a
 * writer measures the face's pcurves in, where the face carries any; a
 * surface's record states the surface, not where its parameter starts.
 * `evidence` is what decided that the face stands on its surface: the weakest
 * class among its members, so a patch of D0 facets and D4 additions is D4 - the
 * honest answer, and the one a writer choosing what to write exactly needs.
 * `facets` are the mesh facets the face was written over. */
struct Face {
  std::shared_ptr<const Surface> surface;
  bool same_sense = true;
  std::optional<Frame> frame;
  std::vector<Loop> loops;
  EvidenceClass evidence = EvidenceClass::Unstated;
  std::vector<std::size_t> facets;
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

/*! What is wrong with a body's structure, one line per defect, empty when
 * nothing is: an index out of range; an edge with no curve, or a B-spline whose
 * knots do not fit its control points; a loop that does not close, a coedge
 * ending where the next does not start; a face on no surface, or with more than
 * one outer loop; in a closed shell an edge not used exactly twice in opposite
 * directions; a face in no shell or in two; and any entity whose evidence class
 * is unstated. Structure only - whether a vertex lies on its edges' curves and
 * an edge on its faces' surfaces is geometry, and asked elsewhere. */
std::vector<std::string> check(const Body& body);

}  // namespace brep
