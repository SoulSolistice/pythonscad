#pragma once

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/node.h"
#include "geometry/Surface.h"

/*! Attaches analytic surface declarations to whatever it wraps.
 *
 * A declaration says "this was meant to be a cylinder". Nothing else in the
 * pipeline can say it for a mesh a user built by hand: `cylinder()` and
 * `sphere()` declare what they drew, but a swept thread or a ramp exists only
 * as a `polyhedron()` list comprehension, and no measurement of the result
 * distinguishes a cylinder from a prism with the same corners. This node is the
 * way for the model to say it.
 *
 * It is a node rather than a call that mutates a geometry because geometry does
 * not exist yet when the model is written, and because being a node is what
 * makes the coordinates come out right: the records are held in world
 * coordinates, and a transform above this node moves the geometry and its
 * declarations together, for free.
 *
 * A wrong declaration is bounded rather than dangerous. The exporter re-checks
 * every record against the mesh and against the topology before acting on it,
 * so a record that does not fit costs a candidate and nothing else. The one
 * case it cannot catch is a declaration which fits some *other* feature of the
 * model exactly - the same reason `minkowski()` drops records rather than
 * scaling them. */
class DeclareSurfaceNode : public AbstractNode
{
public:
  VISITABLE();
  DeclareSurfaceNode(std::shared_ptr<const ModuleInstantiation> mi) : AbstractNode(std::move(mi)) {}
  [[nodiscard]] std::string name() const override { return "declare_surface"; }
  [[nodiscard]] std::string toString() const override
  {
    std::ostringstream stream;
    stream << "declare_surface(n = " << surfaces.size() << ")";
    return stream.str();
  }

  std::vector<std::shared_ptr<Surface>> surfaces;
  /*! `faces = "label"` or `["a", "b"]`: the labels (`polyhedron(labels =
   * ...)`) of the faces below here that the model says are on the record. Each
   * such face is named for it wherever it has gone - through booleans,
   * transforms, copies - which is the model's word per face, what a D0 facet
   * needs (checklist E8). Without it a declaration states the record and names
   * no face: over a generator that names nothing its surface has no D0 ground,
   * and over one that named its faces it never overrules that (E5). */
  std::vector<std::string> labels;
  bool scoped = false;
};
