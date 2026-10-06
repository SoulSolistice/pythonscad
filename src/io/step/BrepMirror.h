#pragma once

/*! The B-rep a STEP file holds, read off its entities (open item 39, step 2).
 *
 * The bridge while the writer's phases still build STEP entities as they
 * decide: what the file asserts, as the neutral B-rep (geometry/brep/Brep.h),
 * so brep::check can hold it to the contract every writer shares, and so the
 * translator that replaces the phases' own entities can be proven against it.
 * It shrinks as the phases fill the B-rep themselves, and goes when nothing is
 * left to read.
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
  bool plane_from_corners = false;  // see brep::Face
  recognition::EvidenceClass record_evidence = recognition::EvidenceClass::Unstated;
  // The surface the maker wrote the face on, and whether it states it derived
  // from `record` - see brep::Face::carrier. The record itself where unstated.
  std::shared_ptr<const Surface> carrier;
  bool carrier_derived = false;
};

/*! What an edge's maker states about its carrier, where it states anything:
 * the error of a fit (a crossing curve's, from its fitter), or that the carrier
 * is its face's own boundary (a row of a Bezier net). Every other edge states
 * nothing, and is certified against its faces or not (brep::stateClasses). */
struct EdgeSource {
  double fit = 0;
  bool stated_exact = false;
};

/*! What its maker registered of a face, stated on the B-rep's face: the record,
 * the facets, and the classes. */
void stateSource(brep::Face& face, const FaceSource& source);

/*! The B-rep, built as the phases decide it (open item 39, step 2d): a phase
 * that builds B-rep entities itself names the entities it still takes from a
 * phase before it through this, and each is read off its STEP entity once - its
 * vertices, curve, pcurves and surface form as the entity holds them, the edge's
 * maker's statement from `edge_sources` - and is the same B-rep entity every
 * time it is named again. As phases convert, less is read, until nothing is.
 *
 * An index is identity, not order: what is written follows the faces, their
 * loops and their coedges, never the order entities were first named in. */
class BrepReader
{
public:
  BrepReader(brep::Body& body, const std::map<const StepKernel::EdgeCurve *, EdgeSource>& edge_sources)
    : body(body), edge_sources(edge_sources)
  {
  }
  std::size_t vertex(const StepKernel::Vertex *v);
  /*! `v` is the body's vertex `index` - one the B-rep holds already, which an
   * entity made later at its point stands for. */
  void bind(const StepKernel::Vertex *v, std::size_t index) { vertex_index.emplace(v, index); }
  std::size_t edge(const StepKernel::EdgeCurve *e);
  /*! A face a phase made as an entity, appended to the body with what its maker
   * registered, `source` - null where it registered nothing, which leaves the
   * face's classes unstated for brep::check to name. */
  std::size_t face(const StepKernel::Face *face, const FaceSource *source);

private:
  brep::Body& body;
  const std::map<const StepKernel::EdgeCurve *, EdgeSource>& edge_sources;
  // Entities by identity, for lookup only.
  std::map<const StepKernel::Vertex *, std::size_t> vertex_index;
  std::map<const StepKernel::EdgeCurve *, std::size_t> edge_index;
  // The entity each edge was read from, by the edge's index; null for one a
  // phase built itself.
  std::vector<const StepKernel::EdgeCurve *> edge_entity;
};

/*! `faces` as a B-rep, read whole: each face through a BrepReader with its
 * record, facets and class from `sources`, and `shells` as indices into
 * `faces`. The classes of vertices and edges are then brep::stateClasses' to
 * derive, from the faces meeting there. */
brep::Body mirrorBrep(const std::vector<StepKernel::Face *>& faces,
                      const std::map<const StepKernel::Face *, FaceSource>& sources,
                      const std::vector<std::vector<std::size_t>>& shells,
                      const std::map<const StepKernel::EdgeCurve *, EdgeSource>& edge_sources = {});

}  // namespace step
