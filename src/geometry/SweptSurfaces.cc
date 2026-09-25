/*! What a sweep of a profile makes - see SweptSurfaces.h. */
#include "geometry/SweptSurfaces.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "geometry/Surface.h"
#include "geometry/recognition/Exact.h"

namespace {

using recognition::forwardError;
using Surfaces = std::vector<std::shared_ptr<Surface>>;

// ---------------------------------------------------------------------------
// Revolution
// ---------------------------------------------------------------------------

// The profile's x is the radius and its y the level along the axis.
//
// What a revolution makes is four derivations, each from one fact and none
// asking another: a vertex off the axis sweeps a circle; a straight edge sweeps
// the conic through its two end circles; a curve record sweeps its own surface;
// a partial turn has an end plane at either end. Until 2026-09-25 the second was
// a six-way classification of each edge's course with a row per case - and a
// chord flag threaded through the rows, each of which had to remember it - and
// two of the rows had forgotten. The degenerate conics are not cases: they are
// what the one formula gives where a difference is zero.

/*! Whether two coordinates of the profile are the same number, at machine
 * precision: each carries at most the rounding of the translation that placed
 * the profile, and the difference one more. */
bool same(double u, double v)
{
  return fabs(u - v) <= forwardError(2, fabs(u) + fabs(v));
}

/*! The records one revolution declares, with the one bookkeeping rule they
 * share: one cylinder per radius. A vertex shared by two walls would otherwise
 * be declared twice, and a record is only ever matched on its radius and axis -
 * the height it carries says where the circle was, not how far the surface
 * reaches, since a boolean may since have cut it back. Where one radius is both
 * a wall and a rim, the face wins (addSurfaceUnique merges the roles). See
 * Surface::witnesses. */
struct RevolutionRecords {
  const Revolution& sweep;
  Surfaces out;
  std::vector<double> radii;

  Vector3d at(double level) const { return sweep.origin + sweep.axis * level; }

