#pragma once

#include <cstddef>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "geometry/Geometry.h"
#include "geometry/linalg.h"

/*!
   A single contour.
   positive is (optionally) used to distinguish between polygon contours and hole contours.
 */
struct Outline2d {
  Outline2d() = default;
  VectorOfVector2d vertices;
  bool positive{true};
  Color4f color;
  [[nodiscard]] BoundingBox getBoundingBox() const;
};
double outline_area(const Outline2d o);

/*! A circle that some of a polygon's vertices were meant to lie on.
 *
 * `Polygon2d` holds points, so a circle is dead the moment it is discretised:
 * an extruder sees sixty vertices and cannot tell a cylinder from a sixty-sided
 * prism. This is the same channel `PolySet::surfaces` is for the 3D side, one
 * level down - a *hint*, recorded by whoever knew the mathematics, carried
 * along, and re-checked against the geometry by whoever consumes it.
 *
 * Being a hint is what makes it cheap. A Clipper operation may have trimmed the
 * arc away, split it, or left nothing of it; keeping a record that no longer
 * describes anything costs nothing, because the consumer fits it to the mesh
 * before believing it. Dropping one costs everything, because exactness cannot
 * be recovered from a polygon.
 *
 * The centre is in the polygon's own 2D frame, before `getTransform3d()`.
 */
struct Arc2d {
  /* Deliberately unaligned. A plain Vector2d makes this struct 24 bytes, so in
   * a std::vector every other element lands off a 16 byte boundary and Eigen's
   * vectorised load segfaults - which is exactly what circle() did the first
   * time it recorded one. Eigen::aligned_allocator does not help: it aligns the
   * allocation, not the stride. DontAlign is the fix that keeps the member
   * usable as a vector everywhere else. */
  Eigen::Matrix<double, 2, 1, Eigen::DontAlign> centre{0., 0.};
  double r{0.};
};

class Polygon2d : public Geometry
{
  enum class Transform3dState { NONE = 0, PENDING = 1, CACHED = 2 };

public:
  VISITABLE_GEOMETRY();
  Polygon2d() = default;
  Polygon2d(Outline2d outline);
  [[nodiscard]] size_t memsize() const override;
  [[nodiscard]] BoundingBox getBoundingBox() const override;
  [[nodiscard]] std::string dump() const override;
  [[nodiscard]] unsigned int getDimension() const override { return 2; }
  [[nodiscard]] bool isEmpty() const override;
  [[nodiscard]] std::unique_ptr<Geometry> copy() const override;
  [[nodiscard]] size_t numFacets() const override
  {
    return std::accumulate(theoutlines.begin(), theoutlines.end(), 0,
                           [](size_t a, const Outline2d& b) { return a + b.vertices.size(); });
  }
  void addOutline(const Outline2d& outline)
  {
    if (trans3dState != Transform3dState::NONE) mergeTrans3d();
    this->theoutlines.push_back(outline);
  }
  void addPolyline(const Outline2d& polyline)
  {
    if (trans3dState != Transform3dState::NONE) mergeTrans3d();
    this->thepolylines.push_back(polyline);
  }
  /*! Circles some of this polygon's vertices lie on. See Arc2d: hints, not
   * guarantees, and consumed by re-checking them against the geometry. */
  std::vector<Arc2d> arcs;

  void reverse(void);
  [[nodiscard]] std::unique_ptr<PolySet> tessellate(bool in3d = false) const;
  [[nodiscard]] double area() const;

  using Outlines2d = std::vector<Outline2d>;
  // Note: The "using" here is a kludge to avoid a compiler warning.
  // It would be better to fix the class relationships, so that Polygon2d does
  // not inherit an unused 3d transform function.
  // But that will likely require significant refactoring.
  const Outlines2d& outlines() const
  {
    return trans3dState == Transform3dState::NONE ? theoutlines : transformedOutlines();
  }
  const Outlines2d& polylines() const
  {
    return trans3dState == Transform3dState::NONE ? thepolylines : transformedPolylines();
  }
  const Outlines2d& untransformedOutlines() const { return theoutlines; }
  const Outlines2d& untransformedPolylines() const { return thepolylines; }
  const Outlines2d& transformedOutlines() const;
  const Outlines2d& transformedPolylines() const;
  using Geometry::transform;

  void transform(const Transform2d& mat);
  void transform3d(const Transform3d& mat);
  bool hasTransform3d() const { return trans3dState != Transform3dState::NONE; }
  const Transform3d& getTransform3d() const
  {
    // lazy initialization doesn't actually violate 'const'
    if (trans3dState == Transform3dState::NONE)
      const_cast<Polygon2d *>(this)->trans3d = Transform3d::Identity();
    return trans3d;
  }
  void resize(const Vector2d& newsize, const Eigen::Matrix<bool, 2, 1>& autosize);
  void resize(const Vector3d& newsize, const Eigen::Matrix<bool, 3, 1>& autosize) override
  {
    resize(Vector2d(newsize[0], newsize[1]), Eigen::Matrix<bool, 2, 1>(autosize[0], autosize[1]));
  }

  [[nodiscard]] bool isSanitized() const { return this->sanitized; }
  void setSanitized(bool s) { this->sanitized = s; }
  [[nodiscard]] bool is_convex() const;
  void setColor(const Color4f& c) override;
  void setColorUndef(const Color4f& c);
  void stamp_color(const Polygon2d& src);
  void stamp_color(const Outline2d& src);
  bool point_inside(const Vector2d& pt) const;

private:
  Outlines2d theoutlines;
  Outlines2d thepolylines;
  bool sanitized{false};
  Transform3dState trans3dState{Transform3dState::NONE};
  Transform3d trans3d;
  Outlines2d trans3dOutlines;
  Outlines2d trans3dPolylines;
  void mergeTrans3d();
  void transformArcs(const Transform2d& mat);
  void applyTrans3dToOutlines(Polygon2d::Outlines2d& outlines) const;
};
