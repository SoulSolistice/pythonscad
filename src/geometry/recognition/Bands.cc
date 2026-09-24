/*! The band recogniser: rings of facets on a declared surface of revolution, their
 * rims, and the zones a stack of them makes on a sphere or torus.
 *
 * Part of the recognition module - see doc/step-recognition-design.md, *The
 * recognition module, as built*. Moved out of AnalyticFeatures.cc as it stood.
 */
#include "geometry/AnalyticFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

#include "geometry/Surface.h"
#include "geometry/recognition/Exact.h"
#include "geometry/recognition/Internal.h"
#include "geometry/recognition/Ledger.h"

namespace AnalyticFeatures {

bool Band::isCone() const
{
  return fabs(r_bottom - r_top) > 1e-9 * std::max(r_bottom, r_top);
}

Result recogniseSurfacesOfRevolution(const recognition::FacetGraph& graph,
                                     const std::vector<std::shared_ptr<Surface>>& surfaces, double tol,
                                     std::size_t declared_count)
{
  const Mesh& mesh = graph.mesh();
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& loop_is_hole = *mesh.is_hole;
  const std::vector<Vector3d>& loop_normals = *mesh.normals;
  const std::size_t face_cnt = loops.size();
  const double model_tol = tol > 0 ? tol : 1e-5;

  Result result;
  result.band_of_loop.assign(face_cnt, NO_BAND);
  result.consumed.assign(face_cnt, 0);
  result.consumed_by.assign(face_cnt, nullptr);
  if (surfaces.empty()) return result;

  std::vector<Band>& bands = result.bands;
  std::vector<char>& consumed = result.consumed;
  std::vector<std::size_t>& band_of_loop = result.band_of_loop;
  std::vector<std::pair<RimRef, RimRef>>& rims = result.rims;

  // The adjacency every recogniser walks, built once - see recognition::FacetGraph.
  const std::map<std::pair<int, int>, std::vector<std::size_t>>& loop_edges_map = graph.edgeLoops();

  auto edge_key = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };

  // Did the model declare a cylinder of this radius about this axis?
  // `which` takes the index of the record that matched, so a caller can tell a
  // surface the model declared from one the approximation fitted - they arrive
  // in one list, declarations first. See Band::on_declared.
  auto declared_cylinder = [&](double radius, const Vector3d& axis, const Vector3d& base,
                               std::size_t *which = nullptr) {
    for (std::size_t i = 0; i < surfaces.size(); i++) {
      const auto *cyl = dynamic_cast<const CylinderSurface *>(surfaces[i].get());
      if (cyl == nullptr) continue;
      if (fabs(cyl->r - radius) > 1e-7 * radius) continue;
      if (fabs(fabs(cyl->normdir.normalized().dot(axis)) - 1.0) > 1e-7) continue;
      if (distanceToAxis(cyl->refpt, base, axis) > 1e-7 * radius) continue;
      if (which != nullptr) *which = i;
      return true;
    }
    return false;
  };

  // Did the model declare a cone which passes through both of these rims?
  //
  // Asked of the two rims rather than of an apex and an angle, because that is
  // what the band has measured and it is well conditioned however shallow the
  // taper. A cone that agrees with both rims of an exactly fitted frustum is
  // the frustum, so nothing further is checked.
  auto declared_cone = [&](double r_bottom, double r_top, const Vector3d& axis, const Vector3d& base,
                           double height, std::size_t *which = nullptr) {
    const double scale = std::max(std::max(r_bottom, r_top), 1e-9);
    for (std::size_t i = 0; i < surfaces.size(); i++) {
      const auto& surface = surfaces[i];
      const auto *cone = dynamic_cast<const ConeSurface *>(surface.get());
      if (cone == nullptr) continue;
      const Vector3d cone_axis = cone->normdir.normalized();
      if (fabs(fabs(cone_axis.dot(axis)) - 1.0) > 1e-7) continue;
      if (distanceToAxis(cone->refpt, base, axis) > 1e-7 * scale) continue;
      if (fabs(cone->radiusAt(base) - r_bottom) > 1e-7 * scale) continue;
      if (fabs(cone->radiusAt(base + axis * height) - r_top) > 1e-7 * scale) continue;
      if (which != nullptr) *which = i;
      return true;
    }
    return false;
  };

  // Walk the strip of quads reached by crossing ruling edges.
  //
  // The previous version grew across edges parallel to the axis, which finds
  // a cylinder and never a frustum: a cone's rulings are tilted, each one
  // differently. Entering a quad through one ruling fixes which pair of its
  // edges are rulings, so the walk needs no axis and is unambiguous even on a
  // cylinder, where both pairs are parallel.
  // Does this facet lie on the surface the band started on? Passing nullptr
  // admits every quad, which is only used for the first, exploratory walk.
  using OnSurface = std::function<bool(std::size_t)>;

  auto walk_strip = [&](std::size_t seed, int entry_side, std::vector<std::size_t>& walls,
                        std::map<std::size_t, int>& entry, const OnSurface *on_surface) {
    walls.clear();
    entry.clear();
    std::vector<std::pair<std::size_t, int>> stack{{seed, entry_side}};
    while (!stack.empty()) {
      const auto cur = stack.back();
      stack.pop_back();
      if (entry.count(cur.first)) continue;
      entry.emplace(cur.first, cur.second);
      walls.push_back(cur.first);
      const auto& loop = loops[cur.first];
      for (const int side : {cur.second, (cur.second + 2) % 4}) {
        const int a = loop[side], b = loop[(side + 1) % 4];
        const auto it = loop_edges_map.find(edge_key(a, b));
        if (it == loop_edges_map.end()) continue;
        for (const std::size_t nb : it->second) {
          if (nb == cur.first || entry.count(nb) || consumed[nb]) continue;
          if (!loop_valid[nb] || loop_is_hole[nb] || loops[nb].size() != 4) continue;
          if (on_surface != nullptr && !(*on_surface)(nb)) continue;
          for (int j = 0; j < 4; j++) {
            if (edge_key(loops[nb][j], loops[nb][(j + 1) % 4]) == edge_key(a, b)) {
              stack.emplace_back(nb, j);
              break;
            }
          }
        }
      }
    }
  };

  // The chords of a set of facets: the edges which are not rulings. They all
  // lie in a plane perpendicular to the axis.
  auto chords_of = [&](const std::vector<std::size_t>& walls, const std::map<std::size_t, int>& entry) {
    std::vector<Vector3d> chords;
    for (const std::size_t f : walls) {
      const int r = entry.at(f);
      for (const int c : {(r + 1) % 4, (r + 3) % 4}) {
        const Vector3d dir = vertices[loops[f][(c + 1) % 4]] - vertices[loops[f][c]];
        if (dir.norm() > 1e-12) chords.push_back(dir.normalized());
      }
    }
    return chords;
  };

  // The rulings of a set of facets: the edges the walk crosses.
  auto rulings_of = [&](const std::vector<std::size_t>& walls, const std::map<std::size_t, int>& entry) {
    std::vector<Vector3d> rulings;
    for (const std::size_t f : walls) {
      const int r = entry.at(f);
      for (const int s : {r, (r + 2) % 4}) {
        const Vector3d dir = vertices[loops[f][(s + 1) % 4]] - vertices[loops[f][s]];
        if (dir.norm() > 1e-12) rulings.push_back(dir.normalized());
      }
    }
    return rulings;
  };

  // On a cylinder every ruling is parallel to the axis, which fixes it with no
  // pairing at all. Worth trying first because the chord route below assumes
  // every chord lies in a plane perpendicular to the axis, and a trim which is
  // not perpendicular breaks that: crossing a chord of the flat rim with a
  // chord of the cut gives an axis pointing nowhere in particular. A cone has
  // no parallel rulings and still needs the chords.
  auto axis_from_rulings = [](const std::vector<Vector3d>& rulings) {
    if (rulings.empty()) return Vector3d(0, 0, 0);
    Vector3d axis = rulings[0];
    for (const Vector3d& r : rulings) {
      if (fabs(fabs(r.dot(axis)) - 1.0) > 1e-9) return Vector3d(0, 0, 0);
    }
    if (axis[2] < 0 || (axis[2] == 0 && axis[0] < 0)) axis = -axis;
    return axis;
  };