  void cylinder(const Vector2d& p, bool face)
  {
    auto record = std::make_shared<CylinderSurface>(at(p[1]), sweep.axis, p[0]);
    record->evidence_only = !face;
    for (const double r : radii) {
      if (same(r, p[0])) {
        if (face) addSurfaceUnique(out, record);  // upgrades the kept record to a face
        return;
      }
    }
    radii.push_back(p[0]);
    addSurfaceUnique(out, record);
  }
};

/*! A vertex off the axis sweeps a circle - whatever its edges are. The circles
 * are evidence, not faces: no facet lies on a circle, and they are the rims the
 * band pass identifies a cone or a torus's rings by. */
void revolveVertex(const Vector2d& p, RevolutionRecords& records)
{
  if (p[0] > 0) records.cylinder(p, false);
}

/*! A straight edge sweeps the conic through the circles its two ends sweep: the
 * cone whose radius changes by b - a in r over b - a in level. Where the level
 * does not change that is the plane both circles lie in - an annulus, or a disc
 * where one end is on the axis; where the radius does not, the cylinder both lie
 * on - or the axis itself, which sweeps nothing. Where one end is on the axis the
 * cone's apex is there, and it is a cone like any other.
 *
 * Only a straight edge: a chord of a curve sweeps a piece of that curve's
 * surface, which the curve's record declares (revolveArc). */
void revolveLine(const ProfileEdge& e, RevolutionRecords& records)
{
  const Vector2d& a = e.a;
  const Vector2d& b = e.b;
  if (same(a[1], b[1])) {
    // A plane at the level the profile puts it - the model's own number, where
    // one fitted to the mesh moves with the tessellation and with whatever a
    // boolean leaves behind. Or, with the radius unchanged too, no edge at all.
    if (!same(a[0], b[0])) {
      addSurfaceUnique(records.out,
                       std::make_shared<PlaneSurface>(records.at(a[1]), records.sweep.axis));
    }
    return;
  }
  if (same(a[0], b[0])) {
    if (a[0] > 0) records.cylinder(a, true);  // a wall, on that cylinder - a face of this solid
    return;
  }
  // Anchored at the wider end, as cylinder(r1, r2) anchors its own
  // (primitives.cc). Until 2026-09-24 a sloped edge declared its end circles
  // alone and the cone was recognised from them under a rim's name - on lid10
  // the seal bead's flanks, 120 facets held on cylinders no facet lies on; until
  // 2026-09-25 one reaching the axis declared nothing (doc/step-export-wip.md,
  // *Inferred where a declaration was possible*, item 4).
  const Vector2d& wide = a[0] >= b[0] ? a : b;
  addSurfaceUnique(records.out, std::make_shared<ConeSurface>(records.at(wide[1]), records.sweep.axis,
                                                              wide[0], (b[0] - a[0]) / (b[1] - a[1])));
}

/*! A circle in the profile sweeps a sphere where its centre is on the axis, and
 * a ring torus where the whole circle is off it. */
void revolveArc(const Arc2d& arc, RevolutionRecords& records)
{
  if (!(arc.r > 0)) return;  // a point
  const Vector2d centre(arc.centre[0], arc.centre[1]);
  if (centre[0] == 0) {
    addSurfaceUnique(records.out,
                     std::make_shared<SphereSurface>(records.at(centre[1]), records.sweep.axis, arc.r));
  } else if (centre[0] > arc.r) {
    addSurfaceUnique(records.out, std::make_shared<TorusSurface>(records.at(centre[1]),
                                                                 records.sweep.axis, centre[0], arc.r));
  }
  // An arc reaching the axis without being centred on it sweeps a horn or a
  // spindle torus, which is self-intersecting; a profile crossing the axis is
  // rejected long before a sweep, so there is nothing to say.
}

// ---------------------------------------------------------------------------
// Extrusion
// ---------------------------------------------------------------------------

// Every station of a linear extrusion is the profile under one map,
//
//   X(p, t) = L (S(t) R(t) p) + base + t h,   t from 0 to 1,
//
// L the profile's own placement, S(t) the scale growing from 1 to (sx, sy), R(t)
// the twist turning from 0 to its angle, h the height. What the extrusion makes
// follows from three facts about that map, each asked once:
//
//   - every station lies in a plane parallel to the first: the caps;
//   - it is affine in t exactly when nothing turns (no twist), and an affine map
//     sweeps a straight edge into the bilinear patch through its four corners,
//     a Bezier into the (n, 1) patch of its control points - exact, because both
//     are affine in their control points;
//   - it keeps a circle a circle exactly when every station is a similarity (L
//     one, sx = sy), and the circle then sweeps the right cone through its two
//     end circles where its centre travels along the plane's normal - a cylinder
//     where the radius does not change.
//
// Until 2026-09-25 these were a chain of refusals per record kind, and the
// taper declared its end circles and not its cone (item 7 and the taper row of
// doc/step-export-wip.md, *Inferred where a declaration was possible*). The
// degenerate forms are not cases: a bilinear patch whose far edge is parallel to
// its near one is a plane; a cone whose radius does not change is a cylinder.

/*! The extrusion's map, and the facts about it the derivations ask. */
struct StationMap {
  const Extrusion& sweep;
  Eigen::Matrix3d linear;
  Vector3d ex, ey;

  explicit StationMap(const Extrusion& s)
    : sweep(s),
      linear(s.placement.linear()),
      ex(linear * Vector3d(1., 0., 0.)),
      ey(linear * Vector3d(0., 1., 0.))
  {
  }

  /*! Where the profile point `p` is at the near (t = 0) or far (t = 1) cap. */
  [[nodiscard]] Vector3d at(const Vector2d& p, int t) const
  {
    if (t == 0) return sweep.placement * Vector3d(p[0], p[1], 0.) + sweep.base;
    const double turn = sweep.twist * M_PI / 180.0;
    const Vector2d q(cos(turn) * p[0] - sin(turn) * p[1], sin(turn) * p[0] + cos(turn) * p[1]);
    return sweep.placement * Vector3d(q[0] * sweep.scale_x, q[1] * sweep.scale_y, 0.) + sweep.base +
           sweep.height;
  }

