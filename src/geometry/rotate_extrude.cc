#include "rotate_extrude.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "core/CurveDiscretizer.h"
#include "core/RotateExtrudeNode.h"
#include "geometry/Geometry.h"
#include "geometry/GeometryUtils.h"
#include "geometry/PolySet.h"
#include "geometry/PolySetBuilder.h"
#include "geometry/PolySetUtils.h"
#include "geometry/Polygon2d.h"
#include "geometry/Barcode1d.h"
#include "geometry/Surface.h"
#include "geometry/linalg.h"
#include "geometry/GeometryEvaluator.h"
#include "utils/calc.h"
#include "utils/degree_trig.h"
#include "utils/printutils.h"
#ifdef ENABLE_MANIFOLD
#include "src/geometry/manifold/manifoldutils.h"
#endif

#ifdef ENABLE_MANIFOLD
static std::unique_ptr<PolySet> assemblePolySetForManifold(
  const Polygon2d& polyref, std::vector<Vector3d>& vertices, PolygonIndices& indices,
  std::vector<Color4f>& colors, std::vector<int> color_indices, bool closed, int convexity,
  int index_offset, bool flip_faces)
{
  auto final_polyset = std::make_unique<PolySet>(3, false);
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

  if (!closed) {
    // Create top and bottom face.
    auto ps_bottom = polyref.tessellate();  // bottom
    // Flip vertex ordering for bottom polygon unless flip_faces is true
    if (!flip_faces) {
      for (auto& p : ps_bottom->indices) {
        std::reverse(p.begin(), p.end());
      }
    }
    std::copy(ps_bottom->indices.begin(), ps_bottom->indices.end(),
              std::back_inserter(final_polyset->indices));

    for (auto& p : ps_bottom->indices) {
      std::reverse(p.begin(), p.end());
      for (auto& i : p) {
        i += index_offset;
      }
    }
    std::copy(ps_bottom->indices.begin(), ps_bottom->indices.end(),
              std::back_inserter(final_polyset->indices));
  }

  for (int j = final_polyset->color_indices.size(); j < final_polyset->indices.size(); j++) {
    final_polyset->color_indices.push_back(colormap[final_polyset->indices[j][0]]);
  }

  //  LOG(PolySetUtils::polySetToPolyhedronSource(*final_polyset));

  return final_polyset;
}
#else
// Version when Manifold is not available - provides basic functionality
static std::unique_ptr<PolySet> assemblePolySetForManifold(const Polygon2d& polyref,
                                                           std::vector<Vector3d>& vertices,
                                                           PolygonIndices& indices, bool closed,
                                                           int convexity, int index_offset,
                                                           bool flip_faces)
{
  auto final_polyset = std::make_unique<PolySet>(3, false);
  final_polyset->setTriangular(true);
  final_polyset->setConvexity(convexity);
  final_polyset->vertices = std::move(vertices);
  final_polyset->indices = std::move(indices);

  if (!closed) {
    // Create top and bottom face using basic tessellation
    // This provides basic functionality without Manifold
    auto ps_bottom = polyref.tessellate();  // bottom
    // Flip vertex ordering for bottom polygon unless flip_faces is true
    if (!flip_faces) {
      for (auto& p : ps_bottom->indices) {
        std::reverse(p.begin(), p.end());
      }
    }
    std::copy(ps_bottom->indices.begin(), ps_bottom->indices.end(),
              std::back_inserter(final_polyset->indices));

    for (auto& p : ps_bottom->indices) {
      std::reverse(p.begin(), p.end());
      for (auto& i : p) {
        i += index_offset;
      }
    }
    std::copy(ps_bottom->indices.begin(), ps_bottom->indices.end(),
              std::back_inserter(final_polyset->indices));
  }

  return final_polyset;
}
#endif

/*!
   Input to extrude should be clean. This means non-intersecting, correct winding order
   etc., the input coming from a library like Clipper.

   FIXME: We should handle some common corner cases better:
   o 2D polygon having an edge being on the Y axis:
    In this case, we don't need to generate geometry involving this edge as it
    will be an internal edge.
   o 2D polygon having a vertex touching the Y axis:
    This is more complex as the resulting geometry will (may?) be nonmanifold.
    In any case, the previous case is a specialization of this, so the following
    should be handled for both cases:
    Since the ring associated with this vertex will have a radius of zero, it will
    collapse to one vertex. Any quad using this ring will be collapsed to a triangle.

   Currently, we generate a lot of zero-area triangles
 */
