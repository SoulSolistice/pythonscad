/*! One solid's worth of STEP - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <limits>
#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Exact.h"
#include "geometry/recognition/Seam.h"
#include "geometry/recognition/Pipeline.h"
#include "geometry/recognition/Reports.h"
#include "io/step/BoundaryCycles.h"
#include "io/step/CornerLadder.h"
#include "io/step/CrossingArc.h"
#include "io/step/CurveChecks.h"
#include "io/step/LoopGeometry.h"
#include "io/step/PlaneSections.h"
#include "utils/printutils.h"
#include <algorithm>  // std::reverse
#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include <map>
#include <utility>
#include <array>
#include <set>
#include <functional>
#include <iomanip>  // put_time

using namespace step;

namespace {

// The implicit-surface machinery these passes solve with - `surfaceImplicit`,
// `projectOntoBoth`, `isQuadric`, `declaredBand` - lives in AnalyticFeatures,
// beside `closestOnSurface`, because it is geometry rather than STEP and
// `export_step.cc` needs the same Newton for its corner placement.
using AnalyticFeatures::declaredBand;
using AnalyticFeatures::isQuadric;
using AnalyticFeatures::projectOntoBoth;
using AnalyticFeatures::surfaceImplicit;

using AnalyticFeatures::projectOntoSurfaceAndPlanes;

}  // namespace

void StepKernel::BodyBuild::run()
{
  reportDeclaredSurfaces();

  buildLoops();

  recognise();

  placeCorners();

  emitBands();

  emitBezierFaces();

  // ---- declared sweeps ---------------------------------------------------
  //
  // One face per claimed region, on the B-spline the declaration describes,
  // bounded by the mesh's own straight edges.
  //
  // That last part is the whole design and it is not a compromise. The obvious
  // move is to fit a curve along each boundary run, the way a fillet's rails
  // become arcs - but a fillet's rail lies in the flat face beside it, and a
  // trimmed sweep's boundary does not. Its neighbours here are the planar
  // facets the boolean left, so a curve on the sweep lies in none of them, and
  // a shared edge between the two can only be the chord it already is. Taking
  // the edges from the same map every other face uses is therefore both the
  // simplest thing and the only one that keeps the shell closed: the neighbour
  // is not asked to give anything up.
  //
  // One neighbour is not a planar facet, and it is the exception that sentence
  // does not cover: where the sweep meets a *declared* surface, the two cross
  // along a curve both of them can derive from their own declarations, and
  // `crossing_edges` holds exactly those edges. Such an edge has to be taken
  // from `crossing_curves` here for the same reason it is taken there - it is
  // one edge, and the two faces on it must name one geometry. Whichever emitter
  // reaches it first builds it and the other reuses the object, which is why
  // that map is declared above both of them rather than beside either.
  std::map<std::pair<int, int>, EdgeCurve *> crossing_curves;
  /*! One face's view of a crossing edge: the surface entity it wrote and the
   *  placement that entity carries, which is what a PCURVE's parameters mean.
   *
   * Recorded per face rather than per surface because the reference direction is
   * chosen from each face's own boundary, so two faces on one cylinder do not
   * share a parameter origin and a pcurve written against the wrong one is off
   * by a rotation.
   */
  struct CrossingSide {
    SurfaceType *surface = nullptr;
    Vector3d base, axis, ref;
    double slope = 0;  // a cone's, in its written direction; zero for a cylinder
  };
  std::map<std::pair<int, int>, std::vector<CrossingSide>> crossing_sides;
  int crossing_written = 0;

  // One per file - nothing about a 2D parametric context varies.
  ParametricContext *param_context = nullptr;
  // Settle the sections and the crossing curves before *either* emitter runs.
  // This used to sit after the grid faces were written, which meant the grid
  // emitter read a set that had not been decided yet and the quadric emitter
  // read one that had: the same edge then got a crossing curve from one face
  // and a chord from the other, and the shell came apart on twice the number
  // of crossings - 1278 edges used once, on the band family at $fn 32. Its own
  // comment says it has to run after the corner placement, and it still does;
  // the placement is a long way above here.
  // A trimmed quadric, bounded by the mesh's own boundary rather than by
  // circles. The band pass writes a cylinder between two circular rims and
  // writes it better - a CIRCLE is a curve a kernel can offset and pattern
  // along - so this only ever sees what that pass could not take: a face whose
  // trim is not a plane section and has no conic to bound it with.
  //
  // The bound is the polyline the neighbouring faceted faces already use, for
  // the same reason a declared sweep's is: those faces have to close against
  // this one edge for edge, and a curve of our own devising there would open
  // the shell. The surface is exact; only its boundary is the mesh's.
  // One curve per plane section, shared by the two faces that meet along it -
  // the same rule the band pass follows for a shared rim. Without it each face
  // would write its own ellipse and the shell would come apart along them.
  // Ask again, now that the corner placement has moved what it is going to move.
  // The faces are settled; what is being re-decided is only which sections both
  // of their faces can still see, and it has to be decided on the geometry that
  // is about to be written rather than on the geometry it was chosen from.
  if (analytic) {
    const std::set<std::set<int>> agreed_before = section_curves;
    std::vector<const AnalyticFeatures::Patch *> live;
    live.reserve(quadric_faces.size());
    for (const auto& patch : quadric_faces) live.push_back(&patch);
    decideSections(live);
    std::size_t lost = 0;
    for (const auto& key : agreed_before) {
      if (section_curves.find(key) == section_curves.end()) lost++;
    }
    if (lost > 0) {
      // Say so rather than let it pass: this is the one place the two meshes can
      // disagree, and in the exact tier it means a face was called exact partly
      // because a section covered chords which are now written as chords after
      // all. A plane fitted to mesh facets is what does not survive: the
      // placement moves a corner onto the surface the model declared, which is
      // exactly off the facet plane it happened to share with a neighbour. A
      // declared plane survives, because that is what the corner was moved onto.
      // An error in the **exact** tier and a warning in the approximation tier,
      // which is the difference between the two meanings this condition has.
      //
      // Under the approximation flag, losing a section's agreement now costs
      // exactness and nothing else: both faces keep the chords they agree on,
      // because a section the far face cannot be handed is no longer written by
      // the near one either. That is a worse boundary and a correct file.
      //
      // In the exact tier it is a correctness fault, because a face was called
      // exact partly on the strength of a section covering chords which are then
      // written as chords after all - the face asserts something its boundary no
      // longer supports.
      //
      // It was made an error outright earlier on 2026-09-10, when the far face
      // could still be left holding chords while the near one wrote a conic; the
      // fan fix below removed that, and with it the reason for the stronger
      // reading in the approximation tier. Measured after: f04 under a relaxed
      // corner placement reaches this and its file is valid.
      //
      // Unreachable on every coupon measured either way:
      // the band family at $fn 24, 32, 48, 64 and 96 and both reference parts
      // all report zero. It became reachable exactly once, when the corner
      // placement was relaxed on 2026-09-10, and what it produced was a file
      // whose shell was open - one boundary written as an ELLIPSE by a cylinder
      // and as a LINE by the plane beside it, two edges where there is one.
      //
      // So reaching this is not a degradation to be reported and lived with. It
      // is two faces contradicting each other about one edge, and the file is
      // wrong. `tests/step-flagship-check.py` fails on any EXPORT-ERROR, which
      // is what makes that enforceable rather than merely stated. See open item
      // 11: the fix is to decide an edge's geometry once, keyed by the edge, the
      // way `crossing_curves` already does.
      LOG(approximate ? message_group::Export_Warning : message_group::Export_Error,
          "STEP export: %1$d plane section%2$s agreed before the corners were placed and %3$s not "
          "after, so %4$s boundary is written as chords%5$s",
          int(lost), lost == 1 ? "" : "s", lost == 1 ? "does" : "do",
          lost == 1 ? "that much of a" : "that much of the",
          approximate ? "" : " under a face called exact");
    }
  }

  std::size_t reseamed = 0, reseam_full = 0;
  for (const auto& patch : grid_faces) {
    const auto *grid = dynamic_cast<const GridSurface *>(patch.surface.get());
    if (grid == nullptr) continue;

    // Where to put the seam of a closed profile, which is free to choose.
    //
    // A face here is written over the rectangle 0 <= v <= 1, so its boundary
    // must not cross the seam. One that does has nowhere to go: the pcurve is a
    // straight line in (u, v), and from v = 0.0000 to v = 0.9956 it sweeps the
    // whole profile instead of stepping the 0.0044 across. A reader then repairs
    // a face whose boundary contradicts its own surface - OpenCASCADE builds a
    // seam edge nobody wrote and widens the vertices there until they merge,
    // 1.08 on lid10, and the LINE beside it comes back bounded by a point 0.7159
    // off its own curve. Open item 15.
    //
    // A boolean that buries one span of the profile leaves the seam somewhere to
    // go: written from the column after an unused span, the face's own facets
    // are contiguous in v and no boundary crosses anything. That costs no face,
    // no refused facet and no cut - the alternatives this replaces - and it is
    // exact, the surface being the same one described from another column.
    //
    // Which spans a face uses is asked of the facets' corners rather than their
    // middles: beside the seam the profile is creased, and a facet's centroid
    // projects onto the flank across the crease - 0.022 on lid10 for one whose
    // corners are at 0.98 and beyond.
    int seam_column = 0;
    const int spans = grid->closed_v ? grid->cols : grid->cols - 1;
    if (grid->closed_v && spans > 1) {
      std::vector<char> used(spans, 0);
      for (const std::size_t f : patch.facets) {
        if (f >= loops.size()) continue;
        double lo = 2, hi = -1;
        for (const int v : loops[f]) {
          double pu = 0, pv = 0;
          if (!grid->project(vertices[v], pu, pv)) continue;
          if (grid->onSeam(pu, pv)) continue;
          lo = std::min(lo, pv);
          hi = std::max(hi, pv);
        }
        if (hi < lo) continue;
        const int span = std::clamp(int(((lo + hi) / 2) * spans), 0, spans - 1);
        used[span] = 1;
      }
      for (int k = 0; k < spans; k++) {
        if (used[k] == 0) {
          seam_column = (k + 1) % grid->cols;
          break;
        }
      }
      if (seam_column != 0) reseamed++;
      else if (std::find(used.begin(), used.end(), 0) == used.end()) reseam_full++;
    }
    const double seam_shift = grid->seamShift(seam_column);
    // Every parameter this face reports is in the surface as it was *written*,
    // which is the same surface described from another column.
    auto parameters = [&](const Vector3d& p, double& pu, double& pv) {
      if (!grid->project(p, pu, pv)) return false;
      if (seam_shift > 0) {
        pv -= seam_shift;
        if (pv < 0) pv += 1.0;
        if (pv > 1) pv -= 1.0;
      }
      return true;
    };

    int du = 0, dv = 0, nu = 0, nv = 0;
    std::vector<Vector3d> ctrl;
    std::vector<double> knots_u, knots_v;
    std::vector<int> mults_u, mults_v;
    if (!grid->splineForm(du, dv, nu, nv, ctrl, knots_u, mults_u, knots_v, mults_v, seam_column)) {
      continue;
    }

    std::vector<std::vector<Point *>> net;
    for (int i = 0; i < nu; i++) {
      std::vector<Point *> row;
      for (int j = 0; j < nv; j++) {
        row.push_back(new Point(entities, ctrl[std::size_t(i) * nv + j]));
      }
      net.push_back(row);
    }
    auto surface = new BSplineSurface(entities, "", du, dv, net);
    surface->setKnots(knots_u, mults_u, knots_v, mults_v);

    // The boundary, one cycle at a time. Patch::Run carries them in order with
    // consecutive runs sharing an endpoint, so a cycle is its runs' vertices
    // with the joins written once.
    std::map<std::size_t, std::vector<int>> cycles;
    for (const auto& run : patch.runs) {
      std::vector<int>& cycle = cycles[run.bound];
      for (std::size_t i = 0; i + 1 < run.verts.size(); i++) cycle.push_back(run.verts[i]);
    }

    std::vector<FaceBound *> bounds;
    std::vector<EdgeCurve *> face_edges_here;
    // Which cycle is the outer bound is a question about the surface's own
    // parameters, not about the model: a hole in a face is a hole in its
    // parameter rectangle. The largest area there is the one that contains the
    // others.
    std::size_t outer = 0;
    double widest = -1.0;
    std::map<std::size_t, double> area;
    for (const auto& entry : cycles) {
      std::vector<std::pair<double, double>> uv;
      for (const int v : entry.second) {
        double pu = 0, pv = 0;
        if (!parameters(vertices[v], pu, pv)) continue;
        uv.emplace_back(pu, pv);
      }
      double twice = 0.0;
      for (std::size_t i = 0; i < uv.size(); i++) {
        const auto& a = uv[i];
        const auto& b = uv[(i + 1) % uv.size()];
        twice += a.first * b.second - b.first * a.second;
      }
      area[entry.first] = fabs(twice) / 2;
      if (area[entry.first] > widest) {
        widest = area[entry.first];
        outer = entry.first;
      }
    }

    for (const auto& entry : cycles) {
      const std::vector<int>& cycle = entry.second;
      if (cycle.size() < 3) continue;
      std::vector<OrientedEdge *> loop;
      for (std::size_t i = 0; i < cycle.size(); i++) {
        const int a = cycle[i], b = cycle[(i + 1) % cycle.size()];
        bool dir = true;
        EdgeCurve *edge = nullptr;
        // The one edge a sweep shares with something that can give it a curve.
        // Same map, same key and the same EdgeCurve object as the quadric
        // emitter uses, so the two faces on this edge name one geometry however
        // the passes are ordered.
        const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
        const auto crossing = crossing_edges.find(key);
        if (crossing != crossing_edges.end()) {
          const auto found = crossing_curves.find(key);
          if (found != crossing_curves.end()) {
            edge = found->second;
            dir = edge->vert1 == get_vertex(a);
          } else {
            std::vector<Vector3d> ctrl;
            if (intersectionArc(crossing->second.first, crossing->second.second, vertices[key.first],
                                vertices[key.second], 1e-7, ctrl)) {
              std::vector<Point *> cp;
              cp.reserve(ctrl.size());
              for (const auto& c : ctrl) cp.push_back(new Point(entities, c));
              edge = new EdgeCurve(entities, get_vertex(key.first), get_vertex(key.second),
                                   new BSplineCurve(entities, "", cp), true);
              crossing_curves.emplace(key, edge);
              crossing_written++;
              dir = edge->vert1 == get_vertex(a);
            }
          }
        }
        if (edge == nullptr) {
          edge = get_line_from_map(edge_map, a, b, get_vertex(a), get_vertex(b), dir, merged_edge_cnt);
        }
        loop.push_back(new OrientedEdge(entities, edge, dir));
        face_edges_here.push_back(edge);
        // Where this edge runs across the surface, written down rather than
        // left to be guessed. A swept surface cannot be projected onto
        // reliably - see the PCURVE classes in StepKernel.h - and this is the
        // one face shape in this exporter that is trimmed by a polyline on
        // such a surface.
        //
        // Taken from the edge's own two vertices, not from a and b: an edge
        // already in the map may run the other way, and a pcurve that
        // disagrees with the direction of the curve it belongs to is worse
        // than none.
        [&]() {
          double ua = 0, va = 0, ub = 0, vb = 0;
          if (!parameters(edge->vert1->point->pt, ua, va)) return;
          if (!parameters(edge->vert2->point->pt, ub, vb)) return;
          const double su = ub - ua, sv = vb - va;
          const double plen = sqrt(su * su + sv * sv);
          // A DIRECTION has to have a magnitude, and an edge whose ends land on
          // the same parameters says nothing about where it runs anyway.
          if (plen < 1e-12) return;
          if (param_context == nullptr) param_context = new ParametricContext(entities);
          auto rep = new DefinitionalRepresentation(
            entities,
            new Line2d(entities, new Point2d(entities, ua, va),
                       new Vector2d(entities, new Direction2d(entities, su / plen, sv / plen), plen)),
            param_context);
          auto pcurve = new PCurve(entities, surface, rep);
          // An edge between two sweep faces is on both of them, and STEP allows
          // a SURFACE_CURVE to say so.
          if (auto *already = dynamic_cast<SurfaceCurve *>(edge->round)) {
            if (already->on.size() < 2) already->on.push_back(pcurve);
          } else {
            edge->round = new SurfaceCurve(entities, edge->round, {pcurve});
          }
        }();
      }
      auto edge_loop = new EdgeLoop(entities, loop);
      bounds.push_back(new FaceBound(entities, edge_loop, true, entry.first == outer));
    }
    if (bounds.empty()) continue;

    // Which way the face points. du x dv has no reason to agree with the mesh,
    // so it is compared against a facet the region actually contains.
    bool outward = true;
    if (!patch.facets.empty()) {
      const std::vector<int>& facet = loops[patch.facets.front()];
      Vector3d centroid = Vector3d::Zero();
      for (const int v : facet) centroid += vertices[v];
      centroid /= double(facet.size());
      double pu = 0, pv = 0;
      grid->project(centroid, pu, pv);
      const double h = 1e-5;
      const Vector3d d_u =
        grid->evaluate(std::min(1.0, pu + h), pv) - grid->evaluate(std::max(0.0, pu - h), pv);
      const Vector3d d_v =
        grid->evaluate(pu, std::min(1.0, pv + h)) - grid->evaluate(pu, std::max(0.0, pv - h));
      outward = d_u.cross(d_v).dot(loop_normals[patch.facets.front()]) > 0;
    }

    sfaces_extra.push_back(new Face(entities, bounds, surface, outward));
    face_edges_extra.push_back(face_edges_here);
  }

  std::map<std::set<int>, EdgeCurve *> section_edges;
  // What binding the written sections' ends to the surface cost - see SectionStats.
  SectionStats section_stats;
  // Sections the far face could not be handed, and which are therefore
  // written as chords by both rather than as a conic by one. Reached only
  // where a planar loop no longer carries the run - see below.
  std::size_t section_unshared = 0;
  std::size_t apex_seams = 0;            // cone faces closed round their apex
  std::size_t band_seams = 0;            // faces round their axis closed by a seam between two rims
  std::set<std::set<int>> section_subs;  // sections already handed to a planar face
  int sections_declared = 0, sections_fitted = 0;
  // Boundary sections refused because their plane was a facet of something
  // curved rather than a plane. See open item 26.
  int sections_on_facet = 0;
  auto conic_placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
    return new Axis2Placement(entities, new Direction(entities, dir), new Direction(entities, towards),
                              new Point(entities, origin));
  };
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
      for (std::size_t i = 0; i < n;) {
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
          i = bs.start + bs.count - 1;
          continue;
        }
        const int a = cycle[i], b = cycle[(i + 1) % n];
        bool dir = true;
        EdgeCurve *edge = nullptr;
        const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
        const auto crossing = crossing_edges.find(key);
        if (crossing != crossing_edges.end()) {
          const auto found = crossing_curves.find(key);
          if (found != crossing_curves.end()) {
            edge = found->second;
            dir = edge->vert1 == get_vertex(a);
          } else {
            std::vector<Vector3d> ctrl;
            const Vector3d& p0 = vertices[key.first];
            const Vector3d& p1 = vertices[key.second];
            if (intersectionArc(crossing->second.first, crossing->second.second, p0, p1, 1e-7, ctrl)) {
              std::vector<Point *> cp;
              for (const auto& c : ctrl) cp.push_back(new Point(entities, c));
              auto curve = new BSplineCurve(entities, "", cp);
              edge = new EdgeCurve(entities, get_vertex(key.first), get_vertex(key.second), curve, true);
              crossing_curves.emplace(key, edge);
              crossing_written++;
              dir = edge->vert1 == get_vertex(a);
            }
          }
          if (edge != nullptr && (cyl != nullptr || cone != nullptr)) {
            // What this face's parameters mean, kept until the other face has
            // written its own and both pcurves can be built. Only a cylinder or a
            // cone: the pcurve is written from their parameters.
            CrossingSide side;
            side.surface = surface;
            side.base = base;
            side.axis = cone != nullptr && cone->slope < 0 ? Vector3d(-axis) : axis;
            side.ref = ref;
            side.slope = cone != nullptr ? fabs(cone->slope) : 0.0;
            crossing_sides[key].push_back(side);
          }
        }
        if (edge == nullptr) {
          edge = get_line_from_map(edge_map, a, b, get_vertex(a), get_vertex(b), dir, merged_edge_cnt);
        }
        loop.push_back(new OrientedEdge(entities, edge, dir));
        face_edges_here.push_back(edge);
        i++;
      }
      // A rim that is one whole section is one closed edge, which is a whole
      // loop on its own.
      if (loop.empty() || (loop.size() < 2 && !entry.second.seam)) continue;
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
          Vertex *start = get_vertex(cycle[0]);
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

  // Say where each crossing curve runs across the two surfaces it lies on, now
  // that both faces have been written and their placements are known.
  //
  // The 3D curve stays definitive - .CURVE_3D., as it is everywhere else here -
  // so a reader that ignores the pcurves sees the file it saw before. What they
  // add is that a curve on two surfaces is *said* to be on them: STEP calls that
  // a SURFACE_CURVE, and a kernel that would otherwise re-project a B-spline
  // onto a quadric and decide for itself is told the answer instead.
  int pcurved = 0;
  for (const auto& entry : crossing_curves) {
    const auto sides = crossing_sides.find(entry.first);
    if (sides == crossing_sides.end() || sides->second.size() != 2) continue;
    auto *bez = dynamic_cast<BSplineCurve *>(entry.second->round);
    if (bez == nullptr || bez->pts.size() < 2) continue;
    const int degree = int(bez->pts.size()) - 1;

    // The curve at its own collocation parameters, which are the points it was
    // fitted through, so the pcurve and the 3D curve agree there exactly.
    std::vector<Vector3d> on_curve(degree + 1);
    for (int i = 0; i <= degree; i++) {
      const double t = double(i) / degree;
      Vector3d p = Vector3d::Zero();
      double binom = 1;
      for (int j = 0; j <= degree; j++) {
        p += bez->pts[j]->pt * (binom * pow(t, j) * pow(1 - t, degree - j));
        binom = binom * (degree - j) / (j + 1);
      }
      on_curve[i] = p;
    }

    Eigen::MatrixXd m(degree + 1, degree + 1);
    for (int i = 0; i <= degree; i++) {
      const double t = double(i) / degree;
      double binom = 1;
      for (int j = 0; j <= degree; j++) {
        m(i, j) = binom * pow(t, j) * pow(1 - t, degree - j);
        binom = binom * (degree - j) / (j + 1);
      }
    }
    const Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(m);

    std::vector<PCurve *> on_both;
    for (const CrossingSide& side : sides->second) {
      const Vector3d ref2 = side.axis.cross(side.ref);
      Eigen::MatrixXd rhs(degree + 1, 2);
      double previous = 0;
      for (int i = 0; i <= degree; i++) {
        const Vector3d rel = on_curve[i] - side.base;
        double u = atan2(rel.dot(ref2), rel.dot(side.ref));
        // A surface of revolution's parameter wraps, and a curve that steps over
        // the seam has to keep counting rather than jump a turn.
        if (i > 0) {
          while (u - previous > M_PI) u -= 2 * M_PI;
          while (u - previous < -M_PI) u += 2 * M_PI;
        }
        previous = u;
        rhs(i, 0) = u;
        rhs(i, 1) = rel.dot(side.axis);
      }
      const Eigen::MatrixXd solved = qr.solve(rhs);
      std::vector<Point2d *> cp;
      for (int j = 0; j <= degree; j++) {
        cp.push_back(new Point2d(entities, solved(j, 0), solved(j, 1)));
      }
      if (param_context == nullptr) param_context = new ParametricContext(entities);
      auto rep =
        new DefinitionalRepresentation(entities, new BSplineCurve2d(entities, cp), param_context);
      on_both.push_back(new PCurve(entities, side.surface, rep));
    }
    entry.second->round = new SurfaceCurve(entities, bez, on_both);
    pcurved++;
  }
  if (pcurved > 0) {
    LOG(
      "STEP export: %1$d of those carr%2$s a pcurve on each of the two surfaces, so the curve "
      "is written as lying on them rather than left to be projected back",
      pcurved, pcurved == 1 ? "ies" : "y");
  }

  // Build the loops, their edges and the carrier planes.
  std::vector<FaceBound *> face_bounds(face_cnt, nullptr);
  std::size_t planes_off_plane = 0;
  double worst_plane_off = 0;
  // What the mesher was entitled to call coplanar, by its own rule - see
  // coplanarTolerance in GeometryEvaluator.cc, which this has to agree with.
  double mesh_coplanar_tol = 1e-9;
  {
    Vector3d lo = vertices.empty() ? Vector3d(0, 0, 0) : vertices[0];
    Vector3d hi = lo;
    for (const auto& v : vertices) {
      lo = lo.cwiseMin(v);
      hi = hi.cwiseMax(v);
    }
    mesh_coplanar_tol = std::max(1e-9, 1e-8 * (hi - lo).norm());
  }
  std::vector<Plane *> planes(face_cnt, nullptr);
  int planes_declared = 0;
  std::vector<std::vector<EdgeCurve *>> loop_edges(face_cnt);

  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i] || consumed[i] || split_for_corners.count(i) != 0) continue;
    const std::vector<int>& loop = loops[i];
    const int n = int(loop.size());

    std::vector<OrientedEdge *> oriented_edges;

    // a rim of a recognised band is one circular edge instead of n straight
    // ones, shared with the band's face
    const auto rim = rim_of_loop.find(i);
    if (rim != rim_of_loop.end()) {
      oriented_edges.push_back(new OrientedEdge(entities, rim->second.first, rim->second.second));
      loop_edges[i].push_back(rim->second.first);
    } else if (arc_subs.count(i) != 0) {
      // one or more runs of this loop's edges belong to a partial band and are
      // replaced, in place, by the single arc that band is bounded by
      const std::vector<ArcSubstitution>& subs = arc_subs[i];
      std::vector<int> starts_here(n, -1);
      std::vector<char> covered(n, 0);
      for (std::size_t s = 0; s < subs.size(); s++) {
        starts_here[subs[s].start % n] = int(s);
        for (std::size_t c = 0; c < subs[s].count; c++) covered[(subs[s].start + c) % n] = 1;
      }
      for (int j = 0; j < n; j++) {
        if (starts_here[j] >= 0) {
          const ArcSubstitution& sub = subs[starts_here[j]];
          oriented_edges.push_back(new OrientedEdge(entities, sub.edge, sub.sense));
          loop_edges[i].push_back(sub.edge);
          continue;
        }
        if (covered[j] != 0) continue;
        const int ind = loop[j];
        const int indn = loop[(j + 1) % n];
        bool edge_dir = true;
        EdgeCurve *edge_curve = get_line_from_map(edge_map, ind, indn, get_vertex(ind), get_vertex(indn),
                                                  edge_dir, merged_edge_cnt);
        oriented_edges.push_back(new OrientedEdge(entities, edge_curve, edge_dir));
        loop_edges[i].push_back(edge_curve);
      }
    } else
      for (int j = 0; j < n; j++) {
        const int ind = loop[j];
        const int indn = loop[(j + 1) % n];
        bool edge_dir = true;
        EdgeCurve *edge_curve = get_line_from_map(edge_map, ind, indn, get_vertex(ind), get_vertex(indn),
                                                  edge_dir, merged_edge_cnt);
        oriented_edges.push_back(new OrientedEdge(entities, edge_curve, edge_dir));
        loop_edges[i].push_back(edge_curve);
      }
    // create the plane. Where the model declared one that this face lies in,
    // that is the plane: a declaration cannot drift, and a fit to mesh vertices
    // moves with the tessellation, with whatever a boolean left behind, and with
    // every vertex a later pass touches. The face's own vertices are unchanged
    // either way - the declaration is only accepted when every one of them is
    // already in it, so this replaces the *coefficients* and nothing else.
    // The plane the face's corners are on, not the one the mesh gave it before
    // the corner placement ran. Newell's formula over the loop as written, kept
    // to the mesh's sense so the face still looks the way the shell is oriented.
    // For a face the placement did not bend the two agree to 1e-9 by
    // construction - the veto is what guarantees that - and for a triangle it
    // tilted they do not, which is the whole reason a tilted triangle used to be
    // refused a move it could perfectly well take.
    Vector3d norm = loop_normals[i];
    {
      Vector3d newell = Vector3d::Zero();
      for (int j = 0; j < n; j++) {
        newell += vertices[loop[j]].cross(vertices[loop[(j + 1) % n]]);
      }
      if (newell.norm() > 1e-18) {
        newell.normalize();
        norm = newell.dot(loop_normals[i]) >= 0 ? newell : Vector3d(-newell);
      }
    }
    Vector3d on_plane = vertices[loop[0]];
    {
      double extent = 0;
      for (int j = 0; j < n; j++) {
        extent = std::max(extent, (vertices[loop[j]] - on_plane).norm());
      }
      const double tol = std::max(1e-12, 1e-9 * std::max(1.0, extent));
      for (std::size_t dp = 0; dp < declared_planes.size(); dp++) {
        const auto& declared = declared_planes[dp];
        if (fabs(fabs(declared.second.dot(norm)) - 1.0) > 1e-9) continue;
        bool holds = true;
        for (int j = 0; holds && j < n; j++) {
          holds = fabs((vertices[loop[j]] - declared.first).dot(declared.second)) <= tol;
        }
        if (!holds) continue;
        if (dp < declared_plane_index.size() && declared_plane_index[dp] < decl_written.size()) {
          decl_written[declared_plane_index[dp]] = 1;
        }
        if (i < facet_class.size()) facet_class[i] = 1;
        // Keep the mesh's sense: a declaration says which plane, not which way
        // the face looks out of it.
        norm = declared.second.dot(norm) > 0 ? declared.second : Vector3d(-declared.second);
        on_plane = declared.first + norm * (vertices[loop[0]] - declared.first).dot(norm);
        // Per face, not per loop: a face with a hole in it computes a plane for
        // its inner bound too, and counting those says a ring was written twice.
        if (!loop_is_hole[i]) planes_declared++;
        break;
      }
    }
    Vector3d ref(0, 0, 0);
    double ref_len = 0;
    for (int j = 0; j < n; j++) {
      const Vector3d dir = vertices[loop[(j + 1) % n]] - vertices[loop[j]];
      if (dir.norm() > ref_len) {
        ref_len = dir.norm();
        ref = dir;
      }
    }
    ref -= norm * norm.dot(ref);
    if (ref.norm() < 1e-12) ref = AnalyticFeatures::perpendicular(norm);
    else ref.normalize();

    {
      // A planar face may not assert a plane one of its own bounds is not in.
      //
      // The veto above refuses a move that would bend a face; this is the same
      // property asked of what is actually written, over every bound the face
      // carries. It is measured against the mesh's own coplanarity tolerance,
      // which is what a merged loop is entitled to be warped by:
      // GeometryEvaluator's coplanarTolerance buckets triangles within
      // 1e-8 of the model's diagonal, so on lid10 - 248 mm across - ten merged
      // quads come out warped by up to 2.262e-06 against that 2.48e-06
      // allowance, and no coupon or fixture exceeds it. Anything further off was
      // bent by something else, which is a defect in this exporter and not in
      // the mesh it was given: on lid10 the corner placement bent two faces
      // through their *holes* by 1.8 and 5.7, which a kernel cannot read as one
      // planar face at all - it splits them, seals the cavity between, and reads
      // the solid 28% too large.
      double off = 0;
      for (const int w : loop) off = std::max(off, fabs((vertices[w] - on_plane).dot(norm)));
      for (std::size_t j = 0; j < face_cnt; j++) {
        if (!loop_valid[j] || loop_is_hole[i] || parents[j] != int(i)) continue;
        for (const int w : loops[j]) {
          off = std::max(off, fabs((vertices[w] - on_plane).dot(norm)));
        }
      }
      if (off > mesh_coplanar_tol) {
        planes_off_plane++;
        worst_plane_off = std::max(worst_plane_off, off);
      }
    }
    auto plane_point = new Point(entities, on_plane);
    auto plane_dir_1 = new Direction(entities, norm);
    auto plane_dir_2 = new Direction(entities, ref);
    auto plane_axis = new Axis2Placement(entities, plane_dir_1, plane_dir_2, plane_point);
    planes[i] = new Plane(entities, plane_axis);

    auto edge_loop = new EdgeLoop(entities, oriented_edges);
    face_bounds[i] = new FaceBound(entities, edge_loop, true, !loop_is_hole[i]);
  }

  if (planes_declared > 0) {
    LOG(
      "STEP export: %1$d planar face%2$s written on a plane the model declared rather than one "
      "fitted to its own corners",
      planes_declared, planes_declared == 1 ? "" : "s");
  }

  const recognition::ReportInputs report_inputs{
    features, surfaces, recognition::Mesh{&vertices, &loops, &loop_valid, &loop_is_hole, &loop_normals},
    faceOrigin, owned};
  recognition::reportDeclarationCensus(report_inputs, analytic, decl_written, facet_class);
  // The fans for the polygons the corner placement bent.
  //
  // Each triangle takes its boundary edges from the same map every other face
  // uses, so the shell still stitches along them; the diagonals are new and
  // used by exactly the two triangles that share them. Each triangle carries
  // the plane its own three corners lie on, which is the point of the fan -
  // the polygon's plane is the one they no longer all sit on.
  for (const auto& sc : split_for_corners) {
    const std::size_t i = sc.first;
    for (const auto& corner : sc.second) {
      const int tri[3] = {corner[0], corner[1], corner[2]};
      const Vector3d e1 = vertices[tri[1]] - vertices[tri[0]];
      const Vector3d e2 = vertices[tri[2]] - vertices[tri[0]];
      if (e1.cross(e2).norm() < area_eps) continue;
      std::vector<OrientedEdge *> oriented;
      std::vector<EdgeCurve *> edges_here;
      for (int j = 0; j < 3; j++) {
        const int a = tri[j], b = tri[(j + 1) % 3];
        bool dir = true;
        EdgeCurve *e =
          get_line_from_map(edge_map, a, b, get_vertex(a), get_vertex(b), dir, merged_edge_cnt);
        oriented.push_back(new OrientedEdge(entities, e, dir));
        edges_here.push_back(e);
      }
      // The triangle's own plane, not the polygon's. A triangle is planar
      // wherever its corners are, which is the whole reason for fanning one
      // out - but only if the PLANE written under it is the one its corners
      // actually lie on. Taking the polygon's normal instead left every fan
      // triangle asserting a plane its corners had just moved off, and
      // step-declare-grid-scad carried the entire 0.096 on its planes while its
      // cylinders and its sweep were already exact.
      //
      // And it is written the way the bound winds, never turned to agree with
      // the polygon. Turning it is what an inverted ear used to produce: a
      // PLANE exactly opposite the loop bounding it, which is not a face.
      // Choosing the apex is what makes that unnecessary - every ear of the
      // chosen fan winds with the polygon - so a disagreement here is a bug
      // rather than a case to paper over, and the triangle is dropped.
      Vector3d tn = e1.cross(e2);
      if (tn.dot(loop_normals[i]) <= 0) continue;
      tn.normalize();
      Vector3d ref = e1 - tn * tn.dot(e1);
      if (ref.norm() < 1e-12) ref = AnalyticFeatures::perpendicular(tn);
      else ref.normalize();
      auto axis = new Axis2Placement(entities, new Direction(entities, tn), new Direction(entities, ref),
                                     new Point(entities, vertices[tri[0]]));
      std::vector<FaceBound *> bounds;
      bounds.push_back(new FaceBound(entities, new EdgeLoop(entities, oriented), true, true));
      sfaces_extra.push_back(new Face(entities, bounds, new Plane(entities, axis), true));
      face_edges_extra.push_back(edges_here);
    }
  }

  // Combine every outer loop with the loops of its holes into one ADVANCED_FACE.
  std::vector<Face *> sfaces;
  std::vector<std::vector<EdgeCurve *>> face_edges;
  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i] || loop_is_hole[i] || consumed[i] || split_for_corners.count(i) != 0) {
      continue;
    }
    std::vector<FaceBound *> singface;
    singface.push_back(face_bounds[i]);
    std::vector<EdgeCurve *> edges = loop_edges[i];
    for (std::size_t j = 0; j < face_cnt; j++) {
      if (!loop_valid[j] || consumed[j] || parents[j] != int(i)) continue;
      singface.push_back(face_bounds[j]);
      edges.insert(edges.end(), loop_edges[j].begin(), loop_edges[j].end());
    }
    sfaces.push_back(new Face(entities, singface, planes[i], true));
    face_edges.push_back(edges);
  }

  if (planes_off_plane > 0) {
    LOG(message_group::Export_Error,
        "STEP export: %1$d planar face%2$s assert a plane one of their own bounds is not in, by up "
        "to %3$.4f against the %4$.2e the mesh's own coplanarity allows - a bound bent by "
        "something other than the merge",
        int(planes_off_plane), planes_off_plane == 1 ? "" : "s", worst_plane_off, mesh_coplanar_tol);
  }
  // the recognised cylinders are faces of the same shell
  for (std::size_t i = 0; i < sfaces_extra.size(); i++) {
    sfaces.push_back(sfaces_extra[i]);
    face_edges.push_back(face_edges_extra[i]);
  }

  // Every edge bounding a recovered curved face was still a straight line: the
  // exporter drops a cylinder behind the mesh's polyline boundary, so the edge
  // is the chord where the surface is the arc, and the gap between them is the
  // sagitta. Measured on the band family, 366 of 388 line-on-cylinder edges lay
  // off the cylinder they bounded, by up to 0.17mm - doc/step-export-development.md
  // has the tables. Every kernel that reads such a file has to widen its
  // tolerance to swallow that; two do it silently and one declines, and none of
  // them is wrong to object.
  //
  // The 22 edges that were already exact say what the fix is. A ruling lies on a
  // cylinder by construction, so the segments running up and down the axis need
  // nothing, and it is the ones running around it - at constant height, chords
  // of a circle - that should be written as arcs of that circle instead.
  //
  // An edge belongs to two faces, so promoting it is only sound when both can
  // contain the arc. A plane can when its normal is the axis, which is what a
  // flat top or an annulus is; anything else declines, and the edge stays the
  // line it was. That is what keeps this from ever moving an edge off a face
  // that was exact already, which would trade one kernel's complaint for
  // everyone's.
  std::map<EdgeCurve *, std::vector<Face *>> edge_faces;
  for (std::size_t i = 0; i < face_edges.size(); i++) {
    for (auto *edge : face_edges[i]) edge_faces[edge].push_back(sfaces[i]);
  }

  // The axis and radius a quadric face implies for an edge, if it implies one:
  // the section at the edge's own height, which exists only when both ends are
  // at the same height and the same distance from the axis.
  auto section_of = [&](const SurfaceType *surface, const Vector3d& p1, const Vector3d& p2,
                        Vector3d& axis, Vector3d& centre, double& radius) {
    Vector3d ax, org;
    double apex_r = 0.0, slope = 0.0;
    if (const auto *cyl = dynamic_cast<const CylindricalSurface *>(surface)) {
      ax = cyl->axis->dir1->pt.normalized();
      org = cyl->axis->point->pt;
      apex_r = cyl->r;
    } else if (const auto *con = dynamic_cast<const ConicalSurface *>(surface)) {
      ax = con->axis->dir1->pt.normalized();
      org = con->axis->point->pt;
      apex_r = con->r;
      slope = tan(con->half_angle);
    } else {
      return false;
    }
    const double h1 = (p1 - org).dot(ax), h2 = (p2 - org).dot(ax);
    if (fabs(h1 - h2) > tol) return false;
    const Vector3d c = org + ax * h1;
    const double r1 = (p1 - c).norm(), r2 = (p2 - c).norm();
    const double want = apex_r + slope * h1;
    if (fabs(r1 - r2) > tol || fabs(r1 - want) > tol) return false;
    axis = ax;
    centre = c;
    radius = want;
    return true;
  };

  // Where the section is not a circle it is still a conic, and the chord is
  // still wrong.
  //
  // The arc promotion above asks for a *circle*: both ends at one height on the
  // axis and at the surface's own radius there. That is the section a plane
  // perpendicular to the axis cuts, and it is the only one it reaches. A plane
  // at any other angle cuts an ellipse, and the edge between such a plane and a
  // cone is a chord of that ellipse - written straight, lying off the cone by up
  // to 0.0759 on lid10 and off nothing else, because a chord of a plane section
  // does lie in the plane.
  //
  // That is SOLIDWORKS fault code 17 exactly - "edge does not lie on face's
  // surface" - and it is what its own repair of `f04` does at a thousandth of
  // the size: it moved no vertex, no surface and no topology, only re-fitting
  // 215 curves by at most 9.583e-05 onto the surfaces they bound. Ours are off
  // by 0.1089.
  //
  // The section pass writes these as ELLIPSE where it recognises a *run* of them
  // on a face it is building. This is the same curve for the edges it did not
  // claim, decided here because here is where the corners are final - the same
  // reason the endpoint invariant binds at the writer. `planeSectionEllipse` is
  // the same solver both use.
  int conics_promoted = 0, conics_declined = 0;
  // Rims the declaration settled, where a neighbouring chord plane would have
  // bent them into an ellipse. See open item 26.
  int rims_kept_circular = 0;
  // Which conics are cut by a declared plane, and which by a tessellation
  // chord. See open item 26.
  int conics_on_declared = 0, conics_on_chord = 0, conics_on_facet = 0;
  // Why the crossing was not available: the facet belongs to the very surface
  // being cut, or the arc would not fit.
  int facet_same_surface = 0;
  int why_noproj = 0, why_offquad = 0, why_offplane = 0, why_nosection = 0, why_shape = 0;
  int why_branch = 0, conics_ellipse = 0, conics_hyperbola = 0, conics_parabola = 0;
  double conic_worst_end = 0, why_offplane_worst = 0;
  auto promote_to_conic = [&](EdgeCurve *edge, const std::vector<Face *>& faces) {
    auto *was_line = dynamic_cast<Line *>(edge->round);
    if (was_line == nullptr) return false;
    const Vector3d p1 = edge->vert1->point->pt;
    const Vector3d p2 = edge->vert2->point->pt;
    if ((p1 - p2).norm() < 1e-12) return false;

    // One quadric and one plane, and every other face at the edge has to be
    // that same plane: a conic lies in exactly one plane, so a second one
    // through the edge would be asserting a curve it does not contain.
    std::shared_ptr<Surface> quadric;
    bool have_plane = false;
    Vector3d pnorm(0, 0, 0), ppoint(0, 0, 0);
    for (auto *face : faces) {
      if (const auto *con = dynamic_cast<const ConicalSurface *>(face->surface)) {
        if (quadric != nullptr) return false;
        quadric = std::make_shared<ConeSurface>(con->axis->point->pt, con->axis->dir1->pt, con->r,
                                                tan(con->half_angle));
      } else if (const auto *cyl = dynamic_cast<const CylindricalSurface *>(face->surface)) {
        if (quadric != nullptr) return false;
        quadric = std::make_shared<CylinderSurface>(cyl->axis->point->pt, cyl->axis->dir1->pt, cyl->r);
      } else if (const auto *pl = dynamic_cast<const Plane *>(face->surface)) {
        const Vector3d n = pl->axis->dir1->pt.normalized();
        const Vector3d o = pl->axis->point->pt;
        if (have_plane) {
          if (fabs(fabs(n.dot(pnorm)) - 1) > 1e-9 || fabs((o - ppoint).dot(pnorm)) > tol) return false;
        } else {
          have_plane = true;
          pnorm = n;
          ppoint = o;
        }
      } else {
        why_shape++;
        return false;  // a sweep or a patch: its crossing is not a conic
      }
    }
    if (quadric == nullptr || !have_plane) return false;

    // Only a chord that is actually off its quadric. A ruling of a cone lies on
    // it exactly, and so does any edge the placement already put there; moving
    // one of those would trade an exact edge for an approximation of itself.
    Vector3d q;
    double off = 0;
    for (const Vector3d *p : {&p1, &p2}) {
      if (!AnalyticFeatures::closestOnSurface(quadric.get(), *p, q)) return false;
      off = std::max(off, (q - *p).norm());
    }
    const Vector3d mid = (p1 + p2) / 2;
    if (!AnalyticFeatures::closestOnSurface(quadric.get(), mid, q)) return false;
    if ((q - mid).norm() <= tol) return false;  // the line is on the surface already

    // The invariant every substitution owes: the curve has to pass through the
    // run's own endpoints. A point is on the conic when it is in the plane and
    // on the quadric, and both are asked of both ends.
    for (const Vector3d *p : {&p1, &p2}) {
      const double owed = std::max(tol, 1e-9 * std::max(1.0, p->norm()));
      if (!AnalyticFeatures::closestOnSurface(quadric.get(), *p, q)) {
        conics_declined++;
        why_noproj++;
        return false;
      }
      if ((q - *p).norm() > owed) {
        conics_declined++;
        why_offquad++;
        conic_worst_end = std::max(conic_worst_end, (q - *p).norm());
        return false;
      }
      if (fabs((*p - ppoint).dot(pnorm)) > owed) {
        conics_declined++;
        why_offplane++;
        why_offplane_worst = std::max(why_offplane_worst, fabs((*p - ppoint).dot(pnorm)));
        return false;
      }
    }

    // A rim is a circle by construction, and no fit may overrule it.
    //
    // The plane in this section is whatever planar face sits beside the edge,
    // and that is not necessarily a plane the model declared: a facet of a
    // curved surface left faceted is a *chord* plane, and cutting the quadric
    // with it gives a conic that touches the true boundary only at the two ends
    // and leaves it in between. The corner arbiter already refuses such planes
    // as constraints - `kCornerTiers` marks MeshPlane `used = false`, because
    // "a chord is not a constraint" - and this pass had no such guard.
    //
    // Measured: step-split-surface.scad wrote its cylinder's rim at z = 10 as a
    // 45 degree ELLIPSE, semi-axes 28.318 x 20, centred at z = 30 on a model 13
    // tall - the chamfer's own slope, leaking out of a chord facet into a curve
    // belonging to the cylinder. The curve arcs off the rim between its
    // endpoints and the solid loses 1.2126 mm3, which no validator and neither
    // kernel objects to. See open item 26.
    //
    // So: where both ends sit at one level along the quadric's axis, and one
    // distance from it, the edge is an arc of that quadric's own circle. That
    // is settled by the declaration - the axis and the radius are the surface's
    // own - and the section plane is then the perpendicular one through the
    // ends, whatever plane the neighbouring face happens to lie in.
    {
      Vector3d qaxis, qbase;
      bool axial = false;
      if (const auto *cyl = dynamic_cast<const CylinderSurface *>(quadric.get())) {
        qaxis = cyl->normdir.normalized();
        qbase = cyl->refpt;
        axial = qaxis.norm() > 0;
      } else if (const auto *con = dynamic_cast<const ConeSurface *>(quadric.get())) {
        qaxis = con->normdir.normalized();
        qbase = con->refpt;
        axial = qaxis.norm() > 0;
      }
      if (axial) {
        const double h1 = qaxis.dot(p1 - qbase), h2 = qaxis.dot(p2 - qbase);
        const double scale = std::max(1.0, std::max(p1.norm(), p2.norm()));
        const double r1 = ((p1 - qbase) - qaxis * h1).norm();
        const double r2 = ((p2 - qbase) - qaxis * h2).norm();
        if (fabs(h1 - h2) <= 1e-9 * scale && fabs(r1 - r2) <= 1e-9 * scale) {
          pnorm = qaxis;
          ppoint = qbase + qaxis * h1;
          rims_kept_circular++;
        }
      }
    }
    // Is the plane doing the cutting a plane the *model* declared?
    //
    // Measured 2026-09-21: **none of them are**, on any reference part - 0 of
    // 78 on lid10, 0 of 18 on the bayonet, 0 of 37 on f04. So this pass never
    // has declaration-level support for its section plane; it always deduces
    // one, and cannot tell a genuine flat face a boolean made from a facet of
    // something curved that was left faceted. The corner arbiter refuses the
    // second as a constraint - kCornerTiers marks MeshPlane `used = false`,
    // "a chord is not a constraint" - and this pass never asked.
    //
    // Counted rather than refused, deliberately: declared planes are rare by
    // construction (item 23 measured 2 of 1533 merged planes matching one), so
    // refusing everything undeclared would refuse the sound cases with the
    // unsound. Telling them apart is what the claim ledger is for.
    {
      bool declared_plane = false;
      const double scale = std::max(1.0, ppoint.norm());
      for (const auto& d : surfaces) {
        const auto *pl = dynamic_cast<const PlaneSurface *>(d.get());
        if (pl == nullptr) continue;
        const Vector3d dn = pl->normdir.normalized();
        if (!(dn.norm() > 0) || fabs(fabs(dn.dot(pnorm)) - 1) > 1e-9) continue;
        if (fabs(dn.dot(ppoint - pl->refpt)) > 1e-9 * scale) continue;
        declared_plane = true;
        break;
      }
      if (declared_plane) conics_on_declared++;
      else conics_on_chord++;
      // Counted, *not* refused - and the measurement is why.
      //
      // A facet of something curved is not a plane, and the conic it cuts is
      // not a curve of this model: the true boundary is where the quadric meets
      // the surface that facet belongs to. But the conic does lie exactly on the
      // quadric, which is what code 17 measures, and the chord it would fall
      // back to does not. Refusing these outright was tried on 2026-09-21 and
      // took code 17 from 42 to 198 on lid10, 42 to 78 on the bayonet and 38 to
      // 100 on f04 - 156 edges put back off the faces they bound.
      //
      // So this cannot be fixed here. The neighbour is written as a plane
      // because a region of a *declared curved surface* was never claimed, and
      // once that has happened no consumer can recover what the model said. The
      // fix is upstream, in the claim; see open item 26 and
      // doc/step-recognition-design.md, *region before representation*.
      const Surface *facet_of =
        declared_plane ? nullptr : planeIsFacetOfCurved(surfaces, pnorm, {p1, p2});
      if (facet_of != nullptr) {
        conics_on_facet++;
        // And *every one of them*, on every reference part, has the facet on
        // the same surface being cut: 78 of 78 on lid10, 18 of 18 on the
        // bayonet, 31 of 31 on f04.
        //
        // So there are not two surfaces here and there is no crossing to write.
        // There is one declared surface, claimed as an analytic face over part
        // of its region and left as facets over the rest, and this edge is the
        // seam between the two halves - written as a section of the surface by
        // its own chord plane, which is not a boundary of anything.
        //
        // These edges should not exist. That is the split-surface defect of
        // open item 26 in another costume, and it cannot be repaired here:
        // refusing the conics was tried and took code 17 from 42 to 198 on
        // lid10, because the chord they fall back to is further off the face
        // than the wrong curve is. Removing the paper does not mend the tear.
        // The fix is upstream - claim the region whole - see
        // doc/step-recognition-design.md, *region before representation*.
        if (facet_of == quadric.get() || facet_of->sameAs(*quadric)) facet_same_surface++;
      }
    }
    SectionConic sec;
    if (!planeSectionConic(quadric.get(), pnorm, ppoint, sec)) {
      conics_declined++;
      why_nosection++;
      return false;
    }

    // Whichever conic it is, written as that conic and trimmed by its own two
    // vertices. The ends are on it already - that was asked above - so all that
    // is left is which way round the edge runs.
    // The edge keeps its two vertices in the order it already has them: the
    // ORIENTED_EDGEs in every loop that uses it were written against that order,
    // and swapping them leaves those loops not closing. What turns instead is
    // the *placement's* axis - a conic lies in its plane either way round, and
    // reversing the normal reverses the direction its parameter runs - which is
    // the same thing the arc promotion above does by choosing `from x to`.
    RoundType *curve = nullptr;
    Vector3d nz = sec.normal.normalized();
    Vector3d minor = nz.cross(sec.major);
    if (sec.kind == SectionConic::ELLIPSE) {
      auto param = [&](const Vector3d& p) {
        const Vector3d rel = p - sec.centre;
        return atan2(rel.dot(minor) / sec.b, rel.dot(sec.major) / sec.a);
      };
      double d = param(p2) - param(p1);
      while (d < 0) d += 2 * M_PI;
      while (d >= 2 * M_PI) d -= 2 * M_PI;
      // One segment of a tessellated boundary is the shorter of the two arcs, so
      // if the way round from vert1 to vert2 is the long one, the axis is the
      // wrong way up.
      if (d > M_PI) {
        nz = -nz;
        minor = -minor;
      }
      curve = new Ellipse(entities, "", conic_placement(sec.centre, nz, sec.major), sec.a, sec.b);
    } else if (sec.kind == SectionConic::HYPERBOLA) {
      // A HYPERBOLA entity describes one branch, the one the reference direction
      // points at, and `a cosh(t)` is never less than `a` - so both ends have to
      // be on that branch or the curve does not contain the edge at all.
      // ...and which branch is not a property of the section, it is which one
      // this edge is on: the two are mirror images and the solver's major axis
      // points at one of them arbitrarily. Turn it round rather than refuse.
      Vector3d major = sec.major;
      auto along = [&](const Vector3d& p) { return (p - sec.centre).dot(major); };
      if (along(p1) < 0 && along(p2) < 0) major = -major;
      if (along(p1) < sec.a * 0.5 || along(p2) < sec.a * 0.5) {
        // One end on each branch is not an edge of one conic at all.
        conics_declined++;
        why_branch++;
        return false;
      }
      // t increases with the minor coordinate, so the axis points whichever way
      // makes it increase from vert1 to vert2.
      if ((p1 - sec.centre).dot(nz.cross(major)) > (p2 - sec.centre).dot(nz.cross(major))) nz = -nz;
      curve = new Hyperbola(entities, "", conic_placement(sec.centre, nz, major), sec.a, sec.b);
    } else {
      if ((p1 - sec.centre).dot(minor) > (p2 - sec.centre).dot(minor)) nz = -nz;
      curve = new Parabola(entities, "", conic_placement(sec.centre, nz, sec.major), sec.a);
    }
    if (curve == nullptr) return false;
    if (sec.kind == SectionConic::ELLIPSE) conics_ellipse++;
    else if (sec.kind == SectionConic::HYPERBOLA) conics_hyperbola++;
    else conics_parabola++;

    edge->round = curve;
    edge->dir = true;
    was_line->live = false;
    was_line->vector->live = false;
    was_line->vector->dir->live = false;
    was_line->point->live = false;
    conics_promoted++;
    return true;
  };

  int arcs_promoted = 0, arcs_declined = 0;
  for (auto& entry : edge_faces) {
    EdgeCurve *edge = entry.first;
    auto *was_line = dynamic_cast<Line *>(edge->round);
    if (was_line == nullptr) continue;
    const Vector3d p1 = edge->vert1->point->pt;
    const Vector3d p2 = edge->vert2->point->pt;
    if ((p1 - p2).norm() < 1e-12) continue;

    // A quadric among the faces proposes the arc; every face then gets a veto.
    Vector3d axis, centre;
    double radius = 0.0;
    bool proposed = false;
    for (auto *face : entry.second) {
      if (section_of(face->surface, p1, p2, axis, centre, radius)) {
        proposed = true;
        break;
      }
    }
    if (!proposed) {
      promote_to_conic(edge, entry.second);
      continue;
    }

    bool agreed = true;
    for (auto *face : entry.second) {
      if (const auto *pl = dynamic_cast<const Plane *>(face->surface)) {
        // A circle about `axis` lies in a plane only if the plane is
        // perpendicular to it, and its endpoints are in the plane already.
        const Vector3d n = pl->axis->dir1->pt.normalized();
        agreed = fabs(fabs(n.dot(axis)) - 1) < 1e-9;
      } else {
        Vector3d other_axis, other_centre;
        double other_radius = 0.0;
        agreed = section_of(face->surface, p1, p2, other_axis, other_centre, other_radius) &&
                 (other_centre - centre).norm() < tol && fabs(other_radius - radius) < tol;
      }
      if (!agreed) break;
    }
    if (!agreed) {
      if (!promote_to_conic(edge, entry.second)) arcs_declined++;
      continue;
    }

    const Vector3d from = (p1 - centre).normalized();
    const Vector3d to = (p2 - centre).normalized();
    // The normal that makes the sweep from `from` to `to` counter clockwise and
    // shorter than half a turn, which one segment of a tessellated boundary
    // always is.
    const Vector3d normal = from.cross(to);
    if (normal.norm() < 1e-12) {
      arcs_declined++;
      continue;
    }
    auto arc_point = new Point(entities, centre);
    auto arc_axis = new Direction(entities, normal.normalized());
    auto arc_ref = new Direction(entities, from);
    edge->round =
      new Circle(entities, "", new Axis2Placement(entities, arc_axis, arc_ref, arc_point), radius);
    edge->dir = true;
    // The line and the three entities it alone referred to would otherwise sit
    // in the file unreferenced. `get_line_from_map` gives every edge its own,
    // so nothing else can be pointing at them.
    was_line->live = false;
    was_line->vector->live = false;
    was_line->vector->dir->live = false;
    was_line->point->live = false;
    arcs_promoted++;
  }
  if (rim_unshared > 0) {
    LOG(message_group::Export_Warning,
        "STEP export: %1$d band rim%2$s given up as one arc, the face across it being fanned into "
        "triangles, which cannot be handed an edge - both faces keep the mesh's own edges there",
        int(rim_unshared), rim_unshared == 1 ? " is" : "s are");
  }
  if (arcs_promoted > 0 || arcs_declined > 0) {
    LOG("STEP export: %1$d edge%2$s written as an arc on the surface it bounds, %3$d left straight",
        arcs_promoted, arcs_promoted == 1 ? "" : "s", arcs_declined);
  }
  if (rims_kept_circular > 0) {
    LOG(
      "STEP export: %1$d edge%2$s a circle of the quadric's own, kept from the conic a chord "
      "plane beside them would have cut - a rim is settled by the declaration, not by the facet "
      "next to it",
      rims_kept_circular, rims_kept_circular == 1 ? " is" : "s are");
  }
  if (facet_same_surface > 0) {
    LOG(message_group::Export_Warning,
        "STEP export: %1$d edge%2$s the seam between an analytic face and facets of the *same* "
        "declared surface, written as a section of that surface by its own chord plane - a "
        "boundary of nothing. The region was not claimed whole; see open item 26",
        facet_same_surface, facet_same_surface == 1 ? " is" : "s are");
  }
  if (conics_on_declared + conics_on_chord > 0) {
    LOG(
      "STEP export: of the conics considered, %1$d are cut by a plane the model declared and "
      "%2$d by a plane nothing declares - of which %3$d are facets of something curved, so the "
      "conic is the wrong curve; kept because the chord is further off the face, and the fix is "
      "upstream in the claim",
      conics_on_declared, conics_on_chord, conics_on_facet);
  }
  if (conics_promoted > 0 || conics_declined > 0) {
    LOG(
      "STEP export: %1$d chord%2$s written as the conic the plane cuts from the quadric beside "
      "it, %3$d left straight - the worst end of those was %4$.2e off the quadric",
      conics_promoted, conics_promoted == 1 ? " is" : "s are", conics_declined, conic_worst_end);
    LOG(
      "STEP export:    of those, %1$d are ellipses, %2$d hyperbolas and %3$d parabolas - a cone "
      "cut steeper than its own half angle is a hyperbola, and writing a chord for one is what "
      "leaves an edge off the face it bounds",
      conics_ellipse, conics_hyperbola, conics_parabola);
    LOG(
      "STEP export:    declined: %1$d no projection, %2$d an end off the quadric, %3$d an end off "
      "the plane (worst %4$.2e), %5$d no conic from that plane, %6$d an end on the far branch, "
      "%7$d a face that is not one",
      why_noproj, why_offquad, why_offplane, why_offplane_worst, why_nosection, why_branch, why_shape);
  }
  // Every edge in the file against its own two vertices, which is the one
  // relation a CAD kernel rejects a substituted curve on and the one nothing
  // here used to measure. It is asked of the finished entities rather than of
  // any pass's intermediate state, so no substitution can be added later that
  // quietly escapes it.
  {
    std::size_t off = 0;
    double worst = 0;
    std::vector<double> strays;
    for (const Entity *ent : entities) {
      const auto *edge = dynamic_cast<const EdgeCurve *>(ent);
      if (edge == nullptr || edge->round == nullptr) continue;
      for (const Vertex *v : {edge->vert1, edge->vert2}) {
        if (v == nullptr || v->point == nullptr) continue;
        const double d = vertexOffCurve(edge->round, v->point->pt);
        const double owed = 1e-6 * std::max(1.0, v->point->pt.norm());
        if (d <= owed) continue;
        off++;
        strays.push_back(d);
        worst = std::max(worst, d);
      }
    }
    if (off > 0) {
      std::sort(strays.begin(), strays.end());
      LOG(message_group::Export_Warning,
          "STEP export: %1$d vertex%2$s not on the curve its own edge is written as, by "
          "%3$.2e median and %4$.2e at worst - a reader checks exactly this, and calls it "
          "\"vertex does not lie on edge\'s curve\"",
          int(off), off == 1 ? " is" : "es are", strays[strays.size() / 2], worst);
    }
  }
  if (reseamed > 0 || reseam_full > 0) {
    LOG(
      "STEP export: %1$d sweep face%2$s written from a column that puts the profile's seam in a "
      "span it does not use, so no boundary of it crosses the seam; %3$d use every span and keep "
      "the seam where it was",
      int(reseamed), reseamed == 1 ? " is" : "s are", int(reseam_full));
  }
  if (section_stats.trimmed + section_stats.dropped > 0) {
    LOG(
      "STEP export: %1$d plane section%2$s shortened and %3$d dropped so that the curve written "
      "for a run passes through that run's own endpoints - the worst end was %4$.2e off the "
      "surface the section is cut from",
      int(section_stats.trimmed), section_stats.trimmed == 1 ? " was" : "s were",
      int(section_stats.dropped), section_stats.end_stray);
  }
  if (crossing_written > 0) {
    LOG(
      "STEP export: %1$d edge%2$s written as the curve where two declared surfaces cross, "
      "fitted to within %3$.2e of both - a chord and a plane section each lie on only one",
      crossing_written, crossing_written == 1 ? "" : "s", crossing_fit);
  }
  if (arc_refused > 0) {
    std::sort(arc_errors.begin(), arc_errors.end());
    const double lo = arc_errors.empty() ? 0 : arc_errors.front();
    const double med = arc_errors.empty() ? 0 : arc_errors[arc_errors.size() / 2];
    const double hi = arc_errors.empty() ? 0 : arc_errors.back();
    LOG(
      "EXPORT-WARNING: STEP export: %1$d edge%2$s could not be written as that curve and fell "
      "back to a chord - %3$d never fitted at any degree, the rest came within "
      "%4$.2e/%5$.2e/%6$.2e min/med/max of both surfaces against the %7$.0e asked for",
      int(arc_refused), arc_refused == 1 ? "" : "s", int(arc_no_fit), lo, med, hi, 1e-7);
    std::sort(arc_on_quadric.begin(), arc_on_quadric.end());
    std::sort(arc_on_fitted.begin(), arc_on_fitted.end());
    if (!arc_on_quadric.empty()) {
      LOG(
        "STEP export:    of that miss, %1$.2e median is against a surface stated algebraically "
        "and %2$.2e median against one that answers by projecting",
        arc_on_quadric[arc_on_quadric.size() / 2], arc_on_fitted[arc_on_fitted.size() / 2]);
      LOG("STEP export:    %1$d of them run across a crease in the declared profile and %2$d do not",
          int(arc_creased), int(arc_uncreased));
    }
  }
  if (sections_on_facet > 0) {
    LOG(
      "STEP export: %1$d boundary section%2$s cut by a facet of something curved rather than by a "
      "plane - the wrong curve, kept because the chord it would fall back to is further off the "
      "face; the fix is upstream, in the claim",
      sections_on_facet, sections_on_facet == 1 ? " is" : "s are");
  }
  if (band_seams > 0) {
    LOG(
      "STEP export: %1$d face%2$s round %3$s axis between two rims closed by a seam along a ruling, not "
      "cut in two",
      int(band_seams), band_seams == 1 ? "" : "s", band_seams == 1 ? "its" : "their");
  }
  if (apex_seams > 0) {
    LOG("STEP export: %1$d cone face%2$s closed round %3$s apex by a seam up a generator",
        int(apex_seams), apex_seams == 1 ? "" : "s", apex_seams == 1 ? "its" : "their");
  }
  if (sections_declared + sections_fitted > 0) {
    // Where each section's plane came from, because it is the difference
    // between a curve that is exact and one that is only as good as the mesh
    // it was fitted to. A declared plane cannot move; a fitted one moves with
    // the tessellation, with what the boolean left behind, and with the
    // tolerance the fit was given - and the section's centre and both its
    // semi-axes are differences taken against that plane.
    LOG(
      "STEP export: %1$d plane section%2$s written as the conic it is - %3$d on a plane the "
      "model declared, %4$d on one taken from the mesh",
      sections_declared + sections_fitted, sections_declared + sections_fitted == 1 ? "" : "s",
      sections_declared, sections_fitted);
    if (section_unshared > 0) {
      LOG(message_group::Export_Warning,
          "STEP export: %1$d plane section%2$s written as chords by both faces instead of as the "
          "conic by one, the face across being fanned into triangles or not carrying the run of "
          "corners it would have to be handed the edge by",
          int(section_unshared), section_unshared == 1 ? " is" : "s are");
    }
    if (!section_misses.empty()) {
      double tilt = 9, off = 9;
      std::size_t parallel = 0;
      for (const auto& m : section_misses) {
        tilt = std::min(tilt, m.tilt);
        if (m.tilt <= 1e-6) {
          parallel++;
          off = std::min(off, m.offset);
        }
      }
      LOG(
        "STEP export:    %1$d cut planes took the mesh's own: %2$d are parallel to a declared "
        "plane, nearest missing its offset by %3$.2e, and the rest tilt from the nearest by "
        "%4$.2e against the 1e-06 asked for; %5$d planes were declared in all",
        int(section_misses.size()), int(parallel), off > 8 ? 0.0 : off, tilt,
        int(declared_planes.size()));
    }
  }

  // A CLOSED_SHELL has to be a single connected shell, so split disconnected
  // bodies into one MANIFOLD_SOLID_BREP each instead of stuffing all of them
  // into one shell that can never close.
  std::vector<int> component(sfaces.size(), 0);
  for (std::size_t i = 0; i < sfaces.size(); i++) component[i] = int(i);
  std::map<EdgeCurve *, int> edge_owner;
  for (std::size_t i = 0; i < face_edges.size(); i++) {
    for (auto *edge : face_edges[i]) {
      auto it = edge_owner.find(edge);
      if (it == edge_owner.end()) {
        edge_owner.emplace(edge, int(i));
        continue;
      }
      const int a = uf_find(component, it->second);
      const int b = uf_find(component, int(i));
      if (a != b) component[a] = b;
    }
  }
  std::map<int, std::vector<Face *>> shell_faces;
  for (std::size_t i = 0; i < sfaces.size(); i++) {
    shell_faces[uf_find(component, int(i))].push_back(sfaces[i]);
  }

  // units and modelling tolerance
  auto length_unit = new SiUnit(entities, SiUnit::LENGTH);
  auto angle_unit = new SiUnit(entities, SiUnit::PLANE_ANGLE);
  auto solid_angle_unit = new SiUnit(entities, SiUnit::SOLID_ANGLE);
  auto uncertainty = new UncertaintyMeasure(entities, length_unit, model_tol);
  auto geom_context =
    new GeometricContext(entities, uncertainty, length_unit, angle_unit, solid_angle_unit);

  // create the base csys
  auto base_point = new Point(entities, Vector3d(0, 0, 0));
  auto base_dir_1 = new Direction(entities, Vector3d(0, 0, 1));
  auto base_dir_2 = new Direction(entities, Vector3d(1, 0, 0));
  auto base_axis = new Axis2Placement(entities, base_dir_1, base_dir_2, base_point);

  // product structure
  const std::string body_name = name != nullptr ? name : "";
  auto app_context = new ApplicationContext(entities);
  new ApplicationProtocolDefinition(entities, app_context);
  auto prod_context = new ProductContext(entities, app_context);
  auto product = new Product(entities, body_name, prod_context);
  new ProductRelatedProductCategory(entities, product);
  auto prod_formation = new ProductDefinitionFormation(entities, product);
  auto prod_def_context = new ProductDefinitionContext(entities, app_context);
  auto product_def = new ProductDefinition(entities, prod_formation, prod_def_context);
  auto product_def_shape = new ProductDefinitionShape(entities, product_def);
  auto shape_repr = new ShapeRepresentation(entities, body_name, base_axis, geom_context);
  new ShapeDefinition_Representation(entities, product_def_shape, shape_repr);

  // build the model
  std::vector<ManifoldSolid *> solids;
  for (auto& shell : shell_faces) {
    auto closed_shell = new Shell(entities, shell.second);
    closed_shell->isOpen = false;
    solids.push_back(new ManifoldSolid(entities, 0, closed_shell));
  }

  if (!solids.empty()) {
    auto adv_brep_shape_pres = new AdvancesBrepRepresentation(entities, body_name, solids, geom_context);
    new ShapeRepresentationRelationShip(entities, shape_repr, adv_brep_shape_pres);
  }
}

StepKernel::Vertex *StepKernel::BodyBuild::get_vertex(int ind)
{
  if (step_verts[ind] == nullptr) {
    const auto key = std::make_tuple(vertices[ind][0], vertices[ind][1], vertices[ind][2]);
    const auto it = vertex_at.find(key);
    if (it != vertex_at.end()) {
      step_verts[ind] = it->second;
    } else {
      auto point = new Point(entities, vertices[ind]);
      step_verts[ind] = new Vertex(entities, point);
      vertex_at.emplace(key, step_verts[ind]);
    }
  }
  return step_verts[ind];
}
