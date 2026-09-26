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
#include "io/step/CornerLadder.h"
#include "io/step/CrossingRuns.h"
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

  /*! A corner of a trimmed quadric that nothing else holds goes on that quadric.
   *
   * The placement above reaches a corner two surfaces made. It does not reach a
   * corner the *mesh* made in the middle of a facet, which a trim then left on
   * the boundary of the face written on that quadric: nothing declared it, so
   * provenance names no owners for it and no crossing curve applies. It is
   * nonetheless a corner of an analytic face, sitting at the inradius of the
   * polygon the surface was tessellated as - 20(1 - cos(pi/32)) = 0.0963 inside
   * the cylinder its own face is written on.
   *
   * On step-band-family that is the last 39 corners, at the height where the
   * ridge tapers to nothing and the trim runs along the bore's own facets
   * instead of across them. They are moved onto the surface their face names,
   * and only where no other exact declared surface already holds them - a
   * corner on two of them belongs where both agree, which is the crossing curve
   * the paths above compute, not on either one alone. */
  void placeOnOwnQuadric();

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

  /*! Keep every exact thing the corner is already on.
   *
   * A corner owned by a declared quadric and a declared sweep was being put
   * where the two cross - but the two are not worth the same. The quadric
   * states where its surface is; the sweep was interpolated through the model's
   * stations and publishes a tessellation band saying how well. Aiming at their
   * crossing curve spends an exact surface to satisfy a fitted one.
   *
   * It shows up as a plane. The mesh's flat bottom on step-declare-grid.py is a
   * 95-corner face, and one of its corners is owned by the wall cylinder and
   * the ridge's sweep; placed on their crossing curve it leaves z = 0 by
   * 0.0423, and a face that big cannot be fanned without turning the part's
   * flat bottom into 93 triangles, three of them off level and one by 21
   * degrees. The corner was exactly on that plane, 0.0103 off the cylinder -
   * 19.2(1 - cos(pi/96)), the wall's own sagitta - and 0.0378 off the sweep,
   * which had declared a band of 0.1290. Nothing was wrong with it that the
   * sweep was entitled to complain about.
   *
   * So: a plane the mesh carries *and a declaration vouches for* is kept, and
   * the corner is placed on that plane crossed with its exact owner instead.
   * Measured over the fixture set, 1533 merged planes are bent by a move and
   * exactly 2 are vouched for - the flat bottoms of step-declare-grid.py and
   * -strip, matching a declaration's own plane to 0.00e+00 - so this reaches
   * the case it is for and no other.
   *
   * The direction is what makes it safe: a declaration only ever *confirms* a
   * plane the mesh already has, and never invents one. refpt is not reliably a
   * rim - declare_cylinder takes the caller's centre, and a fitted cylinder's
   * refpt is wherever the fit landed - so a plane the mesh does not carry must
   * not be conjured out of one. */
  void keepExactSurfaces();

  /*! No move may take a corner off the surface of an analytic face it bounds.
   *
   * The veto below asks that of a face that keeps a PLANE, and nothing asked it
   * of a curved one: the corner is written as a vertex of that face, and a face
   * bounded by a point off its own surface is describing a different surface.
   * Every path above is meant to satisfy this by construction - the crossing
   * puts a corner on both its owners, the conic and triple placements on their
   * owner, the pass onto a quadric on the face's own - and one of them did not.
   * The placement onto a conic trusted provenance's single owner, and where a
   * boolean cut every facet of a bore, that owner was the wall across the cap:
   * band-fn024's 48 bore corners were slid 3.0 off the face they bound, lid10's
   * 120 chamfer corners 0.65 and 2.25, hidden only by the plane veto. That path
   * now keeps a corner on the quadric it is already on (see deserts_exact), and
   * this is measured to be reached by no fixture and no flagship coupon, with
   * the veto and without it.
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

  /*! Every facet loop no analytic face took, as a planar bound with its edges
   * and its carrier PLANE - a declared plane wherever one agrees - with the arcs
   * and conics the analytic faces handed it spliced in. Then the declaration
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

  /*! Combine every outer loop with the loops of its holes into one ADVANCED_FACE.
   * Then the analytic faces the emitters wrote, which are faces of the same
   * shell. */
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

  /*! The faces as closed shells - one per connected component, since a
   * CLOSED_SHELL has to be connected - each a MANIFOLD_SOLID_BREP, and the
   * product structure and contexts around them. */
  void writeShells();

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
  // Of those, the ones whose crossing turns a sweep's crease corner inside the
  // edge, and where: ordered from the edge's lower vertex. See creaseCorners.
  std::map<std::pair<int, int>, std::vector<Vector3d>> crossing_corners;
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
  int sections_declared = 0, sections_fitted = 0;
  // Boundary sections refused because their plane was a facet of something
  // curved rather than a plane. See open item 26.
  int sections_on_facet = 0;
  /*! The placement of a conic at `origin`, its plane's normal `dir`, its
   * reference direction `towards`. */
  Axis2Placement *conic_placement(const Vector3d& origin, const Vector3d& dir, const Vector3d& towards);

  // Each facet loop's bound, its carrier plane and its edges; see emitPlanarFaces.
  std::vector<FaceBound *> face_bounds = std::vector<FaceBound *>(face_cnt, nullptr);
  std::vector<Plane *> planes = std::vector<Plane *>(face_cnt, nullptr);
  std::vector<std::vector<EdgeCurve *>> loop_edges = std::vector<std::vector<EdgeCurve *>>(face_cnt);
  // Planar faces asserting a plane one of their bounds is not in, and by how much.
  std::size_t planes_off_plane = 0;
  double worst_plane_off = 0;
  // What the mesher was entitled to call coplanar, by its own rule - see
  // coplanarTolerance in GeometryEvaluator.cc, which this has to agree with.
  double mesh_coplanar_tol = 1e-9;

  // Every face of the shell, and the edges bounding each; see assembleFaces.
  std::vector<Face *> sfaces;
  std::vector<std::vector<EdgeCurve *>> face_edges;

  // The corner placement's working state: the moves it proposes, which rung
  // proposed each, and what each path added.
  std::map<int, Vector3d> moves = cornerMoves;
  // Which rung placed each move, so the arbiter can weigh it against the
  // others. See kCornerRungs.
  std::map<int, step::CornerRung> move_rung;
  // what each path added
  std::size_t on_conic = 0, on_triple = 0, on_own = 0, on_faces = 0, on_plane_face = 0;
  /*! Whether placing corner `v` at `target` would take it off an exact surface the
   * model declared and the corner is already on - one other than `own`, which is
   * where it is being placed.
   *
   * The placement onto a conic below rests on provenance naming *one* declared
   * owner, and provenance is per original solid, decided by whole facets: an
   * original none of whose facets a boolean left whole owns nothing. Its surface
   * is then invisible to the vote, and a corner on it is handed to whatever the
   * other original at that junction owns. Measured on step-band-family at $fn
   * 24, the ridge cuts all but 2 of the bore's facets - three are needed - so
   * its 48 rim corners, generated on the r = 20 bore to 1e-14, were given the
   * r = 23 outer wall as sole owner, slid 3.0 along the cap onto it, and left the
   * bore face they bound 3.0 off its own surface. lid10 does the same to 120
   * corners of two chamfers, 0.65 and 2.25 along its z = 95 face. On both, only
   * the plane veto kept it out of the file - and it does not need the flagship
   * coupons: a bore with a frustum subtracted through its whole length, three
   * primitives, writes the bore 3.0 off itself with the veto silent.
   *
   * So the question is asked of the declarations rather than of the vote: a
   * corner on a declared quadric stays on it. That is the rule the pass placing
   * corners onto their own quadric already keeps, and the one the reroute below
   * is named for. A surface that answers by projecting is not asked - a grid
   * states a band, not a position, and a corner off it is its fit's business.
   *
   * Asked here and not of the triple-point placement, which rests on the same
   * single owner: across every fixture and flagship coupon it moved no corner
   * off a quadric, and a guard with nothing to refuse has regressions nothing
   * would see. The check before the veto below covers it, loudly. */
  bool deserts_exact(int v, const Surface *own, const Vector3d& target);

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