  // Two chords which are not parallel fix the axis exactly.
  auto axis_from = [](const std::vector<Vector3d>& chords) {
    for (std::size_t c = 1; c < chords.size(); c++) {
      const Vector3d n = chords[0].cross(chords[c]);
      if (n.norm() < 1e-9) continue;
      Vector3d axis = n.normalized();
      if (axis[2] < 0 || (axis[2] == 0 && axis[0] < 0)) axis = -axis;
      return axis;
    }
    return Vector3d(0, 0, 0);
  };

  auto perpendicular_to = [](const std::vector<Vector3d>& chords, const Vector3d& axis) {
    for (const Vector3d& c : chords) {
      if (fabs(c.dot(axis)) >= 1e-9) return false;
    }
    return true;
  };

  // Declarations this pass read in order to identify something. Recorded where
  // the match is made rather than derived afterwards: which records were
  // *consulted* is not recoverable from the claim, because a claim names one
  // surface and a cone is identified by two rims.
  result.evidence.assign(surfaces.size(), 0);
  auto note_evidence = [&](std::size_t which) {
    if (which < result.evidence.size()) result.evidence[which] = 1;
  };
  // Which entry of the caller's list a surface is, so that a claim can name it.
  // A claim that cannot name its surface index cannot be arbitrated by the
  // model's own statement of who owns what - see Claim::surface_index - and
  // cannot be told apart from a claim on a *fitted* surface by anything reading
  // the ledger afterwards.
  auto index_of = [&](const std::shared_ptr<const Surface>& s) {
    if (s == nullptr) return std::size_t(-1);
    for (std::size_t i = 0; i < surfaces.size(); i++) {
      if (surfaces[i].get() == s.get() || (surfaces[i] && surfaces[i]->sameAs(*s))) return i;
    }
    return std::size_t(-1);
  };

  // What this pass would have claimed and refused, keyed by (facet, surface).
  //
  // A band decides which facets it takes by what a band can *express*, and until
  // 2026-09-23 a walk that found facets on a declared surface and then refused
  // them for want of a ring shape left no trace: the pass named a surface only
  // once every shape test had passed. That is the defect *the representation
  // fits the region, never the region the representation* describes, and it
  // was invisible. So the surface is named as soon as the walk is confined to
  // it, and every refusal after that point is recorded here as a declined
  // claim. Flushed at the end of the pass, once, and only where no band claim
  // on that facet and surface was made - a strip is walked from every seed on
  // it, and one seed's refusal is not a claim when another seed's band took it.
  //
  // Walls the pass already holds are not re-walked: every earlier band here is
  // a band on a surface its walls lie on at 1e-7, and a quad lies on one
  // declared quadric at that precision, so the only other claim such a walk
  // could name is the same surface. Examining held facets is the later passes'
  // job, and they do.
  std::map<std::pair<std::size_t, std::size_t>, const char *> declined;

