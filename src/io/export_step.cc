/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2021      Konstantin Podsvirov <konstantin@podsvirov.pro>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "export.h"
#include "Feature.h"
#include "StepKernel.h"
#include "src/geometry/PolySet.h"
#include "src/geometry/cgal/cgalutils.h"
#include "src/geometry/PolySetUtils.h"
#include <unordered_map>
#include "src/utils/boost-utils.h"
#include <src/utils/hash.h>
#include <src/geometry/PolySetUtils.h>
#include <src/geometry/GeometryEvaluator.h>
#include <src/geometry/Surface.h>
#include <set>
#include <limits>
#include <climits>
#include <algorithm>

namespace {

/*! What the mesh knows about where it came from, said out loud and used for
 * nothing else yet.
 *
 * Every gate in this exporter that decides what a facet belongs to decides it
 * by distance - is this vertex near a declared point, is this facet inside the
 * tessellation band. Provenance answers a different question, *which solid was
 * this cut out of*, and it is the question those gates are really asking.
 *
 * This reports it and gates nothing, on purpose. Landing a channel and gating
 * on it in one step means a change in behaviour and a change in what is known
 * arriving together, with no way to tell which moved the result - and the
 * record in doc/step-export-status.md §§17-20 is a long argument for not doing
 * that again. What it is good for immediately is telling two failures apart:
 * a region left faceted can now say which solid it belonged to, rather than
 * quoting its own dihedral back.
 */
void reportProvenance(const PolySet& ps)
{
  if (ps.original_ids.size() != ps.indices.size() || ps.indices.empty()) {
    // The CGAL backend keeps no ids, and hull() drops them on Manifold. Nothing
    // to say, and nothing downstream may assume otherwise.
    return;
  }
  std::map<int32_t, std::size_t> facets;
  for (const int32_t id : ps.original_ids) facets[id]++;
  std::size_t fragments = 0, largest = 0;
  for (const auto& entry : facets) {
    if (entry.second < 3) fragments++;
    largest = std::max(largest, entry.second);
  }
  LOG(
    "STEP export: the mesh comes from %1$d original solid%2$s over %3$d facets - largest "
    "contributes %4$d, %5$d contribute fewer than three",
    int(facets.size()), facets.size() == 1 ? "" : "s", int(ps.indices.size()), int(largest),
    int(fragments));
}

/*! Closest point on a declared surface, where one can be written down.
 *
 * Not every kind: a Bezier patch and a grid answer by projection, which can
 * fail to converge, and the caller has to treat that as "not this surface"
 * rather than as an answer. */
bool closestOnSurface(const Surface *s, const Vector3d& p, Vector3d& out)
{
  if (const auto *cyl = dynamic_cast<const CylinderSurface *>(s)) {
    const Vector3d axis = cyl->normdir.normalized();
    const Vector3d rel = p - cyl->refpt;
    const Vector3d radial = rel - axis * rel.dot(axis);
    if (radial.norm() < 1e-12) return false;
    out = cyl->refpt + axis * rel.dot(axis) + radial.normalized() * cyl->r;
    return true;
  }
  if (const auto *cone = dynamic_cast<const ConeSurface *>(s)) {
    const Vector3d axis = cone->normdir.normalized();
    const Vector3d rel = p - cone->refpt;
    const double h = rel.dot(axis);
    const Vector3d radial = rel - axis * h;
    if (radial.norm() < 1e-12) return false;
    const double want = cone->r + h * cone->slope;
    if (want <= 0) return false;
    out = cone->refpt + axis * h + radial.normalized() * want;
    return true;
  }
  if (const auto *sph = dynamic_cast<const SphereSurface *>(s)) {
    const Vector3d rel = p - sph->refpt;
    if (rel.norm() < 1e-12) return false;
    out = sph->refpt + rel.normalized() * sph->r;
    return true;
  }
  if (const auto *grid = dynamic_cast<const GridSurface *>(s)) {
    double u = 0, v = 0;
    if (!grid->project(p, u, v)) return false;
    out = grid->evaluate(u, v);
    return true;
  }
  return false;
}

/*! The surface's own normal where it passes closest to `p`.
 *
 * Needed because corners are not enough to say a facet lies on a surface. The
 * flat cap an oblique cut leaves on a cylinder has *every* corner exactly on
 * that cylinder - the rim is the intersection - and it belongs to the cutting
 * solid, not to the cylinder. Measured by corners alone all 30 of its facets
 * are claimed for the cylinder, and step-nested-rings does the same thing where
 * a plate's top face meets a bore's rim. A facet lying on a surface also faces
 * the way the surface does there; a cap crosses it at a right angle. */
bool surfaceNormal(const Surface *s, const Vector3d& p, Vector3d& out)
{
  if (const auto *cyl = dynamic_cast<const CylinderSurface *>(s)) {
    const Vector3d axis = cyl->normdir.normalized();
    const Vector3d rel = p - cyl->refpt;
    const Vector3d radial = rel - axis * rel.dot(axis);
    if (radial.norm() < 1e-12) return false;
    out = radial.normalized();
    return true;
  }
  if (const auto *cone = dynamic_cast<const ConeSurface *>(s)) {
    const Vector3d axis = cone->normdir.normalized();
    const Vector3d rel = p - cone->refpt;
    const Vector3d radial = rel - axis * rel.dot(axis);
    if (radial.norm() < 1e-12) return false;
    // grad of |radial| - (r + slope * h)
    out = (radial.normalized() - axis * cone->slope).normalized();
    return true;
  }
  if (const auto *sph = dynamic_cast<const SphereSurface *>(s)) {
    const Vector3d rel = p - sph->refpt;
    if (rel.norm() < 1e-12) return false;
    out = rel.normalized();
    return true;
  }
  if (const auto *tor = dynamic_cast<const TorusSurface *>(s)) {
    const Vector3d axis = tor->normdir.normalized();
    const Vector3d rel = p - tor->refpt;
    const Vector3d radial = rel - axis * rel.dot(axis);
    if (radial.norm() < 1e-12) return false;
    const Vector3d ring = tor->refpt + radial.normalized() * tor->r_major;
    if ((p - ring).norm() < 1e-12) return false;
    out = (p - ring).normalized();
    return true;
  }
  if (const auto *grid = dynamic_cast<const GridSurface *>(s)) {
    double u = 0, v = 0;
    if (!grid->project(p, u, v)) return false;
    const double h = 1e-4;
    const Vector3d du =
      grid->evaluate(std::min(1.0, u + h), v) - grid->evaluate(std::max(0.0, u - h), v);
    const Vector3d dv =
      grid->evaluate(u, std::min(1.0, v + h)) - grid->evaluate(u, std::max(0.0, v - h));
    const Vector3d n = du.cross(dv);
    if (n.norm() < 1e-18) return false;
    out = n.normalized();
    return true;
  }
  if (const auto *patch = dynamic_cast<const BezierPatchSurface *>(s)) {
    double u = 0, v = 0;
    if (!patch->project(p, u, v)) return false;
    const double h = 1e-4;
    const Vector3d du =
      patch->evaluate(std::min(1.0, u + h), v) - patch->evaluate(std::max(0.0, u - h), v);
    const Vector3d dv =
      patch->evaluate(u, std::min(1.0, v + h)) - patch->evaluate(u, std::max(0.0, v - h));
    const Vector3d n = du.cross(dv);
    if (n.norm() < 1e-18) return false;
    out = n.normalized();
    return true;
  }
  return false;
}

/*! Whether two records describe the same surface, geometrically.
 *
 * `Surface::sameAs` deliberately refuses this: it exists for deduplicating the
 * record list, where being wrong loses a surface, so it keeps the same infinite
 * cylinder referred to from two heights as two entries. Nothing is dropped
 * here - the question is only how many *distinct surfaces* claim a vertex - and
 * counting one cylinder twice makes an unambiguous junction look contested,
 * which it did on all 32 of step-declare-cone's. */
bool sameSurfaceGeometrically(const Surface *a, const Surface *b)
{
  if (a == b || a->sameAs(*b)) return true;
  const auto axesAgree = [](const Surface *p, const Surface *q) {
    const Vector3d u = p->normdir.normalized(), w = q->normdir.normalized();
    if (fabs(fabs(u.dot(w)) - 1.0) > 1e-9) return false;
    const Vector3d d = q->refpt - p->refpt;
    return (d - u * d.dot(u)).norm() <= 1e-9;  // the same axis *line*
  };
  if (const auto *ca = dynamic_cast<const CylinderSurface *>(a)) {
    const auto *cb = dynamic_cast<const CylinderSurface *>(b);
    return cb != nullptr && fabs(ca->r - cb->r) <= 1e-9 && axesAgree(a, b);
  }
  if (const auto *ca = dynamic_cast<const ConeSurface *>(a)) {
    const auto *cb = dynamic_cast<const ConeSurface *>(b);
    if (cb == nullptr || !axesAgree(a, b)) return false;
    // Radius at a shared point rather than at each record's own refpt, which is
    // the whole difference between them. The slopes are compared in absolute
    // value because reversing a cone's axis negates it and describes the same
    // cone.
    return fabs(fabs(ca->slope) - fabs(cb->slope)) <= 1e-9 &&
           fabs(ca->radiusAt(cb->refpt) - cb->r) <= 1e-9;
  }
  return false;
}

/*! Which declared surface each original solid's facets lie on.
 *
 * The mapping the snap needs, and the one thing distance cannot supply. A cut
 * vertex sits where a ridge meets a wall, and the question "which surface was
 * this cut out of" has always been answered by *which declared surface is
 * nearest*, which is a different question with a plausible wrong answer: the
 * wall's plane is nearer than the ridge's sweep, so the vertex slides onto the
 * wall and the sweep it belonged to disappears. §21 of the status doc lists
 * that failure and three others as the same gate.
 *
 * Provenance answers it without measuring anything about the vertex. The facets
 * around it carry the id of the solid they came from; count which id's facets
 * lie on which declared surface, and the ridge's id owns the sweep while the
 * wall's id owns none of it - whatever the distances say.
 *
 * Ownership is many to many by nature: one primitive declares its barrel and
 * says nothing about its caps, and one cylinder record can be shared by two
 * solids drawn coaxially. So this counts rather than assigns, and the caller
 * decides what a count is worth. It reports and gates nothing, for the reason
 * reportProvenance does.
 *
 * Membership here is deliberately *not* each surface's own `pointMember`. That
 * one accepts a point within the tessellation band - 0.1290 on the strip
 * coupon - which is the loose gate this exists to replace, and using it
 * reproduces the guess exactly: measured that way the cylinder's own id "owns"
 * 182 facets of the ridge's sweep, because near the junction its facets are
 * within a band of it, and every junction vertex comes out contested. So the
 * test is tight - a grid point is one the generator emitted, a quadric's point
 * is on it to 1e-7, the exact tier's own tolerance - and what it asks is where
 * a surface *runs*, not what is near it.
 */
void reportOwnership(const PolySet& ps)
{
  if (ps.original_ids.size() != ps.indices.size() || ps.indices.empty()) return;
  if (ps.surfaces.empty()) return;

  const std::size_t nsurf = ps.surfaces.size();

  // On the surface, tightly. See the note above for why not pointMember.
  const double tol = 1e-7;
  auto runsThrough = [&](const Surface *s, const Vector3d& p) {
    if (const auto *grid = dynamic_cast<const GridSurface *>(s)) return grid->isDeclaredPoint(p);
    if (const auto *cyl = dynamic_cast<const CylinderSurface *>(s)) {
      const Vector3d rel = p - cyl->refpt;
      const Vector3d axis = cyl->normdir.normalized();
      return fabs((rel - axis * rel.dot(axis)).norm() - cyl->r) <= tol;
    }
    if (const auto *cone = dynamic_cast<const ConeSurface *>(s)) {
      const Vector3d rel = p - cone->refpt;
      const Vector3d axis = cone->normdir.normalized();
      return fabs((rel - axis * rel.dot(axis)).norm() - cone->radiusAt(p)) <= tol;
    }
    if (const auto *sph = dynamic_cast<const SphereSurface *>(s)) {
      return fabs((p - sph->refpt).norm() - sph->r) <= tol;
    }
    if (const auto *tor = dynamic_cast<const TorusSurface *>(s)) {
      const Vector3d rel = p - tor->refpt;
      const Vector3d axis = tor->normdir.normalized();
      const double along = rel.dot(axis);
      const double radial = (rel - axis * along).norm();
      return fabs(hypot(radial - tor->r_major, along) - tor->r_minor) <= tol;
    }
    if (const auto *patch = dynamic_cast<const BezierPatchSurface *>(s)) {
      double u = 0, v = 0;
      if (!patch->project(p, u, v)) return false;
      return (patch->evaluate(u, v) - p).norm() <= tol;
    }
    return false;
  };

  // Which surfaces run through each vertex, once, rather than per facet.
  std::vector<std::vector<char>> on_surface(nsurf, std::vector<char>(ps.vertices.size(), 0));
  // And for a declared grid, *where* in the net each vertex is. A grid answers
  // membership positionally, so it can answer ownership positionally too - which
  // cell of the generator's own net a facet is - with no projection to converge
  // and no normal to estimate. Projection was tried and it is not robust here:
  // taking the grid's normal from it counted 122 facets on the closed sweep and
  // 4 on the open one, from the same mesh.
  std::vector<std::vector<std::pair<int, int>>> cell_of(nsurf);
  for (std::size_t j = 0; j < nsurf; j++) {
    const Surface *s = ps.surfaces[j].get();
    const auto *grid = dynamic_cast<const GridSurface *>(s);
    if (grid != nullptr) {
      // The same quantisation GridSurface::isDeclaredPoint uses, and the same
      // neighbour probe, so a point on the boundary of a bucket still matches.
      auto key = [](const Vector3d& p) {
        return std::make_tuple(int64_t(llround(p[0] / 1e-9)), int64_t(llround(p[1] / 1e-9)),
                               int64_t(llround(p[2] / 1e-9)));
      };
      std::map<std::tuple<int64_t, int64_t, int64_t>, int> where;
      for (std::size_t i = 0; i < grid->net.size(); i++) where.emplace(key(grid->net[i]), int(i));
      cell_of[j].assign(ps.vertices.size(), {-1, -1});
      for (std::size_t v = 0; v < ps.vertices.size(); v++) {
        const auto k = key(ps.vertices[v]);
        for (int64_t dx = -1; dx <= 1 && cell_of[j][v].first < 0; dx++) {
          for (int64_t dy = -1; dy <= 1 && cell_of[j][v].first < 0; dy++) {
            for (int64_t dz = -1; dz <= 1 && cell_of[j][v].first < 0; dz++) {
              const auto it =
                where.find({std::get<0>(k) + dx, std::get<1>(k) + dy, std::get<2>(k) + dz});
              if (it != where.end()) {
                cell_of[j][v] = {it->second / grid->cols, it->second % grid->cols};
                on_surface[j][v] = 1;
              }
            }
          }
        }
      }
      continue;
    }
    for (std::size_t v = 0; v < ps.vertices.size(); v++) {
      on_surface[j][v] = runsThrough(s, ps.vertices[v]) ? 1 : 0;
    }
  }

  // facets of each original, and how many of them lie on each surface - whole,
  // or cut across by the boolean that made this mesh. Both count towards
  // ownership and only the first is a claim: a cut facet has lost a corner to
  // the boolean and still belongs to the surface the other two are on, which is
  // the whole point of asking about origin rather than about fit. On the open
  // strip coupon it is the difference between 4 facets and 240 - the ridge's
  // cut faces are exactly the ones the question is about.
  std::map<int32_t, std::size_t> facets;
  std::map<int32_t, std::vector<std::size_t>> covered, covered_cut;
  for (std::size_t t = 0; t < ps.indices.size(); t++) {
    const int32_t id = ps.original_ids[t];
    facets[id]++;
    auto& row = covered[id];
    if (row.empty()) row.assign(nsurf, 0);
    auto& row_cut = covered_cut[id];
    if (row_cut.empty()) row_cut.assign(nsurf, 0);
    const IndexedFace& f = ps.indices[t];
    if (f.size() < 3) continue;
    const Vector3d fn =
      (ps.vertices[f[1]] - ps.vertices[f[0]]).cross(ps.vertices[f[2]] - ps.vertices[f[0]]);
    if (fn.norm() < 1e-18) continue;

    for (std::size_t j = 0; j < nsurf; j++) {
      std::vector<int> on;
      bool bad = false;
      for (const int v : f) {
        if (v < 0 || std::size_t(v) >= ps.vertices.size()) {
          bad = true;
          break;
        }
        if (on_surface[j][v]) on.push_back(v);
      }
      if (bad || on.size() < 2) continue;

      if (!cell_of[j].empty()) {
        // A grid says it outright: these corners are points the generator
        // emitted, and they are neighbours in its own net. Corners alone would
        // also claim the strip an open profile deliberately excludes, whose
        // corners are all declared points and whose middle is nowhere near the
        // sweep.
        const auto *grid = static_cast<const GridSurface *>(ps.surfaces[j].get());
        bool adjacent = true;
        for (const int a : on) {
          for (const int b : on) {
            if (std::abs(cell_of[j][a].first - cell_of[j][b].first) > 1) adjacent = false;
            int d = std::abs(cell_of[j][a].second - cell_of[j][b].second);
            if (grid->closed_v) d = std::min(d, grid->cols - d);
            if (d > 1) adjacent = false;
          }
        }
        if (!adjacent) continue;
      } else {
        // ...and faces the way the surface does there. See surfaceNormal.
        Vector3d centroid = Vector3d::Zero();
        for (const int v : on) centroid += ps.vertices[v];
        centroid /= double(on.size());
        Vector3d sn;
        if (!surfaceNormal(ps.surfaces[j].get(), centroid, sn)) continue;
        // 60 degrees: loose enough for any tessellation a model would carry,
        // tight enough that a cap crossing the surface at a right angle is out.
        if (fabs(fn.normalized().dot(sn)) < 0.5) continue;
      }
      if (on.size() == f.size()) row[j]++;
      else row_cut[j]++;
    }
  }

  // An id owns a surface when a real part of it lies there. One facet is a
  // coincidence - two solids meeting flush share vertices, and a triangle of a
  // wall can land on a cylinder it was cut against without belonging to it.
  // Whole facets decide it, and cut ones do not - they are reported because
  // they say how much of the surface the boolean took, not because they are
  // evidence of origin. A facet cut across a seam has two corners on the
  // surface on the *other* side of it, so counting those as ownership makes
  // every solid own every surface it touches: it did, and it turned 32
  // unambiguous junctions of step-declare-cone into 64 contested ones. A
  // boundary artefact cannot produce a whole facet, because a facet wholly on
  // a surface is on it.
  const std::size_t least = 3;
  std::map<int32_t, std::vector<std::size_t>> owned;
  for (const auto& entry : covered) {
    for (std::size_t j = 0; j < nsurf; j++) {
      if (entry.second[j] >= least) owned[entry.first].push_back(j);
    }
  }

  // What the mapping is worth is not how many surfaces it names but how many
  // junction vertices it disambiguates: a vertex where two originals meet is
  // exactly where the nearest-surface rule was guessing, and the mapping helps
  // only if it names one owner there.
  std::vector<std::set<int32_t>> ids_at(ps.vertices.size());
  // And how far the mesh itself concedes a vertex might be from the surface it
  // approximates: the longest edge at it. A move further than that is not a cut
  // vertex being put back, it is a vertex belonging to something else.
  std::vector<double> reach(ps.vertices.size(), 0.0);
  for (std::size_t t = 0; t < ps.indices.size(); t++) {
    for (const int v : ps.indices[t]) {
      if (v < 0 || std::size_t(v) >= ids_at.size()) continue;
      ids_at[v].insert(ps.original_ids[t]);
      for (const int w : ps.indices[t]) {
        if (w >= 0 && std::size_t(w) < ps.vertices.size()) {
          reach[v] = std::max(reach[v], (ps.vertices[w] - ps.vertices[v]).norm());
        }
      }
    }
  }
  std::size_t junctions = 0, single = 0, pair = 0, many = 0, unowned = 0;
  std::size_t guess_wrong = 0, guess_right = 0, on_edge = 0;
  double worst_travel = 0, worst_within = 0, band_used = 0;
  std::size_t within_bound = 0;
  for (std::size_t v = 0; v < ps.vertices.size(); v++) {
    if (ids_at[v].size() < 2) continue;
    junctions++;
    // The surfaces owned by the originals meeting here, deduplicated: two ids
    // owning the same record is agreement, not a contest.
    std::vector<std::size_t> candidates;
    for (const int32_t id : ids_at[v]) {
      const auto it = owned.find(id);
      if (it == owned.end()) continue;
      for (const std::size_t j : it->second) {
        // By the surface, not by the record. The list keeps two entries for one
        // cylinder referred to from two heights (see Surface::sameAs), and
        // counting those as two owners makes an unambiguous vertex look
        // contested - which it did, on every junction of step-declare-cone.
        bool have = false;
        for (const std::size_t k : candidates) {
          if (k == j || sameSurfaceGeometrically(ps.surfaces[k].get(), ps.surfaces[j].get())) {
            have = true;
            break;
          }
        }
        if (!have) candidates.push_back(j);
      }
    }
    if (candidates.empty()) {
      unowned++;
      continue;
    }
    // What the *nearest* rule would have answered here, which is the rule
    // being replaced. Only meaningful where provenance names one surface.
    if (candidates.size() == 1) {
      single++;
      std::size_t nearest = candidates[0];
      double best = std::numeric_limits<double>::infinity();
      for (std::size_t j = 0; j < nsurf; j++) {
        Vector3d q;
        if (!closestOnSurface(ps.surfaces[j].get(), ps.vertices[v], q)) continue;
        const double d = (q - ps.vertices[v]).norm();
        if (d < best) {
          best = d;
          nearest = j;
        }
      }
      if (nearest == candidates[0]) guess_right++;
      else guess_wrong++;
    } else if (candidates.size() == 2) {
      pair++;
      // Two declared owners is not a contest to be broken but the trim curve
      // itself: the vertex belongs to both surfaces and its place is where they
      // cross. Alternating projection finds that point where they cross
      // transversally, which is the only case worth acting on. Measured, not
      // moved - this reports and gates nothing.
      const Surface *a = ps.surfaces[candidates[0]].get();
      const Surface *b = ps.surfaces[candidates[1]].get();
      Vector3d p = ps.vertices[v], qa, qb;
      bool ok = true;
      for (int iter = 0; iter < 64; iter++) {
        if (!closestOnSurface(a, p, qa) || !closestOnSurface(b, qa, qb)) {
          ok = false;
          break;
        }
        if ((qb - p).norm() < 1e-12) {
          p = qb;
          break;
        }
        p = qb;
      }
      if (ok && closestOnSurface(a, p, qa) && (qa - p).norm() <= 1e-6) {
        on_edge++;
        const double travel = (p - ps.vertices[v]).norm();
        worst_travel = std::max(worst_travel, travel);
        // What the mesh concedes about where its surface lies. A declared sweep
        // states it outright - the sagitta of its own stations - and that is a
        // property of the model rather than a constant chosen here. Two
        // quadrics declare no such thing, so the mesh's own edges stand in.
        double bound = reach[v];
        for (const std::size_t j : candidates) {
          if (const auto *g = dynamic_cast<const GridSurface *>(ps.surfaces[j].get())) {
            bound = g->tessellationBand();
            band_used = bound;
          }
        }
        if (travel <= bound) {
          within_bound++;
          worst_within = std::max(worst_within, travel);
        }
      }
    } else {
      many++;
    }
  }

  std::set<std::size_t> named;
  for (const auto& e : owned) named.insert(e.second.begin(), e.second.end());

  LOG(
    "STEP export: provenance maps %1$d of %2$d originals onto %3$d of %4$d declared surface%5$s; "
    "of %6$d junction vertices it names one owner for %7$d, two for %8$d, more for %9$d, none "
    "for %10$d",
    int(owned.size()), int(facets.size()), int(named.size()), int(nsurf), nsurf == 1 ? "" : "s",
    int(junctions), int(single), int(pair), int(many), int(unowned));
  if (single > 0) {
    LOG(
      "STEP export: where provenance names one owner, nearest-surface agrees for %1$d and picks "
      "a different surface for %2$d",
      int(guess_right), int(guess_wrong));
  }
  if (pair > 0 && band_used > 0) {
    LOG(
      "STEP export: of %1$d junction vertices owned by two surfaces, %2$d reach the curve where "
      "the two cross, moving at most %3$.4f; %4$d of those move no further than the sweep's "
      "tessellation band of %5$.4f, by at most %6$.4f",
      int(pair), int(on_edge), worst_travel, int(within_bound), band_used, worst_within);
  } else if (pair > 0) {
    LOG(
      "STEP export: of %1$d junction vertices owned by two surfaces, %2$d reach the curve where "
      "the two cross, moving at most %3$.4f; %4$d of those move no further than the mesh's own "
      "edges, by at most %5$.4f",
      int(pair), int(on_edge), worst_travel, int(within_bound), worst_within);
  }

  for (const auto& entry : owned) {
    std::string names;
    for (const std::size_t j : entry.second) {
      const Surface *s = ps.surfaces[j].get();
      const char *kind = dynamic_cast<const GridSurface *>(s)          ? "sweep"
                         : dynamic_cast<const ConeSurface *>(s)        ? "cone"
                         : dynamic_cast<const CylinderSurface *>(s)    ? "cylinder"
                         : dynamic_cast<const SphereSurface *>(s)      ? "sphere"
                         : dynamic_cast<const TorusSurface *>(s)       ? "torus"
                         : dynamic_cast<const BezierPatchSurface *>(s) ? "patch"
                                                                       : "surface";
      names += (names.empty() ? "" : ", ") + std::string(kind) + " " + std::to_string(j) + " (" +
               std::to_string(covered[entry.first][j]) + " facets whole, " +
               std::to_string(covered_cut[entry.first][j]) + " cut)";
    }
    LOG("STEP export: original %1$d, %2$d facets, owns %3$s", int(entry.first), int(facets[entry.first]),
        names.c_str());
  }
}

}  // namespace