VectorOfVector2d alterprofile(VectorOfVector2d vertices, double scalex, double scaley, double origin_x,
                              double origin_y, double offset_x, double offset_y, double rot);

/*! Record the surfaces of revolution a straight profile edge sweeps out.
 *
 * A rotate_extrude of a *line segment* produces exactly what the analytic
 * exporter already knows how to write: a cylinder where the segment is parallel
 * to the axis, a frustum where it is tilted. There is no emission work in it at
 * all, only the statement of intent - which is needed, because a ring of N
 * quads is equally the mesh of an N sided prism and nothing in the geometry
 * says which was meant.
 *
 * A tilted segment declares the circle at each of its ends, which is how a cone
 * states its intent everywhere else in this codebase: an exporter accepts one
 * when both of its rims match a declared cylinder.
 *
 * Only for a sweep whose stations are all the same profile in the same place. A
 * twist, a helical `v`, or a Python `profile_func` makes every station
 * different, and what comes out is then not a surface of revolution at all -
 * which is exactly how a screw thread is built, so this is not a corner case.
 */
static void declareSurfacesOfRevolution(const RotateExtrudeNode& node,
                                        const std::vector<VectorOfVector2d>& profiles, PolySet& polyset)
{
  if (node.v.norm() != 0 || node.twist != 0) return;
#ifdef ENABLE_PYTHON
  if (node.profile_func != nullptr || node.twist_func != nullptr) return;
#endif

  // the profile's x is the radius and its y is the height along the axis
  const double eps = 1e-12;
  // One record per radius. A vertex shared by two walls would otherwise be
  // declared twice, and a record is only ever matched on its radius and axis -
  // the height it carries says where the circle was, not how far the surface
  // reaches, since a boolean may since have cut it back.
  std::vector<double> declared;
  auto declare = [&](const Vector2d& p) {
    for (const double r : declared) {
      if (fabs(r - p[0]) <= eps * std::max(1.0, r)) return;
    }
    declared.push_back(p[0]);
    polyset.surfaces.push_back(
      std::make_shared<CylinderSurface>(Vector3d(0, 0, p[1]), Vector3d(0, 0, 1), p[0]));
  };

  for (const auto& profile : profiles) {
    const std::size_t n = profile.size();
    for (std::size_t i = 0; i < n; i++) {
      const Vector2d& a = profile[i];
      const Vector2d& b = profile[(i + 1) % n];
      // An edge at one height sweeps a flat annulus, and one touching the axis
      // sweeps a disc or an apex. Neither is a wall.
      if (fabs(a[1] - b[1]) < eps) continue;
      if (a[0] <= eps || b[0] <= eps) continue;
      declare(a);
      if (fabs(a[0] - b[0]) > eps) declare(b);
    }
  }
}

