#include "geometry/cgal/CGALNefGeometry.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "geometry/Geometry.h"
#include "geometry/cgal/cgal.h"
#include "geometry/cgal/cgalutils.h"
#include "geometry/Surface.h"
#include "geometry/linalg.h"
#include "utils/printutils.h"
#include "utils/svg.h"

// Copy constructor only performs shallow copies, so all modifying functions
// must reset p3 with a new CGAL_Nef_polyhedron3 object, to prevent cache corruption.
// This is also partly enforced by p3 pointing to a const object.
CGALNefGeometry::CGALNefGeometry(const CGALNefGeometry& src) : Geometry(src)
{
  if (src.p3) this->p3 = src.p3;
  this->surfaces = src.surfaces;
}

std::unique_ptr<Geometry> CGALNefGeometry::copy() const
{
  return std::make_unique<CGALNefGeometry>(*this);
}

// Every boolean keeps both operands' surface records - see mergeSurfaces() for
// why a subtraction keeps the tool's as well.
CGALNefGeometry CGALNefGeometry::operator+(const CGALNefGeometry& other) const
{
  auto merged = this->surfaces;
  mergeSurfaces(merged, other.surfaces);
  return {std::make_shared<CGAL_Nef_polyhedron3>((*this->p3) + (*other.p3)), std::move(merged)};
}

CGALNefGeometry& CGALNefGeometry::operator+=(const CGALNefGeometry& other)
{
  this->p3 = std::make_shared<CGAL_Nef_polyhedron3>((*this->p3) + (*other.p3));
  mergeSurfaces(this->surfaces, other.surfaces);
  return *this;
}

CGALNefGeometry& CGALNefGeometry::operator*=(const CGALNefGeometry& other)
{
  this->p3 = std::make_shared<CGAL_Nef_polyhedron3>((*this->p3) * (*other.p3));
  mergeSurfaces(this->surfaces, other.surfaces);
  return *this;
}

CGALNefGeometry& CGALNefGeometry::operator-=(const CGALNefGeometry& other)
{
  this->p3 = std::make_shared<CGAL_Nef_polyhedron3>((*this->p3) - (*other.p3));
  mergeSurfaces(this->surfaces, other.surfaces);
  return *this;
}

// Note: this is only the fallback method in case of failure in CGALUtils::applyMinkowski (see:
// cgalutils-applyops.cc)
CGALNefGeometry& CGALNefGeometry::minkowski(const CGALNefGeometry& other)
{
  // It is required to construct copies of our const input operands here.
  // "Postcondition: If either of the input polyhedra is non-convex, it is modified during the
  // computation,
  //  i.e., it is decomposed into convex pieces."
  // from https://doc.cgal.org/latest/Minkowski_sum_3/group__PkgMinkowskiSum3Ref.html
  CGAL_Nef_polyhedron3 op1(*this->p3);
  CGAL_Nef_polyhedron3 op2(*other.p3);
  this->p3 = std::make_shared<CGAL_Nef_polyhedron3>(CGAL::minkowski_sum_3(op1, op2));
  // A minkowski sum changes every radius it touches, so no declaration survives
  // it. Dropping them is deliberate: the same decision the Manifold backend
  // makes, and the one case where a record could still fit some other feature
  // of the result and be acted on wrongly.
  this->surfaces.clear();
  return *this;
}

size_t CGALNefGeometry::memsize() const
{
  if (this->isEmpty()) return 0;

  auto memsize = sizeof(CGALNefGeometry);
  memsize += const_cast<CGAL_Nef_polyhedron3&>(*this->p3).bytes();
  return memsize;
}

bool CGALNefGeometry::isEmpty() const
{
  return !this->p3 || this->p3->is_empty();
}

BoundingBox CGALNefGeometry::getBoundingBox() const
{
  if (isEmpty()) {
    return {};
  }
  auto bb = CGALUtils::boundingBox(*this->p3).bbox();

  BoundingBox result;
  result.extend(Vector3d(bb.xmin(), bb.ymin(), bb.zmin()));
  result.extend(Vector3d(bb.xmax(), bb.ymax(), bb.zmax()));
  return result;
}

void CGALNefGeometry::resize(const Vector3d& newsize, const Eigen::Matrix<bool, 3, 1>& autosize)
{
  // Based on resize() in Giles Bathgate's RapCAD (but not exactly)
  if (this->isEmpty()) return;

  transform(CGALUtils::computeResizeTransform(CGALUtils::boundingBox(*this->p3), getDimension(), newsize,
                                              autosize));
}

std::string CGALNefGeometry::dump() const
{
  return OpenSCAD::dump_svg(*this->p3);
}

void CGALNefGeometry::transform(const Transform3d& matrix)
{
  if (!this->isEmpty()) {
    if (matrix.matrix().determinant() == 0) {
      LOG(message_group::Warning, "Scaling a 3D object with 0 - removing object");
      this->reset();
    } else {
      auto N = std::make_shared<CGAL_Nef_polyhedron3>(*this->p3);
      CGALUtils::transform(*N, matrix);
      this->p3 = N;
      // Records are held in world coordinates, so they move with the geometry.
      // A surface which cannot be represented after the transform - a cylinder
      // under a non uniform scale - is dropped rather than left wrong.
      std::vector<std::shared_ptr<Surface>> moved;
      for (const auto& surface : this->surfaces) {
        auto copy = surface->clone();
        if (copy->transform(matrix)) moved.push_back(copy);
      }
      this->surfaces = std::move(moved);
    }
  }
}
