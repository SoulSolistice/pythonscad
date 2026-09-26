/*! The trimmed quadrics, written - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Exact.h"
#include "io/step/BoundaryCycles.h"
#include "io/step/CrossingArc.h"
#include "io/step/PlaneSections.h"

using namespace step;

void StepKernel::BodyBuild::emitQuadricFaces()
{
  for (const auto& patch : quadric_faces) {
    const auto *axial = dynamic_cast<const AxialSurface *>(patch.surface.get());
    if (axial == nullptr) continue;
    const auto *cyl = dynamic_cast<const CylinderSurface *>(patch.surface.get());
    const auto *cone = dynamic_cast<const ConeSurface *>(patch.surface.get());
    const auto *elliptic = dynamic_cast<const EllipticCylinderSurface *>(patch.surface.get());
    const Vector3d axis = axial->axis();
    const Vector3d base = axial->refpt;

    // The endpoint invariant binds *here* and not where the sections were
    // decided. The decision runs before the corner placement, on vertices the
    // mesh still holds a sagitta inside their surface; by the time a face is
    // written its corners have been put on the surfaces the file asserts, and it
    // is the written curve the invariant is about. Binding it in the decision
    // would have dropped every section on lid10 for a staleness that the
    // placement then removed.
    const std::map<std::size_t, BoundaryCycle> boundary =
      boundaryCycles(patch, vertices, loops, loop_valid, taken, section_planes[patch.surface.get()],
                     crossing_keys, true, &section_stats);
    if (boundary.empty()) continue;

    // The reference direction is a radius through the first boundary vertex that
    // has one, so the face starts where its own boundary does rather than at
    // some seam of the surface's own. "That has one" is not defensiveness: a
    // cone's face can be bounded through its apex, which is on the axis and has
    // no radius at all, and taking that one wrote a zero DIRECTION into the
    // file - a placement no reader can use, on a face that was correct in every
    // other respect.
    Vector3d ref = Vector3d::Zero();
    for (const auto& entry : boundary) {
      for (const int v : entry.second.verts) {
        const Vector3d rel = vertices[v] - base;
        const Vector3d radial = rel - axis * axis.dot(rel);
        if (radial.norm() > 1e-9) {
          ref = radial.normalized();
          break;
        }
      }
      if (ref.norm() > 0.5) break;
    }
    if (ref.norm() < 0.5) ref = AnalyticFeatures::perpendicular(axis);
    auto point = new Point(entities, base);
    auto dir_ref = new Direction(entities, ref);
    SurfaceType *surface = nullptr;
    if (elliptic != nullptr) {
      // The section across the sweep, swept: its ELLIPSE in its own principal
      // frame, and the sweep as a unit vector.
      Vector3d e1, e2;
      elliptic->frame(e1, e2);
      auto ellipse =
        new Ellipse(entities, "",
                    new Axis2Placement(entities, new Direction(entities, axis),
                                       new Direction(entities, e1), new Point(entities, base)),
                    elliptic->a(), elliptic->b());
      surface = new SurfaceOfLinearExtrusion(entities, "", ellipse,
                                             new Vector(entities, new Direction(entities, axis), 1.0));
    } else if (cyl != nullptr) {
      auto dir_axis = new Direction(entities, axis);
      surface = new CylindricalSurface(entities, "",
                                       new Axis2Placement(entities, dir_axis, dir_ref, point), cyl->r);
    } else {
      // ISO 10303 wants a half angle in (0, pi/2) and a radius growing along the
      // placement's axis, so a cone narrowing that way is written from its other
      // end - the same convention the band pass follows.
      auto dir_axis = new Direction(entities, cone->slope > 0 ? axis : Vector3d(-axis));
      surface = new ConicalSurface(entities, "", new Axis2Placement(entities, dir_axis, dir_ref, point),
                                   cone->r, atan(fabs(cone->slope)));
    }

    // Which cycle is the outer bound is a question about the surface's own
    // parameters: a hole in a face is a hole in its (theta, z) rectangle.
    std::size_t outer = 0;
    double widest = -1.0;
    const Vector3d ref2 = axis.cross(ref);
    for (const auto& entry : boundary) {
      std::vector<std::pair<double, double>> uv;
      for (const int v : entry.second.verts) {
        const Vector3d rel = vertices[v] - base;
        double t = atan2(rel.dot(ref2), rel.dot(ref));
        if (t < 0) t += 2 * M_PI;
        uv.emplace_back(t * axial->size(), axis.dot(rel));
      }
      double twice = 0.0;
      for (std::size_t i = 0; i < uv.size(); i++) {
        const auto& a = uv[i];
        const auto& b = uv[(i + 1) % uv.size()];
        twice += a.first * b.second - b.first * a.second;
      }
      if (fabs(twice) / 2 > widest) {
        widest = fabs(twice) / 2;
        outer = entry.first;
      }
    }

    std::vector<FaceBound *> bounds;
    std::vector<EdgeCurve *> face_edges_here;
    std::vector<std::pair<const BoundaryCycle *, std::vector<OrientedEdge *>>> seam_rims;
    for (const auto& entry : boundary) {
      const std::vector<int>& cycle = entry.second.verts;
      const std::size_t n = cycle.size();
      if (n < 3) continue;
      // Where the boundary is a plane section, write the ellipse it is rather
      // than the chords the mesh carries. Everything else stays a straight edge:
      // this replaces a stretch of the boundary, never the whole of it.
      std::vector<int> starts_at(n, -1);
      for (std::size_t k = 0; k < entry.second.sections.size(); k++) {
        const BoundarySection& bs = entry.second.sections[k];
        // A section the roll could not free stays a run of chords, which is what
        // the boundary test then refuses the surface for; so does one the face
        // on the other side is not writing - see `section_curves`.
        std::set<int> key;
        for (std::size_t c = 0; c < bs.count; c++) key.insert(cycle[(bs.start + c) % n]);
        // A whole rim, from the seam round to it again, is one closed curve.
        const bool whole = bs.start == 0 && bs.count == n + 1;
        if (bs.start + bs.count > n && !whole) continue;
        if (section_curves.find(key) == section_curves.end()) continue;
        starts_at[bs.start] = int(k);
      }
      std::vector<OrientedEdge *> loop;
      // From where no crossing piece straddles the start, since each is one
      // edge. A section never contains a crossing edge, so this start is never
      // inside a section either.
      const std::size_t start = walkStart(cycle);
      bool closed_piece = false;
      for (std::size_t step = 0; step < n;) {
        const std::size_t i = (start + step) % n;
        if (starts_at[i] >= 0) {
          const BoundarySection& bs = entry.second.sections[starts_at[i]];
          const int a = cycle[bs.start], b = cycle[(bs.start + bs.count - 1) % n];
          std::set<int> key;
          for (std::size_t k = 0; k < bs.count; k++) key.insert(cycle[(bs.start + k) % n]);
          // Where the section is shared with a planar face, that face has to be
          // able to take this very edge, and it is handed it by finding this run
          // of vertices in its loop. That search can fail: a loop fanned into
          // triangles to follow a moved corner no longer carries the run.
          //
          // It used to fail silently, and this wrote the conic anyway. The other
          // face then wrote the chords it still had, and one boundary came out
          // as an ELLIPSE from the cylinder and a LINE from the plane - two
          // edges where the geometry has one, each used once, and the shell
          // open. That is open item 11's root cause A, seen on the band family
          // at $fn 64 as `2 edge(s) used by only one face`.
          //
          // So the search comes first and it is binding: if the far face cannot
          // take the edge, this does not write it either, and both sides keep
          // the chords they agree on. Deciding once means neither side may
          // decide alone.
          std::size_t at_j = 0;
          if (bs.loop >= 0) {
            // A loop that will be fanned into triangles cannot take this edge at
            // all, whatever its corners still spell. The fan writes one straight
            // line per triangle side - it never reads `arc_subs` - and a section
            // spanning a run of several corners could not be one triangle's side
            // even if it did. So the handover is impossible, not merely missed,
            // and writing the conic here would leave the conic on this face and
            // chords on that one.
            //
            // That is open item 11's root cause A: on the band family at $fn 64
            // with the corner placement relaxed, one boundary came out as an
            // ELLIPSE from the cylinder and a LINE from the plane beside it, two
            // edges where the geometry has one. `split_for_corners` is settled
            // long before any face is written, so this is knowable here.
            if (split_for_corners.count(std::size_t(bs.loop)) != 0) {
              section_unshared++;
              starts_at[i] = -1;  // chords on both sides, which is what agrees
              continue;
            }
            const std::vector<int>& other = loops[bs.loop];
            const std::size_t m = other.size();
            const std::size_t span = bs.count - 1;
            bool shared = false;
            for (std::size_t j = 0; j < m && !shared; j++) {
              bool fwd = true, rev = true;
              for (std::size_t c = 0; c <= span; c++) {
                fwd = fwd && other[(j + c) % m] == cycle[(bs.start + c) % n];
                rev = rev && other[(j + c) % m] == cycle[(bs.start + span - c) % n];
              }
              if (!fwd && !rev) continue;
              shared = true;
              at_j = j;
            }
            if (!shared) {
              section_unshared++;
              starts_at[i] = -1;  // chords on both sides, which is what agrees
              continue;
            }
          }
          EdgeCurve *edge = nullptr;
          bool dir = true;
          const auto found = section_edges.find(key);
          if (found != section_edges.end()) {
            edge = found->second;
            dir = edge->vert1 == get_vertex(a);
          } else {
            // Counted, not refused, for the reason the promotion pass gives:
            // the conic is the wrong curve but lies on the quadric, and the
            // chord it would fall back to does not. See open item 26.
            if (planeIsFacetOfCurved(surfaces, bs.normal, {vertices[a], vertices[b]}) != nullptr) {
              sections_on_facet++;
            }
            SectionEllipse sec;
            if (!planeSectionEllipse(patch.surface.get(), bs.normal, bs.on_plane, sec)) {
              starts_at[i] = -1;  // fall back to the chords, which is what was there
              continue;
            }
            auto ell =
              new Ellipse(entities, "", conic_placement(sec.centre, bs.normal, sec.major), sec.a, sec.b);
            // An ELLIPSE runs counter clockwise about its own axis, so the edge
            // says whether it agrees - decided by whether the section's middle
            // lies on the counter clockwise side of its two ends.
            const Vector3d minor = bs.normal.cross(sec.major);
            auto param = [&](const Vector3d& q) {
              const Vector3d rel = q - sec.centre;
              return atan2(rel.dot(minor) / sec.b, rel.dot(sec.major) / sec.a);
            };
            auto ccw = [](double from, double to) {
              double d = to - from;
              while (d < 0) d += 2 * M_PI;
              while (d >= 2 * M_PI) d -= 2 * M_PI;
              return d;
            };
            const double ta = param(vertices[a]), tb = param(vertices[b]);
            if (a == b) {
              // A whole rim: the way the rim is walked from its first vertex.
              dir = ccw(ta, param(vertices[cycle[(bs.start + 1) % n]])) < M_PI;
            } else if (bs.count > 2) {
              // A vertex of the section's own interior settles it exactly.
              const double tm = param(vertices[cycle[(bs.start + bs.count / 2) % n]]);
              dir = ccw(ta, tm) < ccw(ta, tb);
            } else {
              // A section of one edge has no interior vertex to ask, and asking
              // for one returns its far end - which made the test read "equal",
              // took the same branch every time, and sent half the arcs the long
              // way round. What decides it instead is that the edge is one
              // segment of a tessellation: it spans at most a third of the way
              // round even at the coarsest $fn, so the arc that replaces it is
              // the shorter one.
              dir = ccw(ta, tb) <= M_PI;
            }
            edge = new EdgeCurve(entities, get_vertex(dir ? a : b), get_vertex(dir ? b : a), ell, true);
            section_edges.emplace(key, edge);
            if (bs.declared) sections_declared++;
            else sections_fitted++;
          }
          loop.push_back(new OrientedEdge(entities, edge, dir));
          face_edges_here.push_back(edge);
          // And the planar face across it gives up the segments the curve
          // replaces, the same way a band's rim does. The region's boundary is
          // walked the way its own facets go, so the far face necessarily goes
          // the other way; two faces traversing a shared edge the same way is
          // precisely what leaves a shell open.
          if (bs.loop >= 0 && section_subs.insert(key).second) {
            // `at_j` is where the run sits in that face's loop, found above -
            // the same search, done once, and binding rather than advisory.
            const std::size_t span = bs.count - 1;
            if (span == loops[bs.loop].size()) rim_of_loop[bs.loop] = {edge, !dir};
            else arc_subs[bs.loop].push_back({at_j, span, edge, !dir});
          }
          step += bs.count - 1;
          continue;
        }
        const int a = cycle[i], b = cycle[(i + 1) % n];
        std::size_t spans = 1;                            // mesh edges these edges of the file cover
        std::vector<std::pair<EdgeCurve *, bool>> chain;  // the edges, and which way each is used
        // The piece of a crossing this edge belongs to, decided once for both
        // faces - see decideCrossingRuns.
        const CrossingPiece *piece = pieceOf(a, b);
        if (piece != nullptr && !piece->edges.empty()) {
          bool ok = false;
          const bool forward = walksForward(*piece, cycle, i, ok);
          if (ok) {
            for (std::size_t k = 0; k < piece->edges.size(); k++) {
              chain.emplace_back(piece->edges[forward ? k : piece->edges.size() - 1 - k], forward);
            }
            spans = piece->verts.size() - 1;
            closed_piece = closed_piece || piece->verts.front() == piece->verts.back();
          } else {
            crossing_unwalked++;
          }
          if (!chain.empty() && (cyl != nullptr || cone != nullptr)) {
            // What this face's parameters mean, kept until the other face has
            // written its own and both pcurves can be built. Only a cylinder or a
            // cone: the pcurve is written from their parameters.
            CrossingSide side;
            side.surface = surface;
            side.base = base;
            side.axis = cone != nullptr && cone->slope < 0 ? Vector3d(-axis) : axis;
            side.ref = ref;
            side.slope = cone != nullptr ? fabs(cone->slope) : 0.0;
            crossing_sides[std::size_t(piece - crossing_pieces.data())].push_back(side);
          }
        }
        if (chain.empty()) {
          bool dir = true;
          chain.emplace_back(
            get_line_from_map(edge_map, a, b, get_vertex(a), get_vertex(b), dir, merged_edge_cnt), dir);
        }
        for (const auto& link : chain) {
          loop.push_back(new OrientedEdge(entities, link.first, link.second));
          face_edges_here.push_back(link.first);
        }
        step += spans;
      }
      // A rim that is one whole section, or one whole crossing, is one closed
      // edge, which is a whole loop on its own.
      const bool one_crossing = loop.size() == 1 && closed_piece;
      if (loop.empty() || (loop.size() < 2 && !entry.second.seam && !one_crossing)) continue;
      // A cone face bounded by one loop that goes once round the axis has the
      // apex inside it: the region is a disc, pinched to a point where the
      // surface is. The surface is periodic round the axis, so a face covering
      // the whole turn needs a seam, as a band's does - the loop continues from
      // where it started up a generator to the apex and back down the same
      // edge. Without one the face is not bounded in its own parameters, and a
      // reader has to invent the seam; OCCT did, which is not the same as the
      // file being right.
      if (cone != nullptr && boundary.size() == 1 && cone->slope != 0) {
        const Vector3d apex = base - axis * (cone->r / cone->slope);
        // A loop through the apex bounds the face there itself, and the apex is
        // then a vertex the faces beside it share - a Voronoi roof's reflex
        // corner. Only a loop that stays off it can wind round it.
        bool through_apex = false;
        double turn = 0.0;
        for (std::size_t k = 0; k < n && !through_apex; k++) {
          const Vector3d off = vertices[cycle[k]] - apex;
          through_apex = (off - axis * axis.dot(off)).norm() <=
                         recognition::forwardError(12, off.norm() + apex.norm());
          auto angle = [&](int v) {
            const Vector3d rel = vertices[v] - base;
            return atan2(rel.dot(ref2), rel.dot(ref));
          };
          double d = angle(cycle[(k + 1) % n]) - angle(cycle[k]);
          if (d > M_PI) d -= 2 * M_PI;
          if (d < -M_PI) d += 2 * M_PI;
          turn += d;
        }
        // A winding number, so it is an integer: round the axis once, or not.
        if (!through_apex && fabs(turn) > M_PI) {
          // Where the loop starts and ends, which is where it was walked from.
          Vertex *start = get_vertex(cycle[walkStart(cycle)]);
          auto tip = new Vertex(entities, new Point(entities, apex));
          EdgeCurve *seam = create_line_edge_curve(start, tip, true);
          loop.push_back(new OrientedEdge(entities, seam, true));
          loop.push_back(new OrientedEdge(entities, seam, false));
          face_edges_here.push_back(seam);
          apex_seams++;
        }
      }
      if (entry.second.seam) {
        seam_rims.emplace_back(&entry.second, std::move(loop));
        continue;
      }
      auto edge_loop = new EdgeLoop(entities, loop);
      bounds.push_back(new FaceBound(entities, edge_loop, true, entry.first == outer));
    }
    if (seam_rims.size() == 2) {
      // One rim, up the seam, the other rim, and back down: each rim starts
      // where the seam meets it (sealWithSeam), so the four close.
      const int from = seam_rims[0].first->verts[0], to = seam_rims[1].first->verts[0];
      EdgeCurve *seam = create_line_edge_curve(get_vertex(from), get_vertex(to), true);
      std::vector<OrientedEdge *> joined = seam_rims[0].second;
      joined.push_back(new OrientedEdge(entities, seam, true));
      joined.insert(joined.end(), seam_rims[1].second.begin(), seam_rims[1].second.end());
      joined.push_back(new OrientedEdge(entities, seam, false));
      face_edges_here.push_back(seam);
      bounds.push_back(new FaceBound(entities, new EdgeLoop(entities, joined), true, true));
      band_seams++;
    }
    if (bounds.empty()) continue;

    // Which way the face points. A cylinder's own normal is radially outward,
    // so the mesh decides by whether one of its facets agrees.
    bool outward = true;
    if (!patch.facets.empty()) {
      const std::vector<int>& facet = loops[patch.facets.front()];
      Vector3d centroid = Vector3d::Zero();
      for (const int v : facet) centroid += vertices[v];
      centroid /= double(facet.size());
      Vector3d away;
      if (elliptic != nullptr) {
        // the surface's own normal, which for an ellipse is not the radius
        outward =
          !elliptic->normalAt(centroid, away) || away.dot(loop_normals[patch.facets.front()]) > 0;
      } else {
        const Vector3d rel = centroid - base;
        const Vector3d radial = (rel - axis * axis.dot(rel)).normalized();
        outward = radial.dot(loop_normals[patch.facets.front()]) > 0;
      }
    }

    sfaces_extra.push_back(new Face(entities, bounds, surface, outward));
    face_edges_extra.push_back(face_edges_here);
  }
}

StepKernel::Axis2Placement *StepKernel::BodyBuild::conic_placement(const Vector3d& origin,
                                                                   const Vector3d& dir,
                                                                   const Vector3d& towards)
{
  return new Axis2Placement(entities, new Direction(entities, dir), new Direction(entities, towards),
                            new Point(entities, origin));
}