std::unique_ptr<PolySet> rotatePolygonSub(const RotateExtrudeNode& node, const Polygon2d& poly,
                                          int fragments, size_t fragstart, size_t fragend,
                                          bool flip_faces)
{
  double fact = (node.v[2] / node.angle) * (180.0 / G_PI);

  // # of sections. For closed rotations, # vertices is thus fragments*outline_size. For open
  // rotations # vertices is (fragments+1)*outline_size.
  const auto num_sections = fragend - fragstart;
  const bool closed = node.angle == 360 && node.v.norm() == 0;
  // # of rings of vertices
  const size_t num_rings = num_sections + (closed ? 0 : 1);

  // slice_stride is the number of vertices in a single ring
  size_t slice_stride = 0;
  int num_vertices = 0;
#ifdef ENABLE_PYTHON
  if (node.profile_func != NULL) {
    Outline2d outl = python_getprofile(node.profile_func, 3, 0);
    slice_stride += outl.vertices.size();
  } else
#endif
  {
    for (const auto& o : poly.outlines()) {
      slice_stride += o.vertices.size();
    }
  }
  num_vertices = slice_stride * num_rings;
  std::vector<Vector3d> vertices;
  std::vector<VectorOfVector2d> first_ring;
  vertices.reserve(num_vertices);
  PolygonIndices indices;
  std::vector<int> color_indices;
  std::vector<Color4f> colors;
  indices.reserve(slice_stride * num_rings * 2);  // sides + endcaps if needed
  color_indices.reserve(slice_stride * num_rings * 2);

  for (unsigned int j = fragstart; j <= fragend; ++j) {
    Vector3d dv = node.v * j / fragments;

    for (const auto& outline : poly.outlines()) {
      const double angle = node.start + j * node.angle / fragments;  // start on the X axis
      VectorOfVector2d vertices2d;
      double cur_twist = 0;
#ifdef ENABLE_PYTHON
      if (node.profile_func != NULL) {
        Outline2d lastFace;
        Outline2d curFace;
        Outline2d outl = python_getprofile(node.profile_func, 3, j / (double)fragments);
        vertices2d = outl.vertices;
      } else
#endif
        vertices2d = outline.vertices;
#ifdef ENABLE_PYTHON
      if (node.twist_func != NULL) cur_twist = python_doublefunc(node.twist_func, 0);
      else
#endif
        cur_twist = node.twist * j / fragments;
      vertices2d = alterprofile(vertices2d, 1.0, 1.0, node.origin_x, node.origin_y, node.offset_x,
                                node.offset_y, cur_twist);
      double xmid = NAN;
      if (node.method == "centered") {
        double xmin, xmax;
        xmin = xmax = vertices2d[0][0];
        for (const auto& v : vertices2d) {
          if (v[0] < xmin) xmin = v[0];
          if (v[0] > xmax) xmax = v[0];
        }
        xmid = (xmin + xmax) / 2;
      }

      // The profile as it is actually swept, after alterprofile() has applied
      // the origin and offset. Kept for the surface records below, which have
      // to describe where the wall ended up rather than where it was drawn.
      if (j == fragstart) first_ring.push_back(vertices2d);

      for (const auto& v : vertices2d) {
        double tan_pitch = fact / (std::isnan(xmid) ? v[0] : xmid);
        //
        // cos(atan(x))=1/sqrt(1+x*x)
        // sin(atan(x))=x/sqrt(1+x*x)
        double cf = 1 / sqrt(1 + tan_pitch * tan_pitch);
        double sf = cf * tan_pitch;
        Vector3d centripedal = Vector3d(cos_degrees(angle), sin_degrees(angle), 0);
        Vector3d progress = Vector3d(-sin_degrees(angle) * cf, cos_degrees(angle) * cf, sf);
        Vector3d upwards = centripedal.cross(progress);
        Vector3d res = centripedal * v[0] + upwards * v[1] + dv;
        vertices.emplace_back(res);
      }  // vertices
    }  // outlines
  }  // fragments/rings

  // Calculate all indices
  for (unsigned int slice_idx = 1; slice_idx <= num_sections; slice_idx++) {
    const int prev_slice = (slice_idx - 1) * slice_stride;
    const int curr_slice = slice_idx * slice_stride;
    int curr_outline = 0;
    for (const auto& outline : poly.outlines()) {
      assert(outline.vertices.size() > 2);
      int color_ind = colors.size();
      colors.push_back(outline.color);  // TODO effizienter
      for (size_t i = 1; i <= outline.vertices.size(); ++i) {
        const int curr_idx = curr_outline + (i % outline.vertices.size());
        const int prev_idx = curr_outline + i - 1;
        if (flip_faces) {
          indices.push_back({
            (prev_slice + prev_idx) % num_vertices,
            (curr_slice + curr_idx) % num_vertices,
            (prev_slice + curr_idx) % num_vertices,
          });
          indices.push_back({
            (curr_slice + curr_idx) % num_vertices,
            (prev_slice + prev_idx) % num_vertices,
            (curr_slice + prev_idx) % num_vertices,
          });
        } else {
          indices.push_back({
            (prev_slice + curr_idx) % num_vertices,
            (curr_slice + curr_idx) % num_vertices,
            (prev_slice + prev_idx) % num_vertices,
          });
          indices.push_back({
            (curr_slice + prev_idx) % num_vertices,
            (prev_slice + prev_idx) % num_vertices,
            (curr_slice + curr_idx) % num_vertices,
          });
        }
        color_indices.push_back(color_ind);
        color_indices.push_back(color_ind);
      }
      curr_outline += outline.vertices.size();
    }
  }

  // TODO(kintel): Without Manifold, we don't have such tessellator available which guarantees to not
  // modify vertices, so we technically may end up with broken end caps if we build OpenSCAD without
  // ENABLE_MANIFOLD. Should be fixed, but it's low priority and it's not trivial to come up with a test
  // case for this.
  auto result = assemblePolySetForManifold(poly, vertices, indices, colors, color_indices, closed,
                                           node.convexity, slice_stride * num_sections, flip_faces);
  if (result != nullptr) declareSurfacesOfRevolution(node, first_ring, *result);
  return result;
}

