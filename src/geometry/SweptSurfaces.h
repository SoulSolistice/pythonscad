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

#include <memory>
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

/*! The surfaces `profile` makes turned about `sweep`'s axis. */
std::vector<std::shared_ptr<Surface>> revolve(const Profile2d& profile, const Revolution& sweep);

/*! A profile carried along a straight line, scaled towards the far end. */
struct Extrusion {
  Transform3d placement = Transform3d::Identity();  //!< the profile's own 3D transform
  Vector3d base{0., 0., 0.};                        //!< where the near cap is
  Vector3d height{0., 0., 1.};                      //!< from the near cap to the far one
  double scale_x = 1., scale_y = 1.;                //!< the far cap's scale about the profile origin
  double twist = 0.;                                //!< degrees, over the whole height
};

/*! The surfaces `profile` makes extruded by `sweep`. */
std::vector<std::shared_ptr<Surface>> extrude(const Profile2d& profile, const Extrusion& sweep);