  for (std::size_t seed = 0; seed < face_cnt; seed++) {
    if (!loop_valid[seed] || consumed[seed] || loop_is_hole[seed]) continue;
    if (loops[seed].size() != 4) continue;

    for (int side = 0; side < 4; side++) {
      std::vector<std::size_t> walls;
      std::map<std::size_t, int> entry;
      // First walk freely, only to pin down which surface the seed sits on.
      // Left unconstrained this runs off the wall wherever something flat is
      // attached to it - a rib welded to a tube has quads for side faces, so
      // the strip crosses through the rib and back into the next arc, and the
      // whole ring then fails the fit as one band that was never a band.
      walk_strip(seed, side, walls, entry, nullptr);
      if (walls.size() < 3) continue;

      // Take the axis from the seed and the facets joined to it across a
      // ruling, and from nothing else.
      //
      // Taking it from the whole free walk looks more robust and is not: where
      // that walk runs off the surface it brings foreign chords back with it,
      // the perpendicularity test below rejects them, and the candidate is
      // thrown away before the constrained walk ever gets the chance to clean
      // it up. That is how four lug chamfers of the bayonet container came to
      // be silently unrecognisable - the walk crossed the end of a five quad
      // strip into the lug's side face, turned through ninety degrees there
      // because entering a quad by a different edge redefines which pair of
      // its edges are rulings, and came back with four of fourteen chords
      // perpendicular to nothing. Two chords is all the axis needs, and the
      // seed's own neighbours are the two it can trust.
      std::vector<std::size_t> near{seed};
      for (const std::size_t f : walls) {
        if (f == seed) continue;
        bool joined = false;
        for (int j = 0; j < 4 && !joined; j++) {
          const auto key = edge_key(loops[f][j], loops[f][(j + 1) % 4]);
          for (const int s : {side, (side + 2) % 4}) {
            if (key == edge_key(loops[seed][s], loops[seed][(s + 1) % 4])) joined = true;
          }
        }
        if (joined) near.push_back(f);
      }
      // Not const: a band whose tilted rim comes out at the bottom is turned
      // over below, because the rim rules are written for a tilted top.
      Vector3d axis = axis_from_rulings(rulings_of(near, entry));
      if (axis.norm() < 0.5) axis = axis_from(chords_of(near, entry));
      if (axis.norm() < 0.5) continue;

      // Which declared surface the confined walk is on, once there is one to
      // name - see `declined` above. Asked with the same 1e-7 equality against
      // a declaration the match further down uses, of the probe's rims; the
      // walk admits a facet only if every corner is on the probe surface to
      // 1e-7, so this is a statement about declared points, not a fit.
      std::size_t named = std::size_t(-1);
      auto decline = [&](const char *why) {
        if (named == std::size_t(-1)) return;
        for (const std::size_t f : walls) declined.emplace(std::make_pair(f, named), why);
      };

      // Fit the surface from the seed and its first two neighbours - four
      // vertices on each rim, which is enough - and walk again, this time
      // admitting only facets which sit on it.
      {
        std::map<int, double> probe_along;
        for (std::size_t f = 0; f < 3 && f < walls.size(); f++) {
          for (const int v : loops[walls[f]]) probe_along[v] = axis.dot(vertices[v]);
        }
        double probe_lo = probe_along.begin()->second, probe_hi = probe_lo;
        for (const auto& kv : probe_along) {
          probe_lo = std::min(probe_lo, kv.second);
          probe_hi = std::max(probe_hi, kv.second);
        }
        std::vector<int> probe_bottom, probe_top;
        for (const auto& kv : probe_along) {
          if (fabs(kv.second - probe_lo) < model_tol) probe_bottom.push_back(kv.first);
          else if (fabs(kv.second - probe_hi) < model_tol) probe_top.push_back(kv.first);
        }
        Vector3d probe_base, probe_top_centre;
        if (!fitCircleCentre(vertices, probe_bottom, axis, probe_lo, probe_base)) continue;
        double probe_r0 = 0;
        for (const int v : probe_bottom) probe_r0 += distanceToAxis(vertices[v], probe_base, axis);
        probe_r0 /= double(probe_bottom.size());

        // A far rim which is not flat has no circle to fit, and needs none: on
        // a cylinder the far radius is the near radius. Only a far rim which
        // *is* flat, and fits a circle of its own, can make this a cone - so
        // the probe assumes a cylinder unless it is shown a second rim.
        double probe_r1 = probe_r0;
        if (probe_top.size() >= 3 &&
            fitCircleCentre(vertices, probe_top, axis, probe_hi, probe_top_centre)) {
          probe_r1 = 0;
          for (const int v : probe_top) probe_r1 += distanceToAxis(vertices[v], probe_base, axis);
          probe_r1 /= double(probe_top.size());
        }
        const double probe_scale = std::max(probe_r0, probe_r1);
        if (probe_scale < model_tol) continue;

        // On a cylinder a vertex which is on the surface is on it at any
        // height, so a vertex between the probe's two rims is admitted rather
        // than rejected. That is what lets a trim which is not perpendicular
        // to the axis stay in the band; without it the walk stops at the
        // first facet the trim clipped. A cone has to keep the stricter test,
        // because there the radius is what the height says it is.
        const bool cylindrical = fabs(probe_r0 - probe_r1) <= 1e-7 * probe_scale;
        const OnSurface on_surface = [&](std::size_t f) {
          for (const int v : loops[f]) {
            const double t = axis.dot(vertices[v]);
            const double want =
              fabs(t - probe_lo) < model_tol
                ? probe_r0
                : (fabs(t - probe_hi) < model_tol ? probe_r1 : (cylindrical ? probe_r0 : -1.0));
            if (want < 0) return false;
            if (fabs(distanceToAxis(vertices[v], probe_base, axis) - want) > 1e-7 * probe_scale) {
              return false;
            }
          }
          return true;
        };
        {
          std::size_t which = 0, which_top = 0;
          // The same order as the match below: a declared cone is the face, and
          // is named before two rims that only witness it.
          if (cylindrical) {
            if (declared_cylinder(probe_r0, axis, probe_base, &which)) named = which;
          } else if (declared_cone(probe_r0, probe_r1, axis, probe_base, probe_hi - probe_lo, &which)) {
            named = which;
          } else if (declared_cylinder(probe_r0, axis, probe_base, &which) &&
                     declared_cylinder(probe_r1, axis, probe_base, &which_top)) {
            named = std::max(which, which_top);  // as the match below names it
          }
        }
        walk_strip(seed, side, walls, entry, &on_surface);
        if (walls.size() < 3) {
          decline("fewer than three facets walk out on the surface - too short to be a band");
          continue;
        }
      }

      // every wall vertex has to sit on one of the two rims
      std::map<int, double> along;
      auto measure_along = [&]() {
        along.clear();
        for (const std::size_t f : walls) {
          for (const int v : loops[f]) along[v] = axis.dot(vertices[v]);
        }
      };
      measure_along();
      double lo = along.begin()->second, hi = lo;
      for (const auto& kv : along) {
        lo = std::min(lo, kv.second);
        hi = std::max(hi, kv.second);
      }
      if (hi - lo < model_tol) {
        decline("the walk has no height along the axis");
        continue;
      }

      auto count_at = [&](double level) {
        std::size_t n = 0;
        for (const auto& kv : along) {
          if (fabs(kv.second - level) < model_tol) n++;
        }
        return n;
      };

      // A band trimmed square at both ends has every vertex at one end or the
      // other. One trimmed at an angle has a full rim at one end and a spread
      // at the other, and everything below is written for that spread being
      // the *top*, so a band which came out the other way up is turned over.
      // The axis sign is arbitrary - axis_from only normalises it - so this
      // costs nothing but re-measuring.
      if (count_at(lo) < walls.size() && count_at(hi) > count_at(lo)) {
        axis = -axis;
        measure_along();
        const double was_lo = lo;
        lo = -hi;
        hi = -was_lo;
      }

      std::vector<int> bottom_set, top_set;
      bool tilted = false;
      for (const auto& kv : along) {
        if (fabs(kv.second - lo) < model_tol) bottom_set.push_back(kv.first);
        else if (fabs(kv.second - hi) < model_tol) top_set.push_back(kv.first);
        else tilted = true;
      }

      Vector3d top_normal(0, 0, 0);
      if (tilted) {
        // Everything off the flat rim has to lie on one plane, and that plane
        // has to cross the axis rather than run along it: a plane containing
        // the axis direction cuts a cylinder in straight lines, not an
        // ellipse, and there is no rim there to write.
        top_set.clear();
        for (const auto& kv : along) {
          if (fabs(kv.second - lo) >= model_tol) top_set.push_back(kv.first);
        }
        if (!fitPlane(vertices, top_set, model_tol, top_normal)) {
          decline("the far rim is neither at one height nor on one plane - no ring to write");
          continue;
        }
        if (top_normal.dot(axis) < 0) top_normal = -top_normal;
        if (top_normal.dot(axis) < 1e-3) {
          decline("the far rim's plane runs along the axis - no ring to write");
          continue;
        }
      }

      // Now that the walk is confined to one surface, the whole band has to
      // agree with the axis the seed's neighbourhood gave. This is the test
      // that used to run against the free walk, moved to the only set of
      // facets it can be asked of meaningfully - a seed whose neighbourhood
      // happens to give a wrong axis is still rejected here, just later.
      //
      // A tilted band has chords in the cut plane too, which are not
      // perpendicular to anything. Those are already known to be coplanar, so
      // what this has left to catch there is a chord running between the two
      // rims - a ruling the walk mistook for a chord, which is neither.
      if (!tilted) {
        if (!perpendicular_to(chords_of(walls, entry), axis)) {
          decline("a chord runs between the rims - the walls are not one ring");
          continue;
        }
      } else {
        bool chords_ok = true;
        for (const Vector3d& c : chords_of(walls, entry)) {
          if (fabs(c.dot(axis)) >= 1e-9 && fabs(c.dot(top_normal)) >= 1e-9) chords_ok = false;
        }
        if (!chords_ok) {
          decline("a chord runs between the rims - the walls are not one ring");
          continue;
        }
      }

      // A band which closes on itself has one rim vertex per facet; one which
      // stops short of a full turn has one more, the far end of the last
      // facet. Anything else is not a band around a common axis.
      const bool full_turn = bottom_set.size() == walls.size() && top_set.size() == walls.size();
      const bool part_turn = bottom_set.size() == walls.size() + 1 && top_set.size() == walls.size() + 1;
      if (!full_turn && !part_turn) {
        decline("the rims do not have one vertex per wall, or one more - not a ring a band can write");
        continue;
      }

      // A tilted rim which stops short of a full turn would need an elliptical
      // *arc*, and the arc machinery - which run of which loop the rim
      // replaces, which end is which - is written for circles. A full turn
      // needs none of that: it is one closed curve.
      if (tilted && !full_turn) {
        decline(
          "a tilted rim short of a full turn needs an elliptical arc, which a band has no form for");
        continue;
      }

      // Fit each rim on its own: the centroid of a full rim lies on the axis
      // but the centroid of an arc sits inside its chord, and the two rims of
      // a frustum have different radii anyway.
      Vector3d base, top_centre;
      if (!fitCircleCentre(vertices, bottom_set, axis, lo, base)) {
        decline("the near rim does not fit a circle");
        continue;
      }
      if (tilted) {
        // Where the axis pierces the cut plane. That is the ellipse's centre,
        // and it is not the centroid of the rim's vertices unless the
        // tessellation happens to be symmetric about it.
        Vector3d centroid(0, 0, 0);
        for (const int v : top_set) centroid += vertices[v];
        centroid /= double(top_set.size());
        top_centre = base + axis * (top_normal.dot(centroid - base) / top_normal.dot(axis));
      } else {
        if (!fitCircleCentre(vertices, top_set, axis, hi, top_centre)) {
          decline("the far rim does not fit a circle");
          continue;
        }
        if (distanceToAxis(top_centre, base, axis) > 1e-6) {  // coaxial
          decline("the two rims are not coaxial");
          continue;
        }
      }

      double r_bottom = 0, r_top = 0;
      for (const int v : bottom_set) r_bottom += distanceToAxis(vertices[v], base, axis);
      for (const int v : top_set) r_top += distanceToAxis(vertices[v], base, axis);
      r_bottom /= double(bottom_set.size());
      r_top /= double(top_set.size());
      const double scale = std::max(r_bottom, r_top);
      if (scale < model_tol) continue;
      double dev = 0;
      for (const int v : bottom_set) {
        dev = std::max(dev, fabs(distanceToAxis(vertices[v], base, axis) - r_bottom));
      }
      for (const int v : top_set) {
        dev = std::max(dev, fabs(distanceToAxis(vertices[v], base, axis) - r_top));
      }
      if (dev > 1e-7 * scale) {
        decline("a rim vertex is off its rim's circle");
        continue;
      }

      // Intent. A cylinder needs its own record; a frustum has none, because
      // the shape that produces one - hull() of two coaxial cylinders, the
      // standard chamfer - declares the two cylinders rather than the cone
      // between them. Both of its rims matching a declared cylinder is the
      // same statement of intent, made by two primitives instead of one.
      const bool is_cone = fabs(r_bottom - r_top) > 1e-9 * scale;
      std::size_t matched = 0;
      // A cone cut off-axis gives a conic whose rim has no single radius, and
      // the ellipse written below would be the wrong curve rather than an
      // imprecise one. Only a cylinder may be tilted.
      if (tilted && is_cone) {
        decline("a cone cut off-axis has no single-radius rim, and a band writes only those");
        continue;
      }
      if (is_cone) {
        // Either both rims match a declared cylinder - a hull() of two coaxial
        // cylinders declares those and never the cone between them, which is
        // what step-chamfered-cylinder asserts - or the model named the cone
        // itself. The second way exists because the first cannot state a cone
        // whose far rim is only where a boolean cut it, and asking that trim to
        // declare itself is asking the wrong thing of it.
        //
        // Where the model declared the cone, that is the face and the claim
        // names it, whatever the rims also match: a rim cylinder a frustum
        // declares is a *witness* - evidence offered to identify the cone - and
        // a claim naming it attributes the cone's facets to a surface they are
        // not on. It did, on step-split-surface, step-bored-cone and
        // step-cap-meets-wall, until the D0 regions report caught it
        // (2026-09-24). The rims name the claim only where no cone is declared -
        // hull() of two cylinders, which declares no blend; that is a missing
        // declaration, and still misattributed.
        std::size_t which_bottom = 0, which_top = 0, which_cone = 0;
        const bool by_rims = declared_cylinder(r_bottom, axis, base, &which_bottom) &&
                             declared_cylinder(r_top, axis, base, &which_top);
        const bool by_cone = declared_cone(r_bottom, r_top, axis, base, hi - lo, &which_cone);
        if (!by_rims && !by_cone) {
          decline("the rims fitted over the whole walk match no declaration");
          continue;
        }
        matched = by_cone ? which_cone : std::max(which_bottom, which_top);
        // Everything read to identify this cone is evidence, and only one record
        // can be `matched`. Record them all, or a census cannot tell a
        // declaration doing its job from one nothing used - see Result::evidence.
        if (by_rims) {
          note_evidence(which_bottom);
          note_evidence(which_top);
        }
        if (by_cone) note_evidence(which_cone);
      } else if (!declared_cylinder(r_bottom, axis, base, &matched)) {
        decline("the rims fitted over the whole walk match no declaration");
        continue;
      } else {
        note_evidence(matched);
      }

      // A diagnostic, and the only way to obtain the *other* representation of a
      // surface: skip the band on the declared surface named by
      // OPENSCAD_STEP_YIELD_SURFACE, so the general pass covers its whole
      // region instead. Exporting a model twice, with and without it, is how
      // representation-independence is tested - the same solid written either
      // way must enclose the same volume - and it is what
      // tests/step-representation-check.py drives. It changes nothing unless
      // the variable is set. See doc/step-recognition-design.md, *region before
      // representation*.
      if (const char *yield_to_general = getenv("OPENSCAD_STEP_YIELD_SURFACE")) {
        if (matched == std::size_t(atoi(yield_to_general))) {
          decline("yielded to the general pass by OPENSCAD_STEP_YIELD_SURFACE");
          continue;
        }
      }

      Band info;
      // Which record this band answers to, so that a caller can tell a band on
      // the model's own surface from one on a surface the approximation fitted.
      // The declarations are the head of the list the caller passes; anything
      // past `declared_count` is a fit.
      info.on_declared = matched < declared_count;
      info.walls = walls;
      info.axis = axis;
      info.base = base;
      info.r_bottom = r_bottom;
      info.r_top = r_top;
      info.height = tilted ? axis.dot(top_centre - base) : hi - lo;
      info.closed = full_turn;
      info.bottom_set = bottom_set;
      info.top_set = top_set;
      info.top_tilted = tilted;
      info.top_normal = top_normal;

      const Vector3d probe = vertices[loops[walls[0]][0]];
      const Vector3d radial = (probe - base) - axis * axis.dot(probe - base);
      info.outward = radial.normalized().dot(loop_normals[walls[0]]) > 0;

      for (const std::size_t f : walls) {
        consumed[f] = 1;
        result.consumed_by[f] = "a band's wall";
        // D0 on a declaration: every corner is on the surface to 1e-7, the rims
        // match the declaration to 1e-7, and no band or statistic takes part.
        // That is membership only; *which* facets is still what a band can
        // express, which is R2 and D4, and the declines above are its record.
        recognition::recordClaim(result.ledger, recognition::kBandWall, f, surfaces[matched], matched,
                                 declared_count, ClaimFate::Honoured,
                                 "the facet walks out from the band's seed");
        band_of_loop[f] = bands.size();
      }
      bands.push_back(info);
      break;
    }
  }