  /*! Affine in t: nothing turns. */
  [[nodiscard]] bool affine() const { return sweep.twist == 0; }

  /*! Every station a similarity. The placement is tested as it always was, to
   * 1e-12 of its own scale: it is a composition of the user's transforms, whose
   * rounding this cannot bound - the one comparison here that is not machine
   * precision. */
  [[nodiscard]] bool similar(double& scale) const
  {
    const double sx = ex.norm(), sy = ey.norm();
    if (sx < 1e-12 || sy < 1e-12) return false;
    if (fabs(sx - sy) > 1e-12 * sx) return false;          // an ellipse, not a circle
    if (fabs(ex.dot(ey)) > 1e-12 * sx * sy) return false;  // sheared, likewise
    if (sweep.scale_x != sweep.scale_y)
      return false;  // uneven: an ellipse at every station but the first
    scale = 0.5 * (sx + sy);
    return true;
  }

  /*! The unit normal of the profile's plane, or zero where it has none. */
  [[nodiscard]] Vector3d normal() const
  {
    const Vector3d n = ex.cross(ey);
    return n.norm() < 1e-12 ? Vector3d::Zero() : Vector3d(n.normalized());
  }
};

/*! The two caps: the profile's plane at either end. A far cap scaled to nothing
 * is a point, and has no face. */
void extrudeCaps(const StationMap& map, Surfaces& out)
{
  const Vector3d n = map.normal();
  if (n.isZero()) return;
  if (!std::isfinite(map.sweep.scale_x) || !std::isfinite(map.sweep.scale_y)) return;
  addSurfaceUnique(out, std::make_shared<PlaneSurface>(map.sweep.base, n));
  if (map.sweep.scale_x != 0. || map.sweep.scale_y != 0.) {
    addSurfaceUnique(out, std::make_shared<PlaneSurface>(map.sweep.base + map.sweep.height, n));
  }
}

/*! A straight edge, swept by an affine map: the bilinear patch through its four
 * corners - a plane, where the far edge is parallel to the near one. S(1) sends
 * the edge's direction u to (sx u_x, sy u_y), parallel to u exactly when
 * (sx - sy) u_x u_y = 0: an even scale, or an edge along either axis. */
void extrudeLine(const ProfileEdge& e, const StationMap& map, Surfaces& out)
{
  if (!map.affine()) return;  // a twist turns the far edge: no patch of any degree
  const Vector2d u = e.b - e.a;
  if (u[0] == 0 && u[1] == 0) return;  // no edge
  const Vector3d a0 = map.at(e.a, 0), b0 = map.at(e.b, 0), a1 = map.at(e.a, 1), b1 = map.at(e.b, 1);
  const bool parallel = map.sweep.scale_x == map.sweep.scale_y || u[0] == 0 || u[1] == 0;
  if (parallel) {
    const Vector3d wall = (b0 - a0).cross(a1 - a0);
    if (wall.norm() < 1e-12) return;  // the far edge on the near one's line: no wall
    addSurfaceUnique(out, std::make_shared<PlaneSurface>(a0, Vector3d(wall.normalized())));
    return;
  }
  // v varies fastest: each corner as a pair, where it starts and where it goes
  out.push_back(std::make_shared<BezierPatchSurface>(1, 1, std::vector<Vector3d>{a0, a1, b0, b1}));
}

/*! A Bezier segment, swept by an affine map: the (n, 1) patch whose control net
 * is the segment's own control points at either end. */
void extrudeBezier(const Bezier2d& bez, const StationMap& map, Surfaces& out)
{
  if (!map.affine()) return;
  if (bez.degree < 2 || bez.degree > 3) return;
  std::vector<Vector3d> net;
  net.reserve((bez.degree + 1) * 2);
  for (int i = 0; i <= bez.degree; i++) {
    const Vector2d p(bez.ctrl[i][0], bez.ctrl[i][1]);
    net.push_back(map.at(p, 0));
    net.push_back(map.at(p, 1));
  }
  out.push_back(std::make_shared<BezierPatchSurface>(bez.degree, 1, std::move(net)));
}

/*! A circle, swept. With no taper, the cylinder over the section the placement
 * makes of it, along the sweep - circular or elliptic. With a taper, and every
 * station a similarity, the right cone through its two end circles, where the
 * centre travels along the plane's normal. A centre that travels any other
 * way - an oblique sweep, or a scale about a point that is not the centre -
 * sweeps an oblique cone: exact, and not a CONICAL_SURFACE. A twist turns a
 * circle about its own centre into itself, so it only matters where it moves the
 * centre.
 *
 * The cone is a face; the two end circles are evidence, the rims the band pass
 * identifies it by. */
void extrudeConic(const Vector2d& c, const Vector2d& u, const Vector2d& v, const StationMap& map,
                  Surfaces& out);

void extrudeArc(const Arc2d& arc, const StationMap& map, Surfaces& out)
{
  if (!(arc.r > 0)) return;
  extrudeConic(Vector2d(arc.centre), Vector2d(arc.r, 0.), Vector2d(0., arc.r), map, out);
}

/*! An ellipse in the profile, swept: the circle's derivation, since a circle is
 * the ellipse whose semi-diameters are two perpendicular radii. */
void extrudeEllipse(const Ellipse2d& el, const StationMap& map, Surfaces& out)
{
  extrudeConic(Vector2d(el.centre), Vector2d(el.u), Vector2d(el.v), map, out);
}

/*! The conic centre + u cos t + v sin t in the profile, swept. */
void extrudeConic(const Vector2d& c, const Vector2d& u, const Vector2d& v, const StationMap& map,
                  Surfaces& out)
{
  // a circle: u and v two perpendicular radii of one length
  const bool circle = u.norm() == v.norm() && u.dot(v) == 0;
  if (!map.affine() && !(c[0] == 0 && c[1] == 0)) return;  // the centre turns with the twist
  const Vector3d c0 = map.at(c, 0), c1 = map.at(c, 1);
  const Vector3d n = map.normal();
  const Vector3d d = c1 - c0;
  const double len = d.norm();
  if (n.isZero() || !(len > 0)) return;
  // along the normal, at machine precision: the cross product's forward error
  const bool along_normal = d.cross(n).norm() <= forwardError(12, len);
  double scale = 0;
  const bool similar = map.similar(scale);

  // No taper: every station's section is the same curve, the image of the
  // circle under the placement, and the arc sweeps the cylinder over it along
  // d. A circular one where the placement is a similarity and d its normal; an
  // elliptic one otherwise - a shear or an uneven scale in the placement, or an
  // oblique v, which until 2026-09-25 were refusals.
  if (map.sweep.scale_x == 1 && map.sweep.scale_y == 1 && !(circle && similar && along_normal)) {
    const Eigen::Matrix3d L = map.sweep.placement.linear();
    auto elliptic = std::make_shared<EllipticCylinderSurface>(c0, d, L * Vector3d(u[0], u[1], 0.),
                                                              L * Vector3d(v[0], v[1], 0.));
    if (elliptic->usable()) addSurfaceUnique(out, elliptic);
    return;
  }
  if (!circle || !similar || !along_normal)
    return;  // a tapered ellipse, or an oblique cone: none here yet
  const Vector3d axis = d / len;
  const double r0 = u.norm() * scale, r1 = u.norm() * scale * map.sweep.scale_x;
  if (!(r1 >= 0) || !std::isfinite(r1)) return;
  if (r1 == r0) {
    addSurfaceUnique(out, std::make_shared<CylinderSurface>(c0, axis, r0));
    return;
  }
  // Anchored at the wider end, as every cone here is.
  const bool near_wider = r0 >= r1;
  addSurfaceUnique(out, std::make_shared<ConeSurface>(near_wider ? c0 : c1, axis, near_wider ? r0 : r1,
                                                      (r1 - r0) / len));
  auto rim = [&](const Vector3d& at, double r) {
    if (!(r > 0)) return;
    auto record = std::make_shared<CylinderSurface>(at, axis, r);
    record->evidence_only = true;
    addSurfaceUnique(out, record);
  };
  rim(c0, r0);
  rim(c1, r1);
}

}  // namespace

