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

  sk.build_tri_body(exportInfo.title.c_str(), ps->vertices, indicesNew, ps->curves, ps->surfaces,
                    faceParents, newNormals, 1e-5, analytic);
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
  output << "ENDSEC; \n";

  // data section
  output << "DATA;\n";

  for (auto e : sk.entities) e->serialize(output);
  // create the base csys
  output << "ENDSEC;\n";
  output << "END-ISO-10303-21;\n";
}
