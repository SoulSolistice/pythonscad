#pragma once

/*! Recognising analytic surfaces in a merged polygon mesh.
 *
 * Nothing here knows about STEP. The recogniser answers one question - which
 * runs of facets were modelled as a surface of revolution, and can every face
 * sharing their edges agree to the substitution - and returns the answer as
 * plain data. Turning that into CYLINDRICAL_SURFACE, or into whatever a
 * FreeCAD or IGES writer would want, is the caller's job.
 *
 * Recognition needs two inputs that have to agree, and they fail
 * independently:
 *
 *   geometry  do these facets fit the surface exactly?  (measured here)
 *   intent    did the model mean this surface, or is it a prism?  (`surfaces`)
 *   topology  will every face using these edges accept the substitution?
 *
 * The intent gate is not optional. A ring of N quads is exactly the mesh of an
 * N sided prism, and a cube's four sides fit a cylinder through its corners
 * with zero residual, so no measurement of the mesh can tell one from the
 * other. `surfaces` carries what the model declared - see Surface.h and the
 * provenance channel in ManifoldGeometry - and a band is accepted only when
 * the fit succeeds *and* a declaration matches it.
 */

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "geometry/linalg.h"

class Surface;
class BezierPatchSurface;

namespace AnalyticFeatures {

/*! `band_of_loop` entry for a loop which is not part of any band. */
inline constexpr std::size_t NO_BAND = std::size_t(-1);

/*! A band of facets around a common axis: a cylinder when both rims have the
 * same radius, a frustum when they do not.
 *
 * `walls` are the loops it is made of, dropped in favour of a single face.
 * Whether that face can be written depends on what each rim borders, which is
 * decided later - see RimRef. */
struct Band {
  std::vector<std::size_t> walls;
  /*! Set when a chain of bands was merged into one curved zone - a sphere when
   * the run ends at a cap, a torus when it closes on itself.
   *
   * A sphere is not a grid to be grown, it is a *stack of bands*: every ring of
   * its tessellation is already a frustum whose two rims are circles, and the
   * whole zone is the maximal run of them joined at shared rims whose vertices
   * all lie on one declared sphere. The merged band keeps the outer rims of the
   * run, so the rim rules that were resolved for the ends still hold and the
   * caps at either end are untouched. */
  std::shared_ptr<const Surface> zone;
  Vector3d axis, base;  // base is the centre of the bottom rim
  double r_bottom = 0, r_top = 0;
  double height = 0;
  bool closed = false;   // covers the full turn, so the face is periodic
  bool outward = false;  // wall normals point away from the axis
  std::vector<int> bottom_set, top_set;

  /*! Set when the top rim is a planar section which is not perpendicular to
   * the axis, so it is an ellipse rather than a circle.
   *
   * A cylinder cut at an angle is still a cylinder, and every vertex of the
   * cut still lies on it - what stops being true is that the rim sits at one
   * height, which is what the two-rim band model assumes throughout. The
   * plane is kept here so the rim can be written as the conic it is; nothing
   * else about the band changes, and `height` is measured to where the axis
   * pierces this plane. Only a cylinder can be tilted this way: a cone cut
   * off-axis gives a conic the rim machinery has no radius for. */
  bool top_tilted = false;
  Vector3d top_normal{0, 0, 0};  // unit, oriented so that top_normal.dot(axis) > 0

  int seam_bottom = -1, seam_top = -1;  // the ruling the seam runs along
  bool alive = true;
  const char *dropped = nullptr;  // why it was left faceted, for the report

