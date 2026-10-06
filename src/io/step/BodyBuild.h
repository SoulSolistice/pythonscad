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
#include <functional>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "geometry/AnalyticFeatures.h"
#include "geometry/Surface.h"
#include "geometry/brep/Brep.h"
#include "geometry/recognition/Pipeline.h"
#include "io/step/BrepMirror.h"
#include "io/step/BoundaryCycles.h"
#include "io/step/CornerLadder.h"
#include "io/step/CrossingRuns.h"
#include "geometry/linalg.h"
#include "io/StepKernel.h"

struct StepKernel::BodyBuild {
  BodyBuild(StepKernel& kernel, const char *name, const std::vector<Vector3d>& mesh_vertices,
            const std::vector<IndexedFace>& faces, const std::vector<std::shared_ptr<Curve>>& curves,
            const std::vector<std::shared_ptr<Surface>>& surfaces,
            const std::vector<int32_t>& faceOrigin, const std::vector<int32_t>& faceSurface,
            const std::vector<char>& faceDerived, const std::map<int, Vector3d>& cornerMoves,
            const std::map<int, std::size_t>& singleOwner,
            const std::map<int32_t, std::vector<std::size_t>>& owned,
            const std::vector<int>& faceParents, const std::vector<Vector4d>& faceNormals, double tol,
            bool analytic, bool approximate, const std::map<int, std::vector<std::size_t>>& cornerOwners,
            const SolidTree *solidTree)
    : kernel(kernel),
      entities(kernel.entities),
      name(name),
      mesh_vertices(mesh_vertices),
      faces(faces),
      curves(curves),
      surfaces(surfaces),
      faceOrigin(faceOrigin),
      faceSurface(faceSurface),
      faceDerived(faceDerived),
      cornerMoves(cornerMoves),
      singleOwner(singleOwner),
      owned(owned),
      faceParents(faceParents),
      faceNormals(faceNormals),
      tol(tol),
      analytic(analytic),
      approximate(approximate),
      cornerOwners(cornerOwners),
      solidTree(solidTree)
  {
  }

  /*! Every phase, in order. */
  void run();

  StepKernel& kernel;
  /*! Where every entity made here registers itself: the kernel's. */
  std::vector<Entity *>& entities;
  // Where this body's entities begin: everything from here to writeShells is its
  // B-rep's, and none of the product structure is (see writeShells).
  const std::size_t first_entity = entities.size();

  // The inputs, as build_tri_body received them.
  const char *name;
  const std::vector<Vector3d>& mesh_vertices;
  const std::vector<IndexedFace>& faces;
  const std::vector<std::shared_ptr<Curve>>& curves;
  const std::vector<std::shared_ptr<Surface>>& surfaces;
  const std::vector<int32_t>& faceOrigin;
  /*! Per face, the declared surface it was made on, as an index into
   * `surfaces` - identity carried through the booleans (PolySet::surface_tags)
   * - or -1 where that is not known. */
  const std::vector<int32_t>& faceSurface;
  /*! Per face, whether its name is a derivation (mintDerivedTag): D1, not D0. */
  const std::vector<char>& faceDerived;
  const std::map<int, Vector3d>& cornerMoves;
  const std::map<int, std::size_t>& singleOwner;
  const std::map<int32_t, std::vector<std::size_t>>& owned;
  const std::vector<int>& faceParents;
  const std::vector<Vector4d>& faceNormals;
  const double tol;
  const bool analytic;
  const bool approximate;
  const std::map<int, std::vector<std::size_t>>& cornerOwners;
  /*! The model's boolean over the solids its primitives declared, where it was
   * carried (PolySet::solid_tree): what existence is asked of. Null otherwise. */
  const SolidTree *solidTree;

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