  // ---- what each rim borders -------------------------------------------
  //
  // A band whose rim cannot be resolved is dropped, which can leave a
  // neighbour's shared rim unresolvable in turn, so this runs to a fixed
  // point. Dropping is monotone, so it terminates.
  rims.assign(bands.size(), {RimRef(), RimRef()});

  auto rim_edges = [&](std::size_t bi, bool bottom) {
    const Band& band = bands[bi];
    const std::vector<int>& level_v = bottom ? band.bottom_set : band.top_set;
    const std::set<int> level(level_v.begin(), level_v.end());
    std::set<std::pair<int, int>> out;
    for (const std::size_t f : band.walls) {
      const auto& loop = loops[f];
      for (std::size_t j = 0; j < loop.size(); j++) {
        const int a = loop[j], b = loop[(j + 1) % loop.size()];
        if (level.count(a) && level.count(b)) out.insert(edge_key(a, b));
      }
    }
    return out;
  };

  // The two ends of a rim that stops short of a full turn, counter clockwise
  // about the axis.
  //
  // They come out of the rim's own edges - the two vertices used by one of them
  // rather than two - and not out of whatever lies on the far side. That is
  // what lets a rim shared between two bands be handled at all, since there is
  // no neighbouring loop there to index into, and it keeps one code path for
  // both cases rather than two that can drift apart.
  //
  // Ordering them by angle would put the branch cut of atan2 in the way; the
  // sign of one cross product of two *adjacent* rim vertices does not, because
  // adjacent rim vertices are a whole facet apart.
  auto rim_ends = [&](std::size_t bi, bool bottom, int& ccw_start, int& ccw_end) {
    const Band& band = bands[bi];
    const auto edges = rim_edges(bi, bottom);
    std::map<int, std::vector<int>> adjacent;
    for (const auto& edge : edges) {
      adjacent[edge.first].push_back(edge.second);
      adjacent[edge.second].push_back(edge.first);
    }
    std::vector<int> ends;
    for (const auto& kv : adjacent) {
      if (kv.second.size() == 1) ends.push_back(kv.first);
    }
    if (ends.size() != 2) return false;

    const Vector3d centre = bottom ? band.base : band.base + band.axis * band.height;
    const Vector3d va = vertices[ends[0]] - centre;
    const Vector3d vb = vertices[adjacent[ends[0]].front()] - centre;
    const bool first_is_start = band.axis.dot(va.cross(vb)) > 0;
    ccw_start = ends[first_is_start ? 0 : 1];
    ccw_end = ends[first_is_start ? 1 : 0];
    return true;
  };

