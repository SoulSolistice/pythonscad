/*! The recognition pipeline: every stage recognition runs, in its declared
 * order, from the mesh and the declarations to the regions the writer writes.
 *
 * Moved out of StepKernel::build_tri_body, where it was a thousand lines of
 * one function interleaved with the writer - see doc/step-recognition-design.md,
 * migration step 5. What stays with the writer is only what needs its boundary
 * machinery, and that is asked through Writability.
 *
 * The stages, and what each reads:
 *
 *   band      graph, declarations                      -> bands, consumed
 *   fit       graph, what the band stage consumed      -> fitted surfaces, band re-run
 *   Bezier    pure proposals; what is held             -> patches
 *   grid      pure proposals; what is held             -> sweep faces
 *   quadric   pure proposals; what is held; Writability -> trimmed quadric faces
 *
 * Every proposal is pure (proposeBezierPatches, proposeGrids, proposeQuadrics).
 * Who is granted what is today's policy - pass order, then surface order -
 * reproduced exactly while the source of truth is moved (migration step 3).
 */
#include "geometry/recognition/Pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "geometry/Surface.h"
#include "geometry/recognition/Arbiter.h"
#include "utils/printutils.h"

namespace recognition {

namespace {

/*! What every stage shares: the inputs, the result being filled, and what is
 * derived once from the inputs - the graph, the smoothing angle, the surface
 * list the passes walk, provenance. Stages read it; only `effective` grows (the
 * fit stage appends), and only the arbiter decides who holds a facet. */
struct Context {
  const Inputs& in;
  Writability& writability;
  Recognition& out;
  AnalyticFeatures::Mesh mesh;
  FacetGraph graph;
  double smooth_angle;
  std::vector<std::shared_ptr<Surface>> effective;
  AnalyticFeatures::Provenance provenance;
  Arbiter arbiter;

  Context(const Inputs& inputs, Writability& w, Recognition& result)
    : in(inputs),
      writability(w),
      out(result),
      mesh(inputs.mesh),
      graph(inputs.mesh),
      smooth_angle(smoothAngle()),
      effective(*inputs.surfaces),
      arbiter(result.features, inputs.surfaces->size())
  {
    provenance.face_origin = inputs.face_origin;
    provenance.owned = inputs.owned;
    provenance.declared = inputs.surfaces->size();
  }