  /*! Put the junction corners on the curve their two owners cross along - but
   * only once it is known which faces are analytic, and only if no face that
   * keeps a PLANE would be bent by it.
   *
   * A corner two declared surfaces made belongs on their intersection, and
   * reportOwnership has computed that point all along. Where it may be applied
   * is the whole difficulty, and two earlier places are ruled out by
   * measurement, both recorded in doc/step-export-development.md, *Corner
   * placement*: before mergeTriangles it keeps the neighbours planar and
   * destroys the merge, at twelve to sixty-two times the faces; after the merge
   * but before recognition it bends the faceted neighbours by 0.0331 where they
   * are polygons.
   *
   * Here is the place that costs neither. Recognition has already run on the
   * untouched, fully merged mesh, so the claims are what they were; and an
   * analytic face does not care whether its corners are coplanar, only a PLANE
   * does. On step-bored-cone 52 faces use a movable corner and every one of
   * them is written analytic, so nothing is bent and nothing is split.
   *
   * Where a polygon that keeps its plane would be bent, the corners it
   * implicates are refused and the rest kept - see applyCornerMoves. It was all
   * of them or none, per export, while a boundary half moved could open the
   * shell (doc/step-export-development.md, *Half a fix is worse than none*);
   * open item 11 closed the causes of that. */
  void placeCorners();
  // ...whose steps, in order:
  /*! The other half of the placement: a corner one declared surface shares with
   * a plane the mesh carries and nothing declared.
   *
   * Here rather than beside the two-owner case in reportOwnership, because
   * *which* plane is only answerable here. Asked of raw triangles it cannot be
   * done: a frustum cap shares an original with its wall, and no distance
   * threshold separates a cut facet near the rim from a wall facet, or a thin
   * base sliver from either - three attempts, all in
   * doc/step-export-development.md. mergeTriangles has since answered it
   * exactly, because a merged face that keeps a PLANE *is* a plane: the planes
   * at a corner are the distinct ones among the faces using it.
   *
   * One plane, and the corner goes on the conic where it meets the declared
   * surface. Two, and it is a triple point - the cut and the sliver of base a
   * tilted cut leaves behind - which belongs on neither conic and stays. */
  void placeOnMeshPlanes();

  /*! The recognition ledger and its reports, printed between the placement
   * rungs that read recognition and the ones that write the file. */
  void reportRecognition();

  /*! Ordered after the two paths above, and that order is the doctrine rather
   * than a convenience: where the model declared what a corner is on, the
   * declaration decides, and this general path applies only to what the
   * declarations left. Every path checks `moves.count(v)` first, so first refusal
   * is all the ordering does. Measured on step-exact-trim, where 32 corners are
   * reachable both ways: run first, this claimed all 32 and the declared rung
   * reported none.
   * A corner belongs where the surfaces of the analytic faces it bounds cross.
   *
   * This is the general statement of what the two placements above do for
   * special cases, and it is the one the *file* can be held to: whatever
   * provenance says about which original made a vertex, the faces written on it
   * assert that the corner lies on their surfaces, and where two of them meet it
   * asserts both. Provenance cannot answer this on its own - it is per original
   * solid, so it names a cylinder for a corner sitting on that solid's cap
   * (open item 18), and it names two coaxial cylinders 3.000 mm apart, which
   * never meet, for the corners where the band family's sweep crosses its bore.
   * Both are cases where the owners are not the surfaces the file will assert.
   *
   * Measured on `step-band-family` at $fn 32: 47 of its 1234 candidate edges
   * fall back to chords, and they are exactly the edges whose endpoints are off
   * the sweep - by 2.2e-02 to 1.0e-01, the 32-gon's own sagitta
   * 20(1 - cos(pi/32)) = 0.0963, so those ends sit on chord planes and were
   * never placed at all. A Bezier interpolates its endpoints, so no degree and
   * no better tracing can rescue an edge whose ends are not on the surface:
   * placing them is what turns those chords into the curve the two surfaces
   * actually cross along. See open items 3 and 9.
   *
   * Held to the ladder's own rules. The crossing is solved to an absolute
   * 1e-11, accepted only where the result is on *both* surfaces to 1e-9, and a
   * corner may travel no further than its nearest neighbouring vertex - which is
   * section 4's bound, and not the longest edge at it, which is the trap that
   * licensed a 1.9976 move on step-partial-cylinder. */
  void placeOnFileFaces();

