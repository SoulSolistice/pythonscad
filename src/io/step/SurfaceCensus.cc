/*! The census line: what the model declared - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <cstdio>
#include <string>
#include "utils/printutils.h"

void StepKernel::BodyBuild::reportDeclaredSurfaces()
{
  // `curves` and `surfaces` carry the analytic geometry the model was built
  // from: a ring of N quads is exactly the mesh of an N sided prism, so the
  // facets alone never say which was meant.
  //
  // `surfaces` is what the recogniser matches against. `curves` is deliberately
  // ignored, and it costs nothing today: the only Curve subclass is ArcCurve,
  // the only thing which produces one is import_step.cc, and every arc the
  // exporter writes is a rim it derived from the mesh itself once the band was
  // accepted. A declared arc would only add something for a circular edge whose
  // neighbouring wall is *not* recognised - an imported mesh being written back
  // out - which is a feature nobody has asked for yet.
  (void)curves;
  if (!surfaces.empty()) {
    int cylinders = 0, spheres = 0, tori = 0, patches = 0, grids = 0, cones = 0, planes = 0,
        elliptic = 0;
    for (const auto& surface : surfaces) {
      if (dynamic_cast<const PlaneSurface *>(surface.get()) != nullptr) planes++;
      else if (dynamic_cast<const CylinderSurface *>(surface.get()) != nullptr) cylinders++;
      else if (dynamic_cast<const SphereSurface *>(surface.get()) != nullptr) spheres++;
      else if (dynamic_cast<const TorusSurface *>(surface.get()) != nullptr) tori++;
      else if (dynamic_cast<const BezierPatchSurface *>(surface.get()) != nullptr) patches++;
      else if (dynamic_cast<const GridSurface *>(surface.get()) != nullptr) grids++;
      else if (dynamic_cast<const ConeSurface *>(surface.get()) != nullptr) cones++;
      else if (dynamic_cast<const EllipticCylinderSurface *>(surface.get()) != nullptr) elliptic++;
    }
    // Swept grids are named only when there are some. Every other kind is listed
    // unconditionally because a zero there is informative - a model that meant
    // to declare a cylinder and did not is what the line exists to expose - but
    // a grid is declared by hand and by name, so its absence says nothing, and
    // mentioning it always would churn every fixture quoting this line.
    std::string extra;
    if (grids > 0) extra = ", " + std::to_string(grids) + " swept grid";
    // A cone is named by hand for the same reason a grid is, so it is listed on
    // the same terms: only when there is one. Listing it always would say
    // nothing and would rewrite the EXPECT line of every fixture here.
    if (cones > 0) extra += ", " + std::to_string(cones) + " conical";
    // And an elliptic cylinder, on the same terms.
    if (elliptic > 0) extra += ", " + std::to_string(elliptic) + " elliptic";
    char pn[64] = "";
    if (planes > 0) {
      snprintf(pn, sizeof(pn), ", and %d declared plane%s", planes, planes == 1 ? "" : "s");
    }
    const std::string plane_note(pn);
    LOG(
      // Planes are counted apart from the curved surfaces rather than added in.
      // This census is what the recognisers have to match facets against, and a
      // declared plane is not that - a merged planar face is already exact, so
      // nothing is recognised *onto* a plane. It is declared so that a face of
      // the model can be told from a chord of a tessellation, which happens
      // after the merge and reads this list directly.
      "STEP export: %1$d analytic surface%2$s available (%3$d cylindrical, %4$d spherical, "
      "%5$d toroidal, %6$d Bezier%7$s)%8$s",
      int(surfaces.size()) - planes, surfaces.size() - planes == 1 ? "" : "s", cylinders, spheres, tori,
      patches, extra, plane_note.c_str());
  }
}
