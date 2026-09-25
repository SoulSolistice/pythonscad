#pragma once

/*! One solid's worth of STEP: the mesh, the declarations and the corner moves
 * export_step.cc settled, turned into faces, edges and a shell.
 *
 * This was StepKernel::build_tri_body, one function of 4700 lines whose twelve
 * phases shared some sixty locals through lambdas capturing everything. It is
 * an object so that each phase can become a method of its own, and each local a
 * phase hands on a member, one phase at a time and each step proven neutral by
 * `scripts/step-dev.sh compare` (checklist A8, A9). */

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "geometry/Surface.h"
#include "geometry/linalg.h"
#include "io/StepKernel.h"

struct StepKernel::BodyBuild {
  BodyBuild(StepKernel& kernel, const char *name, const std::vector<Vector3d>& mesh_vertices,
            const std::vector<IndexedFace>& faces, const std::vector<std::shared_ptr<Curve>>& curves,
            const std::vector<std::shared_ptr<Surface>>& surfaces,
            const std::vector<int32_t>& faceOrigin, const std::map<int, Vector3d>& cornerMoves,
            const std::map<int, std::size_t>& singleOwner,
            const std::map<int, std::pair<std::size_t, std::size_t>>& ownerSplit,
            const std::map<int32_t, std::vector<std::size_t>>& owned,
            const std::vector<int>& faceParents, const std::vector<Vector4d>& faceNormals, double tol,
            bool analytic, bool approximate, const std::map<int, std::vector<std::size_t>>& cornerOwners)
    : kernel(kernel),
      entities(kernel.entities),
      name(name),
      mesh_vertices(mesh_vertices),
      faces(faces),
      curves(curves),
      surfaces(surfaces),
      faceOrigin(faceOrigin),
      cornerMoves(cornerMoves),
      singleOwner(singleOwner),
      ownerSplit(ownerSplit),
      owned(owned),
      faceParents(faceParents),
      faceNormals(faceNormals),
      tol(tol),
      analytic(analytic),
      approximate(approximate),
      cornerOwners(cornerOwners)
  {
  }

  /*! Every phase, in order. */
  void run();

  StepKernel& kernel;
  /*! Where every entity made here registers itself: the kernel's. */
  std::vector<Entity *>& entities;

  // The inputs, as build_tri_body received them.
  const char *name;
  const std::vector<Vector3d>& mesh_vertices;
  const std::vector<IndexedFace>& faces;
  const std::vector<std::shared_ptr<Curve>>& curves;
  const std::vector<std::shared_ptr<Surface>>& surfaces;
  const std::vector<int32_t>& faceOrigin;
  const std::map<int, Vector3d>& cornerMoves;
  const std::map<int, std::size_t>& singleOwner;
  const std::map<int, std::pair<std::size_t, std::size_t>>& ownerSplit;
  const std::map<int32_t, std::vector<std::size_t>>& owned;
  const std::vector<int>& faceParents;
  const std::vector<Vector4d>& faceNormals;
  const double tol;
  const bool analytic;
  const bool approximate;
  const std::map<int, std::vector<std::size_t>>& cornerOwners;

  EdgeCurve *create_line_edge_curve(Vertex *vert1, Vertex *vert2, bool dir)
  {
    return kernel.create_line_edge_curve(vert1, vert2, dir);
  }
  EdgeCurve *get_line_from_map(std::map<std::pair<int, int>, EdgeCurve *>& edge_map, int ind1, int ind2,
                               Vertex *vert1, Vertex *vert2, bool& edge_dir, int& merge_cnt)
  {
    return kernel.get_line_from_map(edge_map, ind1, ind2, vert1, vert2, edge_dir, merge_cnt);
  }
};
