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

/*! Find the facets which lie on each declared Bezier patch.
 *
 * `consumed` marks loops already taken by a band, which a patch may not also
 * claim. Loops a patch takes are *not* marked here - the caller decides that
 * once it knows the patch can actually be written. */
std::vector<Patch> recogniseBezierPatches(const Mesh& mesh,
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
