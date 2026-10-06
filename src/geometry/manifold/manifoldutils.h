#pragma once

#include <CGAL/Surface_mesh/Surface_mesh.h>

#include <functional>
#include <memory>
#include <string>
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

/*! `mani` with every tag its records share with `taken` renamed to a fresh
 * one - in the records of `surfaces` and in the tag property of the triangles
 * that carry it, the same renaming in both. What keeps a tag naming one record
 * where two copies of one geometry meet: a geometry evaluated once and placed
 * several times carries the same tags in every copy, and each copy's records
 * stand somewhere else. `mani` itself where nothing is shared. */
manifold::Manifold withFreshTags(const manifold::Manifold& mani,
                                 std::vector<std::shared_ptr<Surface>>& surfaces,
                                 const std::set<uint32_t>& taken);

/*! A faceted leaf's envelope (SolidTree::faceted): the box its faces span. A
 * face on no record is its generator's own exact plane, and its corners are
 * where it is; one on a record reaches where that record says
 * (Surface::envelope). Opaque where a record can say nothing, or where the box
 * has no inside. `record_of` says which record a face is on, or null. */
std::shared_ptr<const SolidTree> facetedEnvelope(const PolySet& ps,
                                                 const std::function<const Surface *(size_t)>& record_of,
                                                 const std::string& what);
/*! A faceted leaf's solid, as its declarations state it: the region a
 * declared sweep bounds (SweptRegion), where every face is named for that one
 * sweep or lies in one of its two end planes - the sweep and its ends being a
 * closed boundary, the volume is the declaration's own. Otherwise its
 * envelope (facetedEnvelope), from which only Out follows. */
std::shared_ptr<const SolidTree> facetedSolid(const PolySet& ps,
                                              const std::function<const Surface *(size_t)>& record_of,
                                              const std::string& what);
/*! Which record its maker named a face of `ps` for (PolySet::surface_tags),
 * or null - never by position (checklist E8). For a PolySet not yet a
 * Manifold. */
std::function<const Surface *(size_t)> recordByName(const PolySet& ps);

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
