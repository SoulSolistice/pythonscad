/*
 *  PythonSCAD - analytic surface declarations
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "core/DeclareSurfaceNode.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/Builtins.h"
#include "core/Children.h"
#include "core/ModuleInstantiation.h"
#include "core/Parameters.h"
#include "core/module.h"
#include "geometry/Surface.h"
#include "utils/printutils.h"

namespace {

/*! A radius given either way round, as every primitive here accepts it. */
bool declaredRadius(const Parameters& parameters, const char *rname, const char *dname,
                    const std::shared_ptr<const ModuleInstantiation>& inst, double& out)
{
  const bool has_r = parameters[rname].type() == Value::Type::NUMBER;
  const bool has_d = dname != nullptr && parameters[dname].type() == Value::Type::NUMBER;
  if (has_r && has_d) {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
        "%1$s: give %2$s or %3$s, not both", inst->name(), rname, dname);
    return false;
  }
  if (!has_r && !has_d) {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(), "%1$s needs a %2$s",
        inst->name(), rname);
    return false;
  }
  out = has_d ? parameters[dname].toDouble() / 2 : parameters[rname].toDouble();
  if (!std::isfinite(out) || out <= 0) {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
        "%1$s needs a positive radius", inst->name());
    return false;
  }
  return true;
}

/*! A vector parameter which may be left out, and an axis which must point
 * somewhere to be one. */
bool declaredVector(const Parameters& parameters, const char *name,
                    const std::shared_ptr<const ModuleInstantiation>& inst, bool is_axis, Vector3d& out)
{
  if (parameters[name].type() != Value::Type::UNDEFINED) {
    if (!parameters[name].getVec3(out[0], out[1], out[2], 0.0) || !std::isfinite(out[0]) ||
        !std::isfinite(out[1]) || !std::isfinite(out[2])) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "%1$s: unable to convert %2$s to a vector of three numbers", inst->name(), name);
      return false;
    }
  }
  if (is_axis && !(out.norm() > 0)) {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
        "%1$s: axis has no direction", inst->name());
    return false;
  }
  if (is_axis) out.normalize();
  return true;
}

/*! A declaration which could not be read is dropped, and the children are kept.
 *
 * Not an error: a record is only ever a hint, so a model missing one exports
 * the same body with faceted walls rather than failing. The warning above says
 * what was ignored. */
std::shared_ptr<AbstractNode> withSurface(const std::shared_ptr<const ModuleInstantiation>& inst,
                                          const Children& children, std::shared_ptr<Surface> surface)
{
  auto node = std::make_shared<DeclareSurfaceNode>(inst);
  if (surface) node->surfaces.push_back(std::move(surface));
  return children.instantiate(node);
}

std::shared_ptr<AbstractNode> builtin_declare_cylinder(
  const std::shared_ptr<const ModuleInstantiation>& inst, Arguments arguments, const Children& children)
{
  Parameters parameters =
    Parameters::parse(std::move(arguments), inst->location(), {"r", "d", "center", "axis"});
  Vector3d centre(0, 0, 0), axis(0, 0, 1);
  double r = 0;
  std::shared_ptr<Surface> surface;
  if (declaredRadius(parameters, "r", "d", inst, r) &&
      declaredVector(parameters, "center", inst, false, centre) &&
      declaredVector(parameters, "axis", inst, true, axis)) {
    surface = std::make_shared<CylinderSurface>(centre, axis, r);
  }
  return withSurface(inst, children, std::move(surface));
}

std::shared_ptr<AbstractNode> builtin_declare_sphere(
  const std::shared_ptr<const ModuleInstantiation>& inst, Arguments arguments, const Children& children)
{
  Parameters parameters =
    Parameters::parse(std::move(arguments), inst->location(), {"r", "d", "center", "axis"});
  Vector3d centre(0, 0, 0), axis(0, 0, 1);
  double r = 0;
  std::shared_ptr<Surface> surface;
  if (declaredRadius(parameters, "r", "d", inst, r) &&
      declaredVector(parameters, "center", inst, false, centre) &&
      declaredVector(parameters, "axis", inst, true, axis)) {
    surface = std::make_shared<SphereSurface>(centre, axis, r);
  }
  return withSurface(inst, children, std::move(surface));
}

/*! `declare_cone(r1, r2, h)`, named the way `cylinder()` names the same shape.
 *
 * Two radii and a height rather than an apex and a half angle, because that is
 * how the model already thinks of it, and because a chamfer's apex is often
 * nowhere near the part. `center` is the centre of the `r1` rim and `axis`
 * points from there towards `r2`. */
std::shared_ptr<AbstractNode> builtin_declare_cone(
  const std::shared_ptr<const ModuleInstantiation>& inst, Arguments arguments, const Children& children)
{
  Parameters parameters = Parameters::parse(std::move(arguments), inst->location(),
                                            {"r1", "r2", "h", "d1", "d2", "center", "axis"});
  Vector3d centre(0, 0, 0), axis(0, 0, 1);
  double r1 = 0, r2 = 0;
  std::shared_ptr<Surface> surface;
  if (declaredRadius(parameters, "r1", "d1", inst, r1) &&
      declaredRadius(parameters, "r2", "d2", inst, r2) &&
      declaredVector(parameters, "center", inst, false, centre) &&
      declaredVector(parameters, "axis", inst, true, axis)) {
    const double h = parameters["h"].type() == Value::Type::NUMBER ? parameters["h"].toDouble() : 0.0;
    if (!std::isfinite(h) || h <= 0) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(), "%1$s needs a positive h",
          inst->name());
    } else if (fabs(r2 - r1) < 1e-12) {
      // Not a cone at all. Saying so is better than recording a surface whose
      // apex is at infinity; declare_cylinder is the one that was wanted.
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "%1$s: r1 and r2 are equal, which is a cylinder - use declare_cylinder", inst->name());
    } else {
      surface = std::make_shared<ConeSurface>(centre, axis, r1, (r2 - r1) / h);
    }
  }
  return withSurface(inst, children, std::move(surface));
}

