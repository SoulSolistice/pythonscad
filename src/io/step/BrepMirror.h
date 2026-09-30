#pragma once

/*! The B-rep a STEP file holds, read off its entities (open item 39, step 2).
 *
 * The bridge while the writer's phases still build STEP entities as they
 * decide: what the file asserts, as the neutral B-rep (geometry/brep/Brep.h),
 * so brep::check can hold it to the contract every writer shares, and so the
 * translator that replaces the phases' own entities can be proven against it.
 * It goes when the phases fill the B-rep themselves.
 */

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

#include "geometry/brep/Brep.h"
#include "geometry/recognition/Ledger.h"
#include "io/StepKernel.h"

class Surface;

namespace step {

/*! What a face the file holds stands for, registered where the face is made
 * rather than inferred from it afterwards: the record it is on, the facets it
 * covers, and the class that decided it. */
struct FaceSource {
  std::shared_ptr<const Surface> record;
  std::vector<std::size_t> facets;
  recognition::EvidenceClass evidence = recognition::EvidenceClass::Unstated;
};

/*! What an edge's maker states about its carrier, where it states anything:
 * the error of a fit (a crossing curve's, from its fitter), or that the carrier
 * is its face's own boundary (a row of a Bezier net). Every other edge states
 * nothing, and is certified against its faces or not (brep::stateClasses). */
struct EdgeSource {
  double fit = 0;
  bool stated_exact = false;
};

/*! `faces` as a B-rep: their bounds, edges, vertices, curves, pcurves and
 * surface forms as the entities hold them, each face's record, facets and
 * class from `sources`, and `shells` as indices into `faces`; each edge's
 * maker's statement from `edge_sources`. The classes of vertices and edges are
 * then brep::stateClasses' to derive, from the faces meeting there. */
brep::Body mirrorBrep(const std::vector<StepKernel::Face *>& faces,
                      const std::map<const StepKernel::Face *, FaceSource>& sources,
                      const std::vector<std::vector<std::size_t>>& shells,
                      const std::map<const StepKernel::EdgeCurve *, EdgeSource>& edge_sources = {});

}  // namespace step
