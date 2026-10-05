// this file is split into many separate cgalutils* files
// in order to workaround gcc 4.9.1 crashing on systems with only 2GB of RAM

#ifdef ENABLE_MANIFOLD

#include <cstdint>
#include <map>
#include <memory>
#include <array>
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
    // And the operands' solids themselves: the hull holds each of them.
    std::vector<std::shared_ptr<const SolidTree>> contained;
    // And whether every operand is one declared primitive: then a hull of
    // coaxial ones is a declared solid of its own, not a box
    // (solidOfHullOfRevolution).
    bool all_primitive = true;
    auto note_reach = [&](const std::shared_ptr<const SolidTree>& tree) {
      all_primitive = all_primitive && tree != nullptr && tree->op() == SolidTree::Op::Declared;
      contained.push_back(tree);
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
        // A faceted leaf not yet a Manifold has its statement unresolved:
        // resolve it here by its maker's names, as its entry would by tag.
        const bool faceted = ps->solid_tree && ps->solid_tree->op() == SolidTree::Op::Faceted;
        note_reach(faceted ? facetedEnvelope(*ps, recordByName(*ps), ps->solid_tree->what())
                           : ps->solid_tree);
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
    // And what the hull makes of them, where its operands state it: each read
    // as the solid of revolution it is, or the sector of one their solid trees
    // state (revolutionOperands).
    const auto revolved = revolutionOperands(per_operand, contained);
    mergeSurfaces(surfaces, declareHullOfRevolution(revolved));
    // A hull is one new original: whatever its operands' triangles carried, its
    // own carry the hull's id, so that id is who declared every record here.
    const auto hull = manifold::Manifold::Hull(pts);
    std::set<uint32_t> hull_ids;
    if (hull.OriginalID() >= 0) hull_ids.insert(uint32_t(hull.OriginalID()));
    // Its records are stamped, and its faces named by derivation where they can
    // be: the hull algorithm says which points it joins, not which record a face
    // is made on, but for coaxial solids of revolution which record each face
    // lies on follows in closed form from the rings its corners are on
    // (hullFacetRecords). Derived names (mintDerivedTag) - D1, never D0
    // (checklist E8). Until 2026-09-29 a face was named by the one record its
    // corners lay on - position standing in for a name.
    std::vector<std::shared_ptr<Surface>> stamped = stampTags(stampOwners(surfaces, hull_ids));
    manifold::Manifold tagged = hull;
    {
      const manifold::MeshGL64 topology = ManifoldUtils::topologyMesh(hull.GetMeshGL64());
      std::vector<Vector3d> verts;
      for (std::size_t v = 0; v < topology.vertProperties.size() / 3; v++) {
        verts.emplace_back(topology.vertProperties[3 * v], topology.vertProperties[3 * v + 1],
                           topology.vertProperties[3 * v + 2]);
      }
      std::vector<std::array<std::size_t, 3>> tris;
      for (std::size_t t = 0; t < topology.NumTri(); t++) {
        tris.push_back({std::size_t(topology.triVerts[3 * t]), std::size_t(topology.triVerts[3 * t + 1]),
                        std::size_t(topology.triVerts[3 * t + 2])});
      }
      std::vector<std::shared_ptr<Surface>> derived;
      const std::vector<int> record = hullFacetRecords(revolved, verts, tris, derived);
      // Per derived record, the derived tag it gives the kept record describing
      // the same surface - one name per record, whoever declared it first.
      std::map<int, uint32_t> tag_of;
      std::vector<uint32_t> tri_tag(tris.size(), 0);
      bool any = false;
      for (std::size_t t = 0; t < record.size(); t++) {
        const int k = record[t];
        if (k < 0 || std::size_t(k) >= derived.size()) continue;
        auto it = tag_of.find(k);
        if (it == tag_of.end()) {
          uint32_t tag = 0;
          for (auto& kept : stamped) {
            if (kept == nullptr || !kept->declaresFace() || !kept->sameAs(*derived[std::size_t(k)]))
              continue;
            auto copy = kept->clone();
            tag = mintDerivedTag();
            copy->tags.push_back(tag);
            kept = copy;
            break;
          }
          it = tag_of.emplace(k, tag).first;
        }
        tri_tag[t] = it->second;
        any = any || it->second != 0;
      }
      if (any) {
        manifold::Manifold named(ManifoldUtils::withTagProperty(topology, tri_tag));
        if (named.Status() == manifold::Manifold::Error::NoError) tagged = named;
      }
    }
    // Its solid is a leaf of its own: the hull of exact solids is not a boolean
    // of them, so nothing beneath it answers for its boundary - but it lies
    // within the box that holds its operands, where every one of them states
    // where it lies, and a point outside that box is outside the hull; and it
    // holds every operand, so a point inside one is inside it. See SolidTree.h.
    std::shared_ptr<const SolidTree> solid =
      reach_known ? SolidTree::enveloping(reach, contained, "hull()")
                  : SolidTree::opaque("hull() of an operand that states no extent");
    if (all_primitive) {
      if (auto exact = solidOfHullOfRevolution(per_operand))
        solid = SolidTree::declared(exact, "hull()");
    }
    return std::make_shared<ManifoldGeometry>(
      tagged, std::set<uint32_t>{}, std::map<uint32_t, Color4f>{}, std::set<uint32_t>{}, stamped, solid);
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