std::shared_ptr<AbstractNode> builtin_declare_torus(
  const std::shared_ptr<const ModuleInstantiation>& inst, Arguments arguments, const Children& children)
{
  Parameters parameters =
    Parameters::parse(std::move(arguments), inst->location(), {"r_major", "r_minor", "center", "axis"});
  Vector3d centre(0, 0, 0), axis(0, 0, 1);
  double r_major = 0, r_minor = 0;
  std::shared_ptr<Surface> surface;
  if (declaredRadius(parameters, "r_major", nullptr, inst, r_major) &&
      declaredRadius(parameters, "r_minor", nullptr, inst, r_minor) &&
      declaredVector(parameters, "center", inst, false, centre) &&
      declaredVector(parameters, "axis", inst, true, axis)) {
    surface = std::make_shared<TorusSurface>(centre, axis, r_major, r_minor);
  }
  return withSurface(inst, children, std::move(surface));
}

std::shared_ptr<AbstractNode> builtin_declare_grid(
  const std::shared_ptr<const ModuleInstantiation>& inst, Arguments arguments, const Children& children)
{
  Parameters parameters =
    Parameters::parse(std::move(arguments), inst->location(), {"points", "closed"});
  std::shared_ptr<Surface> surface;

  // Only the extraction is OpenSCAD's business; what makes a grid a grid is
  // GridSurface::fromRows, shared with the Python channel so the two cannot
  // drift apart about which declarations are legal.
  if (parameters["points"].type() != Value::Type::VECTOR) {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
        "declare_grid: points must be a vector of rows, got %1$s",
        parameters["points"].toEchoStringNoThrow());
  } else {
    std::vector<std::vector<Vector3d>> grid;
    bool ok = true;
    std::size_t i = 0;
    for (const Value& rowValue : parameters["points"].toVector()) {
      if (rowValue.type() != Value::Type::VECTOR) {
        LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
            "declare_grid: row %1$d is not a vector of points", int(i));
        ok = false;
        break;
      }
      std::vector<Vector3d> row;
      std::size_t j = 0;
      for (const Value& pointValue : rowValue.toVector()) {
        Vector3d p(0, 0, 0);
        if (!pointValue.getVec3(p[0], p[1], p[2], 0.0) || !std::isfinite(p[0]) || !std::isfinite(p[1]) ||
            !std::isfinite(p[2])) {
          LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
              "declare_grid: point [%1$d][%2$d] must be three finite numbers", int(i), int(j));
          ok = false;
          break;
        }
        row.push_back(p);
        j++;
      }
      if (!ok) break;
      grid.push_back(std::move(row));
      i++;
    }
    if (ok) {
      const bool closed =
        parameters["closed"].type() == Value::Type::BOOL && parameters["closed"].toBool();
      std::string why;
      surface = GridSurface::fromRows(grid, closed, why);
      if (surface == nullptr) {
        LOG(message_group::Warning, inst->location(), parameters.documentRoot(), "%1$s", why);
      }
    }
  }
  return withSurface(inst, children, std::move(surface));
}

}  // namespace

void register_builtin_declare_surface()
{
  Builtins::init("declare_cylinder", new BuiltinModule(builtin_declare_cylinder),
                 {
                   "declare_cylinder(r = 5) { ... }",
                   "declare_cylinder(d = 10, center = [x, y, z], axis = [x, y, z]) { ... }",
                 });
  Builtins::init("declare_sphere", new BuiltinModule(builtin_declare_sphere),
                 {
                   "declare_sphere(r = 5) { ... }",
                   "declare_sphere(d = 10, center = [x, y, z]) { ... }",
                 });
  Builtins::init("declare_cone", new BuiltinModule(builtin_declare_cone),
                 {
                   "declare_cone(r1 = 10, r2 = 6, h = 4) { ... }",
                   "declare_cone(d1 = 20, d2 = 12, h = 4, center = [x, y, z], axis = [x, y, z]) { ... }",
                 });
  Builtins::init("declare_torus", new BuiltinModule(builtin_declare_torus),
                 {
                   "declare_torus(r_major = 10, r_minor = 3) { ... }",
                   "declare_torus(r_major = 10, r_minor = 3, center = [x, y, z], axis = [x, y, z]) { "
                   "... }",
                 });
  // Unlike its siblings this one does not name an exact surface. It hands over
  // the order the points were swept in, and the exporter fits a surface through
  // them - so a declared grid is written only with step-approximate-surfaces as
  // well as step-analytic-surfaces, and only where the fit stays inside the band
  // the model's own tessellation already leaves open.
  Builtins::init("declare_grid", new BuiltinModule(builtin_declare_grid),
                 {
                   "declare_grid(points = [[p, p, ...], [p, p, ...], ...]) { ... }",
                   "declare_grid(points = [[...], ...], closed = true) { ... }",
                 });
}
