#pragma once

#include <CGAL/Surface_mesh/Surface_mesh.h>

#include <memory>

#include "core/enums.h"
#include "geometry/Geometry.h"
#include "geometry/manifold/ManifoldGeometry.h"
#include "manifold/manifold.h"

namespace ManifoldUtils {

/*! The identity a PolySet would have been given had it passed through a
 * boolean: one original id per colour group, minted from Manifold's own
 * allocator, carried on every face, and stamped on the surfaces it declares -
 * faces as owners, evidence as witnesses. The same rule createManifoldFrom*
 * mints by, for a geometry that never went through one: a lone generator's
 * output is one maker, and without this it had no identity at all
 * (doc/step-export-wip.md, open item 35). */
std::shared_ptr<PolySet> mintOriginals(const PolySet& ps);

const char *statusToString(manifold::Manifold::Error status);

std::shared_ptr<ManifoldGeometry> createManifoldFromPolySet(const PolySet& ps);
std::shared_ptr<const ManifoldGeometry> createManifoldFromGeometry(
  const std::shared_ptr<const Geometry>& geom);

template <class SurfaceMesh>
std::shared_ptr<ManifoldGeometry> createManifoldFromSurfaceMesh(const SurfaceMesh& mesh);
template <typename SurfaceMesh>
std::shared_ptr<SurfaceMesh> createSurfaceMeshFromManifold(const manifold::Manifold& mani);

std::shared_ptr<ManifoldGeometry> applyOperator3DManifold(const Geometry::Geometries& children,
                                                          OpenSCADOperator op);

Polygon2d polygonsToPolygon2d(const manifold::Polygons& polygons);

#ifdef ENABLE_CGAL
// FIXME: This shouldn't return const, but it does due to internal implementation details.
std::shared_ptr<const Geometry> applyMinkowski(const Geometry::Geometries& children);
#endif

std::unique_ptr<PolySet> createTriangulatedPolySetFromPolygon2d(const Polygon2d& polygon2d, bool in3d);
};  // namespace ManifoldUtils
