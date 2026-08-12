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
  /*! Set when a chain of bands was merged into one spherical zone.
   *
   * A sphere is not a grid to be grown, it is a *stack of bands*: every ring of
   * its tessellation is already a frustum whose two rims are circles, and the
   * whole zone is the maximal run of them joined at shared rims whose vertices
   * all lie on one declared sphere. The merged band keeps the outer rims of the
   * run, so the rim rules that were resolved for the ends still hold and the
   * caps at either end are untouched. */
  std::shared_ptr<const Surface> sphere;
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
