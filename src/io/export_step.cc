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

#include <clocale>
#include <string>
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
#include <src/geometry/AnalyticFeatures.h>
#include <src/geometry/recognition/Exact.h>
#include "io/step/Meridian.h"
#ifdef ENABLE_MANIFOLD
#include <src/geometry/manifold/manifoldutils.h>
#endif
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
 * record in doc/step-export-development.md, *The ladder a change climbs*, is a
 * long argument for not doing that again. What it is good for immediately is
 * telling two failures apart:
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

using AnalyticFeatures::closestOnSurface;

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
  if (s == nullptr) return false;
  if (s->normalAt(p, out)) return true;
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
void reportOwnership(const PolySet& ps, std::map<int32_t, std::vector<std::size_t>> *out,
                     std::map<int, Vector3d> *out_moves, std::map<int, std::size_t> *out_single,
                     std::map<int, std::pair<std::size_t, std::size_t>> *out_split,
                     std::map<int, std::vector<std::size_t>> *out_owners)
{
  if (ps.original_ids.size() != ps.indices.size() || ps.indices.empty()) return;
  if (ps.surfaces.empty()) return;

  const std::size_t nsurf = ps.surfaces.size();

  // Which of the records are planes, because they take no part in any of this.
  std::vector<char> is_plane(nsurf, 0);
  std::size_t nplane = 0;
  for (std::size_t j = 0; j < nsurf; j++) {
    if (dynamic_cast<const PlaneSurface *>(ps.surfaces[j].get()) != nullptr) {
      is_plane[j] = 1;
      nplane++;
    }
  }
  const std::size_t ncurved = nsurf - nplane;

  // Declared planes take no part in ownership. A corner's owners here are the
  // curved surfaces provenance can name from the originals that made it; which
  // *plane* a corner is on is a different question, answerable only once
  // mergeTriangles has run - doc/step-export-development.md records three ways of
  // guessing it from raw triangles that do not work - and build_tri_body reads
  // the declarations there. Letting a plane in here would put a corner on its
  // own cap into the three-owner bucket, which places nothing at all.

  // On the surface, exactly: a declared quadric or torus at machine precision
  // (recognition::onExactly - the forward error of the residual, no chosen
  // threshold; this was 1e-7 until 2026-09-24 and decided nothing between the
  // two). A grid answers by its own net's points. A Bezier patch is D1, an
  // interpolant reached by projection, and keeps the projection's tolerance.
  // See the note above for why not pointMember.
  const double patch_tol = 1e-7;
  auto runsThrough = [&](const Surface *s, const Vector3d& p) {
    if (const auto *grid = dynamic_cast<const GridSurface *>(s)) return grid->isDeclaredPoint(p);
    if (const auto *patch = dynamic_cast<const BezierPatchSurface *>(s)) {
      double u = 0, v = 0;
      if (!patch->project(p, u, v)) return false;
      return (patch->evaluate(u, v) - p).norm() <= patch_tol;
    }
    return recognition::onExactly(s, p);
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
  // Whether a whole facet of this original lies on this surface by the
  // generator's own points - every corner `runsThrough` it, or is a declared
  // grid point adjacent in the net - asked *before* the facing band below and
  // with no count. D0: float equality for points the generator emitted on the
  // surface. What ownership asks of it is only whether the surface is still
  // *here* for this original; whether the original meant it is identity's
  // question, not this one's. See the ownership block below.
  std::map<int32_t, std::vector<char>> present;
  for (std::size_t t = 0; t < ps.indices.size(); t++) {
    const int32_t id = ps.original_ids[t];
    facets[id]++;
    auto& row = covered[id];
    if (row.empty()) row.assign(nsurf, 0);
    auto& row_cut = covered_cut[id];
    if (row_cut.empty()) row_cut.assign(nsurf, 0);
    auto& row_present = present[id];
    if (row_present.empty()) row_present.assign(nsurf, 0);
    const IndexedFace& f = ps.indices[t];
    if (f.size() < 3) continue;
    const Vector3d fn =
      (ps.vertices[f[1]] - ps.vertices[f[0]]).cross(ps.vertices[f[2]] - ps.vertices[f[0]]);
    if (fn.norm() < 1e-18) continue;

    // A facet wholly on a declared plane is a facet *of that plane*, whatever
    // curved surface its corners also lie on: a cylinder's cap has every corner
    // on the rim, and its interior is the disc, not the wall. The plane is the
    // closed form it lies on, stated by a declaration, at the same machine
    // precision `runsThrough` holds - so this is D0, where the facing band below
    // is a measurement. Measured without it on step-chamfered-cylinder:
    // hull() swallows the r = 8 disc's wall entirely, and the hull's bottom cap
    // alone made the disc's original "present" on that cylinder.
    bool on_declared_plane = false;
    for (std::size_t j = 0; j < nsurf && !on_declared_plane; j++) {
      if (is_plane[j] == 0) continue;
      const Surface *plane = ps.surfaces[j].get();
      bool all = true;
      for (const int v : f) {
        if (v < 0 || std::size_t(v) >= ps.vertices.size() ||
            !recognition::onExactly(plane, ps.vertices[v])) {
          all = false;
          break;
        }
      }
      on_declared_plane = all;
    }

    for (std::size_t j = 0; j < nsurf; j++) {
      if (is_plane[j] != 0) continue;
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
        if (on.size() == f.size() && !on_declared_plane) row_present[j] = 1;
      } else {
        if (on.size() == f.size() && !on_declared_plane) row_present[j] = 1;
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
  //
  // **Superseded 2026-09-23, and kept only to be compared against.** The rule
  // above is a measurement, and it fails exactly where two declarations meet:
  // on step-cylinder-cross, 76 of the vertical cylinder's facets are slivers
  // along the crease whose every corner is on the crossing curve - on *both*
  // cylinders by position - and near the crease the two normals agree within
  // 60 degrees, so the vertical solid "owned" the horizontal cylinder too. On
  // lid10 it granted cylinder 12 to four originals that never declared it.
  const std::size_t least = 3;
  std::map<int32_t, std::vector<std::size_t>> owned_measured;
  for (const auto& entry : covered) {
    for (std::size_t j = 0; j < nsurf; j++) {
      if (is_plane[j] == 0 && entry.second[j] >= least) owned_measured[entry.first].push_back(j);
    }
  }
  // What an id owns is what it *declared*: Surface::owners, stamped where the
  // id was minted and carried through every boolean. Identity, no tolerance,
  // no count. Asked per geometric surface - two records of one wall from two
  // heights are one wall - and over the same population as before, curved
  // surfaces of originals present in this mesh, so every consumer reads the
  // map it always did. See doc/step-export-wip.md, open item 31.
  std::map<int32_t, std::vector<std::size_t>> owned;
  for (std::size_t j = 0; j < nsurf; j++) {
    if (is_plane[j] != 0 || ps.surfaces[j] == nullptr) continue;
    std::set<int32_t> declared_by;
    for (std::size_t k = 0; k < nsurf; k++) {
      if (ps.surfaces[k] == nullptr || !describesSameSurface(*ps.surfaces[k], *ps.surfaces[j])) continue;
      declared_by.insert(ps.surfaces[k]->owners.begin(), ps.surfaces[k]->owners.end());
    }
    // Declared as a face by this original, *and* present in its facets. Identity
    // alone is not enough: step-declare-cone's r = 100 intersection tool did
    // declare its wall, and the intersection removed all of it, so offering that
    // wall at a junction sent a candidate 132.9 mm away. Presence alone is not
    // enough either: identity is what keeps a crease sliver, on two cylinders by
    // position, from giving its solid the other one. And presence is asked of
    // facets not wholly on a declared plane, because a cap has every corner on
    // the rim - measured, without that step-chamfered-cylinder's hull "kept" the
    // disc wall it swallows, and lid10 and the bayonet gained 120 junctions
    // with more owners than meet there.
    for (const int32_t id : declared_by) {
      const auto it = present.find(id);
      if (it == present.end() || it->second[j] == 0) continue;
      owned[id].push_back(j);
    }
  }

  // What the mapping is worth is not how many surfaces it names but how many
  // junction vertices it disambiguates: a vertex where two originals meet is
  // exactly where the nearest-surface rule was guessing, and the mapping helps
  // only if it names one owner there.
  std::vector<std::set<int32_t>> ids_at(ps.vertices.size());
  // The facets at each vertex, so that an owner can be asked whether its maker
  // actually contributes that surface here - see contributesHere below.
  std::vector<std::vector<std::size_t>> tris_at(ps.vertices.size());
  // And how far the mesh itself concedes a vertex might be from the surface it
  // approximates: the longest edge at it. A move further than that is not a cut
  // vertex being put back, it is a vertex belonging to something else.
  std::vector<double> reach(ps.vertices.size(), 0.0);
  for (std::size_t t = 0; t < ps.indices.size(); t++) {
    for (const int v : ps.indices[t]) {
      if (v < 0 || std::size_t(v) >= ids_at.size()) continue;
      ids_at[v].insert(ps.original_ids[t]);
      tris_at[v].push_back(t);
      for (const int w : ps.indices[t]) {
        if (w >= 0 && std::size_t(w) < ps.vertices.size()) {
          reach[v] = std::max(reach[v], (ps.vertices[w] - ps.vertices[v]).norm());
        }
      }
    }
  }
  /*! Does this original actually contribute this surface at this corner?
   *
   * Ownership is per original solid and not per surface - a cylinder's original
   * owns its wall, and its cap facets belong to that same original without being
   * on the wall. So a corner where one solid's *cap* meets another's wall has two
   * owners named, and the pair placement below then puts it where those two walls
   * cross, which is nowhere near it: on a nine-line model of two cylinders and a
   * cone that moved 26 corners by up to 11.0267 on a part of radius 10, and the
   * shell came apart along them. See open item 18.
   *
   * The declaration answers it, by the rule section 4 already uses to tell a
   * chord facet from a cutting plane: a facet lying *on* a surface is spanned by
   * directions tangent to it, so its normal is parallel to the surface's own
   * there, while a cap cuts across and its normal is perpendicular. A
   * tessellation is no coarser than a hexagon, so a chord facet's normal is
   * within pi/6 of the surface normal at any corner it carries; anything beyond
   * that is a facet of something else.
   *
   * Measured on that model: the corners this drops are 5.40 to 5.93 off the
   * owner it drops, where every junction it keeps is within 0.0120 - the 64-gon's
   * own sagitta, 10(1 - cos(pi/64)). */
  auto contributesHere = [&](std::size_t j, int32_t id, std::size_t v) {
    double f = 0;
    Vector3d grad;
    if (!AnalyticFeatures::surfaceImplicit(ps.surfaces[j].get(), ps.vertices[v], f, grad)) return true;
    const double gn = grad.norm();
    if (!(gn > 0)) return true;  // no opinion, so no veto
    const Vector3d nsurf = grad / gn;
    for (const std::size_t t : tris_at[v]) {
      if (id >= 0 && ps.original_ids[t] != id) continue;
      const IndexedFace& tri = ps.indices[t];
      if (tri.size() < 3) continue;
      const Vector3d n =
        (ps.vertices[tri[1]] - ps.vertices[tri[0]]).cross(ps.vertices[tri[2]] - ps.vertices[tri[0]]);
      if (!(n.norm() > 0)) continue;
      if (fabs(n.normalized().dot(nsurf)) >= cos(M_PI / 6)) return true;
    }
    return false;
  };
  std::size_t not_contributed = 0;
  std::size_t junctions = 0, single = 0, pair = 0, many = 0, unowned = 0;
  std::size_t guess_wrong = 0, guess_right = 0, on_edge = 0;
  double worst_travel = 0, worst_within = 0, band_used = 0;
  std::size_t within_bound = 0, on_exact_only = 0;
  // For the corners the acceptance turns down: how far Newton actually got from
  // each of the two owners, and which of them answers by projecting. The test
  // reads only `a`, so whether a corner is kept can depend on which surface the
  // candidate list happened to put first - and a grid cannot place a point on
  // itself as tightly as a quadric states where it is.
  std::vector<double> miss_a, miss_b, miss_fitted, miss_exact;
  double worst_exact_only = 0;
  for (std::size_t v = 0; v < ps.vertices.size(); v++) {
    if (ids_at[v].size() < 2) continue;
    junctions++;
    // The surfaces owned by the originals meeting here, deduplicated: two ids
    // owning the same record is agreement, not a contest.
    std::vector<std::size_t> candidates;
    // Which originals named each candidate: the premise test below asks whether
    // one of *them* has a facet lying on it here, and a wall facet of the solid
    // across can pass for one where two cylinders run nearly parallel.
    std::vector<std::vector<int32_t>> named_by;
    for (const int32_t id : ids_at[v]) {
      const auto it = owned.find(id);
      if (it == owned.end()) continue;
      for (const std::size_t j : it->second) {
        // A declared plane is not an owner here; see the note above nsurf.
        if (is_plane[j] != 0) continue;
        // By the surface, not by the record. The list keeps two entries for one
        // cylinder referred to from two heights (see Surface::sameAs), and
        // counting those as two owners makes an unambiguous vertex look
        // contested - which it did, on every junction of step-declare-cone.
        bool have = false;
        for (std::size_t c = 0; c < candidates.size(); c++) {
          const std::size_t k = candidates[c];
          if (k == j || sameSurfaceGeometrically(ps.surfaces[k].get(), ps.surfaces[j].get())) {
            named_by[c].push_back(id);
            have = true;
            break;
          }
        }
        if (!have) {
          candidates.push_back(j);
          named_by.push_back({id});
        }
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
        if (is_plane[j] != 0) continue;
        if (!closestOnSurface(ps.surfaces[j].get(), ps.vertices[v], q)) continue;
        const double d = (q - ps.vertices[v]).norm();
        if (d < best) {
          best = d;
          nearest = j;
        }
      }
      if (nearest == candidates[0]) guess_right++;
      else guess_wrong++;
      // One declared owner is not one surface. The corner still lies where two
      // things cross, and the other is very often a plane the mesh carries and
      // nothing declared - a cube face taking the base off a cone. *Which*
      // plane cannot be answered from raw triangles, and is answered exactly
      // once mergeTriangles has run, so the vertex is handed on and placed in
      // build_tri_body. doc/step-export-development.md records the three ways of
      // guessing it from triangles that do not work.
      if (out_single != nullptr) out_single->emplace(int(v), candidates[0]);
      if (out_owners != nullptr) (*out_owners)[int(v)].push_back(candidates[0]);
    } else if (candidates.size() == 2) {
      pair++;
      // Both owners have to be surfaces this corner's makers actually put here.
      // Provenance names a surface the *original* owns, and an original owns its
      // wall while contributing its cap at this corner - so a corner where one
      // solid's cap meets another's wall is named by two walls that cross
      // somewhere else entirely. Leaving such a corner alone is the ladder's last
      // rung and always a valid export; placing it on that crossing moved 26
      // corners of a nine-line model by up to 11.0267 on a part of radius 10,
      // and took the shell apart. See open item 18.
      auto contributed = [&](std::size_t c) {
        for (const int32_t id : named_by[c]) {
          if (contributesHere(candidates[c], id, v)) return true;
        }
        return false;
      };
      // Every owner that did contribute here, for the arbiter in build_tri_body:
      // a declared surface this corner's makers put at it is a constraint on the
      // corner whether or not a face of it is written there, and it is the one
      // that says a corner on a bore facet's chord belongs on the bore.
      if (out_owners != nullptr) {
        for (std::size_t c = 0; c < candidates.size(); c++) {
          if (contributed(c)) (*out_owners)[int(v)].push_back(candidates[c]);
        }
      }
      // Whether this corner may be placed where the two cross at all.
      //
      // Asked only where both owners are exact quadrics. Where one of them is a
      // fit, build_tri_body reconsiders the placement against the planes the
      // merged mesh carries - it is `ownerSplit` that reaches that reroute, and
      // a corner where a cap meets a wall is what the reroute exists to place.
      // Where both are exact nothing downstream looks again, and the crossing
      // stands however far away it is: on a nine-line model of two cylinders and
      // a cone, 26 corners each moved up to 11.0267 on a part of radius 10 and
      // the shell came apart along them. See open item 18.
      const bool both_exact =
        dynamic_cast<const GridSurface *>(ps.surfaces[candidates[0]].get()) == nullptr &&
        dynamic_cast<const GridSurface *>(ps.surfaces[candidates[1]].get()) == nullptr;
      const bool crossing_is_its_place = !both_exact || (contributed(0) && contributed(1));
      // Two declared owners is not a contest to be broken but the trim curve
      // itself: the vertex belongs to both surfaces and its place is where they
      // cross. Alternating projection finds that point where they cross
      // transversally, which is the only case worth acting on. Measured, not
      // moved - this reports and gates nothing.
      const Surface *a = ps.surfaces[candidates[0]].get();
      const Surface *b = ps.surfaces[candidates[1]].get();
      Vector3d p = ps.vertices[v], qa;
      // Newton on both implicits at once, rather than projecting onto each in
      // turn. Alternating projection converges - but linearly, contracting the
      // error by cos^2(theta) per cycle where theta is how squarely the two
      // surfaces cross. Under a fixed budget that is not a solver but an angle
      // threshold: at 64 cycles, from a bore facet's sagitta down to 1e-9, it
      // reaches only crossings above 29.98 degrees. On the band family that
      // left 138 corners on one surface and up to 0.0963 off the other, and
      // 141 of the 142 edges that then fell back to chords cross below 29.98.
      // Raising the cap to 4096 placed every one of them; Newton does it in a
      // handful of steps and with no threshold to tune. See open item 3 of
      // doc/step-export-wip.md.
      //
      // Tighter than the acceptance below by two orders, and *absolute* because
      // that test is. Scaling this by the corner's distance from the origin was
      // the same mistake in miniature: at |v| = 28 on the band family it made
      // the solver stop at 2.8e-09 and the test then asked for 1e-09, so 42
      // corners were turned down having missed by a median 1.23e-09 - refused
      // for the last factor of two of a convergence nobody had asked them to
      // finish.
      const double solve_tol = 1e-11;
      // Its answer is `p`, whether or not it met `solve_tol`: the test below is
      // what decides, and it is the same test either way.
      // A declared sweep and a coaxial surface of revolution cross in closed
      // form, and the corner keeps its station on it - see Meridian::place.
      step::Meridian meridian;
      Vector3d exact;
      if (step::Meridian::of(a, b, meridian) && meridian.place(p, exact)) {
        p = exact;
      } else {
        AnalyticFeatures::projectOntoBoth(a, b, p, solve_tol);
      }
      // Which of the two is exact and which is a fit, because they are not
      // worth the same. A declared quadric states where the surface is; a
      // GridSurface was interpolated through the model's own stations and says
      // so, publishing a tessellation band. Where a corner has one of each, the
      // exact one is the thing to be placed on and the fit is a tolerance to be
      // checked against - see doc/step-export-development.md. Handed on
      // for build_tri_body, which is where the planes are answerable.
      if (out_split != nullptr) {
        const bool a_fit = dynamic_cast<const GridSurface *>(a) != nullptr;
        const bool b_fit = dynamic_cast<const GridSurface *>(b) != nullptr;
        if (a_fit != b_fit) {
          out_split->emplace(int(v), a_fit ? std::make_pair(candidates[1], candidates[0])
                                           : std::make_pair(candidates[0], candidates[1]));
        }
      }
      // Where the two do not cross transversally the projection does not
      // converge, and the corner used to be left where the mesh put it. On
      // step-band-family that is 418 of 704 corners, every one of them sitting
      // on a bore facet's chord plane, 20(1 - cos(pi/32)) = 0.0963 inside the
      // cylinder it is written on - the whole of that fixture's remaining stray.
      //
      // The two owners are not worth the same, though, and the failure is the
      // fit's. A declared quadric states where its surface is; the sweep was
      // interpolated through the model's stations and publishes a band saying
      // how well. So when the pair will not converge and exactly one of them is
      // exact, the corner goes on the exact one alone, and the fit only has to
      // agree to within the band it declared. Measured on step-band-family, the
      // sweep crosses the bore at a median 15.85 degrees where the projection
      // fails against 42.70 where it succeeds, which is the whole difference
      // between the two populations.
      if (!(closestOnSurface(a, p, qa) && (qa - p).norm() <= 1e-9)) {
        const auto *a_fit = dynamic_cast<const GridSurface *>(a);
        const auto *b_fit = dynamic_cast<const GridSurface *>(b);
        {
          Vector3d ma, mb;
          const double da = closestOnSurface(a, p, ma) ? (ma - p).norm() : -1;
          const double db = closestOnSurface(b, p, mb) ? (mb - p).norm() : -1;
          if (da >= 0) miss_a.push_back(da);
          if (db >= 0) miss_b.push_back(db);
          if (da >= 0) (a_fit != nullptr ? miss_fitted : miss_exact).push_back(da);
          if (db >= 0) (b_fit != nullptr ? miss_fitted : miss_exact).push_back(db);
        }
        const Surface *exact =
          a_fit != nullptr ? (b_fit != nullptr ? nullptr : b) : (b_fit != nullptr ? a : nullptr);
        const GridSurface *fit = a_fit != nullptr ? a_fit : b_fit;
        Vector3d q;
        if (exact != nullptr && fit != nullptr && closestOnSurface(exact, ps.vertices[v], q)) {
          Vector3d qf;
          const bool got = closestOnSurface(fit, q, qf);
          const double off_fit = got ? (qf - q).norm() : -1;
          const double travel = (q - ps.vertices[v]).norm();
          // The fit may only veto a corner it has a claim on. Measured on
          // step-band-family, 259 of the 262 this refused were *already* off
          // the sweep before any move, by up to 0.3962 against its band of
          // 0.2077: they sit on the wall where the ridge's base meets it, which
          // is the wall's surface and not the sweep's. Refusing them left the
          // corner on a chord plane 0.0963 inside the cylinder it is written
          // on, to satisfy a surface it was never on.
          Vector3d qb0;
          const double before =
            closestOnSurface(fit, ps.vertices[v], qb0) ? (qb0 - ps.vertices[v]).norm() : -1;
          const bool fit_had_a_claim = before >= 0 && before <= fit->membershipTolerance();
          const bool fit_agrees = got && off_fit <= fit->membershipTolerance();
          if (got && (fit_agrees || !fit_had_a_claim) && travel <= reach[v]) {
            on_exact_only++;
            worst_exact_only = std::max(worst_exact_only, (q - ps.vertices[v]).norm());
            if (out_moves != nullptr) out_moves->emplace(int(v), q);
          }
        }
      }
      // To what the projection converges to, not to a millionth. Accepting 1e-6
      // put a corner 5.82e-07 off the cylinder its own face is written on -
      // small, but the file says that vertex is on that cylinder and it is not,
      // and it is the exporter's own slack rather than anything the model did.
      if (closestOnSurface(a, p, qa) && (qa - p).norm() <= 1e-9) {
        on_edge++;
        const double travel = (p - ps.vertices[v]).norm();
        const Vector3d exact_here = p;
        worst_travel = std::max(worst_travel, travel);
        // What the mesh concedes about where its surface lies. A declared sweep
        // states it outright - the sagitta of its own stations - and that is a
        // property of the model rather than a constant chosen here. Two
        // quadrics declare no such thing, so the mesh's own edges stand in.
        double bound = reach[v];
        for (const std::size_t j : candidates) {
          if (const auto *g = dynamic_cast<const GridSurface *>(ps.surfaces[j].get())) {
            bound = g->tessellationBand();
            // The widest, not the last one seen. Each corner is judged against
            // its own grid's band, so with two declared sweeps of different
            // resolutions a last-wins value names a band most of the counted
            // corners were never measured against.
            band_used = std::max(band_used, bound);
          }
        }
        if (!crossing_is_its_place) not_contributed++;
        if (crossing_is_its_place && travel <= bound) {
          within_bound++;
          worst_within = std::max(worst_within, travel);
          if (out_moves != nullptr) out_moves->emplace(int(v), exact_here);
        }
      }
    } else {
      many++;
    }
  }

  if (out != nullptr) *out = owned;

  std::set<std::size_t> named;
  for (const auto& e : owned) named.insert(e.second.begin(), e.second.end());

  LOG(
    "STEP export: provenance maps %1$d of %2$d originals onto %3$d of %4$d declared surface%5$s; "
    "of %6$d junction vertices it names one owner for %7$d, two for %8$d, more for %9$d, none "
    "for %10$d",
    int(owned.size()), int(facets.size()), int(named.size()), int(ncurved), ncurved == 1 ? "" : "s",
    int(junctions), int(single), int(pair), int(many), int(unowned));
  if (not_contributed > 0) {
    LOG(
      "STEP export: %1$d corners are left where the mesh put them: one of the two surfaces "
      "provenance names for them is on no facet at that corner, so the corner is where a cap "
      "meets something else and not where those two surfaces cross",
      int(not_contributed));
  }
  if (single > 0) {
    LOG(
      "STEP export: where provenance names one owner, nearest-surface agrees for %1$d and picks "
      "a different surface for %2$d",
      int(guess_right), int(guess_wrong));
  }
  if (on_exact_only > 0) {
    LOG(
      "STEP export: %1$d corners whose two owners do not cross transversally are placed on the "
      "exact one of them, moving at most %2$.4f, the fit agreeing within its own band",
      int(on_exact_only), worst_exact_only);
    auto spread = [](std::vector<double>& v) {
      std::sort(v.begin(), v.end());
      if (v.empty()) return std::string("none");
      char buf[128];
      snprintf(buf, sizeof(buf), "%.3e/%.3e/%.3e (n=%d)", v.front(), v[v.size() / 2], v.back(),
               int(v.size()));
      return std::string(buf);
    };
    const std::string sa = spread(miss_a), sb = spread(miss_b);
    const std::string sf = spread(miss_fitted), se = spread(miss_exact);
    LOG(
      "STEP export:    how far those got from each owner, min/med/max - first owner %1$s, second "
      "%2$s; by kind, stated algebraically %3$s, answering by projecting %4$s, against the 1e-09 "
      "the first owner alone is asked for",
      sa.c_str(), sb.c_str(), se.c_str(), sf.c_str());
  }
  if (pair > 0 && band_used > 0) {
    LOG(
      "STEP export: of %1$d junction vertices owned by two surfaces, %2$d reach the curve where "
      "the two cross, moving at most %3$.4f; %4$d of those move no further than their sweep's "
      "own tessellation band, the widest of which is %5$.4f, by at most %6$.4f",
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

  // Ownership carried from the generator, held against the ownership the old
  // rule measured. `owned_measured` is inferred from positions - whole facets
  // on the surface by `runsThrough`, facing it within 60 degrees, at least
  // `least` of them - and `Surface::owners` is stamped where the original's id
  // was minted. The first is a measurement wearing the name provenance; the
  // second is identity, and since 2026-09-23 it is what `owned` is. Reported so
  // every export says where the two would have differed. Over originals present
  // in this mesh and surfaces that are not planes. See open item 31.
  {
    std::size_t agree = 0, measured_only = 0, carried_only = 0, unstamped = 0;
    std::string first_measured, first_carried;
    for (std::size_t j = 0; j < nsurf; j++) {
      if (is_plane[j] != 0 || ps.surfaces[j] == nullptr) continue;
      // Per geometric surface, not per record: two records of one wall - the
      // same infinite cylinder declared from two heights - are kept apart, and
      // whoever declared either of them declared the wall.
      std::vector<int32_t> carried;
      for (std::size_t k = 0; k < nsurf; k++) {
        if (ps.surfaces[k] == nullptr || !describesSameSurface(*ps.surfaces[k], *ps.surfaces[j]))
          continue;
        carried.insert(carried.end(), ps.surfaces[k]->owners.begin(), ps.surfaces[k]->owners.end());
      }
      std::sort(carried.begin(), carried.end());
      carried.erase(std::unique(carried.begin(), carried.end()), carried.end());
      if (carried.empty()) unstamped++;
      for (const auto& entry : facets) {
        const int32_t id = entry.first;
        const auto it = owned_measured.find(id);
        const bool measured = it != owned_measured.end() &&
                              std::find(it->second.begin(), it->second.end(), j) != it->second.end();
        const bool stated = std::binary_search(carried.begin(), carried.end(), id);
        if (measured && stated) agree++;
        else if (measured) {
          measured_only++;
          if (first_measured.empty()) {
            first_measured = "original " + std::to_string(int(id)) + " on surface " + std::to_string(j);
          }
        } else if (stated) {
          carried_only++;
          if (first_carried.empty()) {
            first_carried = "original " + std::to_string(int(id)) + " on surface " + std::to_string(j);
          }
        }
      }
    }
    if (!facets.empty()) {
      LOG(
        "STEP export: surface ownership carried from the generator against ownership measured from "
        "positions: %1$d agree, %2$d measured only%3$s, %4$d carried only%5$s, %6$d surfaces carry no "
        "owner",
        int(agree), int(measured_only),
        first_measured.empty() ? "" : (" (first " + first_measured + ")"), int(carried_only),
        first_carried.empty() ? "" : (" (first " + first_carried + ")"), int(unstamped));
    }
  }
}

}  // namespace

/*! Hold LC_NUMERIC at "C" for as long as it exists.
 *
 * `openscad.cc` calls `setlocale(LC_ALL, "")`, so on a comma radix locale - de,
 * fr, ... - every `%f` the export runs through comes out with a comma. The
 * written file survives that because `step_real()` corrects the radix itself,
 * but the *report* does not: on a German machine the recogniser announces "the
 * fitted sweep passes within 0,0660 of the middle", and every fixture asserting
 * that line fails on the separator while the numbers agree exactly. Three did.
 *
 * A test suite that passes or fails on the developer's locale is not measuring
 * the exporter, and the failures look like geometry regressions, which is the
 * expensive way to find out.
 *
 * export_stl, export_dxf, export_svg, export_amf and export_gcode all bracket
 * their output the same way; STEP was the one that did not. They use a bare
 * pair of setlocale calls, which leaks "C" into the rest of the process if the
 * export throws between them - and this one has a great deal more that can
 * throw - so it is a guard here instead. */
namespace {
struct CNumericRadix {
  std::string saved;
  CNumericRadix()
  {
    const char *cur = setlocale(LC_NUMERIC, nullptr);
    if (cur != nullptr) saved = cur;
    setlocale(LC_NUMERIC, "C");
  }
  ~CNumericRadix() { setlocale(LC_NUMERIC, saved.empty() ? "" : saved.c_str()); }
  CNumericRadix(const CNumericRadix&) = delete;
  CNumericRadix& operator=(const CNumericRadix&) = delete;
};
}  // namespace

void export_step(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                 const ExportInfo& exportInfo)
{
  const CNumericRadix radix_is_a_point;

  auto ps = PolySetUtils::getGeometryAsPolySet(geom);
  if (ps == nullptr) return;
#ifdef ENABLE_MANIFOLD
  // Identity for a geometry that never went through a boolean. A plain PolySet
  // reaching the exporter is one generator's output - a primitive, an
  // extrusion, a hull, which is a generator of its own blend - because every
  // combination of makers on this backend goes through Manifold, which mints
  // ids, and on CGAL comes back as a Nef polyhedron, which carries none and is
  // left silent. So it is given the identity Manifold would have given it, by
  // the same rule (ManifoldUtils::mintOriginals). A GeometryList - several
  // top-level objects, concatenated - is several makers and stays silent: that
  // wants identity per member, not one for all. See open item 35.
  if (ps->original_ids.empty() && std::dynamic_pointer_cast<const PolySet>(geom) != nullptr) {
    ps = ManifoldUtils::mintOriginals(*ps);
  }
#endif

  //	printf("export curves: %zu\n",ps->curves.size());
  //	printf("export surfaces: %zu\n",ps->surfaces.size());
  std::vector<int> faceParents;
  std::vector<Vector4d> normals, newNormals;

  std::map<int32_t, std::vector<std::size_t>> owned;
  std::map<int, Vector3d> corner_moves;
  std::map<int, std::size_t> single_owner;
  std::map<int, std::pair<std::size_t, std::size_t>> owner_split;  // corner -> (exact, fitted)
  std::map<int, std::vector<std::size_t>> corner_owners;           // corner -> contributing owners
  if (Feature::ExperimentalStepAnalyticSurfaces.is_enabled()) {
    reportProvenance(*ps);
    reportOwnership(*ps, &owned, &corner_moves, &single_owner, &owner_split, &corner_owners);
  }

  std::vector<IndexedFace> indicesNew;
  normals = calcTriangleNormals(ps->vertices, ps->indices);
  indicesNew = mergeTriangles(ps->indices, normals, newNormals, faceParents, ps->vertices);

  // Which original made each merged face.
  //
  // Provenance is per triangle and the recognisers work on merged faces, so the
  // two have to be joined up before ownership can gate anything. A directed
  // edge belongs to exactly one triangle of a manifold mesh, and merging only
  // ever removes interior edges, so every boundary edge of a merged face still
  // names one of the triangles it was made of. Taking the commonest answer over
  // the whole boundary is proof against the odd edge a merge shared.
  std::vector<int32_t> faceOrigin(indicesNew.size(), -1);
  if (!ps->original_ids.empty() && ps->original_ids.size() >= ps->indices.size()) {
    std::map<std::pair<int, int>, int32_t> edge_origin;
    for (std::size_t t = 0; t < ps->indices.size(); t++) {
      const IndexedFace& tri = ps->indices[t];
      for (std::size_t i = 0; i < tri.size(); i++) {
        edge_origin[{tri[i], tri[(i + 1) % tri.size()]}] = ps->original_ids[t];
      }
    }
    for (std::size_t f = 0; f < indicesNew.size(); f++) {
      std::map<int32_t, int> votes;
      const IndexedFace& poly = indicesNew[f];
      for (std::size_t i = 0; i < poly.size(); i++) {
        const auto it = edge_origin.find({poly[i], poly[(i + 1) % poly.size()]});
        if (it != edge_origin.end()) votes[it->second]++;
      }
      int best = 0;
      for (const auto& v : votes) {
        if (v.second > best) {
          best = v.second;
          faceOrigin[f] = v.first;
        }
      }
    }
  }

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
                    faceOrigin, corner_moves, single_owner, owner_split, owned, faceParents, newNormals,
                    1e-5, analytic, approximate, corner_owners);
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
