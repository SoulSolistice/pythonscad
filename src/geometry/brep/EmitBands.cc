/*! The recognised bands, written - see BodyBuild.h. */
#include "geometry/brep/BodyBuild.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "utils/printutils.h"

// Built on the B-rep, faces and edges alike, and made as no entity (open item
// 39, step 2d, the eighth move): what the reader made of the entities this
// emitter made before, value for value - a placement's directions as given, a
// line along the unit vector from its first vertex.
void step::BodyBuild::emitBands()
{
  auto placement = [](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
    brep::Frame frame;
    frame.origin = origin;
    frame.axis = dir;
    frame.ref = towards;
    return frame;
  };
  auto add_edge = [&](std::size_t from, std::size_t to, brep::CurveForm form) {
    brep::Edge edge;
    edge.start = from;
    edge.end = to;
    edge.same_sense = true;
    edge.curve.form = std::move(form);
    brep_body.edges.push_back(std::move(edge));
    return brep_body.edges.size() - 1;
  };
  auto circle = [](const brep::Frame& frame, double r) {
    return brep::Conic{brep::Conic::Kind::Circle, frame, r, r};
  };
  auto coedge = [](std::size_t edge, bool forward) { return brep::Coedge{edge, forward, std::nullopt}; };
  // The face, in its slot among the analytic faces.
  auto build = [&](brep::SurfaceForm form, std::vector<brep::Coedge> coedges, bool outward,
                   const std::shared_ptr<const Surface>& record, const std::vector<std::size_t>& covered,
                   const std::shared_ptr<const Surface>& carrier) {
    brep::Face face;
    brep::FaceSource source = sourceOf(record, covered, std::nullopt, false);
    source.carrier = carrier;
    brep::stateSource(face, source);
    face.same_sense = outward;
    face.form = std::move(form);
    brep::Loop loop;
    loop.outer = true;
    loop.coedges = std::move(coedges);
    face.loops.push_back(std::move(loop));
    analytic_faces.push_back(std::move(face));
  };
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
    // What the band's face covers, and the declaration it stands on: its
    // walls' claim names it, since a band's own surface is built from its rims.
    std::vector<std::size_t> covered = band.walls;
    covered.insert(covered.end(), band.caps.begin(), band.caps.end());
    int32_t claimed = -1;
    if (!band.walls.empty()) {
      for (const auto *claim : features.ledger.claimants(band.walls.front())) {
        if (!claim->honoured() || claim->surface_index >= surfaces.size()) continue;
        claimed = int32_t(claim->surface_index);
        break;
      }
    }
    // The surface the face is written on, as the band states it (Band::carrier):
    // the zone it closes, or the cylinder or cone its own rims make. Whether
    // that is the record it stands for is the contract's question
    // (brep::checkAttribution), not one this writer answers by assuming it
    // (open item 39, R10).
    const std::shared_ptr<const Surface> carrier = band.carrier();
    // The record the face stands on: the declaration its walls are claimed for,
    // or - a band on a fitted surface - what it is written on.
    std::shared_ptr<const Surface> record =
      claimed >= 0 ? std::shared_ptr<const Surface>(surfaces[claimed]) : carrier;
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

      const brep::Torus surface{placement(centre, axis, radial), tor->r_major, tor->r_minor};
      const std::size_t vert = brepVertex(band.seam_bottom);

      // the major circle: round the axis, through the corner
      const std::size_t edge_major = add_edge(
        vert, vert, circle(placement(centre + axis * along, axis, radial), (rel - axis * along).norm()));

      // the meridian circle: round the tube, in the plane the axis and the
      // radial direction span
      const std::size_t edge_meridian = add_edge(
        vert, vert,
        circle(placement(tube, radial.cross(axis), (corner - tube).normalized()), tor->r_minor));

      build(surface,
            {coedge(edge_major, true), coedge(edge_meridian, true), coedge(edge_major, false),
             coedge(edge_meridian, false)},
            band.outward, record, covered, carrier);
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

        const brep::Sphere surface{placement(centre, axis, radial), sph->r};
        brep::Vertex pole;
        pole.point = centre - axis * sph->r;
        brep_body.vertices.push_back(pole);
        const std::size_t south = brep_body.vertices.size() - 1;
        pole.point = centre + axis * sph->r;
        brep_body.vertices.push_back(pole);
        const std::size_t north = brep_body.vertices.size() - 1;

        // Parameterised from the south pole, so that a quarter turn reaches the
        // seam's own longitude and a half turn the north pole. `radial x axis`
        // is the normal that makes that the positive sweep; the other way round
        // the arc runs down the far side of the sphere, and the face is bounded
        // by the wrong half of the great circle.
        const std::size_t edge_seam =
          add_edge(south, north, circle(placement(centre, radial.cross(axis), Vector3d(-axis)), sph->r));

        build(surface, {coedge(edge_seam, true), coedge(edge_seam, false)}, band.outward, record,
              covered, carrier);
        continue;
      }
    }

    const Vector3d top_centre = band.base + band.axis * band.height;
    const bool is_cone = band.isCone();

    // a radial direction to measure the surface's own parameterisation from
    const int ref_vertex = band.closed ? band.seam_bottom : band.bottom_set.front();
    const Vector3d rel = vertices[ref_vertex] - band.base;
    const Vector3d ref = (rel - band.axis * band.axis.dot(rel)).normalized();

    brep::SurfaceForm surface;
    if (const auto *tor = dynamic_cast<const TorusSurface *>(band.zone.get())) {
      // A partial torus: the placement comes from the record, not from the
      // band's rims. Its own axis and a radial direction through the seam are
      // what the parameterisation is measured from, exactly as for the complete
      // one above.
      const Vector3d axis = tor->normdir.normalized();
      const Vector3d rel_seam = vertices[band.seam_bottom] - tor->refpt;
      const Vector3d radial = (rel_seam - axis * rel_seam.dot(axis)).normalized();
      surface = brep::Torus{placement(tor->refpt, axis, radial), tor->r_major, tor->r_minor};
    } else if (band.zone != nullptr) {
      const auto *sph = dynamic_cast<const SphereSurface *>(band.zone.get());
      surface = brep::Sphere{placement(sph->refpt, band.axis, ref), sph->r};
    } else if (!is_cone) {
      surface = brep::Cylinder{placement(band.base, band.axis, ref), band.r_bottom};
    } else {
      // ISO 10303 wants a half angle in (0, pi/2), so a cone which narrows
      // along the axis is written from its other end instead.
      const bool widens = band.r_top > band.r_bottom;
      const Vector3d origin = widens ? band.base : top_centre;
      const Vector3d dir = widens ? band.axis : Vector3d(-band.axis);
      const double r0 = widens ? band.r_bottom : band.r_top;
      const double half_angle = atan(fabs(band.r_top - band.r_bottom) / band.height);
      surface = brep::Cone{placement(origin, dir, ref), r0, half_angle};
    }

    // the rims
    std::size_t rim_edge[2] = {0, 0};
    bool rim_sense[2] = {true, true};
    // The mesh's own chords along a rim, where the arc cannot be handed to the
    // face across it - see below. Empty whenever the rim is written as the arc
    // it is, which is every rim on every fixture measured.
    std::vector<std::pair<std::size_t, bool>> rim_chords[2];
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
        brep::CurveForm curve;
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
          curve = brep::Conic{brep::Conic::Kind::Ellipse, placement(centre, n, major), radius / cos_tilt,
                              radius};
        } else {
          curve = circle(placement(centre, band.axis, seam_rel.normalized()), radius);
        }
        // A full circle is one edge whose two ends are the same vertex. Built
        // where a face uses it: a rim given up as chords below is no edge of
        // the body, as the entity it was before was never read into it.
        auto rim_circle = [&] {
          const std::size_t vert = brepVertex(seam);
          return add_edge(vert, vert, std::move(curve));
        };
        rim_sense[side] = rim.wall_ccw;
        if (rim.kind == AnalyticFeatures::RimRef::OTHER_BAND) {
          rim_edge[side] = rim_circle();
          shared_rim_edges.emplace(key, rim_edge[side]);
          continue;
        }
        // The same rule as an arc's, below: a loop that is not written cannot
        // take the curve. The whole ring then, as the mesh's own chords from the
        // seam round to it again, the way this face walks the curve - counter
        // clockwise about the axis where the wall runs that way. Asked of the
        // arcs only until 2026-09-30, and a bore's rim in the hole of a fanned
        // facet was handed to a hole nothing wrote (open item 39, R26).
        if (!boundWritten(rim.loop)) {
          const std::vector<int>& ring = loops[rim.loop];
          const std::size_t m = ring.size();
          const auto at = std::find(ring.begin(), ring.end(), seam);
          if (m >= 3 && at != ring.end()) {
            double turn = 0;  // the ring's own sense about the axis
            for (std::size_t k = 0; k < m; k++) {
              turn +=
                (vertices[ring[k]] - centre).cross(vertices[ring[(k + 1) % m]] - centre).dot(band.axis);
            }
            const bool forward = (turn > 0) == rim.wall_ccw;
            const std::size_t s = std::size_t(at - ring.begin());
            for (std::size_t k = 0; k < m; k++) {
              const int a = ring[forward ? (s + k) % m : (s + m - k) % m];
              const int b = ring[forward ? (s + k + 1) % m : (s + 2 * m - k - 1) % m];
              bool dir = true;
              const std::size_t chord = lineEdge(a, b, dir);
              rim_chords[side].emplace_back(chord, dir);
            }
            rim_unshared++;
            continue;
          }
        }
        rim_edge[side] = rim_circle();
        rim_of_loop.emplace(rim.loop, std::make_pair(rim_edge[side], !rim.wall_ccw));
      } else {
        // An arc, from one end of the rim to the other. A CIRCLE is counter
        // clockwise about its own axis, so the arc is always written that way
        // and the face's own direction is carried by rim_sense.
        const int from = rim.ccw_start, to = rim.ccw_end;
        // Built where a face uses it, as the full circle above.
        auto rim_arc = [&] {
          return add_edge(
            brepVertex(from), brepVertex(to),
            circle(placement(centre, band.axis, (vertices[from] - centre).normalized()), radius));
        };
        rim_sense[side] = rim.wall_ccw;
        if (rim.kind == AnalyticFeatures::RimRef::OTHER_BAND_ARC) {
          // shared with another partial band: one arc, two curved faces, no
          // planar loop anywhere along it
          rim_edge[side] = rim_arc();
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
          // worse boundary on this one face, and a closed shell. A hole is
          // written only where its face is (open item 39, R26).
          if (!boundWritten(rim.loop)) {
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
              const std::size_t chord = lineEdge(walk[k], walk[k + 1], dir);
              rim_chords[side].emplace_back(chord, dir);
            }
            if (!rim_chords[side].empty()) {
              rim_unshared++;
              continue;
            }
          }
          rim_edge[side] = rim_arc();
          arc_subs[rim.loop].push_back({rim.start, rim.count, rim_edge[side], !rim.wall_ccw});
        }
      }
    }

    std::vector<brep::Coedge> loop;
    // A rim as the one curve it is, or as the chords it was given up as.
    auto walk_rim = [&](int side) {
      if (rim_chords[side].empty()) {
        loop.push_back(coedge(rim_edge[side], rim_sense[side]));
        return;
      }
      for (const auto& chord : rim_chords[side]) loop.push_back(coedge(chord.first, chord.second));
    };
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
      std::size_t edge_seam = 0;
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
        edge_seam = add_edge(brepVertex(band.seam_bottom), brepVertex(band.seam_top),
                             circle(placement(tube, normal.normalized(), from), tor->r_minor));
      } else if (band.zone != nullptr) {
        const auto *sph = dynamic_cast<const SphereSurface *>(band.zone.get());
        const Vector3d from = (vertices[band.seam_bottom] - sph->refpt).normalized();
        const Vector3d to = (vertices[band.seam_top] - sph->refpt).normalized();
        // this normal is the one that makes the sweep from `from` to `to`
        // counter clockwise and shorter than half a turn, which a zone's
        // latitude range always is - the flat cap at either end sees to that
        const Vector3d normal = from.cross(to);
        edge_seam = add_edge(brepVertex(band.seam_bottom), brepVertex(band.seam_top),
                             circle(placement(sph->refpt, normal.normalized(), from), sph->r));
      } else {
        edge_seam = brepLine(brepVertex(band.seam_bottom), brepVertex(band.seam_top));
      }
      walk_rim(0);
      loop.push_back(coedge(edge_seam, true));
      walk_rim(1);
      loop.push_back(coedge(edge_seam, false));
    } else {
      // walk along the bottom rim, up the end edge, back along the top rim,
      // down the other end edge - so the ends are where one rim's traversal
      // finishes and the other's begins
      const int b_from = rims[i].first.traversalStart();
      const int b_to = rims[i].first.traversalEnd();
      const int t_from = rims[i].second.traversalStart();
      const int t_to = rims[i].second.traversalEnd();

      bool up_dir = true, down_dir = true;
      const std::size_t edge_up = lineEdge(b_to, t_from, up_dir);
      const std::size_t edge_down = lineEdge(t_to, b_from, down_dir);
      walk_rim(0);
      loop.push_back(coedge(edge_up, up_dir));
      walk_rim(1);
      loop.push_back(coedge(edge_down, down_dir));
    }

    // The surface normal of a cylinder or a cone points away from its axis, so
    // a bore - where the material is outside it - is the opposite sense.
    build(std::move(surface), std::move(loop), band.outward, record, covered, carrier);
  }
}
