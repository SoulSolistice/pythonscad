#pragma once

/*! A B-rep written from the model's boundary - a proof of concept.
 *
 * The body build takes its topology from the mesh - which faces exist, which
 * meet, where edges and corners run - and its geometry from the declarations.
 * Where the mesh's boolean decided differently from the model's, which it does
 * wherever a clearance is below a tessellation's sagitta, exact geometry is put
 * on a topology the model does not have: lid10's fins, and its bore written
 * through its slots (doc/step-export-wip.md, item 42). Where every operand is a
 * declared solid the model decides its topology too, and model::evaluate
 * states it (model/Boundary.h); this is its writer's half - each conic to its
 * principal axes, each face to the form its record is carried in - and nothing
 * more. The mesh is not read.
 *
 * Refused where the model's boundary is, with its reason, and where the body
 * it gives is not well formed (check()).
 */

#include <optional>
#include <string>

#include "geometry/brep/Brep.h"

class SolidTree;

namespace brep {

/*! The B-rep the declared tree bounds, or nullopt with `why` filled; and
 * the uncertainty the file is to declare, which its fitted pcurves hold to. */
std::optional<Body> fromModel(const SolidTree& tree, std::string& why, double& uncertainty);

}  // namespace brep
