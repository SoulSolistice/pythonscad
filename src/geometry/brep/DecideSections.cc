/*! Which boundary edges are written as exact curves - see BodyBuild.h. */
#include "geometry/brep/BodyBuild.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Exact.h"
#include "geometry/brep/BoundaryCycles.h"
#include "geometry/brep/CrossingArc.h"
#include "geometry/brep/PlaneSections.h"
#include "utils/printutils.h"

using namespace brep;
using AnalyticFeatures::declaredBand;
using AnalyticFeatures::isQuadric;
using AnalyticFeatures::surfaceImplicit;

void brep::BodyBuild::decideSections(const std::vector<const AnalyticFeatures::Patch *>& standing)
{
  // Whose facets are not there to be written as a planar face: what an
  // earlier pass consumed, plus what this one is standing to claim.
  taken = features.consumed;
  for (const auto *patch : standing) {
    for (const std::size_t f : patch->facets) taken[f] = 1;
  }
  // A loop a band will use as its rim is spoken for too. It is still
  // written as a planar face, so it does not look consumed, but the band
  // replaces a stretch of it with a circle and the writer takes the band's
  // answer - so a second curve handed to the same loop is dropped in
  // silence.
  for (const auto& rim : features.rims) {
    for (const AnalyticFeatures::RimRef *side : {&rim.first, &rim.second}) {
      if (side->kind == AnalyticFeatures::RimRef::WHOLE_LOOP ||
          side->kind == AnalyticFeatures::RimRef::LOOP_RUN) {
        if (side->loop < taken.size()) taken[side->loop] = 1;
      }
    }
  }
  // And the planes those faces cut each surface with. Gathered per surface
  // rather than per region, because a plane that cuts a cone cuts all of
  // it: the region meeting the cut along a single edge could never have
  // seen the plane from that edge alone, and refused the whole surface for
  // it.
  section_planes.clear();
  section_misses.clear();
  for (const auto *patch : standing) {
    std::vector<CutPlane>& list = section_planes[patch->surface.get()];
    for (const auto& run : patch->runs) {
      if (run.loop >= loops.size() || loop_valid[run.loop] == 0) continue;
      if (taken[run.loop] != 0 || loops[run.loop].size() < 3) continue;
      CutPlane cp;
      cp.normal = loop_normals[run.loop].normalized();
      cp.on_plane = vertices[loops[run.loop][0]];
      // A declared plane in preference to the mesh's own. The fit moves
      // with the tessellation and with whatever the boolean left behind;
      // the declaration cannot move at all, and the section derived from
      // it is exact rather than merely close.
      //
      // Which declaration is the facet's name (checklist E9; open item 39,
      // R13): a facet named for a declared plane cuts in that record. It was
      // found by position - the first declared plane every corner of the
      // facet lies on - which agrees wherever a name is given (every export
      // measured, 2026-10-05) and says nothing about whose plane it is. A
      // facet named for a curved surface is a chord of it; the conic its
      // plane cuts lies on both faces as they are written, and is counted
      // as the wrong curve where it is written (item 26). Only a facet named
      // for nothing is asked by position still, and then the plane is a
      // derivation from its corners (D1), said so - on lid10 86 of them, on
      // the bayonet 16, facets whose maker named no plane (R32).
      const Surface *named = namedFor(run.loop);
      const auto *record = dynamic_cast<const PlaneSurface *>(named);
      if (record != nullptr) {
        const Vector3d n = record->normdir.normalized();
        cp.normal = cp.normal.dot(n) > 0 ? n : Vector3d(-n);
        cp.on_plane = record->refpt;
        cp.declared = true;
      }
      double best_tilt = 9, best_off = 9;
      for (const auto& pl : declared_planes) {
        if (named != nullptr) break;
        // How near the nearest declaration comes, whether or not it is
        // taken: a section written on the mesh's own plane is a section
        // that moves when the corners do, and knowing it missed by 1e-5
        // rather than by a right angle is the difference between a
        // tolerance to widen and a plane nobody declared.
        const double tilt = fabs(fabs(pl.second.dot(cp.normal)) - 1.0);
        const double off = fabs((cp.on_plane - pl.first).dot(pl.second));
        if (tilt < best_tilt) best_tilt = tilt;
        if (tilt <= 1e-6 && off < best_off) best_off = off;
        // Taken where every corner of the loop is on it, at machine precision
        // (recognition/Exact.h) - the corners are what the maker put there.
        if (!recognition::onPlane(pl.first, pl.second, vertices, loops[run.loop])) continue;
        cp.normal = cp.normal.dot(pl.second) > 0 ? pl.second : Vector3d(-pl.second);
        cp.on_plane = pl.first;
        cp.declared = true;
        cp.derived = true;
        break;
      }
      if (!cp.declared && named == nullptr) {
        section_misses.push_back({best_tilt, best_off});
      }
      bool have = false;
      for (const auto& seen : list) {
        // One plane or two, at machine precision, whoever states them - where
        // two planes taken from the mesh were compared within 1e-9 in angle
        // and in offset (item 38, C; open item 39, R13).
        have = recognition::samePlane(seen.on_plane, seen.normal, cp.on_plane, cp.normal);
        if (have) break;
      }
      if (!have) list.push_back(cp);
    }
  }
  // Which faces meet along each boundary edge, so that an edge on two
  // declared quadrics can be recognised from either side and written the
  // same way from both.
  std::map<std::pair<int, int>, std::vector<const Surface *>> along;
  auto walk_into_along = [&](const AnalyticFeatures::Patch& patch) {
    for (const auto& entry : rawBoundaryCycles(patch, loops, loop_valid, taken)) {
      const std::vector<int>& verts = entry.second.verts;
      const std::size_t n = verts.size();
      for (std::size_t i = 0; i < n; i++) {
        const int u = verts[i], v = verts[(i + 1) % n];
        along[{std::min(u, v), std::max(u, v)}].push_back(patch.surface.get());
      }
    }
  };
  for (const auto *patch : standing) walk_into_along(*patch);
  // The declared sweeps too. An edge where a sweep meets a cylinder is a
  // crossing of two declarations exactly as much as one between two
  // cylinders is, and leaving the sweeps out of this map is what made the
  // question unaskable rather than answered - `along` would only ever see
  // one owner for such an edge and drop it at the size test below.
  //
  // They are safe to add only because the grid emitter now reads
  // the crossing pieces as well: an edge admitted here and taken by the
  // cylinder's face while the sweep's face still wrote a chord would give
  // one edge two geometries, which is how this opened the shell on 420
  // edges the first time it was tried.
  for (const auto& patch : grid_faces) walk_into_along(patch);
  crossing_edges.clear();
  // What this decision refused, not the one it replaces: decideSections runs
  // at recognition and again once the corners have moved (redecideSections),
  // and these added the two - a curve refused before the placement and taken
  // after it still counted as falling back to a chord.
  crossing_ends_off = 0;
  arc_refused = arc_no_fit = arc_creased = arc_uncreased = 0;
  arc_errors.clear();
  arc_on_quadric.clear();
  arc_on_fitted.clear();
  crossing_corners.clear();
  // The planes each pair of quadrics' pencil states, worked out once a pair.
  std::map<std::pair<const Surface *, const Surface *>, std::vector<std::pair<Vector3d, Vector3d>>>
    pencils;
  for (const auto& entry : along) {
    if (entry.second.size() != 2) continue;
    const Surface *a = entry.second[0], *b = entry.second[1];
    if (a == b) continue;
    const Vector3d& p0 = vertices[entry.first.first];
    const Vector3d& p1 = vertices[entry.first.second];
    // Both ends have to be on both surfaces already. Where a boolean made
    // the vertex that is exactly what it is - the two makers crossing -
    // and where it is not, no arc of theirs belongs here. By the one
    // predicate a corner on its face is asked by (AnalyticFeatures::onFace),
    // where a grid's end was allowed its declared band - how far the mesh
    // stands off the surface, not where the curve's end is (open item 39,
    // R15's pattern).
    bool on = true;
    for (const Surface *s : {a, b}) {
      for (const Vector3d *p : {&p0, &p1}) on = on && AnalyticFeatures::onFace(s, *p);
    }
    if (!on) {
      crossing_ends_off++;
      continue;
    }
    // Where two quadrics cross in planes, the crossing is the conic either
    // plane cuts from either quadric - a closed form from the two declarations
    // (D1), written by the pass that writes that conic: a circle - two
    // surfaces of revolution about one axis, or a plane across the axis of a
    // surface whose sections there are circles - by the arc pass
    // (promoteCurves), each face proposing it from its own surface and every
    // face agreeing; any other conic by the section pass, which is handed the
    // plane as a cutting plane of both surfaces. So the edge is not fitted. It was fitted until
    // 2026-10-06 and handed back where the fit's control points were flat within 1e-9 of the part's
    // size, a measurement standing in for the algebra (open item 39, R7). About one axis the crossing is
    // circles, by identity (coaxialRevolution).
    if (coaxialRevolution(a, b)) continue;
    if (isQuadric(a) && isQuadric(b)) {
      const auto key = std::make_pair(std::min(a, b), std::max(a, b));
      auto found = pencils.find(key);
      if (found == pencils.end()) found = pencils.emplace(key, pencilPlanes(a, b)).first;
      bool conic = false;
      for (const auto& pl : found->second) {
        if (!recognition::onPlane(pl.first, pl.second, p0) ||
            !recognition::onPlane(pl.first, pl.second, p1)) {
          continue;
        }
        // A plane across the axis of one surface whose sections are circles is
        // a circle of it, and the other surface's too - the arc pass's.
        bool circle = false;
        for (const Surface *s : {a, b}) {
          const auto *axial = dynamic_cast<const AxialSurface *>(s);
          circle = circle || (axial != nullptr && axial->circularSections() &&
                              recognition::parallel(pl.second, axial->axis()));
        }
        for (const Surface *s : {a, b}) {
          if (circle) break;  // the arc pass's, which takes no cutting plane
          std::vector<CutPlane>& list = section_planes[s];
          bool have = false;
          for (const auto& seen : list) {
            have = have || recognition::samePlane(seen.on_plane, seen.normal, pl.first, pl.second);
          }
          if (have) continue;
          CutPlane cp;
          cp.on_plane = pl.first;
          cp.normal = pl.second;
          cp.pencil = true;
          list.push_back(cp);
        }
        conic = true;
        break;
      }
      if (conic) continue;
    }
    // The curve at the file's resolution, two ways: one Bezier, and a spline
    // refined along it. No polynomial follows the crossing where
    // it turns a corner - a sweep's crease inside the edge, across its profile
    // or along its path - but the corner itself is a point the crease and the
    // other surface state between them, and either side of it the crossing is
    // smooth; so the spline is fitted either side of its corners and written
    // as one curve with a corner in it. Whether a crease lies inside is asked
    // of the sweep's declared knots, not of the fit (`straddles_crease` looks
    // across the profile only). The spline is also what follows the curve
    // where it turns harder than a Bezier of degree 9 can - at the tip of the
    // lens a sweep makes as it fades into the surface it grows from.
    //
    // The Bezier where it reaches the file's resolution - the simpler curve,
    // and nothing finer is written - and the spline where only that does.
    // Neither reaching it leaves the chord: a fit that stops short is not
    // converging on the curve. decideCrossingRuns fits the same way and writes
    // what it finds.
    const double target = resolution();
    std::vector<Vector3d> ctrl;
    ArcOutcome why;
    double arc_error = std::numeric_limits<double>::infinity();
    const bool arc = intersectionArc(a, b, p0, p1, target, ctrl, arc_error, &why);
    std::vector<Vector3d> corners;
    bool flanks = false;
    if (!arc) {
      corners = creaseCorners(a, b, p0, p1);
      flanks = true;
      Vector3d from = p0;
      for (std::size_t k = 0; k <= corners.size() && flanks; k++) {
        const Vector3d& to = k < corners.size() ? corners[k] : p1;
        CurveFit refined;
        double achieved = 0;
        flanks = fitCrossingRun(a, b, {from, to}, target, refined, achieved);
        from = to;
      }
    }
    if (flanks) {
      crossing_edges.emplace(entry.first, std::make_pair(a, b));
      crossing_corners.emplace(entry.first, corners);
      continue;
    }
    if (!arc) {
      arc_refused++;
      if (why.degree == 0 || why.best_error == std::numeric_limits<double>::infinity()) {
        arc_no_fit++;  // no degree ever got as far as a curve to measure
      } else {
        arc_errors.push_back(why.best_error);
        arc_on_quadric.push_back(why.on_quadric);
        arc_on_fitted.push_back(why.on_fitted);
        if (why.straddles_crease == 1) arc_creased++;
        else if (why.straddles_crease == 0) arc_uncreased++;
      }
      continue;
    }
    crossing_edges.emplace(entry.first, std::make_pair(a, b));
  }
  crossing_keys.clear();
  for (const auto& e : crossing_edges) crossing_keys.insert(e.first);
  // A section is written where both its faces will write it. A planar face
  // across it is not a claimant - it never asks for a curve - so it counts
  // as the second, and is handed the same edge when the faces are written.
  std::map<std::set<int>, int> claims;
  for (const auto *patch : standing) {
    for (const auto& entry : boundaryCycles(*patch, vertices, loops, loop_valid, taken,
                                            section_planes[patch->surface.get()], crossing_keys)) {
      const BoundaryCycle& cycle = entry.second;
      const std::size_t n = cycle.verts.size();
      for (const auto& bs : cycle.sections) {
        std::set<int> key;
        for (std::size_t k = 0; k < bs.count; k++) key.insert(cycle.verts[(bs.start + k) % n]);
        claims[key] += bs.loop >= 0 ? 2 : 1;
      }
    }
  }
  section_curves.clear();
  for (const auto& claim : claims) {
    if (claim.second == 2) section_curves.insert(claim.first);
  }
}