  /*! A welded vertex stays where it is.
   *
   * Those are the insertions that keep the shell conformal: a vertex one face
   * has on an edge another face spans in one line, added to both so they meet
   * edge for edge. It therefore sits on the *boundary* of faces whose loops do
   * not contain it, and the bend gate below - which reads each face's own loop -
   * cannot see what moving it does to them. On step-declare-grid-two-bands one
   * such vertex was moved 1.73e-07 off the flat bottom it lies on the rim of,
   * and every loop in the file was still within 1e-9 of its own plane: the face
   * asserted a plane a corner of it was off, and nothing internal could tell. */
  void holdWeldedVertices();

  /*! No move may take a corner off the surface of an analytic face it bounds.
   *
   * The veto below asks that of a face that keeps a PLANE, and nothing asked it
   * of a curved one: the corner is written as a vertex of that face, and a face
   * bounded by a point off its own surface is describing a different surface.
   * Every path above is meant to satisfy this by construction - the crossing
   * puts a corner on both its owners, the conic and triple placements on their
   * owner, the arbiter's one-face candidate on that face's own - and one of them did not.
   * The placement onto a conic trusted provenance's single owner, and where a
   * boolean cut every facet of a bore, that owner was the wall across the cap:
   * band-fn024's 48 bore corners were slid 3.0 off the face they bound, lid10's
   * 120 chamfer corners 0.65 and 2.25, hidden only by the plane veto. That path
   * now leaves a corner bounding a face named for another curved surface to
   * the rungs that solve it (placeOnMeshPlanes; open item 39, R6), and this is
   * measured to be reached by no fixture and no flagship coupon, with the veto
   * and without it.
   *
   * So it is an error, not a report: reaching it means a placement has a defect
   * the file would carry. The move is held, so the file stays right, and the
   * flagship check fails on the message, so the defect cannot come back quietly.
   *
   * Against each face's own declaration: a quadric states where it is, a grid
   * or patch publishes its band - AnalyticFeatures::declaredBand - and a corner
   * already that far off is judged only on whether the move takes it further.
   * A band is not a Surface record, so its cone or cylinder is read off its
   * axis and rims. A projection that does not answer judges nothing. */
  void checkMovesStayOnFaces();

  /*! The moves, written into the working vertices: the corners a bent planar
   * face implicates are refused, settling until no face bends (refusals only
   * grow), and a polygon a kept move still bends goes into `split_for_corners`
   * to be written as the triangles its corners now lie on.
   *
   * Under the approximation flag only. The exact tier asserts nothing the mesh
   * does not already state, and moving a vertex is such an assertion - without
   * this gate step-bored-cone's analytic export comes out with a PLANE
   * disagreeing with the winding of its own bound. */
  void applyCornerMoves();

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

  /*! The crossing edges as curves: each maximal run of them between the same
   * two faces - chained through every vertex where nothing else meets them -
   * is one piece, and one edge in the file. A vertex ends a run where a third
   * edge will meet it: another face's boundary, a facet no crossing region
   * holds, the seam that closes a face round its axis. A run of several edges
   * is fitted as one curve (CrossingRuns.h), and cut in two where that fit is
   * refused; a single edge is the Bezier intersectionArc fits. Decided here,
   * after the sections and before either emitter, so the two faces on a piece
   * read one geometry for it. */
  void decideCrossingRuns();

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

  /*! Every facet loop no analytic face took, as a loop of the B-rep with its
   * carrier plane - a declared plane wherever one agrees - and the arcs and
   * conics the analytic faces handed it spliced in. Then the declaration
   * census, which is complete once the last face that can stand on a
   * declaration has been written. */
  void emitPlanarFaces();

