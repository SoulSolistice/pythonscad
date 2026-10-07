#pragma once

/*! A B-rep evaluated from the model's declarations alone - a proof of concept.
 *
 * The body build takes its topology from the mesh - which faces exist, which
 * meet, where edges and corners run - and its geometry from the declarations.
 * Where the mesh's boolean decided differently from the model's, which it does
 * wherever a clearance is below a tessellation's sagitta, exact geometry is put
 * on a topology the model does not have: lid10's fins, and its bore written
 * through its slots (doc/step-export-wip.md, item 42). Where every operand is a
 * declared solid, the model decides its topology too: a face is the part of a
 * declared surface where the solid's tree says the boundary is, an edge is
 * where two such faces meet, a corner where three do.
 *
 * This evaluates that for the case where it is a grid: every declared surface a
 * coordinate surface of one cylindrical frame - cylinders about one axis (r =
 * c), planes across it (z = c) and planes through it (theta = c). Their
 * arrangement is then boxes in (r, theta, z); each box's centre is strictly
 * inside it, so the tree says In or Out there exactly (recognition::classify),
 * and the boundary is every box face between an In box and an Out one - on the
 * record at that level, facing out of the In box - merged per record and
 * orientation into faces. Edges are radial lines, arcs and axial lines, each
 * exactly where its two faces' surfaces meet; corners are grid points. Angles
 * a quarter turn apart are added to the grid's own, so no arc exceeds one; no
 * face may lie at an added angle, which has no record.
 *
 * Refused, with the reason, rather than approximated: an operand that is not a
 * declared solid; a bound that is not such a coordinate surface; cylinders on
 * different axes; a box the tree does not decide; material on the axis or
 * reaching beyond the outermost levels; a face at an added angle; a corner two
 * faces of one surface only pinch at. The mesh is not read at all.
 */

#include <optional>
#include <string>

#include "geometry/brep/Brep.h"

class SolidTree;

namespace brep {

/*! The B-rep the declared tree bounds, or nullopt with `why` filled. */
std::optional<Body> fromModel(const SolidTree& tree, std::string& why);

}  // namespace brep
