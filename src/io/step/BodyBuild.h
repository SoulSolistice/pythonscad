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
#include <set>
#include <tuple>
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

  // ---- the phases, in the order run() calls them

  /*! The census line: how many surfaces of each kind the model declared, which
   * is what the recognisers have to match facets against. */
  void reportDeclaredSurfaces();

  /*! The mesh's faces as loops of canonical vertices, each with its plane:
   * slivers of no area dropped and the T-junctions they held welded back into
   * the edge that runs past them, and every hole given the innermost face that
   * encloses it. */
  void buildLoops();

  // ---- what the phases hand each other

  // A working copy, because the corner placement moves junction corners onto
  // the curve their two declared owners cross along, once recognition has run
  // and it is known which faces would mind. Nothing else writes to this.
  std::vector<Vector3d> vertices = mesh_vertices;
  const double model_tol = tol > 0 ? tol : 1e-5;
  // twice the area of the smallest polygon still considered a face
  static constexpr double area_eps = 1e-12;
  const std::size_t face_cnt = faces.size();

  // Each face of the mesh as a loop of canonical vertices, with its plane; see
  // buildLoops. A loop that is not valid is not written.
  std::vector<std::vector<int>> loops = std::vector<std::vector<int>>(face_cnt);
  std::vector<Vector3d> loop_normals = std::vector<Vector3d>(face_cnt, Vector3d(0, 0, 0));
  std::vector<char> loop_valid = std::vector<char>(face_cnt, 0);
  std::vector<char> loop_is_hole = std::vector<char>(face_cnt, 0);
  // The face each hole belongs to, or -1.
  std::vector<int> parents = std::vector<int>(face_cnt, -1);
  // The T-junction vertices welded back into the edge that runs past them.
  std::set<int> welded_verts;

  // The same rule as `canonical` in buildLoops, applied at the moment it can be
  // obeyed. That map is built from `vertices` here, before the corner placement
  // has moved any of them, so it catches vertices the *mesh* left coincident and
  // cannot catch vertices the placement makes coincident - which is a thing a
  // correct placement does. Where a cylinder, two cones and a plane meet, the
  // corners the mesh keeps apart are one point on the surfaces, and moving each
  // onto its own owners brings them together.
  //
  // Measured on lid10, on the export that relaxed the plane veto: three mesh
  // vertices - one cylinder-and-plane corner and two cone-and-plane corners -
  // all placed at (-66.905705, -48.609840, 95.0), and three VERTEX_POINTs
  // written for one point. `validatestep.py` calls that "vertices are not
  // shared" and it is the same defect the comment above describes, arriving too
  // late for the map above to see it.
  //
  // Resolving by position *here* is late enough: every caller is emitting faces,
  // which happens after the moves are applied. Exact coordinates rather than a
  // tolerance, for the same reason the map above uses them - two corners placed
  // on the same crossing of the same surfaces land on the same doubles, and a
  // tolerance would additionally merge things that are merely close.
  std::vector<Vertex *> step_verts = std::vector<Vertex *>(mesh_vertices.size(), nullptr);
  std::map<std::tuple<double, double, double>, Vertex *> vertex_at;
  /*! The one VERTEX_POINT for vertex `ind`, made on first asking. */
  Vertex *get_vertex(int ind);

  // ---- the kernel methods the phases call
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
