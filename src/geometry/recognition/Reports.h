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
#include "geometry/recognition/Existence.h"
#include "geometry/recognition/FacetGraph.h"
#include "geometry/recognition/Membership.h"
#include "geometry/recognition/Representation.h"

class Surface;

namespace recognition {

/*! What every report reads. */
struct ReportInputs {
  const AnalyticFeatures::Result& features;               // consumed, consumed_by, the ledger
  const std::vector<std::shared_ptr<Surface>>& surfaces;  // the model's declarations
  Mesh mesh;
  const std::vector<int32_t>& face_origin;
  const std::map<int32_t, std::vector<std::size_t>>& owned;
  const std::vector<Region> *regions = nullptr;                        // the D0 regions, where computed
  const std::vector<RegionRepresentation> *representations = nullptr;  // parallel to regions
  const std::vector<Exists> *existence = nullptr;  // per facet, where the model's boolean was carried
  const std::vector<const SolidTree *> *existence_by = nullptr;  // the leaf behind each verdict
  const std::vector<Open> *existence_open = nullptr;             // why an undecided facet is so
  const std::vector<int32_t> *face_surface = nullptr;  // identity: the surface each was made on
  const std::vector<char> *face_derived = nullptr;     // whether that name is derived: D1, not D0
};

/*! A leaf's name for the ledger: what its generator called it, or its kind. */
std::string leafName(const SolidTree *leaf);
/*! Why an undecided facet is so, as the ledger says it. */
const char *openName(Open open);

/*! OPENSCAD_STEP_LEDGER=<file>: every claim as a table row, and the surfaces
 * beside it. Changes nothing unless the variable is set. */
void dumpLedger(const ReportInputs& in);

/*! The shadow check, contention, provenance's verdict on it, and split
 * surfaces - what the arbitration did and what it could not decide. */
void reportArbitration(const ReportInputs& in);

/*! The D0 regions against what holds them: is each region - decided by
 * declarations alone, whatever order anything ran in - written whole, or split
 * on a boundary the model does not have? See Membership.h. */
void reportRegions(const ReportInputs& in);

/*! Whether the model has the faces the mesh's facets were made on: how many
 * facets made on a declared surface are on a face of the model's solid, how
 * many the model rules out at every point - phantoms, an EXPORT-ERROR - and how
 * many it leaves undecided. See Existence.h; checklist B10. */
void reportExistence(const ReportInputs& in);

/*! How each D0 region would be written, against what holds it today - the
 * before-measurement for granting regions first; and the facets that are D0 on
 * two surfaces at once, which neither provenance nor membership can decide. */
void reportRepresentations(const ReportInputs& in);

/*! What wrote a facet, as the declaration census counts it (`facet_class`). */
enum FacetClass : unsigned char {
  kTessellated = 0,      // a plane of its own corners, a fan, or nothing at all
  kDeclaredPlane = 1,    // a planar face on the declared plane its maker named it for
  kDeclaredSurface = 2,  // held by a claim for a declared curved surface
  kFitted = 3,           // held by a claim for a surface the approximation fitted
  kUnnamed = 4,          // held by a claim that names no surface: malformed
};

/*! The class of a facet an honoured claim holds, read off the surface the claim
 * names: one of the `declared_count` the model declared, a surface fitted past
 * them, or `std::size_t(-1)`, none at all - which is a malformed claim and not
 * a fit (open item 39, R28). */
FacetClass heldClass(std::size_t surface_index, std::size_t declared_count);

/*! The declaration census: is everything that could be exact written as exact,
 * per declaration and per facet, and why not where it is not. `decl_written`
 * and `facet_class` arrive holding what the writer's planes stand on. Ends by
 * asking every facet its maker named for a declared surface whether it was
 * written on it - an EXPORT-ERROR where not (checklist B6, B7, E8);
 * `approximate` says whether this tier writes a declared sweep at all, and
 * `written_on` is the declared surface each facet's written face stands on, or
 * -1 - what the writers wrote, not what was claimed. */
void reportDeclarationCensus(const ReportInputs& in, bool analytic, bool approximate,
                             std::vector<char>& decl_written, std::vector<unsigned char>& facet_class,
                             const std::vector<int32_t>& written_on);

}  // namespace recognition
