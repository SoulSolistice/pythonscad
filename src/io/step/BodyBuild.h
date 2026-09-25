#pragma once

/*! One solid's worth of STEP: the mesh, the declarations and the corner moves
 * export_step.cc settled, turned into faces, edges and a shell.
 *
 * This was StepKernel::build_tri_body, one function of 4700 lines whose twelve
 * phases shared some sixty locals through lambdas capturing everything. It is
 * an object so that each phase can become a method of its own, and each local a
 * phase hands on a member, one phase at a time and each step proven neutral by
 * `scripts/step-dev.sh compare` (checklist A8, A9). */

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <memory>
#include <utility>
#include <vector>

#include "geometry/AnalyticFeatures.h"
#include "geometry/Surface.h"
#include "geometry/recognition/Pipeline.h"
#include "io/step/BoundaryCycles.h"
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

  /*! Recognition: which facets are written on which declared surface. The
   * writer's half of the question - which of a standing set of regions it could
   * write, given the sections they agree on - is decideSections and
   * boundaryLiesOnSurface, asked through recognition::Writability. */
  void recognise();

  /*! Deciding which plane sections get written, over a given set of faces. This
   * is a function rather than a step because it has to be asked **twice**, on
   * two different meshes: once while it is being settled which faces are
   * analytic at all, and again immediately before they are written. The corner
   * placement runs between those two points and moves vertices - by up to 0.07
   * on step-band-family - so a section agreed on the first mesh need not exist
   * on the second, and a curve one face writes and its neighbour does not is a
   * hole in the shell. Measured before this was split in two: 132 edges with one
   * face, every one of them an ellipse the other side had stopped seeing.
   *
   * It decides the crossing curves first - the edges where two declared
   * surfaces cross, `crossing_edges` - and then the plane sections both faces
   * of an edge agree to write, `section_curves`. */
  void decideSections(const std::vector<const AnalyticFeatures::Patch *>& standing);

  /*! The writer's half of "can this region be written": the sections a
   * standing set of quadric regions agree on, and whether each one's whole
   * boundary then lies on its surface. The writer's boundary machinery;
   * recognition asks it through recognition::Writability and decides what to
   * do with the answer. */
  bool boundaryLiesOnSurface(const AnalyticFeatures::Patch& patch,
                             const std::set<std::set<int>>& agreed);

  /*! The corner placement: every corner a rung can place is moved onto the
   * surfaces the file asserts there, the arbiter choosing among the rungs'
   * candidates (CornerLadder.h), and a planar face bent by a move is split into
   * the triangles it is written as, `split_for_corners`. Runs once recognition
   * has said which faces are analytic, and before any face is written. */
  void placeCorners();

  /*! Emit the recognised bands: one CYLINDRICAL_SURFACE or CONICAL_SURFACE face
   * each, bounded by a circle or an arc at either rim.
   *
   * A band which covers the full turn is periodic, so it cannot be bounded by
   * the rims alone - the loop walks up a seam and back down it, the same edge
   * used once in each direction. One which does not is bounded by an arc at
   * either rim and the band's two end edges, and needs no seam. */
  void emitBands();

  /*! A patch face is bounded by its runs, each of which is one curve read off
   * the same control net the surface is written from - so the curve provably
   * lies on the surface rather than approximately. A seam between two patches
   * is one EdgeCurve used by both, in opposite senses; a run cutting into a
   * planar neighbour is spliced into that loop through the same substitution
   * the arcs use. */
  void emitBezierFaces();

  /*! decideSections asked again, now that the corner placement has moved what
   * it is going to move. The faces are settled; what is re-decided is only
   * which sections both of their faces can still see, on the geometry about to
   * be written rather than the geometry it was chosen from.
   *
   * After placeCorners and before *either* emitter of crossing curves. It used
   * to run after the sweep faces were written, which meant the sweep emitter
   * read a set that had not been decided yet and the quadric emitter read one
   * that had: the same edge then got a crossing curve from one face and a chord
   * from the other, and the shell came apart on twice the number of crossings -
   * 1278 edges used once, on the band family at $fn 32. */
  void redecideSections();

  /*! One face per claimed region, on the B-spline the declaration describes,
   * bounded by the mesh's own straight edges.
   *
   * That last part is the whole design and it is not a compromise. The obvious
   * move is to fit a curve along each boundary run, the way a fillet's rails
   * become arcs - but a fillet's rail lies in the flat face beside it, and a
   * trimmed sweep's boundary does not. Its neighbours here are the planar
   * facets the boolean left, so a curve on the sweep lies in none of them, and
   * a shared edge between the two can only be the chord it already is. Taking
   * the edges from the same map every other face uses is therefore both the
   * simplest thing and the only one that keeps the shell closed: the neighbour
   * is not asked to give anything up. */
  void emitSweepFaces();

  /*! A trimmed quadric, bounded by the mesh's own boundary rather than by
   * circles. The band pass writes a cylinder between two circular rims and
   * writes it better - a CIRCLE is a curve a kernel can offset and pattern
   * along - so this only ever sees what that pass could not take: a face whose
   * trim is not a plane section and has no conic to bound it with.
   *
   * The bound is the polyline the neighbouring faceted faces already use, for
   * the same reason a declared sweep's is: those faces have to close against
   * this one edge for edge, and a curve of our own devising there would open
   * the shell. The surface is exact; only its boundary is the mesh's.
   * One curve per plane section, shared by the two faces that meet along it -
   * the same rule the band pass follows for a shared rim. Without it each face
   * would write its own ellipse and the shell would come apart along them. */
  void emitQuadricFaces();

  /*! Say where each crossing curve runs across the two surfaces it lies on, now
   * that both faces have been written and their placements are known.
   *
   * The 3D curve stays definitive - .CURVE_3D., as it is everywhere else here -
   * so a reader that ignores the pcurves sees the file it saw before. What they
   * add is that a curve on two surfaces is *said* to be on them: STEP calls that
   * a SURFACE_CURVE, and a kernel that would otherwise re-project a B-spline
   * onto a quadric and decide for itself is told the answer instead. */
  void writeCrossingPcurves();

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

  // The faces the analytic emitters write, and the edges of each.
  std::vector<Face *> sfaces_extra;
  std::vector<std::vector<EdgeCurve *>> face_edges_extra;

  // Shared by every emitter because a partial
  // cylinder's two end edges are ordinary straight edges shared with a
  // neighbouring planar face, so both have to come from the same map.
  std::map<std::pair<int, int>, EdgeCurve *> edge_map;
  int merged_edge_cnt = 0;

  // Recognise the bands of facets that were modelled as a surface of
  // revolution. The recogniser is format neutral and lives in
  // geometry/AnalyticFeatures - everything below this point is the STEP
  // specific half, turning its answer into entities.
  // What recognition hands the writer - see recognition::runPipeline. The
  // names below are views into it, so the writer reads as it always has.
  recognition::Recognition rec;
  AnalyticFeatures::Result& features = rec.features;
  std::vector<AnalyticFeatures::Patch>& bezier_patches = rec.bezier_patches;
  // Runs whose curve is exactly a circular arc, keyed by their vertices - which
  // is how the emitter shares one EdgeCurve between the two faces that meet
  // there. Decided per run rather than per face; see where it is filled.
  std::set<std::set<int>>& circular_runs = rec.circular_runs;
  // Declared sweeps whose claimed region can be written as one face: a strip,
  // whose boundary stays inside the surface's parameter rectangle. One closing
  // around its profile crosses the surface's seam and is left faceted.
  std::vector<AnalyticFeatures::Patch>& grid_faces = rec.grid_faces;
  std::vector<AnalyticFeatures::Patch>& quadric_faces = rec.quadric_faces;
  // The plane sections that both of their two faces agree to write as one
  // conic, each named by the set of mesh vertices it runs through. A curve one
  // face writes and its neighbour does not is a hole in the shell, so this is a
  // joint decision and not one a face can take for itself.
  std::set<std::set<int>> section_curves;
  // The planes the model declared, kept where the writer can reach them. A
  // declared plane cannot drift with the mesh, and a section derived from one is
  // exact rather than merely close, so it is preferred wherever it agrees with
  // the face at hand.
  std::vector<std::pair<Vector3d, Vector3d>> declared_planes;  // point, unit normal
  // Which entry of `surfaces` each of those came from, so that a face written
  // on one can be attributed back to the declaration it honoured. Parallel to
  // `declared_planes`; `surfaces.size()` for a fitted plane, which no census
  // row is owed.
  std::vector<std::size_t> declared_plane_index;
  // The declaration census, indexed as `surfaces`. A declaration is written
  // when a face stands on it, evidence when something read it to identify a
  // surface without a face ever standing on it, and otherwise unused - which
  // *a declaration that is not taken is a bug report* makes a defect to
  // explain. See doc/step-rungs.md, *How to read a result*.
  std::vector<char> decl_written = std::vector<char>(surfaces.size(), 0);
  // What class actually wrote each facet: 0 the tessellation, 1 a declared
  // plane, 2 a declared curved surface, 3 a surface the approximation fitted.
  // Per facet and not per declaration, because "a face stands on this
  // declaration" cannot see a declaration that kept one facet of a hundred,
  // and cannot see ground a fit took.
  std::vector<unsigned char> facet_class = std::vector<unsigned char>(faces.size(), 0);
  struct SectionMiss {
    double tilt, offset;
  };
  std::vector<SectionMiss> section_misses;
  // Edges that lie on the curve where two declared quadrics cross, and which two
  // they are. Both faces derive the arc from the same pair of declarations, so
  // they agree on it exactly and the shell closes without either having to know
  // what the other did - which is what makes a curve taken from declarations
  // different in kind from one fitted to the mesh.
  std::map<std::pair<int, int>, std::pair<const Surface *, const Surface *>> crossing_edges;
  std::set<std::pair<int, int>> crossing_keys;  // the same, as the sections' skip set
  double crossing_fit = 0;                      // the worst any of those arcs leaves either surface
  // What the fitter knew and used to discard. A count of chorded edges cannot
  // tell a fit that missed by 3.4e-05 from one that missed by a tenth of a
  // millimetre, and those want different work.
  std::size_t arc_refused = 0, arc_no_fit = 0, arc_creased = 0, arc_uncreased = 0;
  std::vector<double> arc_errors, arc_on_quadric, arc_on_fitted;
  // Which facet loops are not available to be written as planar faces. Filled
  // by the quadric pass and read again by its writer, so both sides of every
  // section agree about who is across it.
  std::vector<char> taken;
  // The planes that cut each quadric surface, gathered once so that every face
  // of that surface sees the same list and they agree about the curve between
  // them. Filled by the quadric pass, read again by its writer.
  std::map<const Surface *, std::vector<step::CutPlane>> section_planes;
  // Parallel to `bezier_patches`: the exact quadric that patch lies on, or null
  // where it is written as the spline it is in general. See quadricOfPatch.
  std::vector<std::shared_ptr<Surface>>& patch_quadric = rec.patch_quadric;

  // Views into what recognition decided.
  const std::vector<AnalyticFeatures::Band>& bands = features.bands;
  const std::vector<std::pair<AnalyticFeatures::RimRef, AnalyticFeatures::RimRef>>& rims = features.rims;
  const std::vector<char>& consumed = features.consumed;
  // A planar face the corner placement bent -> the triangles it is written as.
  std::map<std::size_t, std::vector<std::array<int, 3>>> split_for_corners;

  // What a planar loop gives up to a curve an analytic face writes along it.
  /*! One run of loop edges replaced by a single arc. */
  struct ArcSubstitution {
    std::size_t start = 0, count = 0;
    EdgeCurve *edge = nullptr;
    bool sense = true;  // orientation for this loop's own traversal
  };
  std::map<std::size_t, std::pair<EdgeCurve *, bool>> rim_of_loop;  // whole loop -> circle
  std::map<std::size_t, std::vector<ArcSubstitution>> arc_subs;     // runs inside a loop
  // Rims written as chords by both faces because the face across is fanned into
  // triangles and cannot be handed the arc. See the band writer below.
  std::size_t rim_unshared = 0;
  std::map<std::set<int>, EdgeCurve *> shared_rim_edges;  // rim vertices -> circle

  // The crossing curves the emitters build, keyed by their edge.
  // One neighbour is not a planar facet, and it is the exception that sentence
  // does not cover: where the sweep meets a *declared* surface, the two cross
  // along a curve both of them can derive from their own declarations, and
  // `crossing_edges` holds exactly those edges. Such an edge has to be taken
  // from `crossing_curves` by the sweep emitter for the same reason the quadric one takes it - it is
  // one edge, and the two faces on it must name one geometry. Whichever emitter
  // reaches it first builds it and the other reuses the object, which is why
  // that map belongs to neither of them.
  std::map<std::pair<int, int>, EdgeCurve *> crossing_curves;
  /*! One face's view of a crossing edge: the surface entity it wrote and the
   *  placement that entity carries, which is what a PCURVE's parameters mean.
   *
   * Recorded per face rather than per surface because the reference direction is
   * chosen from each face's own boundary, so two faces on one cylinder do not
   * share a parameter origin and a pcurve written against the wrong one is off
   * by a rotation.
   */
  struct CrossingSide {
    SurfaceType *surface = nullptr;
    Vector3d base, axis, ref;
    double slope = 0;  // a cone's, in its written direction; zero for a cylinder
  };
  std::map<std::pair<int, int>, std::vector<CrossingSide>> crossing_sides;
  int crossing_written = 0;

  // One per file - nothing about a 2D parametric context varies.
  ParametricContext *param_context = nullptr;

  // The plane sections written, and what the quadric emitter counted.
  // Sweep faces whose seam was moved to a column their boundary does not cross.
  std::size_t reseamed = 0, reseam_full = 0;
  std::map<std::set<int>, EdgeCurve *> section_edges;
  // What binding the written sections' ends to the surface cost - see SectionStats.
  step::SectionStats section_stats;
  // Sections the far face could not be handed, and which are therefore
  // written as chords by both rather than as a conic by one. Reached only
  // where a planar loop no longer carries the run - see below.
  std::size_t section_unshared = 0;
  std::size_t apex_seams = 0;            // cone faces closed round their apex
  std::size_t band_seams = 0;            // faces round their axis closed by a seam between two rims
  std::set<std::set<int>> section_subs;  // sections already handed to a planar face
  int sections_declared = 0, sections_fitted = 0;
  // Boundary sections refused because their plane was a facet of something
  // curved rather than a plane. See open item 26.
  int sections_on_facet = 0;
  /*! The placement of a conic at `origin`, its plane's normal `dir`, its
   * reference direction `towards`. */
  Axis2Placement *conic_placement(const Vector3d& origin, const Vector3d& dir, const Vector3d& towards);

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
