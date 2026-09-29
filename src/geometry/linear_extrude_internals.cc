/*! The steps of linear_extrude that need no node - see linear_extrude_internals.h.
 *
 * Their own translation unit so that a unit test can link them: linear_extrude.cc
 * reaches the Python bindings through a node's profile and twist functions, and
 * with them the whole application. */
#include "geometry/linear_extrude_internals.h"

#include <algorithm>
#include <boost/logic/tribool.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "geometry/PolySet.h"
#include "geometry/Polygon2d.h"
#include "geometry/Surface.h"
#include "geometry/SweptSurfaces.h"
#include "geometry/linalg.h"
#include "utils/degree_trig.h"

namespace LinearExtrudeInternals {

/*
  Compare Euclidean length of vectors
  Return:
   -1 : if v1  < v2
    0 : if v1 ~= v2 (approximation to compensate for floating point precision)
    1 : if v1  > v2
*/
int sgn_vdiff(const Vector2d& v1, const Vector2d& v2)
{
  constexpr double ratio_threshold = 1e5;  // 5 orders-of-magnitude difference
  double l1 = v1.norm();
  double l2 = v2.norm();
  // Compare the average and difference, to be independent of geometry scale.
  // If the difference is within ratio_threshold of the avg, treat as equal.
  double scale = (l1 + l2);
  double diff = 2 * std::fabs(l1 - l2) * ratio_threshold;
  return diff > scale ? (l1 < l2 ? -1 : 1) : 0;
}

/**
 *
 * @param vertices The first polyref.length() vertices must be in the same order as polyref and represent
 * the bottom face. Similarly, the last polyref.length() vertices must be in the same order as polyref
 * and represent the top face.
 * @param indices These indexed face sets must not include the top nor bottom faces.
 */
std::unique_ptr<PolySet> assemblePolySetForManifold(const Polygon2d& polyref,
                                                    std::vector<Vector3d>& vertices,
                                                    PolygonIndices& indices,
                                                    std::vector<Color4f>& colors,
                                                    std::vector<int>& color_indices, int convexity,
                                                    boost::tribool isConvex, int index_offset)
{
  auto final_polyset = std::make_unique<PolySet>(3, isConvex);
  final_polyset->setTriangular(true);
  final_polyset->setConvexity(convexity);
  final_polyset->vertices = std::move(vertices);
  final_polyset->indices = std::move(indices);
  final_polyset->colors = std::move(colors);
  final_polyset->color_indices = std::move(color_indices);

  std::vector<int> colormap;
  for (int i = 0; i < final_polyset->vertices.size(); i++) colormap.push_back(0);
  for (int i = 0; i < final_polyset->indices.size(); i++) {
    auto& pol = final_polyset->indices[i];
    for (auto ind : pol) colormap[ind] = final_polyset->color_indices[i];
  }
  // Create top and bottom face.
  auto ps_bottom = polyref.tessellate();  // bottom
  // Flip vertex ordering for bottom polygon
  for (auto& p : ps_bottom->indices) {
    std::reverse(p.begin(), p.end());
  }
  std::copy(ps_bottom->indices.begin(), ps_bottom->indices.end(),
            std::back_inserter(final_polyset->indices));

  // Copy indices for the top face, with appropriate offset.
  for (auto& p : ps_bottom->indices) {
    std::reverse(p.begin(), p.end());
    for (auto& i : p) {
      i += index_offset;
    }
  }

  // Flip vertex ordering for bottom polygon and move it.
  std::copy(ps_bottom->indices.begin(), ps_bottom->indices.end(),
            std::back_inserter(final_polyset->indices));

  // LOG(PolySetUtils::polySetToPolyhedronSource(*final_polyset));

  for (int j = final_polyset->color_indices.size(); j < final_polyset->indices.size(); j++) {
    final_polyset->color_indices.push_back(colormap[final_polyset->indices[j][0]]);
  }
  return final_polyset;
}

/*
   Attempt to triangulate quads in an ideal way.
   Each quad is composed of two adjacent outline vertices: (prev1, curr1)
   and their corresponding transformed points one step up: (prev2, curr2).
   Quads are triangulated across the shorter of the two diagonals, which works well in most cases.
   However, when diagonals are equal length, decision may flip depending on other factors.
 */
void add_slice_indices(PolygonIndices& indices, std::vector<Color4f>& colors,
                       std::vector<int>& color_indices, int slice_idx, int slice_stride,
                       const Polygon2d& poly, double rot1, double rot2, const Vector2d& scale1,
                       const Vector2d& scale2)
{
  int prev_slice = (slice_idx - 1) * slice_stride;
  int curr_slice = slice_idx * slice_stride;

  Eigen::Affine2d trans1(Eigen::Scaling(scale1) * Eigen::Affine2d(rotate_degrees(-rot1)));
  Eigen::Affine2d trans2(Eigen::Scaling(scale2) * Eigen::Affine2d(rotate_degrees(-rot2)));

  bool any_zero = scale2[0] == 0 || scale2[1] == 0;
  // setting back_twist true helps keep diagonals same as previous builds.
  bool back_twist = rot2 <= rot1;

  int curr_outline = 0;
  for (const auto& o : poly.outlines()) {
    // prev1: previous slice, previous vertex
    // prev2: current slice, previous vertex
    Vector2d prev1 = trans1 * o.vertices[0];
    Vector2d prev2 = trans2 * o.vertices[0];

    // For equal length diagonals, flip selected choice depending on direction of twist and
    // whether the outline is negative (eg circle hole inside a larger circle).
    // This was tested against circles with a single point touching the origin,
    // and extruded with twist.  Diagonal choice determined by whichever option
    // matched the direction of diagonal for neighboring edges (which did not exhibit "equal" diagonals).
    bool flip = ((!o.positive) xor (back_twist));
    int color_ind = colors.size();
    colors.push_back(o.color.unpaintedIfFullyTransparent());
    for (int i = 1; i <= o.vertices.size(); ++i) {
      // curr1: previous slice, current vertex
      // curr2: current slice, current vertex
      Vector2d curr1 = trans1 * o.vertices[i % o.vertices.size()];
      Vector2d curr2 = trans2 * o.vertices[i % o.vertices.size()];
      int curr_idx = curr_outline + (i % o.vertices.size());
      int prev_idx = curr_outline + i - 1;

      int diff_sign = sgn_vdiff(prev1 - curr2, curr1 - prev2);
      bool splitfirst = diff_sign == -1 || (diff_sign == 0 && !flip);

      // Split along shortest diagonal,
      // unless at top for a 0-scaled axis (which can create 0 thickness "ears")
      if (splitfirst xor any_zero) {
        indices.push_back({
          prev_slice + curr_idx,
          curr_slice + curr_idx,
          prev_slice + prev_idx,
        });
        indices.push_back({
          curr_slice + prev_idx,
          prev_slice + prev_idx,
          curr_slice + curr_idx,
        });
      } else {
        indices.push_back({
          prev_slice + curr_idx,
          curr_slice + prev_idx,
          prev_slice + prev_idx,
        });
        indices.push_back({
          prev_slice + curr_idx,
          curr_slice + curr_idx,
          curr_slice + prev_idx,
        });
      }
      color_indices.push_back(color_ind);
      color_indices.push_back(color_ind);
      prev1 = curr1;
      prev2 = curr2;
    }
    curr_outline += o.vertices.size();
  }
}

/**
 * Break out extrusion logic to be used in unit tests.
 * @param slice_stride return value
 * @param vertices return value
 * @param indices return value
 */
void prepareVerticesAndIndices(const Polygon2d& polyref, Vector3d h1, Vector3d h2, int num_slices,
                               double scale_x, double scale_y, double twist,
                               std::vector<Vector3d>& vertices, PolygonIndices& indices,
                               int& slice_stride)
{
  std::vector<Color4f> colors;
  std::vector<int> color_indices;

  for (const auto& o : polyref.outlines()) {
    slice_stride += o.vertices.size();
  }
  vertices.reserve(slice_stride * (num_slices + 1));
  indices.reserve(slice_stride * (num_slices + 1) * 2);  // sides + endcaps

  // Calculate all vertices
  Vector2d full_scale(1 - scale_x, 1 - scale_y);
  double full_rot = -twist;
  auto full_height = (h2 - h1);
  for (unsigned int slice_idx = 0; slice_idx <= num_slices; slice_idx++) {
    Eigen::Affine2d trans(Eigen::Scaling(Vector2d(1, 1) - full_scale * slice_idx / num_slices) *
                          Eigen::Affine2d(rotate_degrees(full_rot * slice_idx / num_slices)));

    for (const auto& o : polyref.outlines()) {
      for (const auto& v : o.vertices) {
        auto tmp = trans * v;
        vertices.emplace_back(Vector3d(tmp[0], tmp[1], 0.0) + h1 + full_height * slice_idx / num_slices);
      }
    }
  }

  // Create indices for sides
  for (unsigned int slice_idx = 1; slice_idx <= num_slices; slice_idx++) {
    double rot_bot = twist * (slice_idx - 1) / num_slices;
    double rot_top = twist * slice_idx / num_slices;
    Vector2d scale_bot(1 - (1 - scale_x) * (slice_idx - 1) / num_slices,
                       1 - (1 - scale_y) * (slice_idx - 1) / num_slices);
    Vector2d scale_top(1 - (1 - scale_x) * slice_idx / num_slices,
                       1 - (1 - scale_y) * slice_idx / num_slices);
    add_slice_indices(indices, colors, color_indices, slice_idx, slice_stride, polyref, rot_bot, rot_top,
                      scale_bot, scale_top);
  }
}

/*! Name each triangle by the record it is made on (checklist E8, open item 40):
 * a wall by the record its outline edge sweeps, looked up by the edge's name -
 * which a split outline carries (CurveDiscretizer::splitOutline) - and each cap
 * by its plane. A triangle whose edge nobody named, or whose record the sweep
 * did not make, is named nothing and left to position, as before.
 *
 * The layout is add_slice_indices' and assemblePolySetForManifold's: for each
 * slice, two triangles for each outline edge, the edge from vertex i - 1 to i
 * of the outline the vertices were laid out from; then the near cap's
 * triangles, then as many of the far cap's. Where the count does not come out
 * so, every name would land on the wrong triangle, and none is given. */
void nameExtrudedFacets(const Polygon2d& polyref, std::size_t num_slices, const SweptFaces& faces,
                        std::size_t first_record, PolySet& polyset)
{
  polyset.surfaces = stampTags(polyset.surfaces);
  auto tag_of = [&](int record) -> uint32_t {
    if (record < 0) return 0;
    const std::size_t at = first_record + std::size_t(record);
    if (at >= polyset.surfaces.size()) return 0;
    const auto& tags = polyset.surfaces[at]->tags;
    return tags.empty() ? 0 : tags.back();
  };
  std::vector<uint32_t> tags;
  tags.reserve(polyset.indices.size());
  for (std::size_t slice = 0; slice < num_slices; slice++) {
    for (const auto& o : polyref.untransformedOutlines()) {
      const std::size_t n = o.vertices.size();
      for (std::size_t i = 1; i <= n; i++) {
        uint32_t tag = 0;
        if (o.hasEdgeIds()) {
          const auto it = faces.walls.find(o.edge_ids[i - 1]);
          if (it != faces.walls.end()) tag = tag_of(it->second);
        }
        tags.push_back(tag);
        tags.push_back(tag);
      }
    }
  }
  const std::size_t walls = tags.size();
  if (walls > polyset.indices.size() || (polyset.indices.size() - walls) % 2 != 0) return;
  const std::size_t cap = (polyset.indices.size() - walls) / 2;
  tags.insert(tags.end(), cap, tag_of(faces.near_end));
  tags.insert(tags.end(), cap, tag_of(faces.far_end));
  polyset.surface_tags = std::move(tags);
}

}  // namespace LinearExtrudeInternals
