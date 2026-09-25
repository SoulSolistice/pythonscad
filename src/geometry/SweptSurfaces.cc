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

/*! The cylinders and cones a straight extrusion of the profile's arcs sweeps.
 *
 * An arc extruded along the normal of its own plane sweeps a circular cylinder,
 * exactly. With a uniform `scale` it sweeps a cone instead, stated today - as
 * every cone was until 2026-09-24 - by declaring the circle at each of its two
 * ends, which the band pass accepts as a frustum when both rims match.
 *
 * Everything that would make either untrue is refused rather than approximated:
 *
 *   - a twist, which sweeps a helicoid, or an uneven scale, which sweeps a
 *     general ruled surface;
 *   - an oblique sweep, which sweeps an *oblique* cylinder - a real surface, but
 *     not a CYLINDRICAL_SURFACE, whose circular section is not perpendicular to
 *     its axis;
 *   - a profile plane whose 3D transform is not a similarity, which turns the
 *     circle into an ellipse before it is swept at all;
 *   - a scaled arc whose centre is not the origin the scale is taken about.
 *     `scale` moves such a centre sideways as well as shrinking its radius, so
 *     the surface is an *oblique* cone: still exact, still describable, and not
 *     a CONICAL_SURFACE. Only an arc concentric with the scaling origin tapers
 *     into one.
 */
void extrudeArcs(const Profile2d& profile, const Extrusion& sweep, Surfaces& out)
{
  if (profile.arcs.empty()) return;
  if (sweep.twist != 0 || sweep.scale_x != sweep.scale_y) return;
  const double taper = sweep.scale_x;
  if (!(taper >= 0) || !std::isfinite(taper)) return;
  const double len = sweep.height.norm();
  if (len < 1e-12) return;
  const Vector3d axis = sweep.height / len;

  const Eigen::Matrix3d linear = sweep.placement.linear();
  // the plane the profile lies in, and the scale the transform applies inside it
  const Vector3d ex = linear * Vector3d(1., 0., 0.);
  const Vector3d ey = linear * Vector3d(0., 1., 0.);
  const double sx = ex.norm(), sy = ey.norm();
  if (sx < 1e-12 || sy < 1e-12) return;
  if (fabs(sx - sy) > 1e-12 * sx) return;          // an ellipse, not a circle
  if (fabs(ex.dot(ey)) > 1e-12 * sx * sy) return;  // sheared, likewise
  // and the sweep has to run along that plane's normal, or the cylinder is oblique
  const Vector3d normal = ex.cross(ey) / (sx * sy);
  if (fabs(fabs(normal.dot(axis)) - 1.) > 1e-12) return;

  const double scale = 0.5 * (sx + sy);
  for (const auto& arc : profile.arcs) {
    if (arc.r <= 0) continue;
    const Vector3d centre = sweep.placement * Vector3d(arc.centre[0], arc.centre[1], 0.) + sweep.base;
    const double radius = arc.r * scale;
    if (taper == 1.0) {
      addSurfaceUnique(out, std::make_shared<CylinderSurface>(centre, axis, radius));
      continue;
    }
    // A taper. Only an arc about the scaling origin stays concentric as it
    // shrinks; any other one slides sideways too and sweeps an oblique cone.
    if (arc.centre.norm() > 1e-12 * std::max(1.0, arc.r)) continue;
    // Both are rims of the cone the taper sweeps - evidence, not faces: no facet
    // of this solid lies on either. See Surface::witnesses.
    auto base_rim = std::make_shared<CylinderSurface>(centre, axis, radius);
    base_rim->evidence_only = true;
    addSurfaceUnique(out, base_rim);
    if (taper > 0) {
      auto top_rim = std::make_shared<CylinderSurface>(centre + sweep.height, axis, radius * taper);
      top_rim->evidence_only = true;
      addSurfaceUnique(out, top_rim);
    }
  }
}

/*! The Bezier patches a straight extrusion of the profile's Bezier segments
 * sweeps.
 *
 * A Bezier segment swept along a straight line is a tensor product patch of
 * degree (n, 1) whose control net is the segment's own control points at each
 * end of the sweep - exact, and needing no fit, because both a Bezier and a
 * linear sweep are affine in their control points. So a Bezier survives the
 * shear, the non-uniform scale and the oblique sweep an arc has to refuse: the
 * extrusion's station at t is the profile scaled by lerp(1, s, t), and the
 * patch's own linear interpolation in v gives lerp(P, sP, t), the same points.
 *
 * What it cannot take is a twist, where the station at t is the profile
 * *rotated* by t times the angle. A rotation is not linear in t, and the swept
 * surface is not a Bezier patch of any degree.
 */