  /*! A frustum rather than a cylinder. */
  [[nodiscard]] bool isCone() const;
};

/*! What lies on the other side of one rim of a band.
 *
 * A rim can only be collapsed into a circle when everything using its edges
 * agrees to the substitution. Three cases do:
 *
 *   WHOLE_LOOP      the rim is the complete bound of one neighbouring face
 *   LOOP_RUN        the rim is a consecutive run of edges inside one such loop
 *   OTHER_BAND      the whole circle is shared with another band, which is
 *                   being collapsed too - a wall standing on a chamfer, and
 *                   the case that keeps a chamfered body faceted until both
 *                   halves can be written
 *   OTHER_BAND_ARC  the same, for two bands that each stop short of a full
 *                   turn: one arc bounding two curved faces. A bayonet lug is
 *                   a wall on a chamfer on a wall, none of them going all the
 *                   way round, so every joint in it is this case.
 *
 * Anything else - most often one neighbouring face per facet - leaves the band
 * faceted. */
struct RimRef {
  enum Kind { UNRESOLVED, WHOLE_LOOP, LOOP_RUN, OTHER_BAND, OTHER_BAND_ARC };
  Kind kind = UNRESOLVED;
  std::size_t loop = 0;              // WHOLE_LOOP, LOOP_RUN
  std::size_t start = 0, count = 0;  // LOOP_RUN
  std::size_t band = 0;              // OTHER_BAND, OTHER_BAND_ARC

  /*! The wall facets run counter clockwise about the axis, which is the
   * direction the collapsed face has to traverse this rim: the face replaces
   * those facets, so its boundary is theirs. */
  bool wall_ccw = false;

  /*! The two ends of the arc, in counter clockwise order, for a rim of a band
   * that stops short of a full turn. Taken from the rim's own edges - the two
   * vertices used by one of them rather than two - so they are the same
   * whether the other side of the rim is a planar loop or another band. */
  int ccw_start = -1, ccw_end = -1;

  /*! Where the collapsed face's traversal of this rim begins and ends, which
   * is the arc's two ends taken in the wall's direction. */
  [[nodiscard]] int traversalStart() const { return wall_ccw ? ccw_start : ccw_end; }
  [[nodiscard]] int traversalEnd() const { return wall_ccw ? ccw_end : ccw_start; }
};

/*! The mesh the recogniser reads, as the caller already has it.
 *
 * `loops` are cleaned polygons - no repeated vertices, indices canonical, so
 * two loops naming the same corner name it with the same index. The four
 * vectors are parallel and one entry long per loop. */
struct Mesh {
  const std::vector<Vector3d> *vertices = nullptr;
  const std::vector<std::vector<int>> *loops = nullptr;
  const std::vector<char> *valid = nullptr;    // 0 for a loop to ignore
  const std::vector<char> *is_hole = nullptr;  // 0 for an outer bound
  const std::vector<Vector3d> *normals = nullptr;
};

struct Result {
  std::vector<Band> bands;
  std::vector<std::pair<RimRef, RimRef>> rims;  // bottom, top - one per band
  std::vector<std::size_t> band_of_loop;        // NO_BAND when not in a band
  std::vector<char> consumed;                   // loop is part of a live band

  /*! What was recognised and, for every band that was not, the rule that
   * rejected it.
   *
   * A band that is never recognised looks exactly like one that was never
   * there, so a caller which swallows this loses the only signal that a wall
   * which should have been written was not. Print it. */
  std::vector<std::string> report;
};

/*! A run of facets lying on one declared Bezier patch.
 *
 * Unlike a band, this is not walked out from a seed: the patch is known
 * exactly, so the region is simply every facet all of whose vertices lie on it.
 * There is nothing to fit and nothing to grow, which is the whole point of
 * having the generator declare a spline rather than recovering one. */
struct Patch {
  std::shared_ptr<const Surface> surface;  // a BezierPatchSurface
  std::vector<std::size_t> facets;

  /*! The boundary of the region, split into runs - one per edge of the patch's
   * parameter square that is not degenerate.
   *
   * A run is what has to become a single edge: an edge fillet's two rails are
   * runs along `v = 0` and `v = 1` which the neighbouring face sees as a row of
   * short segments, while its two rulings are single straight edges already. A
   * corner fillet has three runs and no fourth, because its apex row collapses
   * to a point. */
  struct Run {
    int edge = -1;           // 0: u=0, 1: u=1, 2: v=0, 3: v=1
    std::vector<int> verts;  // consecutive along the boundary
    bool straight = false;   // the patch is degree 1 across this edge

