/*! The recognised bands, written - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "utils/printutils.h"

void StepKernel::BodyBuild::emitBands()
{
  for (std::size_t i = 0; i < bands.size(); i++) {
    const AnalyticFeatures::Band& band = bands[i];
    if (!band.alive) continue;

    // A torus is closed in both directions, so its face is bounded by nothing
    // but its own two seams: the major circle through one profile station and
    // the meridian circle at one longitude, each a closed circle through the
    // same vertex and each used once in either direction. That is the same four
    // edge loop a periodic cylinder uses - the doubly periodic case needs no
    // new shape, only a second seam - and it needs nothing from the mesh beyond
    // that one vertex, since both circles come out of the declared record.
    // Only the complete one. A run with a free rim at either end - a rounded
    // corner of a revolved profile, which sweeps a quarter of a torus - is a
    // ring like any other band: two rim circles and one seam, written by the
    // generic path below with a ToroidalSurface under it.
    const auto *whole_torus = dynamic_cast<const TorusSurface *>(band.zone.get());
    if (whole_torus != nullptr && band.seam_bottom != band.seam_top) whole_torus = nullptr;
    if (const auto *tor = whole_torus) {
      const Vector3d centre = tor->refpt;
      const Vector3d axis = tor->normdir.normalized();
      const Vector3d corner = vertices[band.seam_bottom];
      const Vector3d rel = corner - centre;
      const double along = rel.dot(axis);
      const Vector3d radial = (rel - axis * along).normalized();
      // the point on the tube's centre circle nearest the corner
      const Vector3d tube = centre + radial * tor->r_major;

      auto placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
        auto point = new Point(entities, origin);
        auto dir_axis = new Direction(entities, dir);
        auto dir_ref = new Direction(entities, towards);
        return new Axis2Placement(entities, dir_axis, dir_ref, point);
      };

      auto surface =
        new ToroidalSurface(entities, "", placement(centre, axis, radial), tor->r_major, tor->r_minor);
      Vertex *vert = get_vertex(band.seam_bottom);

      // the major circle: round the axis, through the corner
      auto major = new Circle(entities, "", placement(centre + axis * along, axis, radial),
                              (rel - axis * along).norm());
      auto edge_major = new EdgeCurve(entities, vert, vert, major, true);

      // the meridian circle: round the tube, in the plane the axis and the
      // radial direction span
      auto meridian = new Circle(
        entities, "", placement(tube, radial.cross(axis), (corner - tube).normalized()), tor->r_minor);
      auto edge_meridian = new EdgeCurve(entities, vert, vert, meridian, true);

      std::vector<OrientedEdge *> loop{
        new OrientedEdge(entities, edge_major, true),
        new OrientedEdge(entities, edge_meridian, true),
        new OrientedEdge(entities, edge_major, false),
        new OrientedEdge(entities, edge_meridian, false),
      };
      auto edge_loop = new EdgeLoop(entities, loop);
      std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};
      sfaces_extra.push_back(new Face(entities, bounds, surface, band.outward));
      face_edges_extra.push_back({edge_major, edge_meridian});
      continue;
    }

    // A sphere which reaches both poles closes on itself, so like the complete
    // torus above it is bounded by its seam and by nothing else. What differs is
    // that a sphere is periodic in one direction only: the seam is a meridian
    // running pole to pole - half of a great circle - used once in either
    // direction, and the poles are where the two usages meet. There is no rim,
    // because the two polar caps that used to bound the run are part of this
    // face now.
    //
    // The poles come from the surface record - centre +/- r along the axis - and
    // not from the mesh, which has a vertex at neither of them. Nothing else
    // refers to them, so inventing them here costs no other loop a rewrite: the
    // same argument the seam itself rests on.
    //
    // OCCT reads this back as one Sphere with two degenerate edges, which are
    // the zero length edges it inserts at the poles itself, and measures the
    // volume as (4/3)pi r^3.
    if (band.pole_closed) {
      const auto *sph = dynamic_cast<const SphereSurface *>(band.zone.get());
      if (sph != nullptr) {
        const Vector3d centre = sph->refpt;
        const Vector3d axis = band.axis.normalized();
        // the longitude the seam runs down, which is also the direction the
        // surface's own parameterisation is measured from
        const Vector3d rel = vertices[band.seam_bottom] - centre;
        const Vector3d radial = (rel - axis * rel.dot(axis)).normalized();

        auto placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
          auto point = new Point(entities, origin);
          auto dir_axis = new Direction(entities, dir);
          auto dir_ref = new Direction(entities, towards);
          return new Axis2Placement(entities, dir_axis, dir_ref, point);
        };

        auto surface = new SphericalSurface(entities, "", placement(centre, axis, radial), sph->r);
        auto south = new Vertex(entities, new Point(entities, centre - axis * sph->r));
        auto north = new Vertex(entities, new Point(entities, centre + axis * sph->r));

        // Parameterised from the south pole, so that a quarter turn reaches the
        // seam's own longitude and a half turn the north pole. `radial x axis`
        // is the normal that makes that the positive sweep; the other way round
        // the arc runs down the far side of the sphere, and the face is bounded
        // by the wrong half of the great circle.
        auto meridian =
          new Circle(entities, "", placement(centre, radial.cross(axis), Vector3d(-axis)), sph->r);
        auto edge_seam = new EdgeCurve(entities, south, north, meridian, true);

        std::vector<OrientedEdge *> loop{
          new OrientedEdge(entities, edge_seam, true),
          new OrientedEdge(entities, edge_seam, false),
        };
        auto edge_loop = new EdgeLoop(entities, loop);
        std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};
        sfaces_extra.push_back(new Face(entities, bounds, surface, band.outward));
        face_edges_extra.push_back({edge_seam});
        continue;
      }
    }

    const Vector3d top_centre = band.base + band.axis * band.height;
    const bool is_cone = band.isCone();

    auto placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
      auto point = new Point(entities, origin);
      auto dir_axis = new Direction(entities, dir);
      auto dir_ref = new Direction(entities, towards);
      return new Axis2Placement(entities, dir_axis, dir_ref, point);
    };

    // a radial direction to measure the surface's own parameterisation from
    const int ref_vertex = band.closed ? band.seam_bottom : band.bottom_set.front();
    const Vector3d rel = vertices[ref_vertex] - band.base;
    const Vector3d ref = (rel - band.axis * band.axis.dot(rel)).normalized();

    SurfaceType *surface = nullptr;
    if (const auto *tor = dynamic_cast<const TorusSurface *>(band.zone.get())) {
      // A partial torus: the placement comes from the record, not from the
      // band's rims. Its own axis and a radial direction through the seam are
      // what the parameterisation is measured from, exactly as for the complete
      // one above.
      const Vector3d axis = tor->normdir.normalized();
      const Vector3d rel_seam = vertices[band.seam_bottom] - tor->refpt;
      const Vector3d radial = (rel_seam - axis * rel_seam.dot(axis)).normalized();
      surface = new ToroidalSurface(entities, "", placement(tor->refpt, axis, radial), tor->r_major,
                                    tor->r_minor);
    } else if (band.zone != nullptr) {
      const auto *sph = dynamic_cast<const SphereSurface *>(band.zone.get());
      surface = new SphericalSurface(entities, "", placement(sph->refpt, band.axis, ref), sph->r);
    } else if (!is_cone) {
      surface =
        new CylindricalSurface(entities, "", placement(band.base, band.axis, ref), band.r_bottom);
    } else {
      // ISO 10303 wants a half angle in (0, pi/2), so a cone which narrows
      // along the axis is written from its other end instead.
      const bool widens = band.r_top > band.r_bottom;
      const Vector3d origin = widens ? band.base : top_centre;
      const Vector3d dir = widens ? band.axis : Vector3d(-band.axis);
      const double r0 = widens ? band.r_bottom : band.r_top;
      const double half_angle = atan(fabs(band.r_top - band.r_bottom) / band.height);
      surface = new ConicalSurface(entities, "", placement(origin, dir, ref), r0, half_angle);
    }

    // the rims
    EdgeCurve *rim_edge[2] = {nullptr, nullptr};
    bool rim_sense[2] = {true, true};
    // The mesh's own chords along a rim, where the arc cannot be handed to the
    // face across it - see below. Empty whenever the rim is written as the arc
    // it is, which is every rim on every fixture measured.
    std::vector<std::pair<EdgeCurve *, bool>> rim_chords[2];
    for (int side = 0; side < 2; side++) {
      const bool bottom = side == 0;
      const AnalyticFeatures::RimRef& rim = bottom ? rims[i].first : rims[i].second;
      const std::vector<int>& level = bottom ? band.bottom_set : band.top_set;
      const Vector3d centre = bottom ? band.base : top_centre;
      const double radius = bottom ? band.r_bottom : band.r_top;
      const std::set<int> key(level.begin(), level.end());

      const auto shared = shared_rim_edges.find(key);
      if (shared != shared_rim_edges.end()) {
        // the other side of a shared rim already made the circle
        rim_edge[side] = shared->second;
        rim_sense[side] = rim.wall_ccw;
        continue;
      }

      if (band.closed) {
        const int seam = bottom ? band.seam_bottom : band.seam_top;
        const Vector3d seam_rel = vertices[seam] - centre;
        RoundType *curve = nullptr;
        if (!bottom && band.top_tilted) {
          // The rim is the section of the cylinder by a plane which is not
          // perpendicular to the axis, so it is an ellipse. Its minor axis has
          // the cylinder's own radius and lies where the cut plane meets the
          // plane through the centre perpendicular to it; its major axis runs
          // up the steepest ascent of the cut and is longer by exactly the
          // secant of the tilt.
          //
          // Unlike a circle the reference direction is not free - it is what
          // says which semi-axis is which - so the seam vertex does not sit at
          // the start of the parameterisation here. A closed edge does not
          // require it to.
          const Vector3d n = band.top_normal;
          const double cos_tilt = n.dot(band.axis);
          const Vector3d major = (band.axis - n * cos_tilt).normalized();
          curve = new Ellipse(entities, "", placement(centre, n, major), radius / cos_tilt, radius);
        } else {
          curve = new Circle(entities, "", placement(centre, band.axis, seam_rel.normalized()), radius);
        }
        Vertex *vert = get_vertex(seam);
        // a full circle is one edge whose two ends are the same vertex
        rim_edge[side] = new EdgeCurve(entities, vert, vert, curve, true);
        rim_sense[side] = rim.wall_ccw;
        if (rim.kind == AnalyticFeatures::RimRef::OTHER_BAND)
          shared_rim_edges.emplace(key, rim_edge[side]);
        else rim_of_loop.emplace(rim.loop, std::make_pair(rim_edge[side], !rim.wall_ccw));
      } else {
        // An arc, from one end of the rim to the other. A CIRCLE is counter
        // clockwise about its own axis, so the arc is always written that way
        // and the face's own direction is carried by rim_sense.
        const int from = rim.ccw_start, to = rim.ccw_end;
        auto circle = new Circle(
          entities, "", placement(centre, band.axis, (vertices[from] - centre).normalized()), radius);
        rim_edge[side] = new EdgeCurve(entities, get_vertex(from), get_vertex(to), circle, true);
        rim_sense[side] = rim.wall_ccw;
        if (rim.kind == AnalyticFeatures::RimRef::OTHER_BAND_ARC) {
          // shared with another partial band: one arc, two curved faces, no
          // planar loop anywhere along it
          shared_rim_edges.emplace(key, rim_edge[side]);
        } else {
          // A loop that will be fanned into triangles cannot take this arc. The
          // fan writes one straight line per triangle side and never reads
          // `arc_subs`, and a rim spanning a run of corners could not be one
          // triangle's side even if it did - so the handover is impossible
          // rather than missed, and writing the arc here leaves the conic on
          // this face and chords on that one.
          //
          // Open item 11's root cause A, which the plane sections have guarded
          // against since it was closed for them; the rims never did. On the
          // nine-line model of item 18 a cone's top rim was written as one
          // CIRCLE while the cap across it was fanned, and the export carried
          // 25 edges used by one face.
          //
          // So the rim is written as the mesh's own chords instead, taken from
          // the same edge map the fan's triangles use - which is what makes
          // them the same edges rather than a second set on the same points. A
          // worse boundary on this one face, and a closed shell.
          if (!band.closed && split_for_corners.count(rim.loop) != 0) {
            const std::vector<int>& across = loops[rim.loop];
            const std::size_t m = across.size();
            std::vector<int> walk;
            for (std::size_t k = 0; m > 0 && k <= rim.count; k++) {
              walk.push_back(across[(rim.start + k) % m]);
            }
            if (!walk.empty() && walk.front() != rim.traversalStart()) {
              std::reverse(walk.begin(), walk.end());
            }
            for (std::size_t k = 0; k + 1 < walk.size(); k++) {
              bool dir = true;
              EdgeCurve *chord = get_line_from_map(edge_map, walk[k], walk[k + 1], get_vertex(walk[k]),
                                                   get_vertex(walk[k + 1]), dir, merged_edge_cnt);
              rim_chords[side].emplace_back(chord, dir);
            }
            if (!rim_chords[side].empty()) {
              rim_unshared++;
              continue;
            }
          }
          arc_subs[rim.loop].push_back({rim.start, rim.count, rim_edge[side], !rim.wall_ccw});
        }
      }
    }

    std::vector<OrientedEdge *> loop;
    std::vector<EdgeCurve *> face_edges_here;
    if (band.closed) {
      // The seam of a periodic face has to lie *on* the surface. Up a cylinder
      // or a cone that is a straight ruling; over a sphere it is a meridian,
      // and the straight line between the same two vertices is a chord that
      // sags off the surface by far more than the modelling tolerance - 0.05mm
      // on a radius 10 sphere at $fn=32. So a spherical zone seams with an arc
      // of a great circle instead.
      //
      // Nothing else refers to the seam: it is used twice by this one face and
      // by nothing at all in the mesh, so replacing the line costs no
      // neighbouring loop a rewrite.
      EdgeCurve *edge_seam = nullptr;
      if (const auto *tor = dynamic_cast<const TorusSurface *>(band.zone.get())) {
        // Over a torus the seam is a meridian: an arc of the tube's own circle,
        // about the point on the tube's centre circle at the seam's longitude.
        // Same argument as the sphere below - a straight chord sags off the
        // surface - and the same construction, one level in.
        const Vector3d axis = tor->normdir.normalized();
        const Vector3d rel_seam = vertices[band.seam_bottom] - tor->refpt;
        const Vector3d radial = (rel_seam - axis * rel_seam.dot(axis)).normalized();
        const Vector3d tube = tor->refpt + radial * tor->r_major;
        const Vector3d from = (vertices[band.seam_bottom] - tube).normalized();
        const Vector3d to = (vertices[band.seam_top] - tube).normalized();
        const Vector3d normal = from.cross(to);
        auto meridian =
          new Circle(entities, "", placement(tube, normal.normalized(), from), tor->r_minor);
        edge_seam = new EdgeCurve(entities, get_vertex(band.seam_bottom), get_vertex(band.seam_top),
                                  meridian, true);
      } else if (band.zone != nullptr) {
        const auto *sph = dynamic_cast<const SphereSurface *>(band.zone.get());
        const Vector3d from = (vertices[band.seam_bottom] - sph->refpt).normalized();
        const Vector3d to = (vertices[band.seam_top] - sph->refpt).normalized();
        // this normal is the one that makes the sweep from `from` to `to`
        // counter clockwise and shorter than half a turn, which a zone's
        // latitude range always is - the flat cap at either end sees to that
        const Vector3d normal = from.cross(to);
        auto meridian =
          new Circle(entities, "", placement(sph->refpt, normal.normalized(), from), sph->r);
        edge_seam = new EdgeCurve(entities, get_vertex(band.seam_bottom), get_vertex(band.seam_top),
                                  meridian, true);
      } else {
        edge_seam =
          create_line_edge_curve(get_vertex(band.seam_bottom), get_vertex(band.seam_top), true);
      }
      loop.push_back(new OrientedEdge(entities, rim_edge[0], rim_sense[0]));
      loop.push_back(new OrientedEdge(entities, edge_seam, true));
      loop.push_back(new OrientedEdge(entities, rim_edge[1], rim_sense[1]));
      loop.push_back(new OrientedEdge(entities, edge_seam, false));
      face_edges_here = {rim_edge[0], rim_edge[1], edge_seam};
    } else {
      // walk along the bottom rim, up the end edge, back along the top rim,
      // down the other end edge - so the ends are where one rim's traversal
      // finishes and the other's begins
      const int b_from = rims[i].first.traversalStart();
      const int b_to = rims[i].first.traversalEnd();
      const int t_from = rims[i].second.traversalStart();
      const int t_to = rims[i].second.traversalEnd();

      bool up_dir = true, down_dir = true;
      EdgeCurve *edge_up = get_line_from_map(edge_map, b_to, t_from, get_vertex(b_to),
                                             get_vertex(t_from), up_dir, merged_edge_cnt);
      EdgeCurve *edge_down = get_line_from_map(edge_map, t_to, b_from, get_vertex(t_to),
                                               get_vertex(b_from), down_dir, merged_edge_cnt);
      auto walk_rim = [&](int side) {
        if (rim_chords[side].empty()) {
          loop.push_back(new OrientedEdge(entities, rim_edge[side], rim_sense[side]));
          face_edges_here.push_back(rim_edge[side]);
          return;
        }
        for (const auto& chord : rim_chords[side]) {
          loop.push_back(new OrientedEdge(entities, chord.first, chord.second));
          face_edges_here.push_back(chord.first);
        }
      };
      walk_rim(0);
      loop.push_back(new OrientedEdge(entities, edge_up, up_dir));
      walk_rim(1);
      loop.push_back(new OrientedEdge(entities, edge_down, down_dir));
      face_edges_here.push_back(edge_up);
      face_edges_here.push_back(edge_down);
    }

    auto edge_loop = new EdgeLoop(entities, loop);
    std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};

    // The surface normal of a cylinder or a cone points away from its axis, so
    // a bore - where the material is outside it - is the opposite sense.
    sfaces_extra.push_back(new Face(entities, bounds, surface, band.outward));
    face_edges_extra.push_back(face_edges_here);
  }
}