void extrudeBeziers(const Profile2d& profile, const Extrusion& sweep, Surfaces& out)
{
  if (profile.beziers.empty()) return;
  if (sweep.twist != 0) return;
  const Vector2d top_scale(sweep.scale_x, sweep.scale_y);
  if (!top_scale.allFinite()) return;

  for (const auto& bez : profile.beziers) {
    if (bez.degree < 2 || bez.degree > 3) continue;
    // v varies fastest, so the net is control point by control point, each one
    // as a pair: where it starts and where the sweep takes it.
    std::vector<Vector3d> net;
    net.reserve((bez.degree + 1) * 2);
    for (int i = 0; i <= bez.degree; i++) {
      const Vector2d p(bez.ctrl[i][0], bez.ctrl[i][1]);
      net.push_back(sweep.placement * Vector3d(p[0], p[1], 0.) + sweep.base);
      net.push_back(sweep.placement * Vector3d(p[0] * top_scale[0], p[1] * top_scale[1], 0.) +
                    sweep.base + sweep.height);
    }
    out.push_back(std::make_shared<BezierPatchSurface>(bez.degree, 1, std::move(net)));
  }
}

/*! The planes a straight extrusion sweeps: its two caps, and each straight wall.
 *
 * A plane is exact whether it is declared or fitted, so this is not about
 * accuracy - it is about the coefficients staying put. A plane fitted to mesh
 * vertices moves with the tessellation, with whatever a boolean left behind, and
 * with any later pass that touches a vertex; the declared one cannot move.
 *
 * **The caps are always planar.** Every station of a linear extrusion is the
 * profile scaled and rotated *within its own plane* and then translated, so the
 * first and last lie in two parallel planes whatever the sweep does. Only a
 * degenerate top - scaled to nothing - has no face there.
 *
 * **A straight wall is planar unless something turns it.** The wall over a
 * straight profile edge AB is the ruled surface between AB and its image A'B',
 * and it is planar exactly when those two are parallel:
 *
 *   - a twist rotates the image, so A'B' is AB turned and the two are skew;
 *   - an uneven scale sends AB to (sx*u1, sy*u2), parallel to u = (u1, u2) only
 *     when the edge already runs along x or y - so an uneven scale is refused
 *     except for those edges, which are exactly the ones it leaves alone;
 *   - a uniform scale and any sweep direction, oblique included, keep them
 *     parallel, because A'B' = s*AB and the translation is common to both ends.
 *
 * A chord of a recorded arc or Bezier is *not* a straight edge, whatever the
 * outline says: it is the tessellation's chord, its plane moves with `$fn`, and
 * declaring it would put a tessellation artefact on a channel whose whole point
 * is that a declaration does not move. Profile2d says which edges those are.
 */
void extrudePlanes(const Profile2d& profile, const Extrusion& sweep, Surfaces& out)
{
  const Eigen::Matrix3d linear = sweep.placement.linear();
  const Vector3d ex = linear * Vector3d(1., 0., 0.);
  const Vector3d ey = linear * Vector3d(0., 1., 0.);
  const Vector3d normal = ex.cross(ey);
  if (normal.norm() < 1e-12) return;
  const Vector3d unit_normal = normal.normalized();
  const Vector2d top_scale(sweep.scale_x, sweep.scale_y);
  if (!top_scale.allFinite()) return;

  // The two caps.
  addSurfaceUnique(out, std::make_shared<PlaneSurface>(sweep.base, unit_normal));
  if (top_scale[0] != 0. || top_scale[1] != 0.) {
    addSurfaceUnique(out, std::make_shared<PlaneSurface>(sweep.base + sweep.height, unit_normal));
  }

  if (sweep.twist != 0) return;
  const bool even_scale = sweep.scale_x == sweep.scale_y;

  for (const auto& loop : profile.loops) {
    if (loop.size() < 2) continue;
    for (const ProfileEdge& edge : loop) {
      const Vector2d& a = edge.a;
      const Vector2d& b = edge.b;
      if ((b - a).norm() < 1e-12) continue;
      if (edge.kind != EdgeKind::Line) continue;
      // An uneven scale keeps only the edges it does not shear.
      if (!even_scale && fabs(b[0] - a[0]) > 1e-12 && fabs(b[1] - a[1]) > 1e-12) continue;
      const Vector3d p = sweep.placement * Vector3d(a[0], a[1], 0.) + sweep.base;
      const Vector3d q = sweep.placement * Vector3d(b[0], b[1], 0.) + sweep.base;
      const Vector3d up = sweep.placement * Vector3d(a[0] * top_scale[0], a[1] * top_scale[1], 0.) +
                          sweep.base + sweep.height - p;
      const Vector3d wall = (q - p).cross(up);
      if (wall.norm() < 1e-12) continue;
      addSurfaceUnique(out, std::make_shared<PlaneSurface>(p, Vector3d(wall.normalized())));
    }
  }
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
  Surfaces out;
  extrudeArcs(profile, sweep, out);
  extrudeBeziers(profile, sweep, out);
  extrudePlanes(profile, sweep, out);
  return out;
}