    /*! What lies on the other side, which decides whether the run can be
     * collapsed into one curve at all. The same question RimRef answers for a
     * band, and the same three answers: the run is a whole neighbouring face,
     * or a consecutive stretch of one, or it is shared with another patch being
     * collapsed too - which is what a corner's rails are, since each is also a
     * rail of the strip it meets. Anything else, most often one face per
     * segment, leaves the patch faceted. */
    enum Kind { UNRESOLVED, WHOLE_LOOP, LOOP_RUN, OTHER_PATCH };
    Kind kind = UNRESOLVED;
    std::size_t loop = 0;              // WHOLE_LOOP, LOOP_RUN
    std::size_t start = 0, count = 0;  // LOOP_RUN
    std::size_t patch = 0;             // OTHER_PATCH
    bool reversed = false;             // the neighbour traverses the run backwards

    /*! Which of the region's boundary cycles this run lies on. Always 0 for a
     * Bezier patch, which is a disc; a trimmed sweep can be an annulus, and
     * then one cycle is the face's outer bound and the others are holes in
     * it. */
    std::size_t bound = 0;

    /*! For OTHER_PATCH, the run in that patch covering the same segments.
     *
     * The two have to be the same seam vertex for vertex, because they will
     * share one curve: a face on either side of an edge that is one curve for
     * one of them and two for the other leaves the shell open. NO_RUN when the
     * partner disagrees, which the caller must treat as unsubstitutable. */
    std::size_t partner = std::size_t(-1);
  };
  std::vector<Run> runs;

  bool alive = true;
  const char *dropped = nullptr;  // why it was left faceted, for the report
};

/*! The control points of the patch boundary a run lies on, ordered so the
 * curve starts at `run.verts.front()`.
 *
 * The emitter needs the curve to run the way the run does; the net's own order
 * depends on which edge of the parameter square it is. */
/*! Control points of one boundary run, and through `weights_out` the weights
 * that go with them - a rational patch's boundary is a rational curve, and a
 * caller writing the curve without them would describe a parabola where the
 * patch has a circular arc. */
std::vector<Vector3d> runControlPoints(const Patch& patch, const Patch::Run& run,
                                       const std::vector<Vector3d>& vertices,
                                       std::vector<double> *weights_out = nullptr);

/*! A run of facets that both exact passes left faceted, grouped by smoothness.
 *
 * These are the faces nothing could describe: a `polyhedron()` over a computed
 * point list, a helical thread, a warped quad. The exporter writes them as
 * planes and that is always correct, but on the reference part it is 99.8% of
 * the uncovered area, so it is worth knowing what is there and what it would
 * take to do better.
 *
 * `band` is the measurement that matters, and it is the honest way to ask "how
 * wrong could a fitted surface be". The mesh does not say where the true
 * surface is; it says only that the true surface passes through these vertices
 * and cannot stray far from these facets. For two facets meeting at a dihedral
 * theta across a chord c, a circular cross section through them has a sagitta
 * of (c/2)*tan(theta/4) - so that is how much room the tessellation leaves, and
 * a fit which stays inside it asserts nothing the mesh does not already allow.
 * A fit which leaves it is inventing geometry.
 *
 * That test is not hypothetical: a B-spline fitted through this project's own
 * thread mesh overshot by 0.378mm at the run-out where the band is 0.109mm, and
 * the visible result was a hook. Measured against the band it is rejected 3.5
 * times over; measured against the vertices it scored 1e-13 and looked perfect. */
struct SmoothRegion {
  std::vector<std::size_t> facets;
  double worst_dihedral = 0;  // radians, across the region's interior edges
  double band = 0;            // the widest corridor over any interior edge
  /*! The corridor over the *typical* interior edge.
   *
   * A region can be smooth nearly everywhere and still have a handful of edges
   * that are not, which is what a boolean leaves behind when it trims a swept
   * body against a wall. `band` is what a single fitted surface has to live
   * within, so it is the one that decides; `median_band` says whether the
   * region is broadly smooth with a few bad edges or bad throughout, and those
   * want different answers - the first can be split, the second cannot. */
  double median_band = 0;
  double area = 0;

