#pragma once

#include <CGAL/Surface_mesh/Surface_mesh.h>

#include <memory>
#include <vector>

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

/*! `mesh` as one vertex per position: the copies Manifold keeps where a vertex
 * property differs between the triangles meeting there - the surface tag a
 * geometry carries (PolySet::surface_tags) - folded back through the mesh's own
 * merge lists, and the properties past position dropped. What every reader
 * that wants the topology rather than the properties takes. */
manifold::MeshGL64 topologyMesh(const manifold::MeshGL64& mesh);

/*! Per property vertex of `mesh`, the one it is merged into - itself where it
 * is merged into nothing. */
std::vector<size_t> canonicalVertices(const manifold::MeshGL64& mesh);

/*! `topology` - one vertex per position, positions only - with a fourth
 * property carrying each triangle's surface tag (PolySet::surface_tags): a
 * copy of a vertex per tag meeting there, merged back to the first. Itself
 * where no triangle is tagged. */
manifold::MeshGL64 withTagProperty(const manifold::MeshGL64& topology,
                                   const std::vector<uint32_t>& tri_tag);

/*! The tag of the one face-declaring record of `surfaces` a triangle lies on
 * wholly at machine precision; 0 where it lies on none, or on several. */
uint32_t triangleTag(const Vector3d& a, const Vector3d& b, const Vector3d& c,
                     const std::vector<std::shared_ptr<Surface>>& surfaces);

/*! `mani` with every untagged triangle tagged with the record of `surfaces`
 * it was made on - see triangleTag - where records are stamped onto geometry
 * whose triangles already exist: a declaration over a subtree already a
 * Manifold, a hull. Gives each face record in `surfaces` a tag of its own
 * (stampTags). `mani` itself where nothing new is tagged. */
manifold::Manifold withSurfaceTags(const manifold::Manifold& mani,
                                   std::vector<std::shared_ptr<Surface>>& surfaces);

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
