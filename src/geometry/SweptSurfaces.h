#pragma once

/*! What a sweep of a profile makes: one table, every row.
 *
 * A sweep's surfaces are fixed by two things only - what each edge of the
 * profile is (Profile2d) and how it is swept. A straight edge revolved is a
 * plane, a cylinder or a cone; an arc revolved is a torus or a sphere; a
 * straight edge extruded is a plane, an arc a cylinder, a Bezier a patch. Until
 * 2026-09-25 those rows lived inside each extruder, each grown one refusal at a
 * time (doc/step-export-wip.md, *One derivation, not five guards*), and the hull
 * of solids of revolution was about to become a third copy of the cone row.
 * Here each row is stated once, and anything that sweeps a profile - an
 * extruder, or an operation whose operands' declarations state a profile -
 * answers from it.
 *
 * Every row is a closed form of the declarations: nothing is read from the mesh
 * the sweep produces. Where a row declares nothing it says why, so an absence
 * reads as a decision and not as a gap.
 *
 * The sweeps themselves stay with their nodes - which station goes where, the
 * welds, the caps' tessellation. Only *what surfaces the sweep makes* lives here.
 */

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "geometry/Profile2d.h"
#include "geometry/linalg.h"

class Surface;

/*! A profile turned about an axis. The profile's x is the distance from the
 * axis, its y the level along it from `origin`. */
struct Revolution {
  Vector3d origin{0., 0., 0.};
  Vector3d axis{0., 0., 1.};
  /*! Where a partial turn begins and how far it goes, in degrees about +z from
   * +x - rotate_extrude's own frame, the only one that has a partial turn. A
   * full turn closes on itself and has no end faces. */
  double start = 0., angle = 360.;
};

struct SweptFaces;

/*! The surfaces `profile` makes turned about `sweep`'s axis. Where asked,
 * `every_curve` says whether every curved record an edge of the profile is a
 * chord of turned into a surface here - what a caller needs before it states
 * that every face of the sweep is a plane or on a record (SolidTree::faceted) -
 * and `faces` which of them each face is made on: the ends of a partial turn
 * are its first and its last plane. */
std::vector<std::shared_ptr<Surface>> revolve(const Profile2d& profile, const Revolution& sweep,
                                              bool *every_curve = nullptr, SweptFaces *faces = nullptr);

/*! A profile carried along a straight line, scaled towards the far end. */
struct Extrusion {
  Transform3d placement = Transform3d::Identity();        //!< the profile's own 3D transform
  Similarity placement_similarity = Similarity::by(1.0);  //!< what its makers stated of it
  Vector3d base{0., 0., 0.};                              //!< where the near cap is
  Vector3d height{0., 0., 1.};                            //!< from the near cap to the far one
  double scale_x = 1., scale_y = 1.;  //!< the far cap's scale about the profile origin
  double twist = 0.;                  //!< degrees, over the whole height
};

/*! Which record each face of a sweep is made on, each an index into the
 * surfaces the sweep returned, or -1 where it made none: for every edge its
 * maker named, the record the edge's wall is made on, by the edge's name; and
 * the plane of the end face the sweep starts from and of the one it ends on.
 *
 * What a generator names its facets by (checklist E8, open item 40), so it is
 * identity and not position. A wall is made on the record its edge sweeps - a
 * piece of a chord split for extrusion as well, whose split corners are inside
 * the curve: it is still a piece of the chord's wall. A name two edges carry
 * that sweep different records names none, rather than one of them. */
struct SweptFaces {
  std::map<EdgeId, int> walls;
  int near_end = -1, far_end = -1;
};

/*! The surfaces `profile` makes extruded by `sweep`; `every_curve` as for
 * revolve(). Where asked, `faces` says which of them each face is made on. */
std::vector<std::shared_ptr<Surface>> extrude(const Profile2d& profile, const Extrusion& sweep,
                                              bool *every_curve = nullptr, SweptFaces *faces = nullptr);

/*! The surfaces a roof over `profile` makes. Both roof methods raise a point by
 * its distance to the nearest feature of the profile - the straight skeleton's
 * time, the Voronoi diagram's distance - so each face is what that distance is
 * to one feature: over a straight edge, the plane through it rising at slope 1;
 * and, for the Voronoi roof, over a reflex corner - where the nearest feature is
 * the point itself - the right cone with its apex there, axis up, slope 1. The
 * straight skeleton meets a reflex corner with planes instead. And the floor.
 *
 * The profile's loops must run with the material on their left, as a sanitized
 * polygon's do. A chord of a curve is left alone: over a circle the roof the
 * model means is a cone, and a skeleton of chords is not on it.
 *
 * Where asked, `faces` says which of them each face of the roof is made on - the
 * floor, the plane over each named edge, the cone over each named corner - which
 * is how the roof names its faces (checklist E8). */
struct RoofFaces;
std::vector<std::shared_ptr<Surface>> roofSurfaces(const Profile2d& profile, bool voronoi,
                                                   RoofFaces *faces = nullptr);

/*! What a face of a roof stands over, as its maker knows it: the floor; a
 * profile edge, by its name; or - on the Voronoi roof, which is a cone there - a
 * corner, by the names of the edge into it and the edge out of it. A name of 0
 * is one nobody gave. */
struct RoofSite {
  enum class Kind { Floor, Edge, Corner } kind = Kind::Floor;
  EdgeId edge = 0, next = 0;
};

/*! Which of roofSurfaces' records each site's faces are made on, as indices into
 * what it returned; -1 for none. */
struct RoofFaces {
  int floor = -1;
  std::map<EdgeId, int> walls;
  std::map<std::pair<EdgeId, EdgeId>, int> corners;
  /*! The record a face over `site` is made on, or -1. */
  [[nodiscard]] int of(const RoofSite& site) const;
};