  // The direction the wall facets traverse a rim edge is the direction the
  // collapsed face has to traverse the whole rim: the face replaces those
  // facets, so its boundary is theirs.
  auto wall_runs_ccw = [&](std::size_t bi, const std::pair<int, int>& edge) {
    const Band& band = bands[bi];
    for (const std::size_t f : band.walls) {
      const auto& loop = loops[f];
      for (std::size_t j = 0; j < loop.size(); j++) {
        const int a = loop[j], b = loop[(j + 1) % loop.size()];
        if (edge_key(a, b) != edge) continue;
        const Vector3d va = vertices[a] - band.base;
        const Vector3d vb = vertices[b] - band.base;
        return band.axis.dot(va.cross(vb)) > 0;
      }
    }
    return true;
  };

  auto resolve_rim = [&](std::size_t bi, bool bottom, RimRef& out, const char **why) {
    const Band& band = bands[bi];
    const auto edges = rim_edges(bi, bottom);
    if (edges.empty()) {
      *why = "no rim edges";
      return false;
    }
    const std::set<std::size_t> in_band(band.walls.begin(), band.walls.end());

    std::set<std::size_t> others;
    for (const auto& edge : edges) {
      const auto it = loop_edges_map.find(edge);
      if (it == loop_edges_map.end()) {
        *why = "a rim edge belongs to no loop";
        return false;
      }
      std::size_t outside = face_cnt;
      int count = 0;
      for (const std::size_t user : it->second) {
        if (in_band.count(user)) continue;
        count++;
        outside = user;
      }
      if (count != 1) {
        *why = "a rim edge is used by more than two faces";
        return false;
      }
      others.insert(outside);
    }

    out.wall_ccw = wall_runs_ccw(bi, *edges.begin());

    if (others.size() == 1) {
      const std::size_t nb = *others.begin();
      if (band_of_loop[nb] != NO_BAND) {
        *why = "the rim borders a single facet of another band";
        return false;
      }  // a one facet band
      if (!loop_valid[nb] || consumed[nb]) {
        *why = "the neighbouring face was dropped";
        return false;
      }
      const std::vector<int>& nb_loop = loops[nb];
      const std::set<int> key(nb_loop.begin(), nb_loop.end());
      if (key.size() == nb_loop.size() && edges.size() == nb_loop.size()) {
        out.kind = RimRef::WHOLE_LOOP;
        out.loop = nb;
        return true;
      }
      // a run inside the loop, which an arc can replace only when its edges
      // are consecutive there
      const std::size_t n = nb_loop.size();
      std::vector<char> on_rim(n, 0);
      std::size_t cnt = 0;
      for (std::size_t j = 0; j < n; j++) {
        if (edges.count(edge_key(nb_loop[j], nb_loop[(j + 1) % n])) == 0) continue;
        on_rim[j] = 1;
        cnt++;
      }
      if (cnt != edges.size() || cnt >= n) {
        *why = "the rim is not a run of its neighbour's edges";
        return false;
      }
      std::size_t start = n;
      for (std::size_t j = 0; j < n; j++) {
        if (on_rim[j] == 0 || on_rim[(j + n - 1) % n] != 0) continue;
        if (start != n) {
          *why = "the rim is split across its neighbour's loop";
          return false;
        }
        start = j;
      }
      if (start == n) {
        *why = "the rim covers its neighbour's whole loop twice";
        return false;
      }
      out.kind = RimRef::LOOP_RUN;
      out.loop = nb;
      out.start = start;
      out.count = cnt;
      return true;
    }

    // shared with another band, which has to be collapsed too
    std::set<std::size_t> nb_bands;
    for (const std::size_t f : others) nb_bands.insert(band_of_loop[f]);
    if (nb_bands.size() != 1 || *nb_bands.begin() == NO_BAND) {
      *why = "the rim borders one face per facet";
      return false;
    }
    const std::size_t other = *nb_bands.begin();
    if (!bands[other].alive) {
      *why = "the band sharing this rim was dropped";
      return false;
    }
    if (band.closed != bands[other].closed) {
      *why = "a shared rim needs both bands to be the same shape";
      return false;
    }

    if (!band.closed) {
      // Two partial bands, so the shared rim is an arc rather than a circle -
      // a bayonet lug is a wall on a chamfer on a wall and none of the three
      // goes all the way round, so every joint in one is this case.
      //
      // It is the same substitution the closed case makes, and safe under the
      // same condition strengthened: the two bands have to meet along the
      // *whole* of the rim. If either had rim edges the other lacked, the arc
      // would have to be split on one side and not the other, and the two
      // faces could no longer share one edge.
      for (const bool other_bottom : {true, false}) {
        if (rim_edges(other, other_bottom) != edges) continue;
        out.kind = RimRef::OTHER_BAND_ARC;
        out.band = other;
        return true;
      }
      *why = "the two partial bands share only part of the rim";
      return false;
    }

    if (others.size() != bands[other].walls.size()) {
      *why = "the shared rim does not cover the whole neighbouring band";
      return false;
    }
    out.kind = RimRef::OTHER_BAND;
    out.band = other;
    return true;
  };

  // The end edges of a partial band have to be edges the mesh already has.
  //
  // The face closes by running along the bottom rim, up one end, back along
  // the top rim and down the other, so the two ends it needs are "where the
  // bottom traversal finishes to where the top traversal starts" and the
  // reverse. If those are not edges of the mesh the face would be closed with
  // a diagonal that is not an edge at all, which opens the shell against every
  // face that shares the real one.
  auto ends_line_up = [&](const RimRef& bottom, const RimRef& top) {
    return loop_edges_map.count(edge_key(bottom.traversalEnd(), top.traversalStart())) != 0 &&
           loop_edges_map.count(edge_key(top.traversalEnd(), bottom.traversalStart())) != 0;
  };

  // Two bands must not rewrite the same planar loop, or the same run of it.
  for (bool changed = true; changed;) {
    changed = false;
    std::set<std::size_t> whole_taken;
    std::map<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>> runs_taken;

    for (std::size_t i = 0; i < bands.size(); i++) {
      if (!bands[i].alive) continue;
      RimRef bottom, top;
      const char *why = "unresolved";
      if (!resolve_rim(i, true, bottom, &why) || !resolve_rim(i, false, top, &why)) {
        bands[i].alive = false;
        bands[i].dropped = why;
        changed = true;
        continue;
      }

      // A full turn collapses each rim into a closed circle, which can only
      // replace a whole loop or the matching rim of another closed band; a
      // partial band collapses each rim into an arc, which either replaces a
      // run of a loop or is shared with another partial band. Anything else
      // would put a closed circle in the middle of a loop, or an arc where a
      // whole bound was wanted.
      auto is_arc = [](const RimRef& rim) {
        return rim.kind == RimRef::LOOP_RUN || rim.kind == RimRef::OTHER_BAND_ARC;
      };
      const bool shapes_ok =
        bands[i].closed ? (!is_arc(bottom) && !is_arc(top)) : (is_arc(bottom) && is_arc(top));
      if (!shapes_ok) {
        bands[i].alive = false;
        bands[i].dropped = "a rim is an arc, but the band covers the full turn";
        changed = true;
        continue;
      }

      // Both ends of a partial band's rims, taken from the rims themselves.
      if (!bands[i].closed && !(rim_ends(i, true, bottom.ccw_start, bottom.ccw_end) &&
                                rim_ends(i, false, top.ccw_start, top.ccw_end))) {
        bands[i].alive = false;
        bands[i].dropped = "a rim of the band has no two ends";
        changed = true;
        continue;
      }

      // The two ends of a partial band are ordinary edges of the mesh. If the
      // runs do not line up - the vertex ending one rim's run sitting on the
      // same ruling as the vertex starting the other's - the face would be
      // closed with a diagonal that is not an edge at all, which opens the
      // shell against every face that shares the real one.
      if (!bands[i].closed && !ends_line_up(bottom, top)) {
        bands[i].alive = false;
        bands[i].dropped = "the two rims of the band do not end on the same rulings";
        changed = true;
        continue;
      }

      bool clash = false;
      for (const RimRef *rim : {&bottom, &top}) {
        if (rim->kind == RimRef::WHOLE_LOOP) {
          if (!whole_taken.insert(rim->loop).second) clash = true;
        } else if (rim->kind == RimRef::LOOP_RUN) {
          const std::size_t n = loops[rim->loop].size();
          for (const auto& taken : runs_taken[rim->loop]) {
            for (std::size_t a = 0; a < rim->count && !clash; a++) {
              for (std::size_t b = 0; b < taken.second; b++) {
                if ((rim->start + a) % n == (taken.first + b) % n) clash = true;
              }
            }
          }
          runs_taken[rim->loop].push_back({rim->start, rim->count});
        }
      }
      if (clash) {
        bands[i].alive = false;
        bands[i].dropped = "another band already rewrites the same loop";
        changed = true;
        continue;
      }
      rims[i] = {bottom, top};
    }
  }