  /*! The fans for the polygons the corner placement bent.
   *
   * Each triangle takes its boundary edges from the same map every other face
   * uses, so the shell still stitches along them; the diagonals are new and
   * used by exactly the two triangles that share them. Each triangle carries
   * the plane its own three corners lie on, which is the point of the fan -
   * the polygon's plane is the one they no longer all sit on. */
  void emitCornerFans();

  /*! The B-rep's faces, in the order the file states them: every outer planar
   * loop with the loops of its holes, then the analytic faces the emitters
   * wrote, read off their entities, then the fans; and its shells, read off
   * the faces' own edges. */
  void assembleFaces();

  /*! Every edge bounding a recovered curved face was still a straight line: the
   * exporter drops a cylinder behind the mesh's polyline boundary, so the edge
   * is the chord where the surface is the arc, and the gap between them is the
   * sagitta. Measured on the band family, 366 of 388 line-on-cylinder edges lay
   * off the cylinder they bounded, by up to 0.17mm - doc/step-export-development.md
   * has the tables. Every kernel that reads such a file has to widen its
   * tolerance to swallow that; two do it silently and one declines, and none of
   * them is wrong to object.
   *
   * The 22 edges that were already exact say what the fix is. A ruling lies on a
   * cylinder by construction, so the segments running up and down the axis need
   * nothing, and it is the ones running around it - at constant height, chords
   * of a circle - that should be written as arcs of that circle instead.
   *
   * An edge belongs to two faces, so promoting it is only sound when both can
   * contain the arc. A plane can when its normal is the axis, which is what a
   * flat top or an annulus is; anything else declines, and the edge stays the
   * line it was. That is what keeps this from ever moving an edge off a face
   * that was exact already, which would trade one kernel's complaint for
   * everyone's. */
  void promoteCurves();

  /*! Every edge against its own two vertices - SOLIDWORKS fault code 7, asked
   * of our own file - and the report lines for what the emitters counted. */
  void checkAndReport();

  /*! The B-rep the file holds (open item 39, step 2d). The planar faces and
   * the fans build it directly, their own straight edges with them (lineEdge);
   * what they take from the phases before them - the analytic faces, the edges
   * those made, and every vertex - is read off those phases' entities through
   * `reader`, as they name it. Every phase from assembleFaces on reads and
   * writes this and nothing else - promoteCurves, the checks, the translator -
   * and the point where entities stop being made moves upstream a phase at a
   * time, until nothing is read. */
  brep::Body brep_body;
  /*! The B-rep's vertex for mesh vertex `ind`, one wherever its point is, as
   * get_vertex merges them: read off the VERTEX_POINT where an analytic face
   * made one, else made here, on the B-rep, with no entity at all (open item
   * 39, step 2d, the fourth move). An entity get_vertex makes later at the
   * same point is bound to it, so the two are one vertex either way. */
  std::size_t brepVertex(int ind);
  // The vertices brepVertex made itself, by their exact coordinates.
  std::map<std::tuple<double, double, double>, std::size_t> brep_vertex_at;
  /*! The straight edge along mesh edge (a, b), as the B-rep's, and in `forward`
   * whether it runs from `a`. One per mesh edge, whoever names it first: the
   * one an analytic face made along it, read, or else one built here, as the
   * line from its first vertex to its second - which is what the edge map
   * made as an entity before (StepKernel::create_line_edge_curve). Asked only
   * of two corners that are two vertices: where the placement made them one,
   * there is no edge between them (open item 39, R17). */
  std::size_t lineEdge(int a, int b, bool& forward);
  std::map<std::pair<int, int>, std::size_t> line_edges;  // mesh edge (lo, hi) -> B-rep edge
  /*! brep::check over the B-rep: the structure the file asserts, held to the
   * neutral contract - an EXPORT-ERROR where it breaks it - and every vertex's
   * and edge's class stated. */
  void checkBrep();

