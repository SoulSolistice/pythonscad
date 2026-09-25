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

  redecideSections();

  emitSweepFaces();

  emitQuadricFaces();

  writeCrossingPcurves();

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
