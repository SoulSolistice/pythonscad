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

namespace {

/*! Slide a vertex the booleans made onto the surface it was cut out of.
 *
 * A declared sweep or cylinder is smooth; the mesh of it is not. When a boolean
 * cuts a ridge with a wall it cuts the *mesh*, so the vertex it creates lands on
 * a facet - on the chord between two stations rather than on the curve through
 * them - and its distance from any smooth surface interpolating those stations
 * is that chord's sagitta. No fit reaches it, because there is nothing to fit:
 * the point is simply not on the surface the model meant.
 *
 * It can be moved there, and there is exactly one direction free to do it in.
 * The vertex is on the wall's plane as well, and every planar facet around it
 * needs it to stay there; so slide it *within* that plane until it meets the
 * declared surface. That is a point of the intersection of the two exact
 * surfaces, which is where the trim between them belonged in the first place.
 *
 * Alternating projections - onto the surface, back onto the plane, and again -
 * converge on it wherever the two cross transversally, which is the only case
 * this accepts anyway. Bounded by the tessellation band: a vertex that would
 * have to move further than the mesh's own resolution is not a cut vertex being
 * put right, it is a vertex belonging to something else.
 */
struct SnapReport {
  std::size_t moved = 0;
  std::size_t refused = 0;
  double worst = 0.0;
};

bool closestPointOnSurface(const Surface *surface, const Vector3d& p, Vector3d& out)
{
  if (const auto *cyl = dynamic_cast<const CylinderSurface *>(surface)) {
    const Vector3d axis = cyl->normdir.normalized();
    const Vector3d rel = p - cyl->refpt;
    const Vector3d radial = rel - axis * rel.dot(axis);
    if (radial.norm() < 1e-12) return false;
    out = cyl->refpt + axis * rel.dot(axis) + radial.normalized() * cyl->r;
    return true;
  }
  if (const auto *cone = dynamic_cast<const ConeSurface *>(surface)) {
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
  if (const auto *grid = dynamic_cast<const GridSurface *>(surface)) {
    double u = 0, v = 0;
    if (!grid->project(p, u, v)) return false;
    out = grid->evaluate(u, v);
    return true;
  }
  return false;
}

/*! The one plane a vertex is pinned to, if there is exactly one. */
bool pinnedPlane(const std::vector<Vector4d>& planes, Vector3d& normal, double& offset)
{
  if (planes.empty()) return false;
  normal = planes[0].head<3>().normalized();
  offset = planes[0][3] / planes[0].head<3>().norm();
  for (const auto& q : planes) {
    const Vector3d n = q.head<3>().normalized();
    if (fabs(n.dot(normal)) < 0.99999) return false;  // a second plane pins it to a line
    const double d = q[3] / q.head<3>().norm();
    if (fabs((n.dot(normal) > 0 ? d : -d) - offset) > 1e-6) return false;
  }
  return true;
}

}  // namespace

namespace {

/*! Move every cut vertex of a declared surface onto it, where the geometry
 * around it leaves a direction free to move in. Returns what it did. */
SnapReport snapCutVertices(std::vector<Vector3d>& vertices, const std::vector<IndexedFace>& triangles,
                           const std::vector<std::shared_ptr<Surface>>& surfaces)
{
  SnapReport report;
  if (surfaces.empty() || triangles.empty()) return report;

  // The plane of every triangle, and which triangles meet at each vertex.
  std::vector<Vector4d> tri_plane(triangles.size(), Vector4d::Zero());
  std::vector<std::vector<std::size_t>> at(vertices.size());
  for (std::size_t t = 0; t < triangles.size(); t++) {
    const IndexedFace& f = triangles[t];
    if (f.size() < 3) continue;
    const Vector3d n = (vertices[f[1]] - vertices[f[0]]).cross(vertices[f[2]] - vertices[f[0]]);
    if (n.norm() < 1e-12) continue;
    const Vector3d u = n.normalized();
    tri_plane[t] = Vector4d(u[0], u[1], u[2], u.dot(vertices[f[0]]));
    for (const int v : f) {
      if (v >= 0 && std::size_t(v) < at.size()) at[v].push_back(t);
    }
  }

  // How far a vertex may be moved: the sagitta of the facets around it, which
  // is what the mesh already concedes about where its surface lies. A vertex
  // further off than that is not a cut vertex of this surface.
  auto reachAt = [&](std::size_t v) {
    double reach = 0;
    for (const std::size_t t : at[v]) {
      for (const int w : triangles[t]) reach = std::max(reach, (vertices[w] - vertices[v]).norm());
    }
    return reach;
  };

  // A vertex belongs to one surface. Snapping toward each declared surface in
  // turn pulls a ridge's vertex onto the wall it was cut against, and the pass
  // that follows then finds the ridge missing - which is exactly what happened
  // the first time this ran. So a vertex the surfaces disagree about is left
  // alone, and only an unambiguous one is moved.
  std::vector<int> claimed_by(vertices.size(), -1);
  std::vector<char> ambiguous(vertices.size(), 0);
  for (const auto& surface : surfaces) {
    const Surface *s = surface.get();
    if (dynamic_cast<const CylinderSurface *>(s) == nullptr &&
        dynamic_cast<const ConeSurface *>(s) == nullptr &&
        dynamic_cast<const GridSurface *>(s) == nullptr) {
      continue;
    }
    // Which vertices the surface already runs through. Classifying *triangles*
    // is the obvious move and it does not work: every triangle at a cut vertex
    // contains that vertex, so none of them is wholly on the surface and every
    // cut vertex looks unrelated to it.
    std::vector<char> near(vertices.size(), 0);
    for (std::size_t v = 0; v < vertices.size(); v++) {
      Vector3d q;
      near[v] = (closestPointOnSurface(s, vertices[v], q) && (q - vertices[v]).norm() <= 1e-6) ? 1 : 0;
    }
    // A triangle belongs to the surface if any corner of it does. A wall's
    // triangle has none, which is what makes its plane a pin rather than
    // something the move is free to change.
    std::vector<char> on(triangles.size(), 0);
    for (std::size_t t = 0; t < triangles.size(); t++) {
      for (const int v : triangles[t]) {
        if (near[v]) {
          on[t] = 1;
          break;
        }
      }
    }

    for (std::size_t v = 0; v < vertices.size(); v++) {
      if (at[v].empty() || near[v]) continue;
      std::size_t touching = 0;
      for (const std::size_t t : at[v]) touching += on[t] ? 1 : 0;
      // A cut vertex borders the surface without being on it: a triangle around
      // it reaches a vertex the surface does run through.
      if (touching == 0 || touching == at[v].size()) continue;
      Vector3d nearest;
      if (!closestPointOnSurface(s, vertices[v], nearest)) continue;
      const double gap = (nearest - vertices[v]).norm();
      if (gap <= 1e-6) continue;
      const double allow = reachAt(v);
      if (gap > allow) continue;

      std::vector<Vector4d> pins;
      for (const std::size_t t : at[v]) {
        if (!on[t] && tri_plane[t].head<3>().norm() > 1e-12) pins.push_back(tri_plane[t]);
      }
      Vector3d pn;
      double po = 0;
      if (!pinnedPlane(pins, pn, po)) {
        report.refused++;
        continue;
      }

      // Alternate: onto the surface, back onto the plane, until it stops moving.
      Vector3d p = vertices[v];
      bool ok = true;
      for (int iter = 0; iter < 32; iter++) {
        Vector3d q;
        if (!closestPointOnSurface(s, p, q)) {
          ok = false;
          break;
        }
        const Vector3d r = q - pn * (pn.dot(q) - po);
        if ((r - p).norm() < 1e-12) {
          p = r;
          break;
        }
        p = r;
      }
      if (!ok) {
        report.refused++;
        continue;
      }
      Vector3d check;
      const double travelled = (p - vertices[v]).norm();
      if (!closestPointOnSurface(s, p, check) || (check - p).norm() > 1e-6 || travelled > allow) {
        report.refused++;
        continue;
      }
      if (claimed_by[v] >= 0) {
        ambiguous[v] = 1;
        report.refused++;
        continue;
      }
      claimed_by[v] = 1;
      vertices[v] = p;
      report.moved++;
      report.worst = std::max(report.worst, travelled);
    }
  }
  return report;
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

  std::vector<IndexedFace> indicesNew;
  // Put the boolean's own vertices back on the surfaces they were cut out of,
  // before anything is measured against those surfaces. See snapCutVertices.
  std::vector<Vector3d> vertices = ps->vertices;
  const bool analytic_pass = Feature::ExperimentalStepAnalyticSurfaces.is_enabled();
  if (analytic_pass) {
    const SnapReport snapped = snapCutVertices(vertices, ps->indices, ps->surfaces);
    if (snapped.moved > 0 || snapped.refused > 0) {
      LOG(
        "STEP export: %1$d cut vertex%2$s slid onto the surface it was cut from, by up to %3$.4f; "
        "%4$d left where they were",
        int(snapped.moved), snapped.moved == 1 ? "" : "es", snapped.worst, int(snapped.refused));
    }
  }
  normals = calcTriangleNormals(vertices, ps->indices);
  indicesNew = mergeTriangles(ps->indices, normals, newNormals, faceParents, vertices);

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

  sk.build_tri_body(exportInfo.title.c_str(), vertices, indicesNew, ps->curves, ps->surfaces,
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
