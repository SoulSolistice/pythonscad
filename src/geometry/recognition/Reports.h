#pragma once

/*! Reports: queries over the ledger - see Reports.cc. Read-only, except that
 * the declaration census completes the per-declaration and per-facet tallies
 * the writer began (it marks what the planes it wrote stand on).
 */

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/FacetGraph.h"

class Surface;

namespace recognition {

/*! What every report reads. */
struct ReportInputs {
  const AnalyticFeatures::Result& features;               // consumed, consumed_by, the ledger
  const std::vector<std::shared_ptr<Surface>>& surfaces;  // the model's declarations
  Mesh mesh;
  const std::vector<int32_t>& face_origin;
  const std::map<int32_t, std::vector<std::size_t>>& owned;
};

/*! OPENSCAD_STEP_LEDGER=<file>: every claim as a table row, and the surfaces
 * beside it. Changes nothing unless the variable is set. */
void dumpLedger(const ReportInputs& in);

/*! The shadow check, contention, provenance's verdict on it, and split
 * surfaces - what the arbitration did and what it could not decide. */
void reportArbitration(const ReportInputs& in);

/*! The declaration census: is everything that could be exact written as exact,
 * per declaration and per facet, and why not where it is not. `decl_written`
 * and `facet_class` arrive holding what the writer's planes stand on. */
void reportDeclarationCensus(const ReportInputs& in, bool analytic, std::vector<char>& decl_written,
                             std::vector<unsigned char>& facet_class);

}  // namespace recognition
