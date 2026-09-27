// this file is split into many separate cgalutils* files
// in order to workaround gcc 4.9.1 crashing on systems with only 2GB of RAM

#ifdef ENABLE_MANIFOLD

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "core/AST.h"
#include "core/enums.h"
#include "core/node.h"
#include "core/progress.h"
#include "geometry/Geometry.h"
#include "geometry/HullOfRevolution.h"
#include "geometry/PolySet.h"
#include "geometry/manifold/ManifoldGeometry.h"
#include "geometry/manifold/manifoldutils.h"
#include "utils/printutils.h"

namespace ManifoldUtils {

Location getLocation(const std::shared_ptr<const AbstractNode>& node)
{
  return node && node->modinst ? node->modinst->location() : Location::NONE;
}

/*!
   Applies op to all children and returns the result.
   The child list should be guaranteed to contain non-NULL 3D or empty Geometry objects
 */
std::shared_ptr<ManifoldGeometry> applyOperator3DManifold(const Geometry::Geometries& children,
                                                          OpenSCADOperator op)
{
  if (op == OpenSCADOperator::HULL) {
    std::vector<manifold::vec3> pts;
    // The analytic surfaces the children declared. A hull only adds material,
    // so a wall its child declared either survives on the hull's boundary or is
    // buried inside it - and these records are a statement of intent, not
    // geometry: an exporter still has to find an exact fit in the mesh before it
    // uses one. Carrying them is what keeps the commonest chamfer idiom,
    // hull(cylinder(r=R), cylinder(r=R-c)), from costing the wall its identity.
    std::vector<std::shared_ptr<Surface>> surfaces;
    // Each operand's records apart as well, because what the hull itself makes
    // is stated by its operands one by one - see declareHullOfRevolution.
    std::vector<std::vector<std::shared_ptr<Surface>>> per_operand;
    // And where each operand's solid lies, as the model states it: the hull of
    // them lies within the box that holds them all.
    BoundingBox reach;
    bool reach_known = true;
    auto note_reach = [&](const std::shared_ptr<const SolidTree>& tree) {
      BoundingBox b;
      if (tree == nullptr || !tree->extent(b)) reach_known = false;
      else reach.extend(b);
    };
    for (const auto& item : children) {
      if (!item.second) continue;
      auto& chgeom = item.second;
      if (const auto *mani = dynamic_cast<const ManifoldGeometry *>(chgeom.get())) {
        per_operand.push_back(mani->getSurfaces());
        mergeSurfaces(surfaces, mani->getSurfaces());
        note_reach(mani->getSolidTree());
        pts.reserve(pts.size() + mani->numVertices());
        mani->foreachVertexUntilTrue([&](auto& p) {
          pts.push_back(p);
          return false;
        });
      } else if (const auto *ps = dynamic_cast<const PolySet *>(chgeom.get())) {
        per_operand.push_back(ps->surfaces);
        mergeSurfaces(surfaces, ps->surfaces);
        note_reach(ps->solid_tree);
        pts.reserve(pts.size() + ps->indices.size() * 3);
        for (const auto& p : ps->indices) {
          for (const auto& ind : p) {
            auto& v = ps->vertices[ind];
            pts.push_back({v[0], v[1], v[2]});
          }
        }
      } else {
        auto chN = createManifoldFromGeometry(chgeom);
        if (chN && !chN->isEmpty()) {
          per_operand.push_back(chN->getSurfaces());
          mergeSurfaces(surfaces, chN->getSurfaces());
          note_reach(chN->getSolidTree());
          chN->foreachVertexUntilTrue([&](auto& p) {
            pts.push_back(p);
            return false;
          });
        }
      }
      if (item.first) item.first->progress_report();
    }
    if (pts.empty()) return nullptr;
    // And what the hull makes of them, where its operands state it.
    mergeSurfaces(surfaces, declareHullOfRevolution(per_operand));
    // A hull is one new original: whatever its operands' triangles carried, its
    // own carry the hull's id, so that id is who declared every record here.
    const auto hull = manifold::Manifold::Hull(pts);
    std::set<uint32_t> hull_ids;
    if (hull.OriginalID() >= 0) hull_ids.insert(uint32_t(hull.OriginalID()));
    std::vector<std::shared_ptr<Surface>> stamped = stampOwners(surfaces, hull_ids);
    // The hull's own faces, made on what its operands state it makes.
    const auto tagged = ManifoldUtils::withSurfaceTags(hull, stamped);
    // Its solid is a leaf of its own: the hull of exact solids is not a boolean
    // of them, so nothing beneath it answers for it - but it lies within the box
    // that holds its operands, where every one of them states where it lies, and
    // a point outside that box is outside the hull. See SolidTree.h.
    return std::make_shared<ManifoldGeometry>(
      tagged, std::set<uint32_t>{}, std::map<uint32_t, Color4f>{}, std::set<uint32_t>{}, stamped,
      reach_known ? SolidTree::box(reach, "hull()")
                  : SolidTree::opaque("hull() of an operand that states no extent"));
  }

  std::shared_ptr<ManifoldGeometry> geom;

  bool foundFirst = false;

  for (const auto& item : children) {
    auto chN = item.second ? createManifoldFromGeometry(item.second) : nullptr;

    // Intersecting something with nothing results in nothing
    if (!chN || chN->isEmpty()) {
      if (op == OpenSCADOperator::INTERSECTION) {
        geom = nullptr;
        break;
      }
      if (op == OpenSCADOperator::DIFFERENCE && !foundFirst) {
        geom = nullptr;
        break;
      }
      continue;
    }

    // Initialize geom with first expected geometric object
    if (!foundFirst) {
      geom = std::make_shared<ManifoldGeometry>(*chN);
      foundFirst = true;
      continue;
    }

    switch (op) {
    case OpenSCADOperator::UNION:        *geom = *geom + *chN; break;
    case OpenSCADOperator::INTERSECTION: *geom = *geom * *chN; break;
    case OpenSCADOperator::DIFFERENCE:   *geom = *geom - *chN; break;
    case OpenSCADOperator::MINKOWSKI:    *geom = geom->minkowski(*chN); break;
    default:                             LOG(message_group::Error, "Unsupported CGAL operator: %1$d", static_cast<int>(op));
    }
    if (item.first) item.first->progress_report();
  }
  return geom;
}

};  // namespace ManifoldUtils

#endif  // ENABLE_MANIFOLD
