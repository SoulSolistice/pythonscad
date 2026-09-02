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

  if (Feature::ExperimentalStepAnalyticSurfaces.is_enabled()) reportProvenance(*ps);

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
