// Portions of this file are Copyright 2023 Google LLC, and licensed under GPL2+. See COPYING.
#include "geometry/manifold/manifoldutils.h"

#include <manifold/polygon.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>
#ifdef ENABLE_CGAL
#include <CGAL/Surface_mesh.h>
#include <CGAL/convex_hull_3.h>
#endif

#include "geometry/Geometry.h"
#include "geometry/PolySet.h"
#include "geometry/PolySetBuilder.h"
#include "geometry/PolySetUtils.h"
#include "geometry/linalg.h"
#include "geometry/manifold/ManifoldGeometry.h"
#include "geometry/recognition/Exact.h"
#include "geometry/Surface.h"
#include "utils/printutils.h"
#ifdef ENABLE_CGAL
#include "geometry/cgal/cgalutils.h"
#endif
#include "src/core/ColorUtil.h"

using Error = manifold::Manifold::Error;

namespace {

/*! A PolySet's faces by colour - one original per group, which is the rule by
 * which an original id is minted (see mintOriginals). Shared by the two places
 * that mint, so they cannot disagree about what one original is. */
std::map<std::optional<Color4f>, std::vector<size_t>> colourGroups(const PolySet& ps)
{
  std::map<std::optional<Color4f>, std::vector<size_t>> groups;
  for (size_t i = 0, n = ps.indices.size(); i < n; i++) {
    auto color_index = i < ps.color_indices.size() ? ps.color_indices[i] : -1;
    std::optional<Color4f> color;
    if (color_index >= 0) {
      color = ps.colors[color_index];
    }
    groups[color].push_back(i);
  }
  return groups;
}

std::shared_ptr<ManifoldGeometry> createManifoldFromTriangularPolySet(const PolySet& ps)
{
  assert(ps.isTriangular());

  manifold::MeshGL64 mesh;

  mesh.numProp = 3;
  mesh.vertProperties.reserve(ps.vertices.size() * 3);
  for (const auto& v : ps.vertices) {
    mesh.vertProperties.push_back(v.x());
    mesh.vertProperties.push_back(v.y());
    mesh.vertProperties.push_back(v.z());
  }

  mesh.triVerts.reserve(ps.indices.size() * 3);

  std::set<uint32_t> originalIDs;
  std::map<uint32_t, Color4f> originalIDToColor;

  const std::map<std::optional<Color4f>, std::vector<size_t>> colorToFaceIndices = colourGroups(ps);
  auto next_id = manifold::Manifold::ReserveIDs(colorToFaceIndices.size());
  for (const auto& [color, faceIndices] : colorToFaceIndices) {
    auto id = next_id++;
    if (color.has_value()) {
      originalIDToColor[id] = color.value();
    }

    mesh.runIndex.push_back(mesh.triVerts.size());
    mesh.runOriginalID.push_back(id);
    originalIDs.insert(id);

    for (size_t faceIndex : faceIndices) {
      auto& face = ps.indices[faceIndex];
      assert(face.size() == 3);
      mesh.triVerts.push_back(face[0]);
      mesh.triVerts.push_back(face[1]);
      mesh.triVerts.push_back(face[2]);
    }
  }
  mesh.runIndex.push_back(mesh.triVerts.size());

  // Which declared surface each triangle was made on - identity, carried from
  // here through every boolean as a vertex property. See PolySet::surface_tags
  // and Surface::tags. A triangle this geometry already names keeps its name;
  // any other is its generator's own, and names the one surface it lies on
  // wholly at machine precision (checklist E3: a generator's points are on its
  // own declaration), or none - where it lies on none, or on several, identity
  // is what is missing, and nothing is guessed.
  const std::vector<std::shared_ptr<Surface>> tagged = stampTags(ps.surfaces);
  const bool carried = ps.surface_tags.size() == ps.indices.size();
  auto tag_of = [&](size_t face) -> uint32_t {
    if (carried) return ps.surface_tags[face];
    const auto& tri = ps.indices[face];
    return ManifoldUtils::triangleTag(ps.vertices[tri[0]], ps.vertices[tri[1]], ps.vertices[tri[2]],
                                      tagged);
  };
  std::vector<uint32_t> tri_tag;
  tri_tag.reserve(ps.indices.size());
  for (const auto& group : colorToFaceIndices) {
    for (const size_t face : group.second) tri_tag.push_back(tag_of(face));
  }
  mesh = ManifoldUtils::withTagProperty(mesh, tri_tag);

  auto mani = manifold::Manifold(mesh);

  if (mani.Status() != Error::NoError) {
    PRINTD("Manifold creation initially failed");
    bool merged = mesh.Merge();
    mani = manifold::Manifold(mesh);
    if (mani.Status() == Error::NoError && merged) {
      PRINTD("..succeeded after merge");
    } else if (mani.Status() != Error::NoError && merged) {
      PRINTD("..still failing after merge");
    } else if (mani.Status() != Error::NoError && !merged) {
      PRINTD("..unable to merge");
    } else if (mani.Status() == Error::NoError && !merged) {
      PRINTD("..unable to merge, but somehow succeeded anyway?");
    }
  }

  // ps.surfaces carries the analytic surfaces the primitives declared; keep
  // them attached so they survive the boolean operations. The ids minted just
  // above are the originals this geometry's triangles will carry from now on,
  // so they are who declared these records - stamped here, where it is known,
  // rather than inferred from positions downstream. See Surface::owners.
  return std::make_shared<ManifoldGeometry>(mani, originalIDs, originalIDToColor, std::set<uint32_t>{},
                                            stampOwners(tagged, originalIDs));
}

}  // namespace