void export_step(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                 const ExportInfo& exportInfo)
{
  auto ps = PolySetUtils::getGeometryAsPolySet(geom);
  if (ps == nullptr) return;

  //	printf("export curves: %zu\n",ps->curves.size());
  //	printf("export surfaces: %zu\n",ps->surfaces.size());
  std::vector<int> faceParents;
  std::vector<Vector4d> normals, newNormals;

  if (Feature::ExperimentalStepAnalyticSurfaces.is_enabled()) {
    reportProvenance(*ps);
    reportOwnership(*ps);
  }

  std::vector<IndexedFace> indicesNew;
  normals = calcTriangleNormals(ps->vertices, ps->indices);
  indicesNew = mergeTriangles(ps->indices, normals, newNormals, faceParents, ps->vertices);

  StepKernel sk;

  // newNormals holds the outward normal of each merged face; without it the
  // exporter has to guess the plane orientation from the first three points of
  // the loop, which points the wrong way at a concave corner and collapses
  // completely when those points are collinear.
  // Writing CYLINDRICAL_SURFACE and CIRCLE instead of the facets is still
  // provisional: a malformed analytic face is worse than a correct faceted one,
  // and only a few importers have been tried. Opt in with the
  // step-analytic-surfaces feature - the checkbox in Preferences, or
  // --enable=step-analytic-surfaces on the command line - until it has had that
  // exposure.
  const bool analytic = Feature::ExperimentalStepAnalyticSurfaces.is_enabled();
  // Fitting a surface to what the exact pass could not describe is a further
  // step out, and a separate opt-in: it is the one place this exporter would
  // write a surface the model does not prove, so it is bounded by the
  // tessellation's own tolerance and reports what it did. It has no meaning
  // without the exact pass, which decides what is left over in the first place.
  const bool approximate = analytic && Feature::ExperimentalStepApproximateSurfaces.is_enabled();

  sk.build_tri_body(exportInfo.title.c_str(), ps->vertices, indicesNew, ps->curves, ps->surfaces,
                    faceParents, newNormals, 1e-5, analytic, approximate);
  std::time_t tt = std ::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  struct std::tm *ptm = std::localtime(&tt);
  std::stringstream iso_time;
  iso_time << std::put_time(ptm, "%FT%T");

  std::string author = "slugdev";
  std::string org = "org";
  // header info
  output << "ISO-10303-21;\n";
  output << "HEADER;\n";
  output << "FILE_DESCRIPTION(('STP203'),'2;1');\n";
  // the source path has to be escaped: an apostrophe would end the string and a
  // backslash (any Windows path) starts a control directive
  output << "FILE_NAME('" << step_string(exportInfo.sourceFilePath) << "','" << iso_time.str() << "',('"
         << author << "'),('" << org << "'),' ','pythonscad',' ');\n";
  output << "FILE_SCHEMA(('CONFIG_CONTROL_DESIGN'));\n";
  output << "ENDSEC;\n";

  // data section
  output << "DATA;\n";

  for (auto e : sk.entities) {
    if (e->live) e->serialize(output);
  }
  // create the base csys
  output << "ENDSEC;\n";
  output << "END-ISO-10303-21;\n";
}
