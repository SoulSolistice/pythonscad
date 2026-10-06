/*! The shells, and the product around them - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "io/step/BrepToStep.h"
#include "io/step/LoopGeometry.h"
#include "utils/printutils.h"

using namespace step;

// Corners are placed on their faces to the corner ladder's floor, 1e-9 of their
// size (CornerLadder.h - a chosen number itself, to go with open item 39): that
// is how exactly the file states where anything is, and an approximation finer
// than the corners it runs between gains nothing a reader can see. So the
// crossing curves are held to it, and say how closely they came
// (`crossing_fit`); and it is what the file declares as its uncertainty, what a
// reader may take two points as one within. That was 1e-5 whatever the file
// held until 2026-09-28.
double StepKernel::BodyBuild::resolution() const
{
  double size = 1.0;
  for (const Vector3d& v : vertices) size = std::max(size, v.norm());
  return 1e-9 * size;
}

void StepKernel::BodyBuild::writeShells()
{
  // The file's faces are the B-rep's, written by its translator (open item 39,
  // step 2c), the one place this body's geometry becomes entities: the phases
  // build the B-rep and make none (step 2d). A CLOSED_SHELL has to be a single
  // connected shell, so each of the body's shells - its connected components -
  // is one MANIFOLD_SOLID_BREP.
  const brep::Body& body = brep_body;
  ParametricContext *context = nullptr;
  const std::vector<Face *> written = step::writeBrep(entities, body, context);
  if (written.size() != body.faces.size()) {
    // A face or an edge the B-rep carries on no form, which brep::check has
    // named as an EXPORT-ERROR. There is nothing else to write: the file
    // states the B-rep or nothing. It says so rather than write a solid with
    // faces missing.
    LOG(message_group::Export_Error,
        "STEP export: the B-rep has a face or an edge on no form the translator can write, so the "
        "file holds no solid for it (open item 39)");
  }
  std::vector<std::vector<Face *>> shell_faces;
  for (const auto& shell : body.shells) {
    if (written.size() != body.faces.size()) break;
    shell_faces.emplace_back();
    for (const std::size_t i : shell.faces) shell_faces.back().push_back(written[i]);
  }

  // units and the file's uncertainty
  auto length_unit = new SiUnit(entities, SiUnit::LENGTH);
  auto angle_unit = new SiUnit(entities, SiUnit::PLANE_ANGLE);
  auto solid_angle_unit = new SiUnit(entities, SiUnit::SOLID_ANGLE);
  auto uncertainty = new UncertaintyMeasure(entities, length_unit, resolution());
  auto geom_context =
    new GeometricContext(entities, uncertainty, length_unit, angle_unit, solid_angle_unit);

  // create the base csys
  auto base_point = new Point(entities, Vector3d(0, 0, 0));
  auto base_dir_1 = new Direction(entities, Vector3d(0, 0, 1));
  auto base_dir_2 = new Direction(entities, Vector3d(1, 0, 0));
  auto base_axis = new Axis2Placement(entities, base_dir_1, base_dir_2, base_point);

  // product structure
  const std::string body_name = name != nullptr ? name : "";
  auto app_context = new ApplicationContext(entities);
  new ApplicationProtocolDefinition(entities, app_context);
  auto prod_context = new ProductContext(entities, app_context);
  auto product = new Product(entities, body_name, prod_context);
  new ProductRelatedProductCategory(entities, product);
  auto prod_formation = new ProductDefinitionFormation(entities, product);
  auto prod_def_context = new ProductDefinitionContext(entities, app_context);
  auto product_def = new ProductDefinition(entities, prod_formation, prod_def_context);
  auto product_def_shape = new ProductDefinitionShape(entities, product_def);
  auto shape_repr = new ShapeRepresentation(entities, body_name, base_axis, geom_context);
  new ShapeDefinition_Representation(entities, product_def_shape, shape_repr);

  // build the model
  std::vector<ManifoldSolid *> solids;
  for (auto& shell : shell_faces) {
    auto closed_shell = new Shell(entities, shell);
    closed_shell->isOpen = false;
    solids.push_back(new ManifoldSolid(entities, 0, closed_shell));
  }

  if (!solids.empty()) {
    auto adv_brep_shape_pres = new AdvancesBrepRepresentation(entities, body_name, solids, geom_context);
    new ShapeRepresentationRelationShip(entities, shape_repr, adv_brep_shape_pres);
  }
}