namespace ManifoldUtils {

std::shared_ptr<PolySet> mintOriginals(const PolySet& ps)
{
  auto out = std::make_shared<PolySet>(ps);
  const std::map<std::optional<Color4f>, std::vector<size_t>> groups = colourGroups(ps);
  out->original_ids.assign(ps.indices.size(), -1);
  std::set<uint32_t> ids;
  auto next_id = manifold::Manifold::ReserveIDs(groups.size());
  for (const auto& group : groups) {
    const auto id = next_id++;
    ids.insert(id);
    for (const size_t face : group.second) out->original_ids[face] = static_cast<int32_t>(id);
  }
  out->surfaces = stampOwners(ps.surfaces, ids);
  return out;
}

manifold::MeshGL64 withTagProperty(const manifold::MeshGL64& topology,
                                   const std::vector<uint32_t>& tri_tag)
{
  if (!std::any_of(tri_tag.begin(), tri_tag.end(), [](uint32_t t) { return t != 0; })) return topology;
  // A fourth property per vertex, and a copy of a vertex for each further tag
  // the triangles meeting there carry, merged back to it - so the topology is
  // the mesh's own and only the property has a seam, along the edges where two
  // surfaces meet. Every vertex keeps its own index and the copies go after
  // them, so the numbering Manifold works from is the one it would have had
  // without the tags; built in the order triangles were visited instead, every
  // vertex was renumbered, and what follows from the order - which facets
  // merge first, which corner a bound is taken at - moved with it.
  manifold::MeshGL64 mesh = topology;
  const size_t nv = topology.NumVert();
  std::vector<double> props;
  props.reserve(4 * nv);
  for (size_t v = 0; v < nv; v++) {
    for (int c = 0; c < 3; c++) props.push_back(topology.vertProperties[topology.numProp * v + c]);
    props.push_back(0);
  }
  std::vector<char> assigned(nv, 0);
  std::map<std::pair<uint64_t, uint32_t>, uint64_t> copy;
  std::vector<uint64_t> tris;
  tris.reserve(topology.triVerts.size());
  mesh.mergeFromVert.clear();
  mesh.mergeToVert.clear();
  for (size_t t = 0; t < tri_tag.size() && 3 * t + 2 < topology.triVerts.size(); t++) {
    const uint32_t tag = tri_tag[t];
    for (int k = 0; k < 3; k++) {
      const uint64_t v = topology.triVerts[3 * t + k];
      if (!assigned[v]) {
        assigned[v] = 1;
        props[4 * v + 3] = static_cast<double>(tag);
      }
      if (props[4 * v + 3] == static_cast<double>(tag)) {
        tris.push_back(v);
        continue;
      }
      auto it = copy.find({v, tag});
      if (it == copy.end()) {
        const uint64_t index = props.size() / 4;
        for (int c = 0; c < 3; c++) props.push_back(topology.vertProperties[topology.numProp * v + c]);
        props.push_back(static_cast<double>(tag));
        mesh.mergeFromVert.push_back(index);
        mesh.mergeToVert.push_back(v);
        it = copy.emplace(std::make_pair(v, tag), index).first;
      }
      tris.push_back(it->second);
    }
  }
  mesh.numProp = 4;
  mesh.vertProperties = std::move(props);
  mesh.triVerts = std::move(tris);
  return mesh;
}

uint32_t triangleTag(const Vector3d& a, const Vector3d& b, const Vector3d& c,
                     const std::vector<std::shared_ptr<Surface>>& surfaces)
{
  uint32_t found = 0;
  int matches = 0;
  for (const auto& s : surfaces) {
    if (s == nullptr || s->tags.empty() || !s->declaresFace()) continue;
    if (recognition::onExactly(s.get(), a) && recognition::onExactly(s.get(), b) &&
        recognition::onExactly(s.get(), c)) {
      found = s->tags.back();
      matches++;
    }
  }
  return matches == 1 ? found : 0;
}

manifold::Manifold withSurfaceTags(const manifold::Manifold& mani,
                                   std::vector<std::shared_ptr<Surface>>& surfaces)
{
  surfaces = stampTags(surfaces);
  const manifold::MeshGL64 full = mani.GetMeshGL64();
  const manifold::MeshGL64 topology = topologyMesh(full);
  std::vector<uint32_t> tri_tag(topology.NumTri(), 0);
  bool added = false;
  for (size_t t = 0; t < tri_tag.size(); t++) {
    if (full.numProp > 3) {
      tri_tag[t] =
        static_cast<uint32_t>(full.vertProperties[full.triVerts[3 * t] * full.numProp + 3] + 0.5);
    }
    if (tri_tag[t] != 0) continue;
    auto at = [&](int k) {
      const uint64_t v = topology.triVerts[3 * t + k];
      return Vector3d(topology.vertProperties[3 * v], topology.vertProperties[3 * v + 1],
                      topology.vertProperties[3 * v + 2]);
    };
    tri_tag[t] = triangleTag(at(0), at(1), at(2), surfaces);
    added = added || tri_tag[t] != 0;
  }
  if (!added) return mani;
  manifold::Manifold out(withTagProperty(topology, tri_tag));
  return out.Status() == Error::NoError ? out : mani;
}

std::vector<size_t> canonicalVertices(const manifold::MeshGL64& mesh)
{
  std::vector<size_t> to(mesh.NumVert());
  for (size_t v = 0; v < to.size(); v++) to[v] = v;
  for (size_t i = 0; i < mesh.mergeFromVert.size() && i < mesh.mergeToVert.size(); i++) {
    to[mesh.mergeFromVert[i]] = mesh.mergeToVert[i];
  }
  return to;
}

manifold::MeshGL64 topologyMesh(const manifold::MeshGL64& mesh)
{
  if (mesh.numProp <= 3 && mesh.mergeFromVert.empty()) return mesh;
  const std::vector<size_t> to = canonicalVertices(mesh);
  std::vector<uint64_t> index(to.size(), uint64_t(-1));
  manifold::MeshGL64 out;
  out.numProp = 3;
  for (size_t v = 0; v < to.size(); v++) {
    if (to[v] != v) continue;
    index[v] = out.vertProperties.size() / 3;
    for (int k = 0; k < 3; k++) out.vertProperties.push_back(mesh.vertProperties[v * mesh.numProp + k]);
  }
  out.triVerts.reserve(mesh.triVerts.size());
  for (const auto v : mesh.triVerts) out.triVerts.push_back(index[to[v]]);
  out.runIndex = mesh.runIndex;
  out.runOriginalID = mesh.runOriginalID;
  out.runTransform = mesh.runTransform;
  out.runFlags = mesh.runFlags;
  out.faceID = mesh.faceID;
  out.tolerance = mesh.tolerance;
  return out;
}

const char *statusToString(Error status)
{
  switch (status) {
  case Error::NoError:                      return "NoError";
  case Error::NonFiniteVertex:              return "NonFiniteVertex";
  case Error::NotManifold:                  return "NotManifold";
  case Error::VertexOutOfBounds:            return "VertexOutOfBounds";
  case Error::PropertiesWrongLength:        return "PropertiesWrongLength";
  case Error::MissingPositionProperties:    return "MissingPositionProperties";
  case Error::MergeVectorsDifferentLengths: return "MergeVectorsDifferentLengths";
  case Error::MergeIndexOutOfBounds:        return "MergeIndexOutOfBounds";
  case Error::TransformWrongLength:         return "TransformWrongLength";
  case Error::RunIndexWrongLength:          return "RunIndexWrongLength";
  case Error::FaceIDWrongLength:            return "FaceIDWrongLength";
  default:                                  return "unknown";
  }
}

std::shared_ptr<ManifoldGeometry> createManifoldFromPolySet(const PolySet& ps)
{
  // 1. If the PolySet is already manifold, we should be able to build a Manifold object directly
  // (through using manifold::Mesh).
  // We need to make sure our PolySet is triangulated before doing that.
  // Note: We currently don't have a way of directly checking if a PolySet is manifold,
  // so we just try converting to a Manifold object and check its status.
  std::unique_ptr<const PolySet> triangulated;
  if (!ps.isTriangular()) {
    triangulated = PolySetUtils::tessellate_faces(ps);
  }
  const PolySet& triangle_set = ps.isTriangular() ? ps : *triangulated;

  // Note: This function also performs a merge if the first attempt fails.
  auto mani = createManifoldFromTriangularPolySet(triangle_set);
  if (mani->getManifold().Status() == Error::NoError) {
    return mani;
  }

  LOG(message_group::Warning,
      "PolySet -> Manifold conversion failed: %1$s\n"
      "Trying to repair and reconstruct mesh..",
      ManifoldUtils::statusToString(mani->getManifold().Status()));

  // 2. If the PolySet couldn't be converted into a Manifold object, let's try to repair it.
  // We currently have to utilize some CGAL functions to do this.
#ifdef ENABLE_CGAL
  try {
    PolySet psq(ps);
    std::vector<Vector3d> points3d;
    psq.quantizeVertices(&points3d);
    auto ps_tri = PolySetUtils::tessellate_faces(psq);

    CGAL_DoubleMesh m;

    if (ps_tri->isConvex()) {
      using K = CGAL::Epick;
      // Collect point cloud
      std::vector<K::Point_3> points(points3d.size());
      for (size_t i = 0, n = points3d.size(); i < n; i++) {
        points[i] = CGALUtils::vector_convert<K::Point_3>(points3d[i]);
      }
      if (points.size() <= 3) return std::make_shared<ManifoldGeometry>();

      // Apply hull
      CGAL::Surface_mesh<CGAL::Point_3<K>> r;
      CGAL::convex_hull_3(points.begin(), points.end(), r);
      CGALUtils::copyMesh(r, m);
    } else {
      m = CGALUtils::repairPolySet(*ps_tri);
    }

    if (!ps_tri->isConvex()) {
      if (CGALUtils::isClosed(m)) {
        CGALUtils::orientToBoundAVolume(m);
      } else {
        LOG(message_group::Error, "[manifold] Input mesh is not closed!");
      }
    }

    auto geom = createManifoldFromSurfaceMesh(m);
    // TODO: preserve color if polyset is fully monochrome, or maybe pass colors around in surface mesh?
    return geom;
  } catch (const std::exception& e) {
    LOG(message_group::Error, "[manifold] CGAL error: %1$s", e.what());
  }
#endif
  return std::make_shared<ManifoldGeometry>();
}

std::shared_ptr<const ManifoldGeometry> createManifoldFromGeometry(
  const std::shared_ptr<const Geometry>& geom)
{
  if (auto mani = std::dynamic_pointer_cast<const ManifoldGeometry>(geom)) {
    return mani;
  }
  if (auto ps = PolySetUtils::getGeometryAsPolySet(geom)) {
    return createManifoldFromPolySet(*ps);
  }
  return nullptr;
}

Polygon2d polygonsToPolygon2d(const manifold::Polygons& polygons)
{
  Polygon2d poly2d;
  for (const auto& polygon : polygons) {
    Outline2d outline;
    for (const auto& v : polygon) {
      outline.vertices.emplace_back(v[0], v[1]);
    }
    outline.color = *OpenSCAD::parse_color("#f9d72c");
    poly2d.addOutline(std::move(outline));
  }
  return poly2d;
}

std::unique_ptr<PolySet> createTriangulatedPolySetFromPolygon2d(const Polygon2d& polygon2d, bool in3d)
{
  auto polyset = std::make_unique<PolySet>(2);
  polyset->setTriangular(true);

  std::vector<Outline2d> outline_work = in3d ? polygon2d.untransformedOutlines() : polygon2d.outlines();
  /*
    std::vector<Outline2d> outline_work ;
    std::vector<Outline2d> holes ;
    for(const auto & o : in3d ? polygon2d.untransformedOutlines() : polygon2d.outlines()) {
      if(outline_area(o) > 0 || true)   outline_work.push_back(o);
      else holes.push_back(o);
    }
  */
  while (outline_work.size() > 0) {
    std::vector<Outline2d> outline_next;
    int v_off = polyset->vertices.size();
    int c_off = polyset->colors.size();
    manifold::Polygons polygons;
    manifold::SimplePolygon simplePolygon;

    Color4f col = outline_work[0].color;
    polyset->colors.push_back(col.unpaintedIfFullyTransparent());
    for (const auto& outline : outline_work) {
      if (outline.color != col) {
        outline_next.push_back(outline);
        continue;
      }
      for (const auto& vertex : outline.vertices) {
        polyset->vertices.emplace_back(vertex[0], vertex[1], 0.0);
        simplePolygon.emplace_back(vertex[0], vertex[1]);
      }
      polygons.push_back(std::move(simplePolygon));
    }
    /*
        for (const auto& outline : holes) {
          for (const auto& vertex : outline.vertices) {
            polyset->vertices.emplace_back(vertex[0], vertex[1], 0.0);
            simplePolygon.emplace_back(vertex[0], vertex[1]);
          }
          polygons.push_back(std::move(simplePolygon));
        }
    */

    const auto triangles = manifold::Triangulate(polygons);

    for (const auto& triangle : triangles) {
      polyset->indices.push_back({v_off + triangle[0], v_off + triangle[1], v_off + triangle[2]});
      polyset->color_indices.push_back(c_off);
    }
    outline_work = outline_next;
  }
  return polyset;
}

template <class SurfaceMesh>
std::shared_ptr<ManifoldGeometry> createManifoldFromSurfaceMesh(const SurfaceMesh& tm)
{
  using vertex_descriptor = typename SurfaceMesh::Vertex_index;

  manifold::MeshGL64 meshgl;

  meshgl.numProp = 3;
  meshgl.vertProperties.resize(tm.number_of_vertices() * 3);
  for (vertex_descriptor vd : tm.vertices()) {
    const auto& v = tm.point(vd);
    meshgl.vertProperties[3 * vd] = v.x();
    meshgl.vertProperties[3 * vd + 1] = v.y();
    meshgl.vertProperties[3 * vd + 2] = v.z();
  }

  meshgl.triVerts.reserve(tm.number_of_faces() * 3);
  for (const auto& f : tm.faces()) {
    size_t idx[3];
    size_t i = 0;
    for (vertex_descriptor vd : vertices_around_face(tm.halfedge(f), tm)) {
      if (i >= 3) break;
      idx[i++] = vd;
    }
    if (i < 3) continue;
    for (size_t j : {0, 1, 2}) meshgl.triVerts.emplace_back(idx[j]);
  }

  assert((meshgl.triVerts.size() == tm.number_of_faces() * 3) || !"Mesh was not triangular!");

  auto mani = manifold::Manifold(meshgl).AsOriginal();
  if (mani.Status() != Error::NoError) {
    LOG(message_group::Error, "[manifold] Surface_mesh -> Manifold conversion failed: %1$s",
        ManifoldUtils::statusToString(mani.Status()));
    return nullptr;
  }
  std::set<uint32_t> originalIDs;
  auto id = mani.OriginalID();
  if (id >= 0) {
    originalIDs.insert(id);
  }
  return std::make_shared<ManifoldGeometry>(mani, originalIDs);
}

template <class SurfaceMesh>
std::shared_ptr<SurfaceMesh> createSurfaceMeshFromManifold(const manifold::Manifold& mani)
{
  const auto meshgl = topologyMesh(mani.GetMeshGL64());
  auto mesh = std::make_shared<SurfaceMesh>();
  mesh->reserve(meshgl.NumVert(), meshgl.NumTri() * 3, meshgl.NumTri());
  for (size_t i = 0; i < meshgl.NumVert(); i++) {
    const auto& v = meshgl.GetVertPos(i);
    mesh->add_vertex(typename SurfaceMesh::Point(v[0], v[1], v[2]));
  }
  for (size_t i = 0; i < meshgl.NumTri(); i++) {
    const auto& tri = meshgl.GetTriVerts(i);
    mesh->add_face(typename SurfaceMesh::Vertex_index(tri[0]),
                   typename SurfaceMesh::Vertex_index(tri[1]),
                   typename SurfaceMesh::Vertex_index(tri[2]));
  }
  return mesh;
}

#ifdef ENABLE_CGAL
template std::shared_ptr<ManifoldGeometry> createManifoldFromSurfaceMesh(
  const CGAL::Surface_mesh<CGAL::Point_3<CGAL::Epick>>& tm);
template std::shared_ptr<ManifoldGeometry> createManifoldFromSurfaceMesh(const CGAL_DoubleMesh& tm);
template std::shared_ptr<CGAL::Surface_mesh<manifold::vec3>>
createSurfaceMeshFromManifold<CGAL::Surface_mesh<manifold::vec3>>(const manifold::Manifold& mani);
template std::shared_ptr<CGAL::Surface_mesh<CGAL_Point_3>>
createSurfaceMeshFromManifold<CGAL::Surface_mesh<CGAL_Point_3>>(const manifold::Manifold& mani);
#endif

};  // namespace ManifoldUtils
