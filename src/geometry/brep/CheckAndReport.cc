/*! What the writer checks and says at the end - see BodyBuild.h. */
#include "geometry/brep/BodyBuild.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include "utils/printutils.h"

using namespace brep;

void brep::BodyBuild::checkAndReport()
{
  // Every edge against its own two vertices is the contract's question now
  // (brep::checkVerticesOnCurves, asked by checkBrep as an EXPORT-ERROR; open
  // item 39, R12).
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
  // The precision the file states, and what it rests on - the first thing to
  // read when a reader's tolerance is suspected. How closely the crossing curves
  // came, and which fell back to chords, follow below.
  LOG(
    "STEP export: uncertainty declared %1$.2e - the file's resolution, its corners' placement "
    "floor at the part's size; every curve fitted in it is held to that",
    resolution());
  if (crossing_written > 0) {
    LOG(
      "STEP export: %1$d edge%2$s written as the curve where two declared surfaces cross, "
      "fitted to within %3$.2e of both - a chord and a plane section each lie on only one",
      crossing_written, crossing_written == 1 ? "" : "s", crossing_fit);
    // One edge per run of the mesh's, from where something else meets it to the
    // next - see decideCrossingRuns.
    LOG("STEP export: those %1$d curve%2$s cover %3$d edge%4$s of the mesh", crossing_written,
        crossing_written == 1 ? "" : "s", int(crossing_mesh_edges), crossing_mesh_edges == 1 ? "" : "s");
    if (!crossing_corners.empty()) {
      LOG(
        "STEP export: %1$d of those mesh edges cross a sweep's crease inside them, and the curve "
        "keeps the corner there",
        int(crossing_corners.size()));
    }
  }
  if (crossing_ends_off > 0) {
    // Not tried as curves: an end off one of the two surfaces is not on their
    // crossing, and no fit can pass through it. Within a grid's band they used
    // to be tried, missed by the band, and counted as falling back below (open
    // item 39, R15's pattern); where they are is the corner placement's
    // question, which reports the corners off their faces itself (item 25).
    LOG(
      "STEP export: %1$d edge%2$s where two declared surfaces meet %3$s written as chords: an end of "
      "each is off one of the surfaces, and no curve of theirs passes through it",
      int(crossing_ends_off), crossing_ends_off == 1 ? "" : "s", crossing_ends_off == 1 ? "is" : "are");
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
      "%4$.2e/%5$.2e/%6$.2e min/med/max of both surfaces against the file's resolution of %7$.2e",
      int(arc_refused), arc_refused == 1 ? "" : "s", int(arc_no_fit), lo, med, hi, resolution());
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
  if (sections_declared + sections_fitted + sections_pencil > 0) {
    // Where each section's plane came from, because it is the difference
    // between a curve that is exact and one that is only as good as the mesh
    // it was fitted to. A declared plane cannot move; a fitted one moves with
    // the tessellation, with what the boolean left behind, and with the
    // tolerance the fit was given - and the section's centre and both its
    // semi-axes are differences taken against that plane.
    // And of the declared, how many stand on a facet named for the plane, and
    // how many on one named for nothing whose corners lie on it - a derivation
    // (D1), not the maker's word (open item 39, R13).
    LOG(
      "STEP export: %1$d plane section%2$s written as the conic it is - %3$d on a plane the "
      "model declared (%4$d named for it, %5$d on it by position alone, which derives the plane - "
      "D1), %6$d on one taken from the mesh, %7$d on one the pencil of the two surfaces it is "
      "the crossing of states (D1)",
      sections_declared + sections_fitted + sections_pencil,
      sections_declared + sections_fitted + sections_pencil == 1 ? "" : "s", sections_declared,
      sections_declared - sections_derived, sections_derived, sections_fitted, sections_pencil);
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

void brep::BodyBuild::checkBrep()
{
  if (!analytic) return;
  brep::Body& body = brep_body;
  const brep::ClassTally t = brep::stateClasses(body);
  const std::vector<std::string> real = brep::check(body);
  const std::vector<std::string> misattributed = brep::checkAttribution(body);
  const std::vector<std::string> off_curves = brep::checkVerticesOnCurves(body, resolution());
  using recognition::EvidenceClass;
  auto n = [](const std::array<std::size_t, 6>& by, EvidenceClass c) { return int(by[std::size_t(c)]); };
  LOG(
    "STEP export: the B-rep as written - %1$d vertices, %2$d edges, %3$d faces in %4$d shells; vertices "
    "by what "
    "decided them %5$d D0, %6$d D1, %7$d D4 - %8$d off a face they bound, %9$d on one nothing can ask; "
    "edges "
    "%10$d D1, %11$d D2, %12$d D4 - %13$d carried off a face they bound, %14$d on one nothing can ask "
    "(open item "
    "39)",
    int(body.vertices.size()), int(body.edges.size()), int(body.faces.size()), int(body.shells.size()),
    n(t.vertices, EvidenceClass::D0), n(t.vertices, EvidenceClass::D1), n(t.vertices, EvidenceClass::D4),
    int(t.vertices_off), int(t.vertices_unasked), n(t.edges, EvidenceClass::D1),
    n(t.edges, EvidenceClass::D2), n(t.edges, EvidenceClass::D4), int(t.edges_off),
    int(t.edges_unasked));
  if (!real.empty()) {
    LOG(message_group::Export_Error,
        "STEP export: the B-rep as written breaks its own contract in %1$d place%2$s (brep::check) - "
        "first: %3$s",
        int(real.size()), real.size() == 1 ? "" : "s", real.front().c_str());
    for (std::size_t i = 1; i < real.size() && i < 6; i++) LOG("STEP export:   %1$s", real[i].c_str());
  }
  // Apart from the topology: a face can close perfectly on the wrong surface,
  // and which surface it is on is identity, the worst thing to get wrong (open
  // item 39, R10).
  if (!misattributed.empty()) {
    LOG(message_group::Export_Error,
        "STEP export: %1$d face%2$s written on a surface that is not the record %3$s for, nor "
        "derived from it (brep::checkAttribution) - first: %4$s",
        int(misattributed.size()), misattributed.size() == 1 ? " is" : "s are",
        misattributed.size() == 1 ? "it stands" : "they stand", misattributed.front().c_str());
    for (std::size_t i = 1; i < misattributed.size() && i < 6; i++)
      LOG("STEP export:   %1$s", misattributed[i].c_str());
  }
  // And every vertex on the curve of each edge it ends, to the uncertainty the
  // file declares: the one relation a reader rejects a substituted curve on. It
  // was an EXPORT-WARNING in checkAndReport, which a systemic defect is not
  // (open item 39, R12).
  if (!off_curves.empty()) {
    LOG(message_group::Export_Error,
        "STEP export: %1$d edge end%2$s off the curve %3$s edge is written as, against the %4$.2e the "
        "file declares as its uncertainty (brep::checkVerticesOnCurves) - a reader calls it \"vertex "
        "does not lie on edge's curve\"; first: %5$s",
        int(off_curves.size()), off_curves.size() == 1 ? " is" : "s are",
        off_curves.size() == 1 ? "its" : "their", resolution(), off_curves.front().c_str());
    for (std::size_t i = 1; i < off_curves.size() && i < 6; i++)
      LOG("STEP export:   %1$s", off_curves[i].c_str());
  }
}
