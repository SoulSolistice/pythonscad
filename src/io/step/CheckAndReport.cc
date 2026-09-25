/*! What the writer checks and says at the end - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include "io/step/CurveChecks.h"
#include "utils/printutils.h"

using namespace step;

void StepKernel::BodyBuild::checkAndReport()
{
  // Every edge in the file against its own two vertices, which is the one
  // relation a CAD kernel rejects a substituted curve on and the one nothing
  // here used to measure. It is asked of the finished entities rather than of
  // any pass's intermediate state, so no substitution can be added later that
  // quietly escapes it.
  {
    std::size_t off = 0;
    double worst = 0;
    std::vector<double> strays;
    for (const Entity *ent : entities) {
      const auto *edge = dynamic_cast<const EdgeCurve *>(ent);
      if (edge == nullptr || edge->round == nullptr) continue;
      for (const Vertex *v : {edge->vert1, edge->vert2}) {
        if (v == nullptr || v->point == nullptr) continue;
        const double d = vertexOffCurve(edge->round, v->point->pt);
        const double owed = 1e-6 * std::max(1.0, v->point->pt.norm());
        if (d <= owed) continue;
        off++;
        strays.push_back(d);
        worst = std::max(worst, d);
      }
    }
    if (off > 0) {
      std::sort(strays.begin(), strays.end());
      LOG(message_group::Export_Warning,
          "STEP export: %1$d vertex%2$s not on the curve its own edge is written as, by "
          "%3$.2e median and %4$.2e at worst - a reader checks exactly this, and calls it "
          "\"vertex does not lie on edge\'s curve\"",
          int(off), off == 1 ? " is" : "es are", strays[strays.size() / 2], worst);
    }
  }
  if (reseamed > 0 || reseam_full > 0) {
    LOG(
      "STEP export: %1$d sweep face%2$s written from a column that puts the profile's seam in a "
      "span it does not use, so no boundary of it crosses the seam; %3$d use every span and keep "
      "the seam where it was",
      int(reseamed), reseamed == 1 ? " is" : "s are", int(reseam_full));
  }
  if (section_stats.trimmed + section_stats.dropped > 0) {
    LOG(
      "STEP export: %1$d plane section%2$s shortened and %3$d dropped so that the curve written "
      "for a run passes through that run's own endpoints - the worst end was %4$.2e off the "
      "surface the section is cut from",
      int(section_stats.trimmed), section_stats.trimmed == 1 ? " was" : "s were",
      int(section_stats.dropped), section_stats.end_stray);
  }
  if (crossing_written > 0) {
    LOG(
      "STEP export: %1$d edge%2$s written as the curve where two declared surfaces cross, "
      "fitted to within %3$.2e of both - a chord and a plane section each lie on only one",
      crossing_written, crossing_written == 1 ? "" : "s", crossing_fit);
    // One edge per run of the mesh's, from where something else meets it to the
    // next - see decideCrossingRuns.
    LOG("STEP export: those %1$d curve%2$s cover %3$d edge%4$s of the mesh", crossing_written,
        crossing_written == 1 ? "" : "s", int(crossing_mesh_edges), crossing_mesh_edges == 1 ? "" : "s");
  }
  if (crossing_unwalked > 0) {
    // Two faces on one piece that do not both carry it in order: the one that
    // cannot writes chords where the other writes the curve.
    LOG(message_group::Export_Error,
        "STEP export: %1$d crossing edge%2$s met by a face whose boundary does not run along the "
        "rest of %3$s curve, so the two faces there disagree about an edge",
        int(crossing_unwalked), crossing_unwalked == 1 ? "" : "s",
        crossing_unwalked == 1 ? "its" : "their");
  }
  if (arc_refused > 0) {
    std::sort(arc_errors.begin(), arc_errors.end());
    const double lo = arc_errors.empty() ? 0 : arc_errors.front();
    const double med = arc_errors.empty() ? 0 : arc_errors[arc_errors.size() / 2];
    const double hi = arc_errors.empty() ? 0 : arc_errors.back();
    LOG(
      "EXPORT-WARNING: STEP export: %1$d edge%2$s could not be written as that curve and fell "
      "back to a chord - %3$d never fitted at any degree, the rest came within "
      "%4$.2e/%5$.2e/%6$.2e min/med/max of both surfaces against the %7$.0e asked for",
      int(arc_refused), arc_refused == 1 ? "" : "s", int(arc_no_fit), lo, med, hi, 1e-7);
    std::sort(arc_on_quadric.begin(), arc_on_quadric.end());
    std::sort(arc_on_fitted.begin(), arc_on_fitted.end());
    if (!arc_on_quadric.empty()) {
      LOG(
        "STEP export:    of that miss, %1$.2e median is against a surface stated algebraically "
        "and %2$.2e median against one that answers by projecting",
        arc_on_quadric[arc_on_quadric.size() / 2], arc_on_fitted[arc_on_fitted.size() / 2]);
      LOG("STEP export:    %1$d of them run across a crease in the declared profile and %2$d do not",
          int(arc_creased), int(arc_uncreased));
    }
  }
  if (sections_on_facet > 0) {
    LOG(
      "STEP export: %1$d boundary section%2$s cut by a facet of something curved rather than by a "
      "plane - the wrong curve, kept because the chord it would fall back to is further off the "
      "face; the fix is upstream, in the claim",
      sections_on_facet, sections_on_facet == 1 ? " is" : "s are");
  }
  if (band_seams > 0) {
    LOG(
      "STEP export: %1$d face%2$s round %3$s axis between two rims closed by a seam along a ruling, not "
      "cut in two",
      int(band_seams), band_seams == 1 ? "" : "s", band_seams == 1 ? "its" : "their");
  }
  if (apex_seams > 0) {
    LOG("STEP export: %1$d cone face%2$s closed round %3$s apex by a seam up a generator",
        int(apex_seams), apex_seams == 1 ? "" : "s", apex_seams == 1 ? "its" : "their");
  }
  if (sections_declared + sections_fitted > 0) {
    // Where each section's plane came from, because it is the difference
    // between a curve that is exact and one that is only as good as the mesh
    // it was fitted to. A declared plane cannot move; a fitted one moves with
    // the tessellation, with what the boolean left behind, and with the
    // tolerance the fit was given - and the section's centre and both its
    // semi-axes are differences taken against that plane.
    LOG(
      "STEP export: %1$d plane section%2$s written as the conic it is - %3$d on a plane the "
      "model declared, %4$d on one taken from the mesh",
      sections_declared + sections_fitted, sections_declared + sections_fitted == 1 ? "" : "s",
      sections_declared, sections_fitted);
    if (section_unshared > 0) {
      LOG(message_group::Export_Warning,
          "STEP export: %1$d plane section%2$s written as chords by both faces instead of as the "
          "conic by one, the face across being fanned into triangles or not carrying the run of "
          "corners it would have to be handed the edge by",
          int(section_unshared), section_unshared == 1 ? " is" : "s are");
    }
    if (!section_misses.empty()) {
      double tilt = 9, off = 9;
      std::size_t parallel = 0;
      for (const auto& m : section_misses) {
        tilt = std::min(tilt, m.tilt);
        if (m.tilt <= 1e-6) {
          parallel++;
          off = std::min(off, m.offset);
        }
      }
      LOG(
        "STEP export:    %1$d cut planes took the mesh's own: %2$d are parallel to a declared "
        "plane, nearest missing its offset by %3$.2e, and the rest tilt from the nearest by "
        "%4$.2e against the 1e-06 asked for; %5$d planes were declared in all",
        int(section_misses.size()), int(parallel), off > 8 ? 0.0 : off, tilt,
        int(declared_planes.size()));
    }
  }
}