bool brep::BodyBuild::boundaryLiesOnSurface(const AnalyticFeatures::Patch& patch,
                                            const std::set<std::set<int>>& agreed)
{
  const auto *axial = dynamic_cast<const AxialSurface *>(patch.surface.get());
  if (axial == nullptr) return false;
  const Vector3d axis = axial->axis();
  const Vector3d origin = axial->refpt;
  double radius = 0, slope = 0;
  // A cylinder's and a cone's rims round the axis are written as circles by
  // the arc post-pass, which reads CYLINDRICAL_SURFACE and CONICAL_SURFACE
  // and nothing else; any other surface's rim is written as the section it
  // is, or not exact at all.
  bool circular = true;
  if (const auto *cyl = dynamic_cast<const CylinderSurface *>(patch.surface.get())) {
    radius = cyl->r;
  } else if (const auto *con = dynamic_cast<const ConeSurface *>(patch.surface.get())) {
    radius = con->r;
    slope = con->slope;
  } else {
    circular = false;
  }
  for (const auto& entry : boundaryCycles(patch, vertices, loops, loop_valid, taken,
                                          section_planes[patch.surface.get()], crossing_keys)) {
    const BoundaryCycle& cycle = entry.second;
    const std::size_t n = cycle.verts.size();
    if (n < 3) continue;
    // Edges a plane section covers are exact as the conic written for
    // them, so they are not asked to lie on the surface as chords - but
    // only where the face on the other side writes the same conic, which
    // is what `agreed` records, decided over all the faces at once.
    std::vector<char> covered(n, 0);
    for (const auto& bs : cycle.sections) {
      std::set<int> key;
      for (std::size_t k = 0; k < bs.count; k++) key.insert(cycle.verts[(bs.start + k) % n]);
      if (agreed.find(key) == agreed.end()) continue;
      for (std::size_t k = 0; k + 1 < bs.count; k++) covered[(bs.start + k) % n] = 1;
    }
    // How far a point is off the surface, radially. A cone's radius is a
    // function of height, which is the whole of the difference.
    auto off_surface = [&](const Vector3d& p) {
      if (!circular) return axial->offset(p);
      const Vector3d rel = p - origin;
      const double along = rel.dot(axis);
      const double want = radius + slope * along;
      return fabs((rel - axis * along).norm() - want);
    };
    for (std::size_t i = 0; i < n; i++) {
      if (covered[i]) continue;
      // An edge on the curve where this surface crosses another declared
      // one is written as that curve, so it is not asked to be a chord.
      const int u = cycle.verts[i], v = cycle.verts[(i + 1) % n];
      if (crossing_edges.count({std::min(u, v), std::max(u, v)}) != 0) continue;
      const Vector3d a = vertices[cycle.verts[i]] - origin;
      const Vector3d b = vertices[cycle.verts[(i + 1) % n]] - origin;
      const Vector3d along = b - a;
      const double rise = fabs(along.dot(axis));
      // Round the axis at constant height is exact as the arc the
      // post-pass will write. A straight edge is exact when the surface
      // contains the whole line, and the midpoint settles that on its own:
      // a line meets a quadric twice unless it lies in it, so a third
      // point on the surface means every point of it is. That is the test
      // rather than "parallel to the axis", which is only a cylinder's
      // answer - a cone's rulings run to its apex, and calling those
      // chords refused four regions of step-cut-cone for edges that were
      // exactly on the cone all along.
      const Vector3d mid = origin + (a + b) * 0.5;
      if ((rise > 1e-7 || !circular) && off_surface(mid) > 1e-7) return false;
    }
  }
  return true;
}

void brep::BodyBuild::redecideSections()
{
  if (!analytic) return;
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
    // way the crossing pieces already are.
    LOG(approximate ? message_group::Export_Warning : message_group::Export_Error,
        "STEP export: %1$d plane section%2$s agreed before the corners were placed and %3$s not "
        "after, so %4$s boundary is written as chords%5$s",
        int(lost), lost == 1 ? "" : "s", lost == 1 ? "does" : "do",
        lost == 1 ? "that much of a" : "that much of the",
        approximate ? "" : " under a face called exact");
  }
}