std::unique_ptr<Geometry> rotatePolygon(const RotateExtrudeNode& node, const Polygon2d& poly)
{
  if (node.angle == 0) return nullptr;

  double min_x = 0;
  double max_x = 0;
  size_t fragments = 0;
  for (const auto& o : poly.outlines()) {
    for (const auto& v : o.vertices) {
      min_x = fmin(min_x, v[0]);
      max_x = fmax(max_x, v[0]);
    }
  }

  if (max_x > 0 && min_x < 0) {
    LOG(message_group::Error,
        "Children of rotate_extrude() may not lie across the Y axis (Range of X coords for all children "
        "[%1$.2f : %2$.2f])",
        min_x, max_x);
    return nullptr;
  }

  const int num_sections = node.discretizer.getCircularSegmentCount(max_x - min_x, node.angle)
                             .value_or(std::max(1, static_cast<int>(std::fabs(node.angle) / 360 * 3)));
  const bool closed = node.angle == 360;

  bool flip_faces = (min_x >= 0 && node.angle > 0) || (min_x < 0 && node.angle < 0);

  // check if its save to extrude
  bool safe = true;
  do {
    if (node.angle < 300) break;
    if (node.v.norm() == 0) break;
    if (node.v[2] / (node.angle / 360.0) > (max_x - min_x) * 1.5) break;
    safe = false;

  } while (false);
  if (safe) return rotatePolygonSub(node, poly, num_sections, 0, num_sections, flip_faces);

  // now create a fragment splitting plan
  size_t splits = ceil(node.angle / 300.0);
  fragments = num_sections;
  size_t fragstart = 0, fragend;
  std::vector<std::shared_ptr<PolySet>> result_s;
  for (size_t i = 0; i < splits; i++) {
    fragend = fragstart + (fragments / splits) + 1;
    if (fragend > fragments) fragend = fragments;
    std::unique_ptr<PolySet> part_u =
      rotatePolygonSub(node, poly, fragments, fragstart, fragend, flip_faces);
    std::shared_ptr<PolySet> part_s = std::move(part_u);
    result_s.push_back(part_s);
    fragstart = fragend - 1;
  }
  return union_geoms(result_s);
}

std::unique_ptr<Geometry> rotateBarcode(const RotateExtrudeNode& node, const Barcode1d& barcode)
{
  Polygon2d p;
  for (auto e : barcode.untransformedEdges()) {
    if (node.angle == 360 && node.v.norm() < 1e-6) {
      for (int j = 0; j < 2; j++) {
        double d = (j == 0) ? e.end : e.begin;
        int fragments = node.discretizer.getCircularSegmentCount(d, 360).value_or(3);
        Outline2d o;
        o.color = e.color;
        o.vertices.resize(fragments);
        for (int i = 0; i < fragments; ++i) {
          double phi = (360 * ((j == 0) ? i : (fragments - 1 - i))) / fragments;
          o.vertices[i] = {d * cos_degrees(phi), d * sin_degrees(phi)};
        }
        p.addOutline(o);
      }
    } else {
      double v = node.v.norm();
      int fragments = node.discretizer.getCircularSegmentCount(e.end, node.angle).value_or(3);
      Outline2d o;
      o.color = e.color;
      o.vertices.resize(2 * fragments);
      for (int i = 0; i < fragments; ++i) {
        double vext = v * i / (fragments + 1);
        double phi = node.angle * i / (fragments - 1);
        o.vertices[i] = {(e.end + vext) * cos_degrees(phi), (e.end + vext) * sin_degrees(phi)};
        o.vertices[2 * fragments - 1 - i] = {(e.begin + vext) * cos_degrees(phi),
                                             (e.begin + vext) * sin_degrees(phi)};
      }
      p.addOutline(o);
    }
  }
  p.transform3d(barcode.getTransform3d());
  p.setSanitized(true);
  return std::make_unique<Polygon2d>(p);
}