  /*! Whether the region still carries the grid its generator laid down.
   *
   * Fitting a surface to a run of facets needs their *ordering* - which facet
   * follows which along the sweep - and none of the fitting machinery can
   * recover that from an unordered set. A mesh straight from a generator has
   * it: a swept quad grid, split into triangles the same way everywhere, gives
   * every interior vertex a valence of exactly 6. A boolean does not preserve
   * it; trimming a sweep against a wall retriangulates the seam and the valence
   * spreads out.
   *
   * So `regularity` is the fraction of interior vertices sitting at the modal
   * valence, and it is the measurement that decides whether fitting is even
   * available: 1.0 is a pristine grid, and anything low means the ordering is
   * gone and only the generator can say what the surface was. */
  std::size_t interior_vertices = 0;
  std::size_t modal_valence = 0;
  double regularity = 0;
};

/*! Group the facets neither pass claimed into smooth regions.
 *
 * `consumed` marks the loops a band or patch already took. Facets are joined
 * across an edge when they meet at less than `smooth_angle`, so a region is a
 * piece of surface a single fitted patch could plausibly cover, and a sharp
 * model edge ends it. */
/*! The cylinder a smooth region lies on, or null when it lies on none.
 *
 * This is the exporter making a declaration the model did not, which every
 * other path here refuses to do on purpose: a hexagonal prism and a six sided
 * tessellation of a cylinder are the same mesh, and only the model knows which
 * it meant. What makes it defensible is the region, not the fit. A region is
 * grown across edges meeting at less than the smoothing angle, so a genuine
 * prism - whose dihedrals are tens of degrees - never forms one. The caller
 * chooses that angle, and it is the whole of the intent judgement.
 *
 * The fit itself is not an approximation. A tessellated cylinder has its
 * vertices *on* the cylinder - only its facets are inside it - so `tol` is a
 * modelling tolerance rather than the region's band, and a region whose
 * vertices are not on one common cylinder to within it is refused.
 *
 * The axis comes from the facet normals, which on a cylinder are all
 * perpendicular to it, and the radius from a least squares circle through the
 * vertices projected onto that plane. */
std::shared_ptr<Surface> fitCylinder(const Mesh& mesh, const SmoothRegion& region, double tol);

/*! The quad grid a smooth region was swept as, or null when it has none.
 *
 * The other half of "fit where the generator's grid survives, declare where the
 * boolean took it". `SmoothRegion::regularity` says whether the ordering is
 * still there; this recovers it. The facets are paired back into quads - a pair
 * of triangles is one quad when the edge between them is the longest edge of
 * both, which is what splitting a quad by a diagonal makes it - and the quads
 * are laid out on integer coordinates by flood fill. A region that really is a
 * swept grid comes out as a rectangle of coordinates; one that is not disagrees
 * with itself, and is refused rather than approximated.
 *
 * The result is an ordinary GridSurface, so what happens to it afterwards is
 * exactly what happens to one the model declared. */
std::shared_ptr<Surface> gridFromRegion(const Mesh& mesh, const SmoothRegion& region, double tol,
                                        const char **why = nullptr);

/*! The rings a smooth region was turned on, or empty when it was not.
 *
 * A cone and a sphere have no surface type to declare in this exporter, and
 * that is deliberate rather than missing: primitives.cc expresses a frustum as
 * two rims matching declared cylinders, and a sphere as a stack of those bands
 * absorbed into a spherical zone. So a fit has to produce *rings*, and this
 * returns one CylinderSurface per ring of the tessellation - after which the
 * ordinary band recogniser makes cones out of them with no new machinery.
 *
 * The region's own boundary is the way in. A frustum's two rims and a sphere's
 * two cap circles are its boundary cycles, and a circle fitted to one gives
 * both a radius to declare and, in its normal, the axis every other ring is
 * measured along. An apex contributes nothing - there is no circle there, and a
 * radius of zero would match every other radius of zero. */
std::vector<std::shared_ptr<Surface>> fitRevolved(const Mesh& mesh, const SmoothRegion& region,
                                                  double tol, const char **why = nullptr);

std::vector<SmoothRegion> uncoveredRegions(const Mesh& mesh, const std::vector<char>& consumed,
                                           double smooth_angle);

/*! The quadric a rational Bezier patch lies on exactly, or nullptr.
 *
 * `FilletNode` draws its strips and corners as rational quadratics, which are
 * circular arcs rather than parabolas, so a constant radius fillet meeting
 * perpendicular faces really is a piece of a cylinder along each edge and an
 * octant of a sphere at each corner. Both go out as B_SPLINE_SURFACE_WITH_KNOTS
 * otherwise, which is valid and imports - but a CYLINDRICAL_SURFACE is what a
 * CAD kernel can offset, thread and pattern, and a B-spline is what it
 * tolerates.
 *
 * A candidate axis or centre is read off the control net, and then the patch is
 * *measured* against it on a grid: the boundary of the net does not determine
 * the interior, and the evaluation is what brings the weights into the test.
 * Anything that does not fit within `tol` returns nullptr and stays a spline,
 * which is the exporter's rule everywhere - exact fit or stay faceted.
 *
 * Returns a CylinderSurface or a SphereSurface. */
std::shared_ptr<Surface> quadricOfPatch(const BezierPatchSurface& bez, double tol,
                                        const char **why = nullptr);

/*! The circle one curved boundary run of a patch lies on, oriented so the run
 * sweeps counter clockwise about `normal` - the direction a STEP CIRCLE is
 * written in. False when the run is not a quadratic arc.
 *
 * A quadric patch face has to be bounded by CIRCLEs rather than by splines off
 * its own net: those are the same curve, but only one of them is a curve a CAD
 * kernel will offset or pattern along. */
bool runCircle(const Patch& patch, const Patch::Run& run, const std::vector<Vector3d>& vertices,
               Vector3d& centre, Vector3d& normal, double& radius);

/*! Find the facets which lie on each declared Bezier patch.
 *
 * `consumed` marks loops already taken by a band, which a patch may not also
 * claim. Loops a patch takes are *not* marked here - the caller decides that
 * once it knows the patch can actually be written. */
std::vector<Patch> recogniseBezierPatches(const Mesh& mesh,
                                          const std::vector<std::shared_ptr<Surface>>& surfaces,
                                          const std::vector<char>& consumed,
                                          std::vector<std::string>& report);

/*! Find the facets which lie on each declared grid, and split their boundaries.
 *
 * The same answer as recogniseBezierPatches - a sheet of facets and the runs
 * its boundary splits into - reached by a different rule, because a declared
 * grid is not a patch that covers its own parameter square. A Bezier's boundary
 * lies on the four edges of that square and splits by geometry; a grid is
 * trimmed wherever the boolean cut it, so its boundary lies nowhere in
 * particular and splits by topology instead: one run per stretch of consecutive
 * boundary segments with the same face on the far side.
 *
 * `Run::edge` and `Run::straight` are therefore meaningless here and left at
 * their defaults. As with the Bezier path, loops a patch takes are not marked
 * consumed - the caller decides that once it knows the patch can be written. */
std::vector<Patch> recogniseGridPatches(const Mesh& mesh,
                                        const std::vector<std::shared_ptr<Surface>>& surfaces,
                                        const std::vector<char>& consumed,
                                        std::vector<std::string>& report);

/*! Find the bands of facets which were modelled as a surface of revolution.
 *
 * `surfaces` are the analytic surfaces the model declared; with none of them
 * nothing is recognised, by design. `tol` is the modelling tolerance. */
Result recogniseSurfacesOfRevolution(const Mesh& mesh,
                                     const std::vector<std::shared_ptr<Surface>>& surfaces, double tol);

/*! An arbitrary unit vector perpendicular to `norm`. */
Vector3d perpendicular(const Vector3d& norm);

/*! Least squares circle centre for points known to lie on a circle about
 * `axis`, returned projected onto the plane at `level`.
 *
 * Averaging is not good enough here. The centroid of a *full* rim lies on the
 * axis, which is why the closed ring case can get away with it, but the
 * centroid of an arc sits inside the chord and taking it for the centre puts
 * the axis somewhere else entirely - on a 54 degree arc of a radius 78 wall it
 * came out at radius 266, and the wall was rejected as not fitting itself.
 *
 * Kasa's linearisation (x^2 + y^2 = 2ax + 2by + c) is exact when the points
 * really are concyclic, and every caller re-measures the residual afterwards,
 * so an inexact fit is caught rather than trusted. */
bool fitCircleCentre(const std::vector<Vector3d>& vertices, const std::vector<int>& ids,
                     const Vector3d& axis, double level, Vector3d& centre);

/*! Distance from a point to the line through `base` along `axis`. */
double distanceToAxis(const Vector3d& pt, const Vector3d& base, const Vector3d& axis);

}  // namespace AnalyticFeatures
