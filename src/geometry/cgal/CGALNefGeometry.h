#pragma once

#include "geometry/cgal/cgal.h"
#include "geometry/Geometry.h"
#include "geometry/Surface.h"
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "geometry/linalg.h"

class CGALNefGeometry : public Geometry
{
public:
  VISITABLE_GEOMETRY();
  CGALNefGeometry() = default;
  CGALNefGeometry(std::shared_ptr<const CGAL_Nef_polyhedron3> p,
                  std::vector<std::shared_ptr<Surface>> surfaces = {})
    : p3(std::move(p)), surfaces(std::move(surfaces))
  {
  }
  CGALNefGeometry(const CGALNefGeometry& src);
  CGALNefGeometry& operator=(const CGALNefGeometry&) = default;
  CGALNefGeometry(CGALNefGeometry&&) = default;
  CGALNefGeometry& operator=(CGALNefGeometry&&) = default;
  ~CGALNefGeometry() override = default;

  [[nodiscard]] size_t memsize() const override;
  // FIXME: Implement, but we probably want a high-resolution BBox..
  [[nodiscard]] BoundingBox getBoundingBox() const override;
  [[nodiscard]] std::string dump() const override;
  [[nodiscard]] unsigned int getDimension() const override { return 3; }
  // Empty means it is a geometric node which has zero area/volume
  [[nodiscard]] bool isEmpty() const override;
  [[nodiscard]] std::unique_ptr<Geometry> copy() const override;
  [[nodiscard]] size_t numFacets() const override { return p3->number_of_facets(); }

  void reset() { p3.reset(); }
  CGALNefGeometry operator+(const CGALNefGeometry& other) const;
  CGALNefGeometry& operator+=(const CGALNefGeometry& other);
  CGALNefGeometry& operator*=(const CGALNefGeometry& other);
  CGALNefGeometry& operator-=(const CGALNefGeometry& other);
  CGALNefGeometry& minkowski(const CGALNefGeometry& other);
  void transform(const Transform3d& matrix) override;
  void resize(const Vector3d& newsize, const Eigen::Matrix<bool, 3, 1>& autosize) override;

  std::shared_ptr<const CGAL_Nef_polyhedron3> p3;

  /*! The analytic surfaces the model declared, in world coordinates.
   *
   * The same channel `PolySet::surfaces` and `ManifoldGeometry::surfaces_`
   * carry, and it has to exist here for the same reason: a union or a
   * difference on the CGAL backend converts both operands to Nef polyhedra, and
   * without somewhere to put them every declaration was lost on the way in.
   * That made an analytic export come out silently faceted under
   * `--backend=CGAL` while the identical model exported with surfaces under
   * Manifold.
   *
   * A Nef polyhedron is a set of half-spaces and knows nothing of these; they
   * are carried, not used, until something converts back to a PolySet. */
  std::vector<std::shared_ptr<Surface>> surfaces;
};
