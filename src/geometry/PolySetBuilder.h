#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "geometry/Geometry.h"
#include "geometry/GeometryUtils.h"
#include "geometry/Polygon2d.h"
#include "geometry/Reindexer.h"
#include "geometry/linalg.h"
#include "utils/boost-utils.h"
#include "geometry/Curve.h"
#include "geometry/Surface.h"

class PolySet;

class PolySetBuilder
{
public:
  PolySetBuilder(int vertices_count = 0, int indices_count = 0, int dim = 3,
                 boost::tribool convex = unknown);
  void reserve(int vertices_count = 0, int indices_count = 0);
  void setConvexity(int n);
  int vertexIndex(const Vector3d& coord);
  int numVertices() const;
  int numPolygons() const;
  bool isEmpty() const;

  void appendPolySet(const PolySet& ps);
  void appendGeometry(const std::shared_ptr<const Geometry>& geom);
  void appendPolygon(const std::vector<int>& inds);
  void appendPolygon(const std::vector<Vector3d>& v);

  void beginPolygon(int nvertices);
  void addVertex(int ind);
  void addVertex(const Vector3d& v);
  // Calling this is optional; will be called automatically when adding a new polygon or building the
  // PolySet
  void endPolygon(const Color4f& color = {});
  void copyVertices(std::vector<Vector3d>& vertices);
  void copyVertices(std::vector<Vector3f>& vertices);
  void addCurve(std::shared_ptr<Curve> curve);
  void addSurface(std::shared_ptr<Surface> surface);
  /*! Name every polygon appended from here on for the record carrying `tag`
   * (Surface::tags), or for none with 0 - the maker's word, which the built
   * PolySet carries as its surface_tags (checklist E8). */
  void setSurfaceTag(uint32_t tag) { current_tag_ = tag; }
  void addColor(const Color4f& color);
  void addColorIndex(int idx);  // should be paired with begin/endPolygon()

  std::unique_ptr<PolySet> build();

private:
  Reindexer<Vector3d> vertices_;
  PolygonIndices indices_;
  std::vector<int32_t> color_indices_;
  std::vector<Color4f> colors_;
  std::vector<std::shared_ptr<Curve>> curves;
  std::vector<std::shared_ptr<Surface>> surfaces;
  int convexity_{1};
  int dim_;
  boost::tribool convex_;

  // Will be initialized by beginPolygon() and cleared by endPolygon()
  IndexedFace current_polygon_;
  // The name of each polygon kept, in step with indices_ - see setSurfaceTag.
  std::vector<uint32_t> tags_;
  uint32_t current_tag_ = 0;
  bool named_ = false;
};
