#pragma once

/*! Whether the model's solid has a face where the mesh has a facet made on a
 * declared surface.
 *
 * Identity (Provenance::face_surface) says which surface a facet was made on;
 * whether the model's solid has a face of that surface there is a separate
 * question, and the mesh boolean - run on chords - answers it wrongly wherever
 * a feature is smaller than a sagitta. A cutter 0.1 inside a bore whose chords
 * sag 0.107 leaves slivers in the mesh whose every face is made on a declared
 * surface and none of which the model has. This asks the model: the facet's
 * corners and centroid, each taken onto the surface it was made on, classified
 * against the boolean the model built (recognition/Classify.h).
 *
 * A facet is a **phantom** only where every one of those points is decided Out
 * or In - off the solid's boundary - and none is On or Unknown. A facet whose
 * surface the solid's boundary crosses has a point on two surfaces at once,
 * which the classification leaves Unknown; it is undecided, and left to what
 * the mesh says. So nothing is called a phantom that the model does not rule
 * out at every point the facet has.
 *
 * Pure: a function of the graph, the declarations, identity and the model's
 * solid, reading no claim (checklist A1). doc/step-recognition-design.md,
 * "Existence is a D0 question too"; checklist B10.
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "geometry/recognition/FacetGraph.h"

class Surface;
class SolidTree;

namespace recognition {

struct Provenance;

enum class Exists : unsigned char {
  NotAsked,   // made on no declared surface, or identity not carried
  Face,       // the model's solid has a face of that surface at the facet's centroid
  Phantom,    // at every point of the facet, the model's solid has none
  Undecided,  // a point on two surfaces, or an operand that states no solid
};

/*! Per facet, what the model says of the face it was made on. Empty where the
 * model's boolean was not carried. See the file comment. */
[[nodiscard]] std::vector<Exists> existence(const FacetGraph& graph,
                                            const std::vector<std::shared_ptr<Surface>>& surfaces,
                                            const Provenance& provenance, const SolidTree *tree);

}  // namespace recognition