  // A periodic face needs a seam, and the seam has to be a ruling: both of
  // its ends on the same radial direction, or the line would cut through the
  // surface instead of lying on it.
  //
  // Picking each end independently by angle does not work, however obvious it
  // looks. atan2 has its branch cut at pi, a polygon with an even number of
  // facets has a vertex sitting exactly there, and which side of the cut it
  // lands on is decided by the sign of a y coordinate which is zero to
  // fifteen digits. Two rims of one wall disagreed on that sign, their seams
  // came out on different rulings, and a cylinder that was otherwise perfect
  // was dropped.
  //
  // So only one end is chosen, and the other is *derived* from it. Where two
  // bands share a rim they have to use the same vertex - the circle between
  // them is one edge - so a band takes whichever of its rims is already
  // settled and derives the other; a single pass suffices, because a band
  // which finds neither settled settles both.
  std::map<std::set<int>, int> rim_seam;

  auto vertex_on_ruling = [&](int from, const std::vector<int>& level, const Vector3d& axis,
                              const Vector3d& centre) {
    const Vector3d a = vertices[from] - centre;
    const Vector3d ra = (a - axis * axis.dot(a)).normalized();
    for (const int v : level) {
      const Vector3d b = vertices[v] - centre;
      const Vector3d rb = (b - axis * axis.dot(b)).normalized();
      if ((ra - rb).norm() < 1e-6) return v;
    }
    return -1;
  };

  for (std::size_t i = 0; i < bands.size(); i++) {
    Band& band = bands[i];
    if (!band.alive || !band.closed) continue;
    const std::set<int> bottom_key(band.bottom_set.begin(), band.bottom_set.end());
    const std::set<int> top_key(band.top_set.begin(), band.top_set.end());
    const Vector3d top_centre = band.base + band.axis * band.height;

    const auto settled_bottom = rim_seam.find(bottom_key);
    const auto settled_top = rim_seam.find(top_key);
    if (settled_bottom != rim_seam.end()) {
      band.seam_bottom = settled_bottom->second;
      band.seam_top = vertex_on_ruling(band.seam_bottom, band.top_set, band.axis, band.base);
    } else if (settled_top != rim_seam.end()) {
      band.seam_top = settled_top->second;
      band.seam_bottom = vertex_on_ruling(band.seam_top, band.bottom_set, band.axis, top_centre);
    } else {
      band.seam_bottom = band.bottom_set.front();
      band.seam_top = vertex_on_ruling(band.seam_bottom, band.top_set, band.axis, band.base);
    }

    if (band.seam_bottom == -1 || band.seam_top == -1) {
      band.alive = false;
      band.dropped = "the two rims have no ruling in common to run a seam along";
      continue;
    }
    rim_seam[bottom_key] = band.seam_bottom;
    rim_seam[top_key] = band.seam_top;
  }

  // dropping a band puts its facets back
  for (std::size_t i = 0; i < bands.size(); i++) {
    if (bands[i].alive) continue;
    for (const std::size_t f : bands[i].walls) {
      consumed[f] = 0;
      result.consumed_by[f] = nullptr;
      result.ledger.release(f);
      band_of_loop[f] = NO_BAND;
    }
  }

