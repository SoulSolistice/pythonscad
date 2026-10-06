/*! Chords promoted to the curves they are - see BodyBuild.h. */
#include "geometry/brep/BodyBuild.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Exact.h"
#include "geometry/brep/PlaneSections.h"
#include "utils/printutils.h"

using namespace brep;

void brep::BodyBuild::promoteCurves()
{
  // On the B-rep (open item 39, step 2d): which faces use each edge, once per
  // use, walked in the order the faces meet their edges - the B-rep's own, so no
  // address decides anything (checklist F9). An edge whose uses carry pcurves
  // is a SURFACE_CURVE in the file, not a bare line, and is not promoted.
  brep::Body& body = brep_body;
  std::vector<std::vector<std::size_t>> edge_faces(body.edges.size());
  std::vector<std::size_t> edge_order;
  std::vector<char> carries_pcurve(body.edges.size(), 0);
  for (std::size_t f = 0; f < body.faces.size(); f++) {
    for (const brep::Loop& loop : body.faces[f].loops) {
      for (const brep::Coedge& c : loop.coedges) {
        if (edge_faces[c.edge].empty()) edge_order.push_back(c.edge);
        edge_faces[c.edge].push_back(f);
        if (c.pcurve) carries_pcurve[c.edge] = 1;
      }
    }
  }
  auto is_line = [&](std::size_t e) {
    return std::holds_alternative<brep::Line>(body.edges[e].curve.form) && carries_pcurve[e] == 0;
  };

  // The surface each face is written on is its carrier, as its maker stated it
  // - never one rebuilt from the form written from it, which is a re-derivation
  // with rounding in place of the surface and identity lost (open item 39,
  // R10). Whether the carrier is the record the face stands for is the
  // contract's question (brep::checkAttribution, in checkBrep), asked of every
  // face, not this pass's.
  auto quadric_of = [&](std::size_t f) -> const Surface * {
    const brep::Face& face = body.faces[f];
    const Surface *s = face.carrier.get();
    if (std::holds_alternative<brep::Cylinder>(face.form) &&
        dynamic_cast<const CylinderSurface *>(s) != nullptr)
      return s;
    if (std::holds_alternative<brep::Cone>(face.form) && dynamic_cast<const ConeSurface *>(s) != nullptr)
      return s;
    return nullptr;
  };
  auto plane_of = [&](std::size_t f) -> const PlaneSurface * {
    return dynamic_cast<const PlaneSurface *>(body.faces[f].carrier.get());
  };

  // The circle a quadric face implies for an edge, if it implies one: the
  // section across its axis through both ends, which exists exactly when both
  // ends are on the surface and on one plane across the axis - at machine
  // precision, and then the radius is the surface's own there, nothing
  // measured. Within the model's tolerance until 2026-10-05, which promoted an
  // arc between ends up to 1e-5 apart in height: a circle missing its own
  // endpoints (open item 39, R11).
  auto section_of = [&](std::size_t f, const Vector3d& p1, const Vector3d& p2, Vector3d& axis,
                        Vector3d& centre, double& radius) {
    const Surface *record = quadric_of(f);
    if (record == nullptr) return false;
    if (!recognition::onExactly(record, p1) || !recognition::onExactly(record, p2)) return false;
    const auto *axial = static_cast<const AxialSurface *>(record);
    const Vector3d ax = axial->axis(), org = axial->refpt;
    const Vector3d c = org + ax * (p1 - org).dot(ax);
    if (!recognition::onPlane(c, ax, p2)) return false;
    const auto *cone = dynamic_cast<const ConeSurface *>(record);
    axis = ax;
    centre = c;
    radius = cone != nullptr ? cone->radiusAt(c) : static_cast<const CylinderSurface *>(record)->r;
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
  int conics_on_declared = 0, conics_on_chord = 0, conics_on_facet = 0, conics_beside_unnamed = 0;
  // Why the crossing was not available: the facet belongs to the very surface
  // being cut, or the arc would not fit.
  int facet_same_surface = 0;
  int why_noproj = 0, why_offquad = 0, why_offplane = 0, why_nosection = 0, why_shape = 0;
  int why_branch = 0, why_curve = 0, conics_ellipse = 0, conics_hyperbola = 0, conics_parabola = 0;
  double conic_worst_end = 0, why_offplane_worst = 0;
  auto promote_to_conic = [&](std::size_t e, const std::vector<std::size_t>& faces) {
    if (!is_line(e)) return false;
    brep::Edge& edge = body.edges[e];
    const Vector3d p1 = body.vertices[edge.start].point;
    const Vector3d p2 = body.vertices[edge.end].point;
    if (edge.start == edge.end) return false;  // no length: brep::check names it (R17)

    // One quadric and one plane, and every other face at the edge has to be
    // that same plane: a conic lies in exactly one plane, so a second one
    // through the edge would be asserting a curve it does not contain.
    std::shared_ptr<const Surface> quadric;
    bool have_plane = false;
    std::size_t plane_face = 0;
    Vector3d pnorm(0, 0, 0), ppoint(0, 0, 0);
    for (const std::size_t f : faces) {
      const brep::SurfaceForm& form = body.faces[f].form;
      if (std::holds_alternative<brep::Cone>(form) || std::holds_alternative<brep::Cylinder>(form)) {
        if (quadric != nullptr) return false;
        const Surface *record = quadric_of(f);
        if (record == nullptr) return false;
        quadric = body.faces[f].carrier;
      } else if (std::holds_alternative<brep::Plane>(form)) {
        const PlaneSurface *pl = plane_of(f);
        if (pl == nullptr) return false;
        const Vector3d n = pl->normdir.normalized();
        const Vector3d o = pl->refpt;
        if (have_plane) {
          if (!recognition::samePlane(o, n, ppoint, pnorm)) return false;
        } else {
          have_plane = true;
          plane_face = f;
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
    // Whether it does is a closed-form certificate (brep::curveOn: a line on a
    // quadric where three of its points are), not a midpoint within the model's
    // tolerance - which left a chord sagging less than 1e-5 straight, off the
    // face it bounds by up to a hundred times OpenCASCADE's own 1e-7 (R11).
    if (brep::curveOn(edge.curve.form, p1, p2, quadric.get())) return false;
    Vector3d q;

    // The invariant every substitution owes: the curve has to pass through the
    // run's own endpoints. A point is on the conic when it is in the plane and
    // on the quadric, and both are asked of both ends - at machine precision,
    // where the model's tolerance let a conic miss its own ends by 1e-5 (R11).
    for (const Vector3d *p : {&p1, &p2}) {
      if (!AnalyticFeatures::closestOnSurface(quadric.get(), *p, q)) {
        conics_declined++;
        why_noproj++;
        return false;
      }
      if (!recognition::onExactly(quadric.get(), *p)) {
        conics_declined++;
        why_offquad++;
        conic_worst_end = std::max(conic_worst_end, (q - *p).norm());
        return false;
      }
      if (!recognition::onPlane(ppoint, pnorm, *p)) {
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
        // Both ends are on the quadric, asked above, so one plane across the
        // axis through both makes them one circle of it - exactly (R11).
        const Vector3d level = qbase + qaxis * qaxis.dot(p1 - qbase);
        if (recognition::onPlane(level, qaxis, p2)) {
          pnorm = qaxis;
          ppoint = level;
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
      // By the face's record: a plane the model declared is one it stands on by
      // name or position, never a plane of its corners - and not any declared
      // plane that happens to pass through it (open item 39, R10).
      const brep::Face& beside = body.faces[plane_face];
      const bool declared_plane =
        !beside.plane_from_corners && surfaceIndexOf(beside.surface.get()) >= 0;
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
      // Which curved surface the plane is a chord facet of, by the name its
      // facets carry (open item 39, R4) - by angle until 2026-09-30.
      // A facet named for nothing cannot say, and is counted as such rather
      // than guessed at: on lid10 these are the chords of surfaces whose makers
      // name nothing yet (item 40), which the angle used to find.
      const Surface *facet_of = nullptr;
      bool beside_unnamed = false;
      if (!declared_plane) {
        for (const std::size_t facet : body.faces[plane_face].facets) {
          const Surface *named = namedFor(facet);
          if (named == nullptr) beside_unnamed = true;
          else if (dynamic_cast<const PlaneSurface *>(named) == nullptr) facet_of = named;
        }
      }
      if (facet_of == nullptr && beside_unnamed) conics_beside_unnamed++;
      if (facet_of != nullptr) {
        conics_on_facet++;
        // Until 2026-09-30, told by angle, *every one of them* on every
        // reference part had the facet on the same surface being cut - and so
        // was read as the seam of one surface claimed over part of its region,
        // a boundary of nothing (open item 27). By name, not one does: on lid10
        // all 29 are a cone against the chords of a cylinder, on f04 all 37 a
        // cylinder against a declared sweep, and on the bayonet 10 a cone and 8
        // a cylinder against its thread's sweep. The angle took the quadric
        // itself for the facet's surface - the edge's ends lie on it, and the
        // chord of a surface tangent to it has nearly its normal - and could
        // not evaluate a sweep at all. These are crossings of two declared
        // surfaces, whose curve is the crossing (rung C3), written as a conic
        // because the far side is written as facets.
        //
        // Still counted, because a seam of one surface is still possible and
        // is still a boundary of nothing where it occurs.
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
    using Kind = brep::Conic::Kind;
    brep::Conic curve;
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
      curve = brep::Conic{Kind::Ellipse, {sec.centre, nz, sec.major}, sec.a, sec.b};
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
      curve = brep::Conic{Kind::Hyperbola, {sec.centre, nz, major}, sec.a, sec.b};
    } else {
      if ((p1 - sec.centre).dot(minor) > (p2 - sec.centre).dot(minor)) nz = -nz;
      curve = brep::Conic{Kind::Parabola, {sec.centre, nz, sec.major}, sec.a, 0};
    }
    // And the conic as written has to hold the run's own ends: that both are on
    // the quadric and in the plane says the section passes through them, not
    // that the entity built for it does. Unasked until 2026-10-05, and every
    // parabola missed its ends - by 2.43 on the roof - with only the closing
    // vertex-on-curve report to say so (open item 39, R30). Held to what that
    // report holds the file to, so no substitution writes what it flags.
    for (const Vector3d *p : {&p1, &p2}) {
      if (brep::distanceToCurve(curve, *p) > resolution()) {
        conics_declined++;
        why_curve++;
        return false;
      }
    }
    if (sec.kind == SectionConic::ELLIPSE) conics_ellipse++;
    else if (sec.kind == SectionConic::HYPERBOLA) conics_hyperbola++;
    else conics_parabola++;

    edge.curve.form = curve;
    edge.same_sense = true;
    conics_promoted++;
    return true;
  };

  int arcs_promoted = 0, arcs_declined = 0;
  for (const std::size_t e : edge_order) {
    if (!is_line(e)) continue;
    brep::Edge& edge = body.edges[e];
    const std::vector<std::size_t>& faces = edge_faces[e];
    const Vector3d p1 = body.vertices[edge.start].point;
    const Vector3d p2 = body.vertices[edge.end].point;
    if (edge.start == edge.end) continue;  // no length: brep::check names it (R17)

    // A quadric among the faces proposes the arc; every face then gets a veto.
    Vector3d axis, centre;
    double radius = 0.0;
    bool proposed = false;
    for (const std::size_t f : faces) {
      if (section_of(f, p1, p2, axis, centre, radius)) {
        proposed = true;
        break;
      }
    }
    if (!proposed) {
      promote_to_conic(e, faces);
      continue;
    }

    bool agreed = true;
    for (const std::size_t f : faces) {
      if (std::holds_alternative<brep::Plane>(body.faces[f].form)) {
        // A circle about `axis` lies in a plane only if the plane is
        // perpendicular to it, and its endpoints are in the plane already.
        const PlaneSurface *pl = plane_of(f);
        agreed = pl != nullptr && recognition::parallel(pl->normdir, axis);
      } else {
        Vector3d other_axis, other_centre;
        double other_radius = 0.0;
        agreed = section_of(f, p1, p2, other_axis, other_centre, other_radius) &&
                 recognition::samePoint(other_centre, centre) &&
                 fabs(other_radius - radius) <= recognition::forwardError(9, other_radius + radius);
      }
      if (!agreed) break;
    }
    if (!agreed) {
      if (!promote_to_conic(e, faces)) arcs_declined++;
      continue;
    }

    const Vector3d from = (p1 - centre).normalized();
    const Vector3d to = (p2 - centre).normalized();
    // The normal that makes the sweep from `from` to `to` counter clockwise and
    // shorter than half a turn, which one segment of a tessellated boundary
    // always is.
    const Vector3d normal = from.cross(to);
    // Ends and centre on a line - the two ends across a diameter - fix no
    // plane for the arc and no sense to run it in.
    if (!recognition::statesPlane(centre, p1, p2)) {
      arcs_declined++;
      continue;
    }
    edge.curve.form =
      brep::Conic{brep::Conic::Kind::Circle, {centre, normal.normalized(), from}, radius, radius};
    edge.same_sense = true;
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
      "%2$d by a plane nothing declares - of which %3$d are facets of something curved by their "
      "names, so the conic is the wrong curve, and %4$d are facets named for nothing, which "
      "cannot say; kept because the chord is further off the face, and the fix is upstream in "
      "the claim",
      conics_on_declared, conics_on_chord, conics_on_facet, conics_beside_unnamed);
  }
  if (conics_beside_unnamed > 0) {
    LOG(message_group::Export_Warning,
        "STEP export: %1$d edge%2$s cut from a quadric by a plane whose facets are named for "
        "nothing - whether each is the seam of a surface claimed over part of its region (open "
        "item 26) cannot be told without the name its maker does not give (item 40)",
        conics_beside_unnamed, conics_beside_unnamed == 1 ? " is" : "s are");
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
      "%7$d a face that is not one, %8$d a conic that as written misses an end",
      why_noproj, why_offquad, why_offplane, why_offplane_worst, why_nosection, why_branch, why_shape,
      why_curve);
  }
}
