/*! What a sweep of a profile makes - see SweptSurfaces.h. */
#include "geometry/SweptSurfaces.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "geometry/Surface.h"

namespace {

using Surfaces = std::vector<std::shared_ptr<Surface>>;

// ---------------------------------------------------------------------------
// Revolution
// ---------------------------------------------------------------------------

/*! Where a profile edge runs, relative to the axis it turns about. Every edge is
 * exactly one of these, and each has its row in revolveEdge. */
enum class Course {
  Point,   //!< no extent at all
  Across,  //!< one level, two radii: an annulus, or a disc where it reaches the axis
  OnAxis,  //!< along the axis itself
  Apex,    //!< changing level, one end on the axis
  Along,   //!< one radius, two levels: a wall
  Sloped,  //!< neither, and off the axis at both ends
};

// the profile's x is the radius and its y the level along the axis
const double kEps = 1e-12;

Course courseOf(const ProfileEdge& e)
{
  const Vector2d& a = e.a;
  const Vector2d& b = e.b;
  if (fabs(a[1] - b[1]) < kEps) return fabs(a[0] - b[0]) > kEps ? Course::Across : Course::Point;
  if (a[0] <= kEps && b[0] <= kEps) return Course::OnAxis;
  if (a[0] <= kEps || b[0] <= kEps) return Course::Apex;
  return fabs(a[0] - b[0]) <= kEps ? Course::Along : Course::Sloped;
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
      if (fabs(r - p[0]) <= kEps * std::max(1.0, r)) {
        if (face) addSurfaceUnique(out, record);  // upgrades the kept record to a face
        return;
      }
    }
    radii.push_back(p[0]);
    addSurfaceUnique(out, record);
  }
};

/*! What one straight edge of the profile sweeps. */
void revolveEdge(const ProfileEdge& e, bool has_beziers, RevolutionRecords& records)
{
  const Vector2d& a = e.a;
  const Vector2d& b = e.b;
  switch (courseOf(e)) {
  case Course::Point: return;  // sweeps a circle, which bounds a face and is not one
  case Course::Across:
    // A flat annulus, or a disc - a *plane*, exactly, at the level the profile
    // puts it, and saying so is worth as much as saying which cylinder a wall is
    // on. A plane fitted to the mesh instead moves with the tessellation and
    // with whatever a boolean leaves behind; this one is the model's own number.
    addSurfaceUnique(records.out, std::make_shared<PlaneSurface>(records.at(a[1]), records.sweep.axis));
    return;
  case Course::OnAxis: return;  // the axis sweeps nothing
  case Course::Apex:
    // A cone whose apex is on the axis, and a wall like any other: the cone
    // itself as a face, anchored at its one rim. Refused
    // until 2026-09-25 - first because the fan at the apex left zero-area facets
    // that opened the shell (rotate_extrude welds them now), then because nobody
    // took the refusal out: doc/step-export-wip.md, *Inferred where a
    // declaration was possible*, item 4.
    // No rim as evidence: evidence names a cone by its two rims, and this one
    // has a point where the second would be.
    {
      const Vector2d& rim = a[0] > b[0] ? a : b;
      if (has_beziers || e.kind != EdgeKind::Line) return;
      addSurfaceUnique(records.out,
                       std::make_shared<ConeSurface>(records.at(rim[1]), records.sweep.axis, rim[0],
                                                     (b[0] - a[0]) / (b[1] - a[1])));
    }
    return;
  case Course::Along:
    records.cylinder(a, true);  // a wall, on that cylinder - a face of this solid
    return;
  case Course::Sloped:
    // A cone, and the circle at each end of it as *evidence* - rims the band
    // pass identifies it by; no facet lies on them.
    records.cylinder(a, false);
    records.cylinder(b, false);
    // A chord of a recorded arc is the arc's surface, not a cone; and where the
    // profile carries Bezier records an edge may be a chord of one of those, so
    // it declares nothing it cannot state, and the rims stay evidence only.
    if (has_beziers || e.kind == EdgeKind::Arc) return;
    // Until 2026-09-24 a sloped edge declared its rims alone, so the cone it
    // sweeps was *recognised* from them and claimed under a rim's name: on lid10
    // the seal bead's two flanks - 120 facets - were held on cylinders no facet
    // lies on, and no identity could make them D0. Anchored at the wider rim, as
    // cylinder(r1, r2) anchors its own (primitives.cc).
    {
      const Vector2d& wide = a[0] >= b[0] ? a : b;
      addSurfaceUnique(records.out,
                       std::make_shared<ConeSurface>(records.at(wide[1]), records.sweep.axis, wide[0],
                                                     (b[0] - a[0]) / (b[1] - a[1])));
    }
    return;
  }
}

/*! What one arc record sweeps: a sphere where its centre is on the axis, a ring
 * torus where the whole circle is off it. */
void revolveArc(const Arc2d& arc, RevolutionRecords& records)
{
  if (arc.r <= kEps) return;  // a point
  const Vector2d centre(arc.centre[0], arc.centre[1]);
  if (fabs(centre[0]) <= kEps) {
    addSurfaceUnique(records.out,
                     std::make_shared<SphereSurface>(records.at(centre[1]), records.sweep.axis, arc.r));
  } else if (centre[0] > arc.r + kEps) {
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
  const bool has_beziers = !profile.beziers.empty();
  for (const auto& loop : profile.loops) {
    for (const ProfileEdge& edge : loop) revolveEdge(edge, has_beziers, records);
  }

  // The two faces a partial sweep is closed with. Each is the profile's own
  // plane, standing at one end of the arc and containing the axis, so its normal
  // is perpendicular to that plane and the axis both. A full turn closes on
  // itself and has neither.
  if (fabs(sweep.angle) < 360 - 1e-9) {
    for (const double at : {sweep.start, sweep.start + sweep.angle}) {
      const double rad = at * M_PI / 180.0;
      addSurfaceUnique(records.out,
                       std::make_shared<PlaneSurface>(sweep.origin, Vector3d(-sin(rad), cos(rad), 0)));
    }
  }

  for (const auto& arc : profile.arcs) revolveArc(arc, records);
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