std::vector<std::shared_ptr<Surface>> revolve(const Profile2d& profile, const Revolution& sweep)
{
  RevolutionRecords records{sweep, {}, {}};
  for (const auto& loop : profile.loops) {
    for (const ProfileEdge& edge : loop) {
      revolveVertex(edge.a, records);
      if (edge.kind == EdgeKind::Line) revolveLine(edge, records);
    }
  }
  for (const auto& arc : profile.arcs) revolveArc(arc, records);

  // The two faces a partial turn is closed with. Each is the profile's own
  // plane, standing at one end of the turn and containing the axis, so its
  // normal is perpendicular to that plane and the axis both.
  if (fabs(sweep.angle) < 360) {
    for (const double at : {sweep.start, sweep.start + sweep.angle}) {
      const double rad = at * M_PI / 180.0;
      addSurfaceUnique(records.out,
                       std::make_shared<PlaneSurface>(sweep.origin, Vector3d(-sin(rad), cos(rad), 0)));
    }
  }
  return std::move(records.out);
}

std::vector<std::shared_ptr<Surface>> extrude(const Profile2d& profile, const Extrusion& sweep)
{
  const StationMap map(sweep);
  Surfaces out;
  for (const auto& arc : profile.arcs) extrudeArc(arc, map, out);
  for (const auto& el : profile.ellipses) extrudeEllipse(el, map, out);
  for (const auto& bez : profile.beziers) extrudeBezier(bez, map, out);
  extrudeCaps(map, out);
  for (const auto& loop : profile.loops) {
    for (const ProfileEdge& edge : loop) {
      if (edge.kind == EdgeKind::Line) extrudeLine(edge, map, out);
    }
  }
  return out;
}

