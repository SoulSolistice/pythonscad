#pragma once

/*! The neutral B-rep, written as STEP entities (open item 39, step 2c).
 *
 * The one place a boundary representation becomes ISO 10303 entities: every
 * vertex a VERTEX_POINT, every edge an EDGE_CURVE on its curve - wrapped as a
 * SURFACE_CURVE where its uses carry pcurves - every loop an EDGE_LOOP in a
 * FACE_BOUND, every face an ADVANCED_FACE on the surface its form states. It
 * decides nothing about geometry: what the B-rep says is what is written.
 *
 * The writer's conventions it keeps, because a file is compared entity for
 * entity against the one the phases wrote while they still write their own:
 * a LINE starts at its edge's start and its VECTOR's magnitude is the edge's
 * length (an edge of no length is given a unit x direction - open item 39,
 * R17, recorded to be fixed rather than reproduced for ever); one parametric
 * context per file, shared by every pcurve; every other entity made fresh
 * where it is used.
 */

#include <vector>

#include "geometry/brep/Brep.h"
#include "io/StepKernel.h"

namespace step {

/*! `body`'s faces as STEP entities appended to `entities`, in the body's face
 * order. `context` is the file's one parametric context, made on the first
 * pcurve that needs it and reused after. Empty, with nothing made, where the
 * body has a face carried by no form or an edge on no curve: it writes what the
 * B-rep states, and that states nothing to write there. */
std::vector<StepKernel::Face *> writeBrep(std::vector<StepKernel::Entity *>& entities,
                                          const brep::Body& body,
                                          StepKernel::ParametricContext *& context);

}  // namespace step