  // ---- merge a run of bands lying on one declared sphere or torus -------
  //
  // A sphere is not a grid to be grown. Every ring of its tessellation is
  // already a frustum whose rims are circles, so the zone is the maximal run of
  // bands joined at shared rims whose vertices all lie on one declared sphere -
  // which means the band pass has done the work and this only has to join up
  // its answer. The merged band keeps the run's outer rims, so the rules that
  // were resolved for the end bands still hold, and the flat cap at either end
  // is untouched.
  //
  // The alternative, flooding across every edge into any face whose vertices
  // are on the sphere, does not work: an OpenSCAD sphere is a closed polyhedron
  // inscribed in the sphere and its caps have every vertex on the surface too,
  // with the same sag as any other facet. There is no local geometric test that
  // separates a cap from a ring quad, because geometrically there is nothing to
  // separate - only the structure says which is which.
  {
    // which live bands meet at each rim, keyed by the rim's vertex set
    std::map<std::set<int>, std::vector<std::size_t>> at_rim;
    for (std::size_t i = 0; i < bands.size(); i++) {
      if (!bands[i].alive || !bands[i].closed) continue;
      for (const bool bottom : {true, false}) {
        const std::vector<int>& level = bottom ? bands[i].bottom_set : bands[i].top_set;
        at_rim[std::set<int>(level.begin(), level.end())].push_back(i);
      }
    }

    // Every vertex of the band on the surface, to a tolerance relative to the
    // surface's own size. A sphere and a torus are the two zone shapes: for the
    // torus the distance measured is to the tube's centre circle, which is what
    // a torus is.
    auto on_zone = [&](const Surface *zone, std::size_t bi) {
      const auto *sph = dynamic_cast<const SphereSurface *>(zone);
      const auto *tor = dynamic_cast<const TorusSurface *>(zone);
      if (sph == nullptr && tor == nullptr) return false;
      for (const std::size_t f : bands[bi].walls) {
        for (const int v : loops[f]) {
          // On the declared zone exactly - machine precision, see
          // recognition/Exact.h; a relative 1e-7 until 2026-09-24.
          if (!recognition::onExactly(zone, vertices[v])) return false;
        }
      }
      return true;
    };

    std::vector<char> absorbed(bands.size(), 0);
    for (const auto& surface : surfaces) {
      const bool is_torus = dynamic_cast<const TorusSurface *>(surface.get()) != nullptr;
      if (dynamic_cast<const SphereSurface *>(surface.get()) == nullptr && !is_torus) continue;

      for (std::size_t seed = 0; seed < bands.size(); seed++) {
        if (!bands[seed].alive || absorbed[seed] || bands[seed].zone != nullptr) continue;
        if (!bands[seed].closed || !on_zone(surface.get(), seed)) continue;

        // walk the run outwards from the seed, one rim at a time
        std::vector<std::size_t> run{seed};
        for (const bool up : {false, true}) {
          std::size_t cur = seed;
          for (;;) {
            const std::vector<int>& level = up ? bands[cur].top_set : bands[cur].bottom_set;
            const auto it = at_rim.find(std::set<int>(level.begin(), level.end()));
            if (it == at_rim.end() || it->second.size() != 2) break;
            const std::size_t next = it->second[0] == cur ? it->second[1] : it->second[0];
            if (absorbed[next] || next == seed) break;
            if (std::find(run.begin(), run.end(), next) != run.end()) break;  // closed on itself
            if (!on_zone(surface.get(), next)) break;
            run.push_back(next);
            absorbed[next] = 1;
            cur = next;
          }
        }
        if (run.size() < 2) continue;

        // the ends of the run are the bands with a rim no other band in it uses
        std::map<std::set<int>, int> uses;
        for (const std::size_t bi : run) {
          for (const bool bottom : {true, false}) {
            const std::vector<int>& level = bottom ? bands[bi].bottom_set : bands[bi].top_set;
            uses[std::set<int>(level.begin(), level.end())]++;
          }
        }
        std::size_t low = bands.size(), high = bands.size();
        for (const std::size_t bi : run) {
          const std::set<int> b(bands[bi].bottom_set.begin(), bands[bi].bottom_set.end());
          const std::set<int> t(bands[bi].top_set.begin(), bands[bi].top_set.end());
          if (uses[b] == 1) low = bi;
          if (uses[t] == 1) high = bi;
        }
        if (low == bands.size() || high == bands.size()) {
          // No free rim at either end: the run closes on itself, which is a
          // torus, and a *complete* one - the pass below writes it as a face
          // bounded by two seams and nothing else.
          for (const std::size_t bi : run) absorbed[bi] = 0;
          continue;
        }

        // For a torus, an end of the run has to be an end of the *surface* and
        // not merely of the walk. A torus's profile turns around at its widest
        // and narrowest points, and the two bands meeting there meet top to top,
        // so a walk that always follows the top rim turns back at the turnaround
        // and stops - leaving a run whose ends look free because the band beyond
        // each of them is not in the run. Merging that would break one complete
        // torus into fragments.
        //
        // What separates the two cases is not whether the end rim is shared -
        // the end rim of a rounded corner is shared with the wall it runs into,
        // and that is the ordinary case - but *what it is shared with*. A band
        // across the rim which lies on this same torus is a turnaround; anything
        // else is a real end.
        if (is_torus) {
          bool turnaround = false;
          for (const auto& end : {std::make_pair(low, true), std::make_pair(high, false)}) {
            const std::vector<int>& level =
              end.second ? bands[end.first].bottom_set : bands[end.first].top_set;
            const auto it = at_rim.find(std::set<int>(level.begin(), level.end()));
            if (it == at_rim.end()) continue;
            for (const std::size_t other : it->second) {
              if (other != end.first && on_zone(surface.get(), other)) turnaround = true;
            }
          }
          if (turnaround) {
            for (const std::size_t bi : run) absorbed[bi] = 0;
            continue;
          }
        }

        std::vector<std::size_t> walls;
        for (const std::size_t bi : run) {
          walls.insert(walls.end(), bands[bi].walls.begin(), bands[bi].walls.end());
        }

        // Read both ends *before* writing anything. The merged band is one of
        // the run - it has to be, or its facets would be put back - so the
        // reference below may alias bands[low] or bands[high], and assigning
        // through it would change what the other end still has to be read from.
        // Taken the other way round the top rim's centre came out at the second
        // ring rather than the last, its reference direction tilted 87 degrees
        // out of the rim's plane, and the flat cap bounded by it stopped being
        // flat.
        const Vector3d base = bands[low].base;
        const Vector3d top_centre = bands[high].base + bands[seed].axis * bands[high].height;

        // ---- does the run reach both poles? --------------------------------
        //
        // A sphere carries no latitude bound - `sphere()` means the whole
        // sphere, and a fitted one is the sphere its rings lie on, neither of
        // them bounded - but the mesh ends in a flat disc at either end,
        // because OpenSCAD's tessellation puts its outermost ring half a ring
        // step short of the axis instead of converging to a pole vertex. Those
        // discs are the tessellation's, not the model's. Written as they stand
        // they export a sphere with its poles sliced off: 523.5806 where a
        // radius 5 sphere has 523.5988, which is 0.0035% and a disagreement
        // with (4/3)pi r^3 in the fourth figure.
        //
        // So the run absorbs them - but only once the gap they span is the one
        // the tessellation leaves rather than one a boolean cut. The test is
        // angular and measured against the run's own bands: a cap subtends half
        // of one ring step, so a gap of a whole step or more means a ring is
        // missing and something has taken a piece out of this sphere. A cut
        // lands where the modeller put it, and anywhere is far outside half a
        // step at any $fn worth exporting.
        //
        // The one cut this cannot see is one exactly at the last ring: it
        // removes no facet, so the mesh it leaves is identical to the uncut
        // sphere's and nothing downstream of the mesh can tell them apart.
        //
        // Both ends or neither, which is a deliberate boundary rather than an
        // oversight. A sphere with something standing on one pole - a knob
        // welded to the top - has a real polar cap at the other end that could
        // be closed on its own, but the face that would take it is a shape this
        // writer does not have: one rim, and a seam running from that rim to a
        // degenerate pole. Refusing leaves such a model exporting exactly as it
        // did before, which is the safe direction to be wrong in.
        bool pole_closed = false;
        std::size_t cap_low = 0, cap_high = 0;
        if (const auto *sph = dynamic_cast<const SphereSurface *>(surface.get())) {
          const Vector3d axis = bands[seed].axis;
          // the angle a point subtends from the pole the axis points at
          auto polar = [&](const Vector3d& p) {
            const double c = (p - sph->refpt).dot(axis) / sph->r;
            return acos(std::max(-1.0, std::min(1.0, c)));
          };
          const RimRef& lo_rim = rims[low].first;
          const RimRef& hi_rim = rims[high].second;
          // Each end rim has to be the whole bound of one neighbouring face,
          // which is what a cap is: a rim bordering many faces is a junction
          // with something else and the sphere does not end there.
          if (lo_rim.kind == RimRef::WHOLE_LOOP && hi_rim.kind == RimRef::WHOLE_LOOP &&
              lo_rim.loop < loops.size() && hi_rim.loop < loops.size()) {
            const double gap_low = M_PI - polar(base);
            const double gap_high = polar(top_centre);
            const double step_low =
              fabs(polar(bands[low].base) - polar(bands[low].base + axis * bands[low].height));
            const double step_high =
              fabs(polar(bands[high].base) - polar(bands[high].base + axis * bands[high].height));
            // and the caps have to be flat discs across the axis rather than
            // whatever else a single neighbouring face might be
            const auto across = [&](std::size_t f) {
              return fabs(fabs(loop_normals[f].normalized().dot(axis)) - 1.0) <= 1e-6;
            };
            if (gap_low <= step_low && gap_high <= step_high && step_low > 0 && step_high > 0 &&
                across(lo_rim.loop) && across(hi_rim.loop)) {
              pole_closed = true;
              cap_low = lo_rim.loop;
              cap_high = hi_rim.loop;
              walls.push_back(cap_low);
              walls.push_back(cap_high);
            }
          }
        }
        const std::vector<int> bottom_set = bands[low].bottom_set;
        const std::vector<int> top_set = bands[high].top_set;
        const double r_bottom = bands[low].r_bottom;
        const double r_top = bands[high].r_top;
        const int seam_bottom = bands[low].seam_bottom;
        const int seam_top = bands[high].seam_top;
        const bool outward = bands[low].outward;
        // keep the run's outer rims, and with them the rules already resolved
        const std::pair<RimRef, RimRef> ends{rims[low].first, rims[high].second};

        Band& merged = bands[seed];
        merged.walls = walls;
        merged.bottom_set = bottom_set;
        merged.top_set = top_set;
        merged.base = base;
        // stated as the two centres rather than as a sum of heights, so that it
        // stays right however the run was ordered
        merged.height = merged.axis.dot(top_centre - base);
        merged.r_bottom = r_bottom;
        merged.r_top = r_top;
        merged.seam_bottom = seam_bottom;
        merged.seam_top = seam_top;
        merged.outward = outward;
        merged.zone = surface;
        merged.pole_closed = pole_closed;
        // The caps are the face's now, so they stop being faces of their own.
        // Their rims go with them: the closed sphere has no rim at all, and the
        // circles that used to bound the run's ends are not written.
        if (pole_closed) {
          consumed[cap_low] = 1;
          consumed[cap_high] = 1;
          result.consumed_by[cap_low] = "a band's cap";
          result.consumed_by[cap_high] = "a band's cap";
          // The caps are part of the zone's face now, so the claim is *on the
          // zone* and says so. Left unnamed, these two were honoured claims
          // with `surface_index` at its `size_t(-1)` default, which anything
          // reading the ledger cannot tell from a claim on a fitted surface:
          // measured 2026-09-22, the declaration census read step-sphere's two
          // polar caps as "written on a surface the approximation fitted" at
          // the *exact* tier, where nothing can be fitted at all.
          const std::size_t zone_index = index_of(surface);
          // D4: a cap is absorbed because the gap it spans is under one ring
          // step of this tessellation - the tessellation deciding, however
          // exact the sphere it closes.
          for (const std::size_t cap : {cap_low, cap_high}) {
            recognition::recordClaim(result.ledger, recognition::kBandCap, cap, surface, zone_index,
                                     declared_count, ClaimFate::Honoured, "the loop closing the band");
          }
        }
        rims[seed] = ends;
        for (const std::size_t bi : run) {
          if (bi == seed) continue;
          bands[bi].alive = false;
          bands[bi].dropped = nullptr;  // absorbed, not rejected: keep its facets
          absorbed[bi] = 1;
        }
        for (const std::size_t f : walls) band_of_loop[f] = seed;
        absorbed[seed] = 0;
      }
    }
  }