  /*! The faces as closed shells - one per connected component, since a
   * CLOSED_SHELL has to be connected - each a MANIFOLD_SOLID_BREP, and the
   * product structure and contexts around them. */
  void writeShells();
  /*! The file's resolution: how exactly its corners are placed on their faces,
   * the corner ladder's floor at the part's size - see writeShells. What every
   * approximation in the file is held to, and what it declares as its
   * uncertainty. A gap in the file wider than this is a defect, and
   * checkAndReport holds the file to it. */
  [[nodiscard]] double resolution() const;

  // ---- what the phases hand each other

  // A working copy, because the corner placement moves junction corners onto
  // the curve their two declared owners cross along, once recognition has run
  // and it is known which faces would mind. Nothing else writes to this.
  std::vector<Vector3d> vertices = mesh_vertices;
  const double model_tol = tol > 0 ? tol : 1e-5;
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

  // The faces the analytic emitters write.
  std::vector<Face *> sfaces_extra;
  // The faces an emitter built on the B-rep rather than as entities, by their
  // place in `sfaces_extra`, which holds a null there: the analytic faces enter
  // the B-rep in that one order, whoever built them (open item 39, step 2d).
  std::map<std::size_t, brep::Face> built_faces;

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
  // What class actually wrote each facet - recognition::FacetClass: the
  // tessellation, a declared plane, a declared curved surface, a surface the
  // approximation fitted, or a claim naming none.
  // Per facet and not per declaration, because "a face stands on this
  // declaration" cannot see a declaration that kept one facet of a hundred,
  // and cannot see ground a fit took.
  std::vector<unsigned char> facet_class = std::vector<unsigned char>(faces.size(), 0);
  // What each face an emitter made as an entity stands for, registered where
  // the face is made rather than inferred from it afterwards: the record it is
  // on, the facets it covers, and the class that decided it. The one channel
  // the named-facet census and the B-rep both read (open item 39); a face built
  // on the B-rep states the same on itself. Keyed by the face for lookup only;
  // faces are walked in `sfaces_extra` order, never this map's.
  using FaceSource = step::FaceSource;
  std::map<const Face *, FaceSource> face_source;
  // What an edge's maker states about its carrier, where it states anything - a
  // fit's error, or that it is its face's own boundary; see step::EdgeSource.
  std::map<const EdgeCurve *, step::EdgeSource> edge_source;
  /*! Where the B-rep takes what a phase before it made as an entity. */
  step::BrepReader reader{brep_body, edge_source};
  /*! What a face stands for, standing on `record` over `facets`. Its class is
   * the weakest its facets' honoured claims state, unless the caller states it
   * - a planar face, which no recogniser claims. */
  [[nodiscard]] FaceSource sourceOf(std::shared_ptr<const Surface> record,
                                    std::vector<std::size_t> facets,
                                    std::optional<recognition::EvidenceClass> evidence = std::nullopt,
                                    bool plane_from_corners = false) const;
  /*! Register `face`, just made as an entity, as sourceOf says - written on
   * `carrier`, the surface its maker built it from, where that is not `record`
   * itself; `derived` where the maker derived it from `record` in closed form
   * (see brep::Face::carrier). */
  void registerFace(const Face *face, std::shared_ptr<const Surface> record,
                    std::vector<std::size_t> facets,
                    std::optional<recognition::EvidenceClass> evidence = std::nullopt,
                    bool plane_from_corners = false, std::shared_ptr<const Surface> carrier = nullptr,
                    bool derived = false);
  /*! The entry of `surfaces` that is `record`, or -1: a fitted record, or a
   * plane derived from a face's corners. */
  [[nodiscard]] int32_t surfaceIndexOf(const Surface *record) const;
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
  // Of those, the ones whose crossing turns a sweep's crease corner inside the
  // edge, and where: ordered from the edge's lower vertex. See creaseCorners.
  std::map<std::pair<int, int>, std::vector<Vector3d>> crossing_corners;
  std::set<std::pair<int, int>> crossing_keys;  // the same, as the sections' skip set
  double crossing_fit =
    0;  // the worst any written crossing curve leaves either surface - the file's uncertainty
  // What the fitter knew and used to discard. A count of chorded edges cannot
  // tell a fit that missed by 3.4e-05 from one that missed by a tenth of a
  // millimetre, and those want different work.
  std::size_t arc_refused = 0, arc_no_fit = 0, arc_creased = 0, arc_uncreased = 0;
  // Edges between two declared surfaces with an end off one of them: no curve
  // of theirs passes through it, so they stay chords, the corners' to place.
  std::size_t crossing_ends_off = 0;
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
  // where it is written as the spline it is in general - as its maker stated
  // it (BezierPatchSurface::stateQuadric).
  std::vector<std::shared_ptr<Surface>>& patch_quadric = rec.patch_quadric;