std::vector<std::shared_ptr<Surface>> roofSurfaces(const Profile2d& profile, bool voronoi)
{
  Surfaces out;
  const Vector3d up(0., 0., 1.);
  addSurfaceUnique(out, std::make_shared<PlaneSurface>(Vector3d(0., 0., 0.), up));
  for (const auto& loop : profile.loops) {
    const std::size_t n = loop.size();
    for (std::size_t i = 0; i < n; i++) {
      const ProfileEdge& e = loop[i];
      if (e.kind != EdgeKind::Line) continue;
      const Vector2d d = e.b - e.a;
      if (d[0] == 0 && d[1] == 0) continue;
      // Over the edge: away from the material, which is on the edge's left, is
      // its right; the height is the distance behind the edge.
      const Vector2d away = Vector2d(d[1], -d[0]).normalized();
      addSurfaceUnique(out, std::make_shared<PlaneSurface>(Vector3d(e.a[0], e.a[1], 0.),
                                                           Vector3d(away[0], away[1], 1.).normalized()));
      // Over the corner this edge turns into: reflex where it turns right, away
      // from the material. Anchored one unit up, where its radius is one.
      const ProfileEdge& next = loop[(i + 1) % n];
      if (!voronoi || next.kind != EdgeKind::Line) continue;
      const Vector2d d2 = next.b - next.a;
      if (d[0] * d2[1] - d[1] * d2[0] >= 0) continue;
      addSurfaceUnique(out, std::make_shared<ConeSurface>(Vector3d(e.b[0], e.b[1], 1.), up, 1., 1.));
    }
  }
  return out;
}
