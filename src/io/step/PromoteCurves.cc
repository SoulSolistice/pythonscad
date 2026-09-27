/*! Chords promoted to the curves they are - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "io/step/PlaneSections.h"
#include "utils/printutils.h"

using namespace step;

void StepKernel::BodyBuild::promoteCurves()
{
  // Looked up by address, but walked in the order the faces meet their edges:
  // walking the map itself visits edges in allocation order, which differs from
  // run to run, and each promotion mints its curve's entities as it goes - so
  // the same file came out with its curves under other ids (checklist F9).
  std::map<EdgeCurve *, std::vector<Face *>> edge_faces;
  std::vector<EdgeCurve *> edge_order;
  for (std::size_t i = 0; i < face_edges.size(); i++) {
    for (auto *edge : face_edges[i]) {
      auto& faces = edge_faces[edge];
      if (faces.empty()) edge_order.push_back(edge);
      faces.push_back(sfaces[i]);
    }
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
  for (EdgeCurve *edge : edge_order) {
    const auto& entry = *edge_faces.find(edge);
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
}