  // Views into what recognition decided.
  const std::vector<AnalyticFeatures::Band>& bands = features.bands;
  const std::vector<std::pair<AnalyticFeatures::RimRef, AnalyticFeatures::RimRef>>& rims = features.rims;
  const std::vector<char>& consumed = features.consumed;
  // A planar face the corner placement bent -> the triangles it is written as.
  std::map<std::size_t, std::vector<std::array<int, 3>>> split_for_corners;
  /*! Whether facet loop `loop` is written as a bound of a planar face: an
   * outer loop no analytic face took and the placement did not fan, or a hole
   * of one. The one question every face that hands a planar loop a curve along
   * it has to ask first - a fan writes one straight line per triangle side, so
   * a loop that is not written cannot take the curve, and the curve is then the
   * mesh's chords on both sides - and the one the planar writer walks by. */
  [[nodiscard]] bool boundWritten(std::size_t loop) const;
  /*! Whether planar face `face` is flat where it stands with its corners at
   * `at`: every corner, its holes' too, on the declared plane the face was made
   * on - the first its unmoved corners lie on - or all on one plane where it
   * was made on none, at machine precision (recognition::allCoplanar). What the
   * writer holds a face to, asked before a move is applied and of every face a
   * fan replaces (open item 39, R2, R3). */
  [[nodiscard]] bool flatWhereItStands(std::size_t face, const std::function<Vector3d(int)>& at) const;
  /*! The declared surface facet `facet` was made on, by its maker's name, or
   * null where it carries none. A facet named for a declared plane is a face of
   * the model on that record; one named for a curved surface is a chord of it
   * wherever its corners are - identity, not an angle (open item 39, R4). */
  [[nodiscard]] const Surface *namedFor(std::size_t facet) const;

  // What a planar loop gives up to a curve an analytic face writes along it.
  /*! One run of loop edges replaced by a single arc. */
  /*! An edge a phase hands to the planar faces: an entity a phase still
   * makes, read when it is named, or one built on the B-rep - its index (open
   * item 39, step 2d). */
  struct EdgeRef {
    EdgeRef() = default;
    EdgeRef(EdgeCurve *e) : entity(e) {}  // NOLINT: an entity converts
    static EdgeRef built(std::size_t index)
    {
      EdgeRef out;
      out.brep = index;
      return out;
    }
    EdgeCurve *entity = nullptr;
    std::size_t brep = std::size_t(-1);
  };
  /*! The B-rep's edge `ref` names. */
  std::size_t brepEdge(const EdgeRef& ref)
  {
    return ref.entity != nullptr ? reader.edge(ref.entity) : ref.brep;
  }
  struct ArcSubstitution {
    std::size_t start = 0, count = 0;
    EdgeRef edge;
    bool sense = true;  // orientation for this loop's own traversal
  };
  std::map<std::size_t, std::pair<EdgeRef, bool>> rim_of_loop;   // whole loop -> circle
  std::map<std::size_t, std::vector<ArcSubstitution>> arc_subs;  // runs inside a loop
  // Rims written as chords by both faces because the face across is fanned into
  // triangles and cannot be handed the arc. See the band writer below.
  std::size_t rim_unshared = 0;
  std::map<std::set<int>, EdgeCurve *> shared_rim_edges;  // rim vertices -> circle