  /*! A patch names its surface by pointer; the ledger and Provenance both work
   * in indices into `effective`, so a claim needs the translation. */
  [[nodiscard]] std::size_t indexOf(const std::shared_ptr<const Surface>& s) const
  {
    if (s == nullptr) return std::size_t(-1);
    for (std::size_t i = 0; i < effective.size(); i++) {
      if (effective[i].get() == s.get() || (effective[i] && effective[i]->sameAs(*s))) return i;
    }
    return std::size_t(-1);
  }

private:
  static double smoothAngle()
  {
    // How sharp an edge still counts as one surface. It is the whole of the
    // intent judgement the approximation pass makes, so it is read once, here,
    // and used by both the fitting and the measuring below.
    double smooth_angle = 25.0;
    if (const char *env = getenv("OPENSCAD_STEP_SMOOTH_ANGLE")) smooth_angle = atof(env);
    smooth_angle *= M_PI / 180.0;
    return smooth_angle;
  }
};

/*! The band stage: rings of facets on a declared surface of revolution. Reads
 * the graph and the declarations only. */
void runBands(Context& ctx)
{
  [[maybe_unused]] const std::vector<Vector3d>& vertices = *ctx.in.mesh.vertices;
  [[maybe_unused]] const std::vector<std::vector<int>>& loops = *ctx.in.mesh.loops;
  [[maybe_unused]] const std::vector<char>& loop_valid = *ctx.in.mesh.valid;
  [[maybe_unused]] const std::vector<char>& loop_is_hole = *ctx.in.mesh.is_hole;
  [[maybe_unused]] const std::vector<std::shared_ptr<Surface>>& surfaces = *ctx.in.surfaces;
  [[maybe_unused]] const double model_tol = ctx.in.model_tol;
  [[maybe_unused]] const bool approximate = ctx.in.approximate;
  [[maybe_unused]] const std::size_t face_cnt = loops.size();
  [[maybe_unused]] const AnalyticFeatures::Mesh& mesh = ctx.mesh;
  [[maybe_unused]] const FacetGraph& graph = ctx.graph;
  [[maybe_unused]] const double smooth_angle = ctx.smooth_angle;
  [[maybe_unused]] std::vector<std::shared_ptr<Surface>>& effective = ctx.effective;
  [[maybe_unused]] const AnalyticFeatures::Provenance& provenance = ctx.provenance;
  [[maybe_unused]] AnalyticFeatures::Result& features = ctx.out.features;
  [[maybe_unused]] Writability& writability = ctx.writability;
  [[maybe_unused]] auto indexOf = [&ctx](const std::shared_ptr<const Surface>& s) {
    return ctx.indexOf(s);
  };

  // Say so even when there is nothing, so that a model whose declarations
  // never arrived cannot be mistaken for a build that predates them. The
  // availability line above prints only when the list is non-empty, which
  // makes those two cases look identical - silence.
  if (surfaces.empty()) LOG("STEP export: no analytic surfaces were declared");
  features =
    AnalyticFeatures::recogniseSurfacesOfRevolution(graph, surfaces, model_tol, surfaces.size());
}

/*! The fit stage, under the approximation flag: surfaces the model did not
 * declare, fitted to what the band stage left, then the band stage again with
 * them in hand. Its report ends with the band report, which is final only once
 * the re-run is done.
 *
 * The approximation pass, and the only place in this exporter where a
 * surface is written that the model never declared.
 *
 * It runs on what the declared pass left over, fits a cylinder to each
 * smooth region there, and then simply *declares* it - after which the
 * ordinary recogniser does everything else, including refusing the fit if
 * the mesh does not lie on it after all. That is the whole design: the
 * approximation contributes a declaration, not a face, so nothing
 * downstream has to trust it.
 *
 * What makes declaring on the model's behalf defensible is the region
 * rather than the fit. Regions are grown across edges meeting at less than
 * the smoothing angle, so a hexagonal prism - which is the same mesh as a
 * six sided tessellation of a cylinder, and the reason the exact path
 * refuses to guess - never forms one. */
void runFits(Context& ctx)
{
  [[maybe_unused]] const std::vector<Vector3d>& vertices = *ctx.in.mesh.vertices;
  [[maybe_unused]] const std::vector<std::vector<int>>& loops = *ctx.in.mesh.loops;
  [[maybe_unused]] const std::vector<char>& loop_valid = *ctx.in.mesh.valid;
  [[maybe_unused]] const std::vector<char>& loop_is_hole = *ctx.in.mesh.is_hole;
  [[maybe_unused]] const std::vector<std::shared_ptr<Surface>>& surfaces = *ctx.in.surfaces;
  [[maybe_unused]] const double model_tol = ctx.in.model_tol;
  [[maybe_unused]] const bool approximate = ctx.in.approximate;
  [[maybe_unused]] const std::size_t face_cnt = loops.size();
  [[maybe_unused]] const AnalyticFeatures::Mesh& mesh = ctx.mesh;
  [[maybe_unused]] const FacetGraph& graph = ctx.graph;
  [[maybe_unused]] const double smooth_angle = ctx.smooth_angle;
  [[maybe_unused]] std::vector<std::shared_ptr<Surface>>& effective = ctx.effective;
  [[maybe_unused]] const AnalyticFeatures::Provenance& provenance = ctx.provenance;
  [[maybe_unused]] AnalyticFeatures::Result& features = ctx.out.features;
  [[maybe_unused]] Writability& writability = ctx.writability;
  [[maybe_unused]] auto indexOf = [&ctx](const std::shared_ptr<const Surface>& s) {
    return ctx.indexOf(s);
  };

  if (approximate) {
    const std::vector<AnalyticFeatures::SmoothRegion> candidates =
      AnalyticFeatures::uncoveredRegions(mesh, features.consumed, smooth_angle);
    std::size_t fitted = 0, recovered = 0, turned = 0, tried = 0, coned = 0;
    // Fits the model had already declared. A guess that duplicates a declaration
    // is not a surface gained: it is a declaration something refused, and the
    // refusal is upstream - see open item 19.
    std::size_t already_declared = 0;
    std::map<std::string, int> turned_refusals;
    double coarsest = 0;
    for (const auto& region : candidates) {
      if (region.facets.size() < 3) continue;
      tried++;
      std::shared_ptr<Surface> guess = AnalyticFeatures::fitCylinder(mesh, region, model_tol);
      if (guess != nullptr) {
        fitted++;
        coarsest = std::max(coarsest, region.band);
        if (!addSurfaceUnique(effective, guess)) already_declared++;
        continue;
      }
      // A cone, which is a quadric fitCylinder cannot describe and correctly
      // refuses - its normals make a constant angle with the axis rather than
      // lying in a plane. The reference lid's hose socket is bored as a cone
      // over most of its depth, so without this most of its wall area has no
      // quadric to be written on at all.
      guess = AnalyticFeatures::fitCone(mesh, region, model_tol);
      if (guess != nullptr) {
        coned++;
        coarsest = std::max(coarsest, region.band);
        if (!addSurfaceUnique(effective, guess)) already_declared++;
        continue;
      }
      // Not a quadric, but perhaps still a sweep. `regularity` is what says
      // whether that question can even be asked: fitting needs the facets'
      // ordering, a mesh straight from a generator still has it at every
      // interior vertex, and a mesh a boolean has been through does not.
      // Below the threshold there is nothing to recover and the region is
      // left alone rather than fitted to something plausible.
      // Or a surface of revolution the model turned but could not name. A
      // cone and a sphere are declared here as rings rather than as shapes,
      // so this contributes one cylinder per ring and the band pass makes
      // the cones out of them.
      const char *not_turned = "no reason was given";
      const std::vector<std::shared_ptr<Surface>> rings =
        AnalyticFeatures::fitRevolved(mesh, region, model_tol, &not_turned);
      if (rings.empty()) turned_refusals[not_turned]++;
      if (!rings.empty()) {
        turned++;
        coarsest = std::max(coarsest, region.band);
        for (const auto& ring : rings) addSurfaceUnique(effective, ring);
        continue;
      }
      if (region.regularity < 0.95 || region.interior_vertices == 0) continue;
      const char *why = "no reason was given";
      guess = AnalyticFeatures::gridFromRegion(mesh, region, model_tol, &why);
      if (guess == nullptr) {
        // A region this regular is one the measurement said could be fitted,
        // so failing to recover it is worth a line rather than a silence.
        LOG("STEP export: a region of %1$d facets kept its ordering but was not recovered: %2$s",
            int(region.facets.size()), why);
        continue;
      }
      recovered++;
      coarsest = std::max(coarsest, region.band);
      addSurfaceUnique(effective, guess);
    }
    for (const auto& entry : turned_refusals) {
      LOG("STEP export: %1$d regions are not turned surfaces because %2$s", int(entry.second),
          entry.first.c_str());
    }
    if (tried > 0) {
      LOG(
        "STEP export: approximation took %1$d of %2$d uncovered regions - %3$d as cylinders, "
        "%4$d as cones, %5$d as rings of a turned surface, %6$d as swept grids - the coarsest "
        "tessellated to %7$.4f",
        int(fitted + coned + turned + recovered), int(tried), int(fitted), int(coned), int(turned),
        int(recovered), coarsest);
    }
    if (already_declared > 0) {
      // Not a degradation in itself - the region is written by whichever pass
      // can take it - but it says a declaration was refused upstream and the
      // approximation then re-derived the same surface by measurement. The
      // rule that refused it is in the band report above. See open item 19.
      LOG(message_group::Export_Warning,
          "STEP export: %1$d region%2$s fitted to a surface the model had already declared, so "
          "the fit gained nothing - a declaration was refused upstream and re-derived here",
          int(already_declared), already_declared == 1 ? " is" : "s are");
    }
    // What the declared surfaces alone had already claimed. The re-run below
    // starts from scratch with the fits in hand, and an exact declaration has
    // to keep what it recognised: a fit is a measurement of the mesh, a
    // declaration is what the model said, and no combination of the two may
    // end with the measurement holding ground the declaration had.
    std::vector<char> exact_consumed = features.consumed;
    // `coned` belongs in this condition as much as the others: a fit the band
    // pass never sees is a fit that cannot become a face.
    if (fitted + coned + turned + recovered > 0) {
      // Re-run with the fits in hand. Cheaper than threading them through the
      // pass that has already run, and it means a fitted surface goes through
      // exactly the checks a declared one does.
      features =
        AnalyticFeatures::recogniseSurfacesOfRevolution(graph, effective, model_tol, surfaces.size());
      std::size_t lost = 0, demoted = 0;
      for (std::size_t f = 0; f < exact_consumed.size() && f < features.consumed.size(); f++) {
        if (exact_consumed[f] == 0) continue;  // the declared-only pass did not have it
        if (features.consumed[f] == 0) {
          lost++;
          continue;
        }
        // It was claimed by a declaration - the first pass had nothing else -
        // so it has to still be on one.
        const std::size_t b = features.band_of_loop[f];
        if (b == AnalyticFeatures::NO_BAND || b >= features.bands.size()) continue;
        if (!features.bands[b].on_declared) demoted++;
      }
      if (lost > 0 || demoted > 0) {
        // Measured unreachable: on lid10 and the bayonet part, 536 and 628
        // facets go into the re-run claimed by a declared surface and every
        // one of them comes out still claimed by one. Reaching this means the
        // approximation has taken ground away from the model's own statement,
        // which is the one thing the declaration channel exists to prevent.
        LOG(message_group::Export_Error,
            "STEP export: the approximation's re-run cost the declared surfaces %1$d facet%2$s "
            "they had already recognised and moved %3$d onto a fitted surface - a measurement "
            "may not displace a declaration",
            int(lost), lost == 1 ? "" : "s", int(demoted));
      }
    }
  }
  for (const auto& line : features.report) LOG("STEP export: %1$s", line);

  // Bezier patches are recognised here and written further down, with the
  // B_SPLINE_SURFACE_WITH_KNOTS faces. The report is emitted either way, and
  // before the writing: it says whether the fillet declarations reached the
  // exporter and whether the regions and their boundary runs came out right,
  // which a file with no B-spline face in it cannot distinguish from a model
  // that declared none. A patch that is silently not recognised looks exactly
  // like one that was never declared - the same trap the band report exists
  // for.
  // Every pass below walks `effective` in order and the first surface to accept
  // a facet takes it, so the order *is* the precedence: the model's own
  // surfaces are the first `surfaces.size()` entries and every fit is appended
  // after them. Held rather than assumed, because nothing else in the file
  // says it and a fit offered first would take facets the declaration states.
  {
    std::size_t out_of_order = 0;
    for (std::size_t i = 0; i < surfaces.size(); i++) {
      if (i >= effective.size() || effective[i] != surfaces[i]) out_of_order++;
    }
    if (out_of_order > 0) {
      LOG(message_group::Export_Error,
          "STEP export: %1$d of the model's own surfaces are no longer the first entries of the "
          "list the passes walk, so a fitted surface can be offered a facet before the "
          "declaration that states it",
          int(out_of_order));
    }
  }
}

/*! The Bezier stage: declared fillet patches, and which of them are exactly quadrics. */
void runBezier(Context& ctx)
{
  [[maybe_unused]] const std::vector<Vector3d>& vertices = *ctx.in.mesh.vertices;
  [[maybe_unused]] const std::vector<std::vector<int>>& loops = *ctx.in.mesh.loops;
  [[maybe_unused]] const std::vector<char>& loop_valid = *ctx.in.mesh.valid;
  [[maybe_unused]] const std::vector<char>& loop_is_hole = *ctx.in.mesh.is_hole;
  [[maybe_unused]] const std::vector<std::shared_ptr<Surface>>& surfaces = *ctx.in.surfaces;
  [[maybe_unused]] const double model_tol = ctx.in.model_tol;
  [[maybe_unused]] const bool approximate = ctx.in.approximate;
  [[maybe_unused]] const std::size_t face_cnt = loops.size();
  [[maybe_unused]] const AnalyticFeatures::Mesh& mesh = ctx.mesh;
  [[maybe_unused]] const FacetGraph& graph = ctx.graph;
  [[maybe_unused]] const double smooth_angle = ctx.smooth_angle;
  [[maybe_unused]] std::vector<std::shared_ptr<Surface>>& effective = ctx.effective;
  [[maybe_unused]] const AnalyticFeatures::Provenance& provenance = ctx.provenance;
  [[maybe_unused]] AnalyticFeatures::Result& features = ctx.out.features;
  [[maybe_unused]] Writability& writability = ctx.writability;
  [[maybe_unused]] auto indexOf = [&ctx](const std::shared_ptr<const Surface>& s) {
    return ctx.indexOf(s);
  };
  [[maybe_unused]] std::vector<AnalyticFeatures::Patch>& bezier_patches = ctx.out.bezier_patches;
  [[maybe_unused]] std::vector<std::shared_ptr<Surface>>& patch_quadric = ctx.out.patch_quadric;
  [[maybe_unused]] std::set<std::set<int>>& circular_runs = ctx.out.circular_runs;

  std::vector<std::string> patch_report;
  std::vector<AnalyticFeatures::Patch> patches = AnalyticFeatures::recogniseBezierPatches(
    graph, effective, features.consumed, patch_report, &features.ledger, surfaces.size());
  for (const auto& line : patch_report) LOG("STEP export: %1$s", line);
  std::size_t curved_runs = 0, straight_runs = 0, mesh_edges = 0, covered = 0, live = 0;
  for (const auto& patch : patches) {
    if (!patch.alive) continue;
    live++;
    covered += patch.facets.size();
    for (const auto& run : patch.runs) {
      (run.straight ? straight_runs : curved_runs)++;
      if (!run.verts.empty()) mesh_edges += run.verts.size() - 1;
    }
  }
  if (live > 0) {
    LOG(
      "STEP export: their boundaries are %1$d curved runs over %2$d mesh edges, and "
      "%3$d straight edges",
      int(curved_runs), int(mesh_edges), int(straight_runs));

    // What each run borders decides whether it can be collapsed at all, and
    // it is the last thing the emission needs that has never been seen on a
    // real model. A run left unresolved is one the substitution cannot make.
    int whole = 0, part = 0, shared = 0, stuck = 0;
    for (const auto& patch : patches) {
      if (!patch.alive) continue;
      for (const auto& run : patch.runs) {
        switch (run.kind) {
        case AnalyticFeatures::Patch::Run::WHOLE_LOOP:  whole++; break;
        case AnalyticFeatures::Patch::Run::LOOP_RUN:    part++; break;
        case AnalyticFeatures::Patch::Run::OTHER_PATCH: shared++; break;
        default:                                        stuck++; break;
        }
      }
    }
    LOG(
      "STEP export: those runs border %1$d whole faces, %2$d stretches of a face, "
      "%3$d other patches, %4$d unresolved",
      whole, part, shared, stuck);
    LOG("STEP export: written as %1$d faces instead of %2$d", int(face_cnt - covered + live),
        int(face_cnt));
  }
  // Only patches whose every boundary can be substituted are written. One
  // that cannot stays faceted, which is always a valid export.
  //
  // Every patch the pass found gets its fate in the ledger, written or not -
  // see AnalyticFeatures::ClaimFate. A patch the pass itself could not make a
  // sheet of is its own refusal; one this writer cannot bound is the writer's.
  for (auto& patch : patches) {
    const bool found_dead = !patch.alive;
    if (patch.alive) {
      for (const auto& run : patch.runs) {
        if (run.kind == AnalyticFeatures::Patch::Run::UNRESOLVED) {
          patch.alive = false;
          patch.dropped = "one of its boundaries borders more than one face";
          break;
        }
      }
    }
    const AnalyticFeatures::ClaimFate fate =
      patch.alive
        ? AnalyticFeatures::ClaimFate::Honoured
        : (found_dead ? AnalyticFeatures::ClaimFate::Declined : AnalyticFeatures::ClaimFate::Refused);
    ctx.arbiter.settle(kBezierPatch, patch, indexOf(patch.surface), fate,
                       patch.alive ? "the facet lies on the declared Bezier patch" : patch.dropped);
  }

  // Which of the surviving patches are exactly quadrics.
  //
  // Since the fillet's Beziers went rational an edge strip is an exact
  // cylinder quadrant and a corner an exact sphere octant, and writing them
  // as such is the difference between a face a CAD kernel can offset, thread
  // and pattern and one it merely tolerates. Everything else - a varying
  // radius, faces that are not perpendicular - is a genuine spline and stays
  // one.
  patch_quadric.assign(patches.size(), nullptr);
  std::map<std::string, int> quadric_refusals;
  for (std::size_t p = 0; p < patches.size(); p++) {
    if (!patches[p].alive) continue;
    const auto *bez = dynamic_cast<const BezierPatchSurface *>(patches[p].surface.get());
    if (bez == nullptr) continue;
    const char *why = "no reason was given";
    patch_quadric[p] = AnalyticFeatures::quadricOfPatch(*bez, model_tol, &why);
    if (patch_quadric[p] == nullptr) quadric_refusals[why]++;
  }

  // A curved run shared with two patches becomes one EdgeCurve used by both,
  // so the two have to agree on what kind of curve it is.
  //
  // This used to be settled by withdrawing the quadric: a quadric face was
  // bounded by CIRCLEs and a spline face by curves read off its own net, so a
  // patch whose partner was not a quadric gave up being one, to a fixed point
  // because withdrawing one could withdraw its partner's partner in turn. It
  // cost real faces - OCCT reported six of step-fillet-refusals' thirty
  // splines as exact cylinders of radius sqrt(3), which is exactly the set
  // this rule withdrew.
  //
  // The question it was answering is better asked of the *run*. runCircle
  // reads the declared control net, weights and all, so it says whether that
  // boundary is exactly a circular arc rather than whether the face beside it
  // is a quadric. When both sides say so about the same circle, a CIRCLE lies
  // on both surfaces exactly and bounding either with it is sound - and a
  // kernel offsets and patterns along a circle where it merely tolerates a
  // spline. When they disagree, or only one side is a patch at all, the run
  // stays a spline and it is the *curve* that gives way rather than the face.
  for (std::size_t p = 0; p < patches.size(); p++) {
    if (!patches[p].alive) continue;
    for (const auto& run : patches[p].runs) {
      if (run.straight) continue;
      Vector3d centre, normal;
      double radius = 0;
      if (!AnalyticFeatures::runCircle(patches[p], run, vertices, centre, normal, radius)) {
        continue;
      }
      if (run.kind == AnalyticFeatures::Patch::Run::OTHER_PATCH) {
        // Both nets have to describe the same circle, or the two faces would
        // be bounded by a curve only one of them lies on.
        if (run.patch >= patches.size() || !patches[run.patch].alive) continue;
        const AnalyticFeatures::Patch& other = patches[run.patch];
        if (run.partner >= other.runs.size()) continue;
        Vector3d c2, n2;
        double r2 = 0;
        if (!AnalyticFeatures::runCircle(other, other.runs[run.partner], vertices, c2, n2, r2)) {
          continue;
        }
        const double scale = std::max(1.0, radius);
        if (fabs(r2 - radius) > 1e-9 * scale || (c2 - centre).norm() > 1e-9 * scale) continue;
      }
      circular_runs.insert(std::set<int>(run.verts.begin(), run.verts.end()));
    }
  }

  // Why the rest are not, counted by reason. A patch refused for a reason
  // nobody expected is the only way to tell a genuine spline from a quadric
  // the recogniser is failing to see, and this suite had exactly that: OCCT
  // named six exact cylinders among the splines while this pass reported a
  // bare zero.
  for (const auto& entry : quadric_refusals) {
    LOG("STEP export: %1$d patches are not quadrics because %2$s", int(entry.second),
        entry.first.c_str());
  }

  int quad_cyl = 0, quad_sph = 0;
  for (std::size_t p = 0; p < patches.size(); p++) {
    if (patch_quadric[p] == nullptr) continue;
    if (dynamic_cast<const CylinderSurface *>(patch_quadric[p].get()) != nullptr) quad_cyl++;
    else quad_sph++;
  }
  if (live > 0) {
    LOG(
      "STEP export: %1$d of %2$d patches are exactly quadrics - %3$d cylindrical, "
      "%4$d spherical",
      quad_cyl + quad_sph, int(live), quad_cyl, quad_sph);
  }
  bezier_patches = patches;
}

/*! The grid stage: declared sweeps, written as faces where the tier allows. */
void runGrids(Context& ctx)
{
  [[maybe_unused]] const std::vector<Vector3d>& vertices = *ctx.in.mesh.vertices;
  [[maybe_unused]] const std::vector<std::vector<int>>& loops = *ctx.in.mesh.loops;
  [[maybe_unused]] const std::vector<char>& loop_valid = *ctx.in.mesh.valid;
  [[maybe_unused]] const std::vector<char>& loop_is_hole = *ctx.in.mesh.is_hole;
  [[maybe_unused]] const std::vector<std::shared_ptr<Surface>>& surfaces = *ctx.in.surfaces;
  [[maybe_unused]] const double model_tol = ctx.in.model_tol;
  [[maybe_unused]] const bool approximate = ctx.in.approximate;
  [[maybe_unused]] const std::size_t face_cnt = loops.size();
  [[maybe_unused]] const AnalyticFeatures::Mesh& mesh = ctx.mesh;
  [[maybe_unused]] const FacetGraph& graph = ctx.graph;
  [[maybe_unused]] const double smooth_angle = ctx.smooth_angle;
  [[maybe_unused]] std::vector<std::shared_ptr<Surface>>& effective = ctx.effective;
  [[maybe_unused]] const AnalyticFeatures::Provenance& provenance = ctx.provenance;
  [[maybe_unused]] AnalyticFeatures::Result& features = ctx.out.features;
  [[maybe_unused]] Writability& writability = ctx.writability;
  [[maybe_unused]] auto indexOf = [&ctx](const std::shared_ptr<const Surface>& s) {
    return ctx.indexOf(s);
  };
  [[maybe_unused]] std::vector<AnalyticFeatures::Patch>& grid_faces = ctx.out.grid_faces;

  // What is left, and what it would take to do better.
  //
  // Everything above writes a surface only where the model declared one and
  // the mesh fits it exactly. What remains is the geometry OpenSCAD never
  // held the mathematics for - a polyhedron() over a computed point list, a
  // helical thread - and it is written as planes, which is always correct and
  // on the reference part is 99.8% of the uncovered area.
  //
  // `band` is what makes this measurable rather than a matter of taste. The
  // mesh does not say where the true surface is; it says the true surface
  // passes through these vertices and cannot stray further than the sagitta
  // of the facets between them. A fitted surface inside that band asserts
  // nothing the mesh does not already allow. One outside it is inventing
  // geometry - which is exactly what a B-spline fitted through this project's
  // own thread did at the run-out, overshooting 0.378mm where the band is
  // 0.109mm, while scoring 1e-13 against the vertices it interpolated.
  // What a declared sweep claims. This is the other half of the measurement
  // below: uncoveredRegions() says the mesh has lost the ordering a fit would
  // need, and a GridSurface is the generator handing that ordering back. The
  // question it has to answer is whether the declaration still matches the
  // mesh once the booleans have run - a record which matches nothing is
  // harmless but useless, and looks identical to one that was never made.
  for (const auto& surface : effective) {
    const auto *grid = dynamic_cast<const GridSurface *>(surface.get());
    if (grid == nullptr) continue;
    std::size_t whole = 0, partly = 0;
    std::vector<Vector3d> unused;  // pointMember's scratch argument, as elsewhere
    for (std::size_t i = 0; i < loops.size(); i++) {
      if (!loop_valid[i] || loop_is_hole[i]) continue;
      std::size_t corners = 0;
      for (const int v : loops[i]) {
        corners += const_cast<GridSurface *>(grid)->pointMember(unused, vertices[v]);
      }
      if (corners == loops[i].size()) whole++;
      else if (corners > 0) partly++;
    }
    LOG(
      // The band is printed because it is the tolerance being trusted. A
      // declared sweep is smooth and the mesh is its tessellation, so a
      // vertex the boolean created lies on a facet rather than on the
      // surface, up to the sagitta of a station away. That is the widest a
      // claim here can be wrong by, and it is the model's own resolution
      // rather than a constant somebody chose.
      "STEP export: a declared %1$dx%2$d %3$s sweep claims %4$d facets whole, %5$d cut across "
      "it, within its tessellation band of %6$.4f",
      grid->rows, grid->cols, grid->interpolated() ? "cubic" : "linear", int(whole), int(partly),
      grid->tessellationBand());
  }

  // What those claimed facets look like as a face: one sheet, and a boundary
  // split into the runs that would each have to become one curve. That is the
  // step between claiming facets and writing them, and it is reported before
  // anything is written for the same reason the rest of this is - a sweep
  // that cannot be made into a face has to say so, rather than looking like a
  // sweep that was never declared.
  std::vector<std::string> grid_report;
  const std::vector<AnalyticFeatures::Patch> grid_patches = AnalyticFeatures::recogniseGridPatches(
    graph, effective, features.consumed, grid_report, &features.ledger, surfaces.size());
  for (const auto& line : grid_report) LOG("STEP export: %1$s", line);

  // Where the claimed region sits on the sweep, which is the question a face
  // has to answer and a count of facets does not. A sweep whose profile is
  // declared closed is a tube, and its surface is closed across v: the region
  // covering every span of the profile wraps that seam, and a face on a
  // surface written as an open rectangle cannot be bounded across it. One
  // covering only some spans is a strip, whose boundary stays inside the
  // rectangle and can be written as it stands.
  std::size_t wrapping = 0, strips = 0;
  // The fate of every sweep patch the pass found, written or not. Only a
  // strip under the approximation flag is written; the rest are recorded here
  // with the reason, so a facet of a declared sweep left planar can say why.
  auto grid_fate = [&](const AnalyticFeatures::Patch& patch, AnalyticFeatures::ClaimFate fate,
                       const char *why) {
    ctx.arbiter.settle(kDeclaredSweep, patch, indexOf(patch.surface), fate, why);
  };
  for (const auto& patch : grid_patches) {
    if (!patch.alive) {
      grid_fate(patch, AnalyticFeatures::ClaimFate::Declined, patch.dropped);
      continue;
    }
    const auto *g = dynamic_cast<const GridSurface *>(patch.surface.get());
    if (g == nullptr) continue;
    const int segs = g->closed_v ? g->cols : g->cols - 1;
    if (segs < 1) {
      grid_fate(patch, AnalyticFeatures::ClaimFate::Refused, "the sweep's profile has no span to write");
      continue;
    }
    std::vector<char> span_used(segs, 0);
    for (const std::size_t f : patch.facets) {
      Vector3d centroid = Vector3d::Zero();
      for (const int v : loops[f]) centroid += vertices[v];
      centroid /= double(loops[f].size());
      double pu = 0, pv = 0;
      if (!g->project(centroid, pu, pv)) continue;
      int span = int(pv * segs);
      span = std::max(0, std::min(segs - 1, span));
      span_used[span] = 1;
    }
    int used = 0;
    for (const char c : span_used) used += c ? 1 : 0;
    const bool wraps = used == segs && g->closed_v;
    if (wraps) {
      wrapping++;
      grid_fate(patch, AnalyticFeatures::ClaimFate::Refused,
                "the region closes round the profile, and its face would cross the surface's seam");
    } else {
      strips++;
      // Written only under the approximation flag. Every other analytic face
      // this exporter writes carries a surface the mesh lies on exactly; a
      // declared sweep is matched to within its tessellation band, which is
      // the model's own resolution and not zero.
      if (approximate) {
        grid_faces.push_back(patch);
        grid_fate(patch, AnalyticFeatures::ClaimFate::Honoured,
                  "the sweep's patch is written and takes it");
      } else {
        grid_fate(patch, AnalyticFeatures::ClaimFate::Refused,
                  "a declared sweep is written only under the approximation flag");
      }
    }
    LOG("STEP export: its facets lie over %1$d of the profile's %2$d spans - %3$s", used, segs,
        wraps ? "the region closes around the profile, so its face crosses the surface's seam"
              : "the region is a strip, whose boundary stays inside the surface's rectangle");
  }

  if (!grid_faces.empty()) {
    std::size_t written_facets = 0;
    for (const auto& patch : grid_faces) written_facets += patch.facets.size();
    LOG("STEP export: %1$d declared sweep%2$s written as one face each, replacing %3$d facets",
        int(grid_faces.size()), grid_faces.size() == 1 ? "" : "s", int(written_facets));
  }
  if (wrapping > 0 || strips > grid_faces.size()) {
    // Which sweep is blocked on what is worth saying apart. A strip is
    // bounded by the mesh's own edges and needs only the approximation flag;
    // one that closes around its profile crosses a seam, which is a different
    // piece of work.
    LOG(message_group::Export_Warning,
        "STEP export: %1$d declared sweep%2$s left faceted - %3$d wrap the surface's seam, "
        "%4$d await the approximation flag",
        int(wrapping + strips - grid_faces.size()),
        wrapping + strips - grid_faces.size() == 1 ? "" : "s", int(wrapping),
        int(strips - grid_faces.size()));
  }
}

/*! The quadric stage: trimmed regions of declared or fitted quadrics, all of a
 * surface or none of it, asked of the writer through Writability. */
void runQuadrics(Context& ctx)
{
  [[maybe_unused]] const std::vector<Vector3d>& vertices = *ctx.in.mesh.vertices;
  [[maybe_unused]] const std::vector<std::vector<int>>& loops = *ctx.in.mesh.loops;
  [[maybe_unused]] const std::vector<char>& loop_valid = *ctx.in.mesh.valid;
  [[maybe_unused]] const std::vector<char>& loop_is_hole = *ctx.in.mesh.is_hole;
  [[maybe_unused]] const std::vector<std::shared_ptr<Surface>>& surfaces = *ctx.in.surfaces;
  [[maybe_unused]] const double model_tol = ctx.in.model_tol;
  [[maybe_unused]] const bool approximate = ctx.in.approximate;
  [[maybe_unused]] const std::size_t face_cnt = loops.size();
  [[maybe_unused]] const AnalyticFeatures::Mesh& mesh = ctx.mesh;
  [[maybe_unused]] const FacetGraph& graph = ctx.graph;
  [[maybe_unused]] const double smooth_angle = ctx.smooth_angle;
  [[maybe_unused]] std::vector<std::shared_ptr<Surface>>& effective = ctx.effective;
  [[maybe_unused]] const AnalyticFeatures::Provenance& provenance = ctx.provenance;
  [[maybe_unused]] AnalyticFeatures::Result& features = ctx.out.features;
  [[maybe_unused]] Writability& writability = ctx.writability;
  [[maybe_unused]] auto indexOf = [&ctx](const std::shared_ptr<const Surface>& s) {
    return ctx.indexOf(s);
  };
  [[maybe_unused]] std::vector<AnalyticFeatures::Patch>& quadric_faces = ctx.out.quadric_faces;

  // A quadric the band pass could not write, because its trim is not a plane
  // section: a bored cylinder, whose rim is a quartic and whose face is
  // therefore lost for want of a bound. Runs last and over what nothing else
  // claimed, so it can only ever add faces.
  //
  // Which tier it runs in is decided by proof rather than by the flag. A
  // region cut from a declared surface is mostly exact - a boolean puts its
  // new vertices on the facet planes, so it is the fringe where the cut
  // landed that strays off the surface and not the interior - and a face
  // whose every corner is on the surface to 1e-7 asserts nothing the mesh
  // does not already state. Those are written by the exact pass. Spending
  // the tessellation band, which is what makes a face this exporter cannot
  // prove, still needs the approximation flag.
  {
    const double max_off = approximate ? std::numeric_limits<double>::infinity() : 1e-7;
    std::vector<std::string> quadric_report;
    const std::vector<AnalyticFeatures::Patch> found = AnalyticFeatures::recogniseQuadricPatches(
      graph, effective, features.consumed, provenance, smooth_angle, max_off, quadric_report,
      &features.ledger, surfaces.size());
    for (const auto& line : quadric_report) LOG("STEP export: %1$s", line);
    // Corners on the surface are not enough to call a face exact. The
    // boundary between two of them is a straight edge in the mesh, and a
    // straight edge lies on a quadric only if it runs along the axis, or
    // around it at a constant height where it can be written as an arc.
    // Anything else is a chord under a surface that bulges away from it by
    // the sagitta - truthful edge, truthful surface, inconsistent pair, and
    // it is the pair a kernel has to widen its tolerance over.
    //
    // So the exact tier asks for both. Measured on the reference lid, asking
    // only for the corners bought nine more analytic faces and cost 39 edges
    // sagging up to 0.107 off the cylinder they bounded, on an export that
    // had none at all.
    writability.begin(effective);
    // All of a surface or none of it, and that decision and the sections are
    // circular - a face may be writable only because a section covers its
    // chords, and the section is only there while that face stands - so they
    // are settled together, by dropping whatever the last round refused and
    // asking again. Refusals only ever grow, so it stops. Whether a standing
    // region's boundary lies on its surface is the writer's to say - see
    // recognition::Writability; what to do with the answer is policy, here.
    std::map<const Surface *, bool> surface_ok;
    {
      std::vector<char> standing(found.size(), 0);
      for (std::size_t i = 0; i < found.size(); i++) standing[i] = found[i].alive ? 1 : 0;
      for (;;) {
        std::vector<const AnalyticFeatures::Patch *> live;
        for (std::size_t i = 0; i < found.size(); i++) {
          if (standing[i]) live.push_back(&found[i]);
        }
        const std::vector<char> ok = writability.boundaryOnSurface(live);
        surface_ok.clear();
        for (std::size_t k = 0; k < live.size(); k++) {
          const Surface *key = live[k]->surface.get();
          auto it = surface_ok.find(key);
          if (it == surface_ok.end()) surface_ok.emplace(key, ok[k] != 0);
          else it->second = it->second && ok[k] != 0;
        }
        // Under the approximation flag a face is written whatever its boundary
        // does, so nothing is dropped and one round settles it.
        if (approximate) break;
        bool changed = false;
        for (std::size_t i = 0; i < found.size(); i++) {
          if (standing[i] && !surface_ok[found[i].surface.get()]) {
            standing[i] = 0;
            changed = true;
          }
        }
        if (!changed) break;
      }
    }
    std::size_t claimed_facets = 0, refused = 0, unbounded = 0;
    // The fate of every region the pass handed on. The facets it dropped
    // before a region existed are recorded in the pass; these are the rest.
    // D4 evidence throughout - see recogniseQuadricPatches.
    // The precision a claim carries is the one its *index* implies - the
    // Arbiter derives it. Hardcoding DeclaredExact here once labelled a
    // winning **fit** as a declaration, and `ClaimLedger::misheld` - whose
    // whole job is to find a facet held by a claimant less precise than one
    // that also covered it - reads that label.
    auto quadric_fate = [&](const AnalyticFeatures::Patch& patch, AnalyticFeatures::ClaimFate fate,
                            const char *why) {
      ctx.arbiter.settle(kQuadricFace, patch, indexOf(patch.surface), fate, why);
    };
    for (const auto& patch : found) {
      if (!patch.alive) {
        refused++;
        quadric_fate(patch, AnalyticFeatures::ClaimFate::Declined, patch.dropped);
        continue;
      }
      if (!approximate && !surface_ok[patch.surface.get()]) {
        unbounded++;
        quadric_fate(patch, AnalyticFeatures::ClaimFate::Refused,
                     "some of the surface's boundary is off it, and half a surface is worse than none");
        continue;
      }
      quadric_faces.push_back(patch);
      claimed_facets += patch.facets.size();
      quadric_fate(patch, AnalyticFeatures::ClaimFate::Honoured,
                   "the quadric's patch is written and takes it");
    }
    if (!quadric_faces.empty()) {
      LOG(
        "STEP export: %1$d trimmed quadric%2$s written as one face each, replacing %3$d facets "
        "(%4$s)",
        int(quadric_faces.size()), quadric_faces.size() == 1 ? "" : "s", int(claimed_facets),
        approximate ? "within their tessellation band" : "exactly on their surface");
    }
    if (refused > 0) {
      LOG("STEP export: %1$d trimmed quadric region%2$s could not be bounded", int(refused),
          refused == 1 ? "" : "s");
    }
    if (unbounded > 0) {
      LOG(
        "STEP export: %1$d trimmed quadric region%2$s left faceted - the surface is exact but "
        "some of it is bounded off itself, and half a surface is worse than none",
        int(unbounded), unbounded == 1 ? "" : "s");
    }
  }
}

/*! What no stage took, reported under the approximation flag - the regions left
 * to the tessellation, and whether a fit or only a declaration could reach them. */
void runLeftover(Context& ctx)
{
  [[maybe_unused]] const std::vector<Vector3d>& vertices = *ctx.in.mesh.vertices;
  [[maybe_unused]] const std::vector<std::vector<int>>& loops = *ctx.in.mesh.loops;
  [[maybe_unused]] const std::vector<char>& loop_valid = *ctx.in.mesh.valid;
  [[maybe_unused]] const std::vector<char>& loop_is_hole = *ctx.in.mesh.is_hole;
  [[maybe_unused]] const std::vector<std::shared_ptr<Surface>>& surfaces = *ctx.in.surfaces;
  [[maybe_unused]] const double model_tol = ctx.in.model_tol;
  [[maybe_unused]] const bool approximate = ctx.in.approximate;
  [[maybe_unused]] const std::size_t face_cnt = loops.size();
  [[maybe_unused]] const AnalyticFeatures::Mesh& mesh = ctx.mesh;
  [[maybe_unused]] const FacetGraph& graph = ctx.graph;
  [[maybe_unused]] const double smooth_angle = ctx.smooth_angle;
  [[maybe_unused]] std::vector<std::shared_ptr<Surface>>& effective = ctx.effective;
  [[maybe_unused]] const AnalyticFeatures::Provenance& provenance = ctx.provenance;
  [[maybe_unused]] AnalyticFeatures::Result& features = ctx.out.features;
  [[maybe_unused]] Writability& writability = ctx.writability;
  [[maybe_unused]] auto indexOf = [&ctx](const std::shared_ptr<const Surface>& s) {
    return ctx.indexOf(s);
  };

  if (approximate) {
    // What is left after everything, including the fits above. A region of
    // one or two facets is not among them: a single flat facet is already
    // written exactly as a PLANE, and reporting it as something left faceted
    // says a surface was lost where none was.
    std::vector<AnalyticFeatures::SmoothRegion> regions;
    for (auto& region : AnalyticFeatures::uncoveredRegions(mesh, features.consumed, smooth_angle)) {
      if (region.facets.size() >= 3) regions.push_back(std::move(region));
    }
    std::size_t left = 0;
    double worst_band = 0.0;
    for (const auto& region : regions) {
      left += region.facets.size();
      worst_band = std::max(worst_band, region.band);
    }
    if (regions.empty()) {
      LOG("STEP export: approximation found nothing left to fit");
    } else {
      LOG(
        "STEP export: %1$d smooth region%2$s left faceted, %3$d facets in all; the "
        "tessellation leaves at most %4$.4f to fit inside",
        int(regions.size()), regions.size() == 1 ? "" : "s", int(left), worst_band);
      const std::size_t show = std::min<std::size_t>(regions.size(), 3);
      for (std::size_t i = 0; i < show; i++) {
        LOG(
          "STEP export:   region of %1$d facets, area %2$.1f, band %3$.4f (typical %4$.4f), "
          "worst dihedral %5$.1f degrees",
          int(regions[i].facets.size()), regions[i].area, regions[i].band, regions[i].median_band,
          regions[i].worst_dihedral * 180.0 / M_PI);
        // Whether a fit is even available here, which is a different question
        // from whether it would be accurate.
        LOG(
          "STEP export:     grid %1$.0f% regular over %2$d interior vertices at valence "
          "%3$d - %4$s",
          regions[i].regularity * 100.0, int(regions[i].interior_vertices),
          int(regions[i].modal_valence),
          regions[i].interior_vertices == 0
            ? "too thin to tell - every vertex is on the boundary"
            : (regions[i].regularity > 0.95
                 ? "the generator's ordering survives, a fit could be made"
                 : "the ordering is gone, only a declaration could describe this"));
      }
      // These are the ones the fit could not take, and saying so is the point -
      // a pass which quietly wrote nothing here would look exactly like one
      // which found nothing to write.
      LOG(message_group::Export_Warning,
          "STEP export: %1$d region%2$s faceted, no fit having been found - which is always "
          "a valid export",
          int(regions.size()), regions.size() == 1 ? " stays" : "s stay");
    }
  }
}

/*! What each stage needs to exist before it runs, and what it makes. */
enum Product : unsigned {
  kGraph = 1u << 0,         // the mesh and its adjacency
  kDeclarations = 1u << 1,  // the model's surface records
  kProvenance = 1u << 2,    // who made each facet, who declared each surface
  kBands = 1u << 3,         // rings on declared surfaces of revolution, and what they hold
  kFits = 1u << 4,          // the surface list the passes walk, fits appended
  kBezierFaces = 1u << 5,
  kSweepFaces = 1u << 6,
  kQuadricFaces = 1u << 7,
  kLeftover = 1u << 8,
};

/*! The stages, in the order the policy in force runs them, each with what it
 * reads and what it writes.
 *
 * The order is checked against the declarations every run rather than trusted:
 * doc/step-recognition-design.md, consequence 3 - "dependencies are discovered
 * by breakage" - is what this ends. The quadric stage reads the sweep faces
 * because the writer's sections, asked through Writability, walk them; that
 * dependency once opened a shell on 420 edges when it was discovered the other
 * way. A stage whose inputs another produces is placed after it, and a table
 * that says otherwise is an EXPORT-ERROR. */
struct Stage {
  const char *name;
  unsigned reads;
  unsigned writes;
  void (*run)(Context&);
};
constexpr Stage kStages[] = {
  {"band", kGraph | kDeclarations, kBands, runBands},
  {"fit", kGraph | kDeclarations | kBands, kFits | kBands, runFits},
  {"Bezier", kGraph | kFits | kBands, kBezierFaces, runBezier},
  {"grid", kGraph | kFits | kBands | kBezierFaces, kSweepFaces, runGrids},
  {"quadric", kGraph | kFits | kProvenance | kBands | kBezierFaces | kSweepFaces, kQuadricFaces,
   runQuadrics},
  {"leftover", kGraph | kBands | kBezierFaces | kSweepFaces | kQuadricFaces, kLeftover, runLeftover},
};

}  // namespace

void runPipeline(const Inputs& in, Writability& writability, Recognition& out)
{
  Context ctx(in, writability, out);
  unsigned produced = kGraph | kDeclarations | kProvenance;
  for (const Stage& stage : kStages) {
    if ((stage.reads & ~produced) != 0) {
      LOG(message_group::Export_Error,
          "STEP export: the recognition stage '%1$s' runs before something it reads exists - "
          "the stage table's order contradicts its declarations",
          stage.name);
    }
    stage.run(ctx);
    produced |= stage.writes;
  }
}

}  // namespace recognition
