#pragma once

/*! What a face stands for, as its maker states it (open item 39, step 3).
 *
 * Stated where the face is made rather than inferred from it afterwards: the
 * record it is on, the facets it covers, and the classes that decided it. Every
 * maker of a face states it, whichever writer then writes the body.
 */

#include <cstddef>
#include <memory>
#include <vector>

#include "geometry/brep/Brep.h"
#include "geometry/recognition/Ledger.h"

class Surface;

namespace brep {

/*! What a face stands for: the record it is on, the facets it covers, and the
 * class that decided it. */
struct FaceSource {
  std::shared_ptr<const Surface> record;
  std::vector<std::size_t> facets;
  recognition::EvidenceClass evidence = recognition::EvidenceClass::Unstated;
  bool plane_from_corners = false;  // see brep::Face
  recognition::EvidenceClass record_evidence = recognition::EvidenceClass::Unstated;
  // The surface the maker wrote the face on, and whether it states it derived
  // from `record` - see brep::Face::carrier. The record itself where unstated.
  std::shared_ptr<const Surface> carrier;
  bool carrier_derived = false;
};

/*! What its maker stated of a face, stated on the B-rep's face: the record,
 * the facets, and the classes. */
inline void stateSource(Face& face, const FaceSource& source)
{
  face.surface = source.record;
  face.facets = source.facets;
  face.evidence = source.evidence;
  face.plane_from_corners = source.plane_from_corners;
  face.record_evidence = source.record_evidence;
  face.carrier = source.carrier != nullptr ? source.carrier : source.record;
  face.carrier_derived = source.carrier_derived;
}

}  // namespace brep