  // The crossing curves, decided once and keyed by the run of mesh edges each
  // one is written for - see decideCrossingRuns.
  //
  // Where a sweep or a quadric meets another *declared* surface, the two cross
  // along a curve both can derive from their own declarations, and
  // `crossing_edges` holds exactly those mesh edges. The two faces on such a
  // boundary must name one geometry for it, which is why neither emitter builds
  // it: each looks its edges up here and writes the piece they belong to.
  /*! One curve two faces meet along: a run of crossing edges, from a vertex
   * where something else meets it to the next. */
  struct CrossingPiece {
    std::vector<int> verts;  // along the curve; front() == back() for a closed one
    const Surface *a = nullptr, *b = nullptr;
    /*! The file's edges along it, in order from verts.front() to verts.back():
     * one, and one more for every corner the curve turns inside a mesh edge,
     * where a vertex of the file stands - a kernel wants each edge's curve G1.
     * Empty: written as chords. */
    std::vector<EdgeCurve *> edges;
    std::vector<step::CurveFit> fits;  // parallel to `edges`; empty for one Bezier
  };
  std::vector<CrossingPiece> crossing_pieces;
  std::map<std::pair<int, int>, std::size_t> piece_of_edge;  // mesh edge (lo, hi) -> piece
  std::size_t crossing_mesh_edges = 0;                       // the mesh edges the pieces cover
  /*! The piece mesh edge (a, b) belongs to, or null. */
  const CrossingPiece *pieceOf(int a, int b) const;
  /*! Where to start walking a boundary cycle so that no piece straddles the
   * start: 0 unless the cycle begins inside a piece of several edges. */
  std::size_t walkStart(const std::vector<int>& cycle) const;
  /*! Whether a face walking `cycle` from index `i` traverses `piece` from its
   * first vertex (true), from its last (false) - and false in `ok` where the
   * cycle does not carry the piece's vertices in either order from there. */
  bool walksForward(const CrossingPiece& piece, const std::vector<int>& cycle, std::size_t i,
                    bool& ok) const;
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
  std::map<std::size_t, std::vector<CrossingSide>> crossing_sides;  // piece -> the faces on it
  int crossing_written = 0;
  // A face whose boundary cycle does not carry a piece's vertices in order where
  // it meets the piece: the two faces then disagree about an edge.
  std::size_t crossing_unwalked = 0;

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
  int sections_declared = 0, sections_fitted = 0, sections_derived = 0, sections_pencil = 0;
  // Boundary sections refused because their plane was a facet of something
  // curved rather than a plane. See open item 26.
  int sections_on_facet = 0;
  /*! The placement of a conic at `origin`, its plane's normal `dir`, its
   * reference direction `towards`. */
  Axis2Placement *conic_placement(const Vector3d& origin, const Vector3d& dir, const Vector3d& towards);

  // Each facet loop as a loop of the B-rep, and each outer one's face - its
  // plane and what the plane stands for, its loops added by assembleFaces; see
  // emitPlanarFaces.
  std::vector<brep::Loop> planar_loops = std::vector<brep::Loop>(face_cnt);
  std::map<std::size_t, brep::Face> planar_faces;  // outer loop -> its face
  // The triangles of the fans, as faces of the B-rep; see emitCornerFans.
  std::vector<brep::Face> fan_faces;
  // Planar faces asserting a plane one of their bounds is not in, and by how much.
  std::size_t planes_off_plane = 0;
  double worst_plane_off = 0;

  // The corner placement's working state: the moves it proposes, which rung
  // proposed each, and what each path added.
  std::map<int, Vector3d> moves = cornerMoves;
  // Which rung placed each move, so the arbiter can weigh it against the
  // others. See kCornerRungs.
  std::map<int, step::CornerRung> move_rung;

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
