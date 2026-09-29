#pragma once

/*! The steps of linear_extrude that need no node, for the unit tests to drive:
 * where each vertex and triangle goes, and which record each triangle is made
 * on. Declared here, where the definitions include them, rather than restated
 * by a test that then drifts from them (see linear_extrude_test.cc). */

#include <boost/logic/tribool.hpp>
#include <cstddef>
#include <memory>
#include <vector>

#include "geometry/PolySet.h"
#include "geometry/Polygon2d.h"
#include "geometry/SweptSurfaces.h"
#include "geometry/linalg.h"

namespace LinearExtrudeInternals {

/*! -1, 0 or 1 as v1 is shorter than, about as long as, or longer than v2. */
int sgn_vdiff(const Vector2d& v1, const Vector2d& v2);

/*! The two triangles each outline edge of `poly` makes between two slices. */
void add_slice_indices(PolygonIndices& indices, std::vector<Color4f>& colors,
                       std::vector<int>& color_indices, int slice_idx, int slice_stride,
                       const Polygon2d& poly, double rot1, double rot2, const Vector2d& scale1,
                       const Vector2d& scale2);

std::unique_ptr<PolySet> assemblePolySetForManifold(const Polygon2d& polyref,
                                                    std::vector<Vector3d>& vertices,
                                                    PolygonIndices& indices,
                                                    std::vector<Color4f>& colors,
                                                    std::vector<int>& color_indices, int convexity,
                                                    boost::tribool isConvex, int index_offset);

void prepareVerticesAndIndices(const Polygon2d& polyref, Vector3d h1, Vector3d h2, int num_slices,
                               double scale_x, double scale_y, double twist,
                               std::vector<Vector3d>& vertices, PolygonIndices& indices,
                               int& slice_stride);

/*! Name each triangle of `polyset`, laid out by the two above from `polyref`
 * in `num_slices` slices, by the record it is made on - `faces` indexing the
 * records from `first_record` on. */
void nameExtrudedFacets(const Polygon2d& polyref, std::size_t num_slices, const SweptFaces& faces,
                        std::size_t first_record, PolySet& polyset);

}  // namespace LinearExtrudeInternals