  // ---- merge a run of bands which closes on itself, on a declared torus ----
  //
  // The same observation as for a sphere: a torus is a stack of bands, one per
  // profile edge, and the zone is the run of them joined at shared rims. The
  // difference is only that the run has no ends - every rim is shared - so
  // there is nothing to keep, and the face is bounded by its own two seams
  // instead. That is why it needs a declaration of its own rather than falling
  // out of the ring circles: those already collapse a torus into a stack of
  // exact cones, and only a TorusSurface says the stack was one surface.
  {
    std::vector<char> absorbed(bands.size(), 0);
    for (const auto& surface : surfaces) {
      const auto *tor = dynamic_cast<const TorusSurface *>(surface.get());
      if (tor == nullptr) continue;

      auto on_torus = [&](std::size_t bi) {
        for (const std::size_t f : bands[bi].walls) {
          for (const int v : loops[f]) {
            if (!recognition::onExactly(tor, vertices[v])) return false;  // see recognition/Exact.h
          }
        }
        return true;
      };

      std::map<std::set<int>, std::vector<std::size_t>> at_rim;
      for (std::size_t i = 0; i < bands.size(); i++) {
        if (!bands[i].alive || !bands[i].closed || bands[i].zone != nullptr) continue;
        if (!on_torus(i)) continue;
        for (const bool bottom : {true, false}) {
          const std::vector<int>& level = bottom ? bands[i].bottom_set : bands[i].top_set;
          at_rim[std::set<int>(level.begin(), level.end())].push_back(i);
        }
      }

      for (std::size_t seed = 0; seed < bands.size(); seed++) {
        if (!bands[seed].alive || absorbed[seed] || bands[seed].zone != nullptr) continue;
        if (!bands[seed].closed || !on_torus(seed)) continue;

        // Walk the ring until it comes back to the seed, leaving each band by
        // the rim it was not entered by.
        //
        // Not by its top rim, which is what the sphere pass does. A sphere's
        // bands stack monotonically along the axis, so every rim is one band's
        // top and the next one's bottom. A torus's profile turns around at its
        // widest and narrowest points, and the two bands meeting there meet top
        // to top: a walk which always follows the top rim comes straight back
        // to where it started and the run stops two bands long.
        std::vector<std::size_t> run{seed};
        std::size_t cur = seed;
        std::set<int> came_by(bands[seed].bottom_set.begin(), bands[seed].bottom_set.end());
        bool cyclic = false;
        for (;;) {
          const std::set<int> bottom(bands[cur].bottom_set.begin(), bands[cur].bottom_set.end());
          const std::set<int> top(bands[cur].top_set.begin(), bands[cur].top_set.end());
          const std::set<int>& level = bottom == came_by ? top : bottom;
          const auto it = at_rim.find(level);
          if (it == at_rim.end() || it->second.size() != 2) break;
          const std::size_t next = it->second[0] == cur ? it->second[1] : it->second[0];
          if (next == seed) {
            cyclic = true;
            break;
          }
          if (absorbed[next] || std::find(run.begin(), run.end(), next) != run.end()) break;
          run.push_back(next);
          came_by = level;
          cur = next;
        }
        if (!cyclic || run.size() < 3) continue;

        std::vector<std::size_t> walls;
        for (const std::size_t bi : run) {
          walls.insert(walls.end(), bands[bi].walls.begin(), bands[bi].walls.end());
        }

        // A torus face is bounded by nothing but its own two seams, so the only
        // thing the emitter needs from the mesh is one vertex where they cross.
        // Everything else - both circles, their centres, their radii - comes
        // out of the record.
        const int corner = bands[seed].bottom_set.front();

        // Which way the face looks: away from the tube's centre circle, which
        // for the inner half of a torus is the opposite of away from the axis.
        const Vector3d probe = vertices[loops[bands[seed].walls[0]][0]];
        const Vector3d rel = probe - tor->refpt;
        const double along = rel.dot(tor->normdir);
        const Vector3d radial = rel - tor->normdir * along;
        const Vector3d tube = tor->refpt + radial.normalized() * tor->r_major;
        const bool outward = (probe - tube).normalized().dot(loop_normals[bands[seed].walls[0]]) > 0;

        Band& merged = bands[seed];
        merged.walls = walls;
        merged.seam_bottom = corner;
        merged.seam_top = corner;
        merged.outward = outward;
        merged.zone = surface;
        for (const std::size_t bi : run) {
          if (bi == seed) continue;
          bands[bi].alive = false;
          bands[bi].dropped = nullptr;  // absorbed, not rejected: keep its facets
          absorbed[bi] = 1;
        }
        for (const std::size_t f : walls) band_of_loop[f] = seed;
      }
    }
  }

  std::size_t collapsed = 0, alive = 0, cones = 0, partial = 0, spheres = 0, tori = 0;
  for (const auto& band : bands) {
    if (!band.alive) continue;
    alive++;
    collapsed += band.walls.size();
    if (dynamic_cast<const TorusSurface *>(band.zone.get()) != nullptr) tori++;
    else if (band.zone != nullptr) spheres++;
    else if (band.isCone()) cones++;
    if (!band.closed) partial++;
  }
  if (alive > 0) {
    result.report.push_back(
      format("%d surface%s recognised (%d toroidal, %d spherical, %d conical, %d partial), "
             "%d facets replaced",
             int(alive), alive == 1 ? "" : "s", int(tori), int(spheres), int(cones), int(partial),
             int(collapsed)));
  }
  // Every band here fits its axis exactly and was declared by the model, so a
  // drop is always the topology around it rather than the surface itself.
  // Naming the rule that rejected it is the only way to tell a wall which
  // cannot be written from one which should have been.
  for (const auto& band : bands) {
    if (band.alive || band.dropped == nullptr) continue;
    result.report.push_back(format("r=%g band of %d facets left faceted: %s", band.r_bottom,
                                   int(band.walls.size()), band.dropped));
  }

  // The refusals, once each. See `declined` above.
  for (const auto& entry : declined) {
    const std::size_t f = entry.first.first, si = entry.first.second;
    if (result.ledger.has(f, recognition::kBandWall.by, si)) continue;
    recognition::recordClaim(result.ledger, recognition::kBandWall, f, surfaces[si], si, declared_count,
                             ClaimFate::Declined, entry.second);
  }

  return result;
}

}  // namespace AnalyticFeatures
