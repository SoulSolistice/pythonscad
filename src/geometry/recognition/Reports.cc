/*! Reports: every line recognition prints about itself, as a query over the
 * ledger and what recognition handed the writer.
 *
 * doc/step-recognition-design.md: "Contention is then `claims_on[f].size() >
 * 1`, computed once, reported once, by the component whose job it is." These
 * lived inside StepKernel::build_tri_body, between the corner placement and the
 * face writer; they read and decide nothing, so they are here, one function per
 * question, each with the inputs it reads spelled out.
 */
#include "geometry/recognition/Reports.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "geometry/Surface.h"
#include "utils/printutils.h"

namespace recognition {

void dumpLedger(const ReportInputs& in)
{
  [[maybe_unused]] const AnalyticFeatures::Result& features = in.features;
  [[maybe_unused]] const std::vector<std::shared_ptr<Surface>>& surfaces = in.surfaces;
  [[maybe_unused]] const std::vector<Vector3d>& vertices = *in.mesh.vertices;
  [[maybe_unused]] const std::vector<std::vector<int>>& loops = *in.mesh.loops;
  [[maybe_unused]] const std::vector<char>& loop_valid = *in.mesh.valid;
  [[maybe_unused]] const std::vector<char>& loop_is_hole = *in.mesh.is_hole;
  [[maybe_unused]] const std::vector<int32_t>& faceOrigin = in.face_origin;
  [[maybe_unused]] const std::map<int32_t, std::vector<std::size_t>>& owned = in.owned;
  // The ledger as data, for any tool to read.
  //
  // Every report line above and below is a summary of the ledger, and a summary
  // can only answer the question it was written for. With OPENSCAD_STEP_LEDGER
  // set to a path, every claim is written there as one tab-separated row, so a
  // question nobody wrote a line for - "which facets does the exact tier write
  // on a quadric that the approximation tier leaves planar?" - is a join of two
  // files rather than an instrumented build. Facet indices are the merged mesh's
  // and are the same in both tiers: nothing moves a facet before recognition
  // has finished. Changes nothing unless the variable is set.
  if (const char *ledger_path = getenv("OPENSCAD_STEP_LEDGER")) {
    std::ofstream out(ledger_path);
    AnalyticFeatures::Provenance prov;
    prov.face_origin = &faceOrigin;
    prov.owned = &owned;
    prov.declared = surfaces.size();
    auto fate_name = [](AnalyticFeatures::ClaimFate fate) {
      switch (fate) {
      case AnalyticFeatures::ClaimFate::Honoured:  return "honoured";
      case AnalyticFeatures::ClaimFate::Withdrawn: return "withdrawn";
      case AnalyticFeatures::ClaimFate::Held:      return "held";
      case AnalyticFeatures::ClaimFate::Declined:  return "declined";
      default:                                     return "refused";
      }
    };
    auto kind_of = [](const Surface *sf) {
      return sf == nullptr                                             ? "none"
             : dynamic_cast<const CylinderSurface *>(sf) != nullptr    ? "cylinder"
             : dynamic_cast<const ConeSurface *>(sf) != nullptr        ? "cone"
             : dynamic_cast<const SphereSurface *>(sf) != nullptr      ? "sphere"
             : dynamic_cast<const TorusSurface *>(sf) != nullptr       ? "torus"
             : dynamic_cast<const GridSurface *>(sf) != nullptr        ? "sweep"
             : dynamic_cast<const BezierPatchSurface *>(sf) != nullptr ? "bezier"
             : dynamic_cast<const PlaneSurface *>(sf) != nullptr       ? "plane"
                                                                       : "surface";
    };
    out << "facet\torigin\tconsumed_by\tby\tsurface\tkind\tdeclared\tprecision\tevidence\tfate\t"
           "maker_owns\tmaker_declared\tx\ty\tz\twhy\n";
    // And the surfaces the rows name, with their parameters and who declared
    // them - so "which part of the model is this" is a lookup, not a hunt.
    {
      std::ofstream sout(std::string(ledger_path) + ".surfaces.tsv");
      sout << "surface\tkind\trefx\trefy\trefz\tdirx\tdiry\tdirz\tr\tslope\towners\twitnesses\n";
      for (std::size_t i = 0; i < surfaces.size(); i++) {
        const Surface *sf = surfaces[i].get();
        if (sf == nullptr) continue;
        double r = 0, slope = 0;
        if (const auto *cyl = dynamic_cast<const CylinderSurface *>(sf)) r = cyl->r;
        if (const auto *cone = dynamic_cast<const ConeSurface *>(sf)) {
          r = cone->r;
          slope = cone->slope;
        }
        if (const auto *sph = dynamic_cast<const SphereSurface *>(sf)) r = sph->r;
        auto ids = [](const std::vector<int32_t>& v) {
          std::string s;
          for (const int32_t id : v) s += (s.empty() ? "" : ",") + std::to_string(int(id));
          return s.empty() ? std::string("-") : s;
        };
        sout << i << '\t' << kind_of(sf) << '\t' << sf->refpt[0] << '\t' << sf->refpt[1] << '\t'
             << sf->refpt[2] << '\t' << sf->normdir[0] << '\t' << sf->normdir[1] << '\t'
             << sf->normdir[2] << '\t' << r << '\t' << slope << '\t' << ids(sf->owners) << '\t'
             << ids(sf->witnesses) << '\n';
      }
    }
    for (const auto& c : features.ledger.all()) {
      const bool declared = c.surface_index < surfaces.size();
      // yes / no / silent: provenance is a gate that says nothing on a fitted
      // surface or where the maker's ownership is too thin to state.
      const char *owns = !declared ? "silent" : prov.excludes(c.facet, c.surface_index) ? "no" : "yes";
      if (declared && c.facet < faceOrigin.size() &&
          (faceOrigin[c.facet] < 0 || owned.find(faceOrigin[c.facet]) == owned.end())) {
        owns = "silent";
      }
      // The same question asked of identity: was this surface stamped with the
      // facet's own original when that original was minted? See Surface::owners.
      const char *stated = "silent";
      // Per geometric surface: a wall declared as two records is one wall.
      if (declared && c.facet < faceOrigin.size() && faceOrigin[c.facet] >= 0 &&
          surfaces[c.surface_index] != nullptr) {
        bool any = false, mine = false;
        for (const auto& other : surfaces) {
          if (other == nullptr || !describesSameSurface(*other, *surfaces[c.surface_index])) continue;
          any = any || !other->owners.empty();
          mine =
            mine || std::binary_search(other->owners.begin(), other->owners.end(), faceOrigin[c.facet]);
        }
        if (any) stated = mine ? "yes" : "no";
      }
      const char *holder =
        c.facet < features.consumed_by.size() && features.consumed_by[c.facet] != nullptr
          ? features.consumed_by[c.facet]
          : "-";
      out << c.facet << '\t' << (c.facet < faceOrigin.size() ? faceOrigin[c.facet] : -1) << '\t'
          << holder << '\t' << (c.by != nullptr ? c.by : "-") << '\t'
          << (c.surface_index == std::size_t(-1) ? -1 : long(c.surface_index)) << '\t'
          << kind_of(c.surface.get()) << '\t' << (declared ? 1 : 0) << '\t' << int(c.precision) << "\tD"
          << (c.evidence == AnalyticFeatures::EvidenceClass::Unstated
                ? std::string("?")
                : std::to_string(int(c.evidence) - 1))
          << '\t' << fate_name(c.fate) << '\t' << owns << '\t' << stated << '\t';
      Vector3d centre(0, 0, 0);
      if (c.facet < loops.size() && !loops[c.facet].empty()) {
        for (const int v : loops[c.facet]) centre += vertices[v];
        centre /= double(loops[c.facet].size());
      }
      out << centre[0] << '\t' << centre[1] << '\t' << centre[2] << '\t'
          << (c.why != nullptr ? c.why : "-") << '\n';
    }
    LOG("STEP export: the claim ledger - %1$d claims - written to %2$s",
        int(features.ledger.all().size()), ledger_path);
  }
}

void reportRegions(const ReportInputs& in)
{
  if (in.regions == nullptr || in.regions->empty()) return;
  const std::vector<std::vector<int>>& loops = *in.mesh.loops;
  const ClaimLedger& ledger = in.features.ledger;
  // What holds a facet, as the file will write it: the honoured claimant and
  // the surface it names - or nothing, and the tessellation writes the facet.
  auto holder_of = [&](std::size_t f, std::size_t surface) -> std::string {
    const Claim *holder = ledger.firstClaimant(f);
    if (holder == nullptr || holder->by == nullptr) return "the tessellation";
    if (holder->surface_index != surface) return std::string(holder->by) + " on another surface";
    return holder->by;
  };
  std::set<std::size_t> surfaces_seen;
  std::size_t facets = 0, whole = 0, split = 0, inner_edges = 0, faceted = 0, elsewhere = 0;
  std::size_t filtered = 0;
  std::vector<std::string> first_split;
  for (const Region& region : *in.regions) {
    surfaces_seen.insert(region.surface_index);
    facets += region.facets.size();
    std::map<std::string, std::size_t> by_holder;
    for (const std::size_t f : region.facets) {
      const std::string h = holder_of(f, region.surface_index);
      by_holder[h]++;
      if (h == "the tessellation") faceted++;
      else if (h.size() > 19 && h.compare(h.size() - 19, 19, " on another surface") == 0) elsewhere++;
      // A trimmed quadric proposes every D0 facet of a cylinder or cone it can
      // see; one it did not propose as D0 was refused by one of OnQuadric's own
      // tests - the lean or the interior - which are the tessellation's.
      const Surface *s =
        region.surface_index < in.surfaces.size() ? in.surfaces[region.surface_index].get() : nullptr;
      if (dynamic_cast<const CylinderSurface *>(s) != nullptr ||
          dynamic_cast<const ConeSurface *>(s) != nullptr) {
        bool proposed = false;
        for (const Claim *c : ledger.claimants(f)) {
          if (c->by != nullptr && std::string(c->by) == kQuadricFace.by &&
              c->surface_index == region.surface_index && c->evidence == EvidenceClass::D0) {
            proposed = true;
          }
        }
        if (!proposed) filtered++;
      }
    }
    // Mesh edges inside the region with a differently-held facet on each side:
    // each is a boundary the file will have and the model does not.
    std::size_t edges = 0;
    std::map<EdgeKey, std::vector<std::size_t>> sides;
    for (const std::size_t f : region.facets) {
      const std::vector<int>& loop = loops[f];
      for (std::size_t i = 0; i < loop.size(); i++) {
        sides[edgeKey(loop[i], loop[(i + 1) % loop.size()])].push_back(f);
      }
    }
    for (const auto& kv : sides) {
      if (kv.second.size() != 2) continue;
      if (holder_of(kv.second[0], region.surface_index) !=
          holder_of(kv.second[1], region.surface_index)) {
        edges++;
      }
    }
    if (by_holder.size() == 1 && by_holder.begin()->first != "the tessellation" &&
        by_holder.begin()->first.find(" on another surface") == std::string::npos) {
      whole++;
      continue;
    }
    split++;
    inner_edges += edges;
    if (first_split.size() < 3) {
      std::string parts;
      for (const auto& kv : by_holder) {
        parts += (parts.empty() ? "" : ", ") + std::to_string(kv.second) + " to " + kv.first;
      }
      first_split.push_back("surface " + std::to_string(region.surface_index) + ", " +
                            std::to_string(region.facets.size()) + " facets: " + parts + "; " +
                            std::to_string(edges) + " edges inside it between differently-held facets");
    }
  }
  LOG(
    "STEP export: D0 regions, by declaration alone - %1$d regions of %2$d declared surfaces over %3$d "
    "facets: %4$d written whole by one kind of face on their own surface, %5$d split on a boundary the "
    "model does not have (%6$d mesh edges inside a region between differently-held facets); %7$d of "
    "their facets left to the tessellation, %8$d held on a different surface",
    int(in.regions->size()), int(surfaces_seen.size()), int(facets), int(whole), int(split),
    int(inner_edges), int(faceted), int(elsewhere));
  for (const std::string& line : first_split) LOG("STEP export:   split: %1$s", line.c_str());
  if (filtered > 0) {
    LOG(
      "STEP export: %1$d facets of a declared cylinder or cone are D0 and the trimmed-quadric pass did "
      "not "
      "propose them as such - refused by its lean or interior test, which are the tessellation's",
      int(filtered));
  }
}

void reportArbitration(const ReportInputs& in)
{
  [[maybe_unused]] const AnalyticFeatures::Result& features = in.features;
  [[maybe_unused]] const std::vector<std::shared_ptr<Surface>>& surfaces = in.surfaces;
  [[maybe_unused]] const std::vector<Vector3d>& vertices = *in.mesh.vertices;
  [[maybe_unused]] const std::vector<std::vector<int>>& loops = *in.mesh.loops;
  [[maybe_unused]] const std::vector<char>& loop_valid = *in.mesh.valid;
  [[maybe_unused]] const std::vector<char>& loop_is_hole = *in.mesh.is_hole;
  [[maybe_unused]] const std::vector<int32_t>& faceOrigin = in.face_origin;
  [[maybe_unused]] const std::map<int32_t, std::vector<std::size_t>>& owned = in.owned;
  // The shadow check (doc/step-recognition-design.md, step 2).
  //
  // The ledger records every claim and decides nothing; `consumed` still
  // decides everything. So the ledger's answer *under the policy in force* -
  // whoever claimed first - must be the holder `consumed_by` recorded. Where
  // they disagree, the ledger does not yet describe what the exporter does, and
  // nothing may be built on it until it does. Loud, because it is the net that
  // makes flipping the source of truth safe later.
  //
  // Facets taken by a pass that does not record a claim yet - the smooth
  // regions - have no `consumed_by` and are skipped rather than counted as
  // disagreements.
  {
    std::size_t checked = 0, disagreed = 0, overclaimed = 0;
    const char *first_bad = nullptr, *first_over = nullptr;
    for (std::size_t f = 0; f < features.consumed_by.size(); f++) {
      const char *holder = features.consumed_by[f];
      if (holder == nullptr) {
        // The other direction, and the one a one-sided check cannot see: a
        // facet nothing holds must have no live claimant either. A pass records
        // its claim while walking facets, but only takes them if the patch they
        // land in turns out to be a sheet - so a claim on a facet of a patch
        // that died is a claim the exporter never honoured.
        const AnalyticFeatures::Claim *live = features.ledger.firstClaimant(f);
        if (live != nullptr && f < features.consumed.size() && features.consumed[f] == 0) {
          overclaimed++;
          if (first_over == nullptr) first_over = live->by != nullptr ? live->by : "something";
        }
        continue;
      }
      checked++;
      const AnalyticFeatures::Claim *first = features.ledger.firstClaimant(f);
      const char *by = first != nullptr ? first->by : nullptr;
      if (by != nullptr && std::string(by) == holder) continue;
      disagreed++;
      if (first_bad == nullptr) first_bad = by != nullptr ? by : "nothing";
    }
    if (overclaimed > 0) {
      LOG(message_group::Export_Error,
          "STEP export: the claim ledger holds %1$d facets nothing actually took - the first by "
          "%2$s. A claim the exporter never honoured; see doc/step-recognition-design.md",
          int(overclaimed), first_over);
    }
    if (disagreed > 0) {
      LOG(message_group::Export_Error,
          "STEP export: the claim ledger disagrees with what holds %1$d of %2$d facets - the "
          "first says %3$s where the exporter recorded a different holder. The ledger does not "
          "yet describe what this exporter does; see doc/step-recognition-design.md",
          int(disagreed), int(checked), first_bad);
    } else if (checked > 0) {
      AnalyticFeatures::ClaimPrecision lost = AnalyticFeatures::ClaimPrecision::Fitted;
      const std::size_t misheld = features.ledger.misheld(&lost);
      const char *kind =
        lost == AnalyticFeatures::ClaimPrecision::DeclaredExact
          ? "a declared exact surface"
          : (lost == AnalyticFeatures::ClaimPrecision::DeclaredInterpolant ? "a declared interpolant"
                                                                           : "a fit");
      // How much of the contention the *model* settles, as against how much a
      // ranking of evidence would have to.
      //
      // Provenance says which original made a facet and which surfaces that
      // original owns. That is identity, not measurement: it needs no
      // tolerance, and it does not care what order the passes run in. So the
      // strategic question - whether arbitrating facet ownership is a matter of
      // asking the model, or an endless tuning of match orders - is answered by
      // how many ties it decides outright.
      //
      // It is a gate and is deliberately silent twice over: on a fitted surface
      // nothing declared, and on an original whose ownership is too thin to
      // state. Silence is counted separately, because mistaking it for
      // agreement is exactly how a threshold gets adopted by accident.
      AnalyticFeatures::Provenance prov;
      prov.face_origin = &faceOrigin;
      prov.owned = &owned;
      prov.declared = surfaces.size();
      std::size_t prov_decides = 0, prov_silent = 0, prov_unnamed = 0, prov_same = 0;
      for (std::size_t f = 0; f < features.consumed_by.size(); f++) {
        const auto claims = features.ledger.claimants(f);
        if (claims.size() < 2) continue;
        const AnalyticFeatures::Claim *holder = nullptr;
        for (const auto *c : claims) {
          if (c->honoured()) {
            holder = c;
            break;
          }
        }
        if (holder == nullptr) continue;
        bool named = holder->surface_index != std::size_t(-1);
        bool split = false;
        for (const auto *c : claims) {
          if (c == holder || c->honoured()) continue;
          if (c->surface_index == std::size_t(-1)) {
            named = false;
            continue;
          }
          if (prov.excludes(f, c->surface_index) != prov.excludes(f, holder->surface_index)) {
            split = true;
          }
        }
        // Are the rivals even rivals? A band's wall and a quadric face can name
        // the *same* declared surface - one original owning a cylinder that two
        // passes each recognise - in which case there is nothing to arbitrate
        // and the contention is an artefact of recognising it twice.
        bool same_surface = false;
        for (const auto *c : claims) {
          if (c == holder || c->honoured()) continue;
          if (c->surface_index != std::size_t(-1) && c->surface_index == holder->surface_index) {
            same_surface = true;
          }
        }
        if (same_surface) prov_same++;
        else if (!named) prov_unnamed++;
        else if (split) prov_decides++;
        else prov_silent++;
      }
      // Is one declared surface held by more than one kind of recogniser?
      //
      // This is the question a count of contention cannot reach. Two passes
      // covering one surface is harmless if one of them ends up with all of it.
      // It is not harmless if the surface is *split* between them: the same
      // cylinder written partly as a band's wall and partly as a quadric patch
      // has a boundary between the two that the model never asked for, and
      // which half goes to which is decided by the order the passes run in.
      // Neither holder is wrong about the surface, and neither is right about
      // the face.
      {
        std::map<std::size_t, std::set<std::string>> holders_of;
        std::map<std::size_t, std::size_t> facets_of;
        for (std::size_t f = 0; f < features.consumed_by.size(); f++) {
          const AnalyticFeatures::Claim *holder = features.ledger.firstClaimant(f);
          if (holder == nullptr || holder->by == nullptr) continue;
          if (holder->surface_index == std::size_t(-1)) continue;
          holders_of[holder->surface_index].insert(holder->by);
          facets_of[holder->surface_index]++;
        }
        std::size_t split_surfaces = 0, split_facets = 0;
        for (const auto& kv : holders_of) {
          if (kv.second.size() < 2) continue;
          split_surfaces++;
          split_facets += facets_of[kv.first];
        }
        if (split_surfaces > 0) {
          LOG(message_group::Export_Warning,
              "STEP export: %1$d declared surface%2$s split between different kinds of "
              "recogniser, over %3$d facets - one surface written as two kinds of face, with the "
              "division decided by the order the passes run in",
              int(split_surfaces), split_surfaces == 1 ? " is" : "s are", int(split_facets));
          for (const auto& kv : holders_of) {
            if (kv.second.size() < 2) continue;
            std::string who;
            for (const auto& name : kv.second) who += (who.empty() ? "" : " and ") + name;
            // `surfaces` is what the model declared; anything past it was fitted
            // by the approximation pass and this cannot name it.
            const char *kind = kv.first >= surfaces.size() ? "a fitted surface" : "a surface";
            if (kv.first < surfaces.size() && surfaces[kv.first] != nullptr) {
              const Surface *sf = surfaces[kv.first].get();
              kind = dynamic_cast<const CylinderSurface *>(sf) ? "a cylinder"
                     : dynamic_cast<const ConeSurface *>(sf)   ? "a cone"
                     : dynamic_cast<const GridSurface *>(sf)   ? "a declared sweep"
                     : dynamic_cast<const PlaneSurface *>(sf)  ? "a plane"
                                                               : "a surface";
            }
            // Is the division gratuitous? A cone a boolean left in two
            // disconnected pieces legitimately needs two faces. It is only a
            // defect where facets held by *different* kinds touch: one
            // connected region of one declared surface, cut in two by nothing
            // but the order the passes ran in.
            std::map<std::pair<int, int>, std::vector<std::string>> across;
            for (std::size_t f = 0; f < features.consumed_by.size(); f++) {
              const AnalyticFeatures::Claim *h = features.ledger.firstClaimant(f);
              if (h == nullptr || h->by == nullptr || h->surface_index != kv.first) continue;
              if (f >= loops.size() || !loop_valid[f]) continue;
              const std::vector<int>& loop = loops[f];
              for (std::size_t i = 0; i < loop.size(); i++) {
                const int a = loop[i], b = loop[(i + 1) % loop.size()];
                across[{std::min(a, b), std::max(a, b)}].push_back(h->by);
              }
            }
            std::map<std::string, std::size_t> share;
            for (std::size_t f = 0; f < features.consumed_by.size(); f++) {
              const AnalyticFeatures::Claim *h = features.ledger.firstClaimant(f);
              if (h == nullptr || h->by == nullptr || h->surface_index != kv.first) continue;
              share[h->by]++;
            }
            std::string division;
            for (const auto& sv : share) {
              division += (division.empty() ? "" : ", ") + std::to_string(sv.second) + " to " + sv.first;
            }
            // What the model says about the pieces. If the facets both kinds hold
            // come from the same original, and that original declared this
            // surface, then the region is not ambiguous at all - it is one
            // region of one declared surface, and only the *representation* is
            // in question. That is the difference between "who owns these
            // facets" and "how is this surface written".
            std::map<std::string, std::set<int32_t>> origins;
            for (std::size_t f = 0; f < features.consumed_by.size(); f++) {
              const AnalyticFeatures::Claim *h = features.ledger.firstClaimant(f);
              if (h == nullptr || h->by == nullptr || h->surface_index != kv.first) continue;
              if (f < faceOrigin.size()) origins[h->by].insert(faceOrigin[f]);
            }
            std::string by_origin;
            for (const auto& ov : origins) {
              std::string ids;
              for (const int32_t id : ov.second) {
                ids += (ids.empty() ? "" : "/") + std::to_string(int(id));
              }
              by_origin += (by_origin.empty() ? "" : "; ") + ov.first + " from original " + ids;
            }
            std::size_t touching = 0;
            for (const auto& e : across) {
              if (e.second.size() != 2) continue;
              if (e.second[0] != e.second[1]) touching++;
            }
            LOG(message_group::Export_Warning,
                "STEP export:   %1$s, surface %2$d, %3$d facets (%7$s) - %5$d mesh edges have a "
                "differently-held facet on each side%6$s - %8$s",
                kind, int(kv.first), int(facets_of[kv.first]), who, int(touching),
                touching > 0 ? ", so this is one connected region cut in two by pass order"
                             : ", so the pieces are disconnected and two faces are correct",
                division, by_origin);
          }
        }
      }
      // Which recognisers are meeting, and on what. A count of contention says
      // nothing about whether it matters; the pair does. Two passes covering
      // one declared surface is overlapping *capability* - benign if the face
      // they would write is the same, a real question if it is not.
      std::map<std::string, std::size_t> pairs;
      for (std::size_t f = 0; f < features.consumed_by.size(); f++) {
        const auto claims = features.ledger.claimants(f);
        if (claims.size() < 2) continue;
        const AnalyticFeatures::Claim *holder = nullptr;
        for (const auto *c : claims) {
          if (c->honoured()) {
            holder = c;
            break;
          }
        }
        if (holder == nullptr || holder->by == nullptr) continue;
        for (const auto *c : claims) {
          if (c == holder || c->honoured() || c->by == nullptr) continue;
          const bool same =
            c->surface_index != std::size_t(-1) && c->surface_index == holder->surface_index;
          pairs[std::string(holder->by) + " holds, " + c->by + " also covered" +
                (same ? " (the same surface)" : " (a different surface)")]++;
        }
      }
      for (const auto& kv : pairs) {
        LOG("STEP export:   contention: %1$s - %2$d facets", kv.first, int(kv.second));
      }
      LOG(
        "STEP export: of the contested facets, %1$d name the *same* declared surface from two "
        "recognisers - nothing to arbitrate, the surface was recognised twice; provenance "
        "separates the claimants on %2$d, is silent on %3$d, and %4$d have a claimant that "
        "cannot name its declared surface",
        int(prov_same), int(prov_decides), int(prov_silent), int(prov_unnamed));
      LOG(
        "STEP export: the claim ledger agrees with the holder of all %1$d claimed facets; %2$d "
        "are contested by more than one recogniser, and %3$d of those are held by a claimant less "
        "precise than one that also covered them (the best that lost is %4$s), and %5$d are a tie "
        "between recognisers of equal precision, which nothing decides but the order they run in",
        int(checked), int(features.ledger.contested()), int(misheld), kind, int(features.ledger.tied()));
    }
  }
}

void reportDeclarationCensus(const ReportInputs& in, bool analytic, std::vector<char>& decl_written,
                             std::vector<unsigned char>& facet_class)
{
  [[maybe_unused]] const AnalyticFeatures::Result& features = in.features;
  [[maybe_unused]] const std::vector<std::shared_ptr<Surface>>& surfaces = in.surfaces;
  [[maybe_unused]] const std::vector<Vector3d>& vertices = *in.mesh.vertices;
  [[maybe_unused]] const std::vector<std::vector<int>>& loops = *in.mesh.loops;
  [[maybe_unused]] const std::vector<char>& loop_valid = *in.mesh.valid;
  [[maybe_unused]] const std::vector<char>& loop_is_hole = *in.mesh.is_hole;
  [[maybe_unused]] const std::vector<int32_t>& faceOrigin = in.face_origin;
  [[maybe_unused]] const std::map<int32_t, std::vector<std::size_t>>& owned = in.owned;
  // ---- the declaration census ------------------------------------------
  //
  // *Is everything that could be D0 actually D0?* The declarations are the D0
  // feedstock and they are already in hand, so the question is answerable
  // directly rather than inferred from what came out. This reports it and
  // decides nothing.
  //
  // The rule it serves is doc/step-export-development.md, *a declaration that
  // is not taken is a bug report*: a declaration takes precedence over fitting
  // and approximation through the whole consumer chain, and where one is not
  // taken the reason has to be found rather than worked around. That doctrine
  // had no measurement until this line.
  //
  // **Four fates, and a count that stops at two reports the channel working as
  // a list of defects.**
  //
  //   written    a face stands on it.
  //   evidence   something read it to identify a surface and no face stands on
  //              it. `hull()` of two coaxial cylinders declares the two
  //              cylinders and nothing declares the cone between them, so the
  //              band pass matches *both rims*: those two have done exactly
  //              their job and will never be a face.
  //   removed    unused, and no facet of the mesh comes from a maker that owns
  //              it - the booleans took the geometry away. step-bored-cylinder
  //              declares the bore's two end discs and `difference()` consumes
  //              them; there is nothing left to write and nothing to explain.
  //   unused     unused, and facets of the mesh *do* belong to it. That one is
  //              the bug report.
  //
  // **A fifth category was tried and refuted, 2026-09-22.** lid10's original 67
  // owns cylinder 39 *and* cylinder 43 over the same 120 facets, and original 40
  // owns cylinder 9 and cylinder 56 the same way, which looks exactly like one
  // surface declared twice with the twin unwritable. `Surface::sameAs` says no
  // on every pair, on every model: they are a ring's two walls, distinct
  // surfaces with equal facet counts. The category fired zero times and was
  // removed rather than landed - a bucket nothing exercises is a deleted check
  // that still costs a reader a paragraph. The pairs are genuine bug reports.
  //
  // The third category is decided by provenance, which is identity rather than
  // measurement: the generator says which original made each facet and which
  // surfaces that original owns, so "is any of this surface still in the mesh"
  // needs no tolerance and no distance test.
  //
  // What counts as written is what the exporter *finally holds*: a facet with
  // `consumed` set and the claim that holds it. A claim made and then given
  // back - a dropped band, a pass's own refusal - is not a face, and the ledger
  // models both, which is what the shadow check established.
  if (analytic && !surfaces.empty()) {
    for (std::size_t f = 0; f < features.consumed.size(); f++) {
      if (features.consumed[f] == 0) continue;
      for (const auto *claim : features.ledger.claimants(f)) {
        if (!claim->honoured()) continue;
        if (claim->surface_index < decl_written.size()) {
          decl_written[claim->surface_index] = 1;
          if (f < facet_class.size()) facet_class[f] = 2;
        } else if (f < facet_class.size()) {
          // Past `surfaces` is a surface the approximation fitted. A facet held
          // under one is D3 ground however exact the mesh under it happens to
          // be, and if a declaration owned it then a measurement has displaced
          // a declaration.
          facet_class[f] = 3;
        }
        break;
      }
    }
    // Which declarations still have mesh on them at all - and, of that mesh,
    // how much is already held by a claim naming something else.
    //
    // **`consumed` does not mean consumed by the most exact claimant**, and
    // that is the foundation under all of this. Once a facet is taken, every
    // later pass is blind to it: there is no losing claim to count, so
    // `misheld` reads zero and the ledger records no contest. A declaration can
    // therefore come out unexplained not because nothing could write it but
    // because something *else* got its facets first - which is a different
    // defect with a different fix, and reporting the two as one sends the
    // reader after the wrong one.
    //
    // The denominator is coarse and is not a coverage figure: `owned` maps an
    // *original* to the surfaces it owns, so an original owning two surfaces
    // counts all its facets for both. It is used only to ask a categorical
    // question - is any of this declaration's mesh held under another name -
    // which coarseness cannot turn from no into yes.
    std::vector<char> present(surfaces.size(), 0);
    std::vector<std::size_t> held_elsewhere(surfaces.size(), 0);
    for (std::size_t f = 0; f < faceOrigin.size() && f < loops.size(); f++) {
      if (!loop_valid[f]) continue;
      const auto it = owned.find(faceOrigin[f]);
      if (it == owned.end()) continue;
      std::size_t holder = std::size_t(-1);
      if (f < features.consumed.size() && features.consumed[f] != 0) {
        for (const auto *claim : features.ledger.claimants(f)) {
          if (!claim->honoured()) continue;
          holder = claim->surface_index;
          break;
        }
      }
      for (const std::size_t si : it->second) {
        if (si >= present.size()) continue;
        present[si] = 1;
        if (holder != std::size_t(-1) && holder != si) held_elsewhere[si]++;
      }
    }
    struct Tally {
      std::size_t total = 0, written = 0, evidence = 0, removed = 0, idle = 0, taken = 0;
    };
    Tally curved, flat;
    std::vector<std::string> to_explain;
    for (std::size_t i = 0; i < surfaces.size(); i++) {
      const Surface *sf = surfaces[i].get();
      if (sf == nullptr) continue;
      const bool is_plane = dynamic_cast<const PlaneSurface *>(sf) != nullptr;
      Tally& t = is_plane ? flat : curved;
      t.total++;
      if (decl_written[i] != 0) {
        t.written++;
        continue;
      }
      if (i < features.evidence.size() && features.evidence[i] != 0) {
        t.evidence++;
        continue;
      }
      if (present[i] == 0) {
        t.removed++;
        continue;
      }
      t.idle++;
      if (held_elsewhere[i] > 0) t.taken++;
      if (to_explain.size() < 12) {
        const char *kind = dynamic_cast<const CylinderSurface *>(sf)      ? "cylinder"
                           : dynamic_cast<const ConeSurface *>(sf)        ? "cone"
                           : dynamic_cast<const SphereSurface *>(sf)      ? "sphere"
                           : dynamic_cast<const TorusSurface *>(sf)       ? "torus"
                           : dynamic_cast<const GridSurface *>(sf)        ? "sweep"
                           : dynamic_cast<const BezierPatchSurface *>(sf) ? "patch"
                                                                          : "plane";
        to_explain.push_back(std::string(kind) + " " + std::to_string(int(i)));
      }
    }
    auto say = [&](const char *what, const Tally& t) {
      if (t.total == 0) return;
      LOG(
        "STEP export:   %1$d declared %2$s: %3$d written as a face, %4$d read to identify a "
        "surface without one standing on them, %5$d with no mesh left on them, %6$d unexplained",
        int(t.total), what, int(t.written), int(t.evidence), int(t.removed), int(t.idle));
    };
    LOG(
      "STEP export: the declaration census - is everything that could be exact written as "
      "exact?");
    say("surfaces", curved);
    say("planes", flat);
    const std::size_t idle = curved.idle + flat.idle;
    const std::size_t taken = curved.taken + flat.taken;
    if (idle > 0) {
      std::string names;
      for (const auto& n : to_explain) names += (names.empty() ? "" : ", ") + n;
      LOG(message_group::Export_Warning,
          "STEP export:   %1$d declaration%2$s has mesh on it and no face written on it, and "
          "nothing read it: %3$s%4$s - a declaration that is not taken is a bug report",
          int(idle), idle == 1 ? "" : "s", names.c_str(), idle > to_explain.size() ? ", ..." : "");
      // Which of the two defects each one is. A facet already taken is
      // invisible to every later pass, so the declaration's own recogniser
      // never got to propose and nothing recorded a contest.
      LOG(message_group::Export_Warning,
          "STEP export:   of those, %1$d have mesh held under another surface's name - their "
          "facets were consumed before the declaration's own recogniser could claim them, so no "
          "losing claim exists to count - and %2$d have mesh nothing claimed at all",
          int(taken), int(idle - taken));
    }
    // And the caution that belongs beside the word *written*: it means a face
    // stands on the declaration, **not** that the declaration holds everything
    // it should. A declaration holding one facet of a hundred reads as written
    // here. That is why the per-facet tally below exists beside it, and why it
    // is the one to read when the question is *how much of this model is
    // exact*.

    // ---- the same question per facet, which is where it cannot hide --------
    //
    // A per-declaration answer has two blind spots and the second is the worse.
    // It cannot see a declaration that kept one facet of a hundred. And it
    // cannot see this: **a fitted surface consumes a region that was really a
    // combination of exact primitives, the fit leaves a few facets behind, and
    // those facets are now attributable to nothing.** The declarations that
    // should have covered them had their ground taken, so their recognisers
    // never proposed and no losing claim exists; the leftovers are orphans with
    // no route back. Once a facet is consumed every later pass is blind to it,
    // so nothing downstream can notice, and the only place it is visible is
    // here, by asking what class wrote each facet.
    //
    // That is the whole reason the rung order has to be followed rather than
    // admired: D0 is covered first precisely so that a D3 fit never gets the
    // chance to take exact ground and strand the remainder.
    {
      std::size_t by_plane = 0, by_declared = 0, by_fit = 0, by_mesh = 0, unnamed = 0;
      for (std::size_t f = 0; f < facet_class.size() && f < loops.size(); f++) {
        if (!loop_valid[f] || loop_is_hole[f]) continue;
        switch (facet_class[f]) {
        case 1:  by_plane++; break;
        case 2:  by_declared++; break;
        case 3:  by_fit++; break;
        case 4:  unnamed++; break;
        default: by_mesh++; break;
        }
      }
      LOG(
        "STEP export:   by facet: %1$d written on a declared surface, %2$d on a declared plane, "
        "%3$d on a surface the approximation fitted, %4$d left to the tessellation",
        int(by_declared), int(by_plane), int(by_fit), int(by_mesh));
      // Loud, because every number on the line above is only as good as the
      // claims behind it. A claim the exporter *acted on* that cannot say which
      // surface it was for leaves the census unable to tell a declaration from
      // a fit, and an unreliable number is worse than none when rungs are being
      // designed against it. Measured unreachable before this was made an
      // error: step-sphere and step-sphere-closed had two facets each - their
      // polar caps - until the band's cap claims were given their zone.
      if (unnamed > 0) {
        LOG(message_group::Export_Error,
            "STEP export:   %1$d facets are held by an honoured claim that cannot name its "
            "surface, so the census above cannot tell a declaration from a fit on them - the "
            "claim is malformed; see AnalyticFeatures::Claim::surface_index",
            int(unnamed));
      }
      // **Why is a facet below what a declaration could give it?** The question
      // that always has to be answered wherever a less precise rung holds
      // ground: a wrong classification, a missing declaration, or ground
      // already consumed by a misfitting claimant. Until 2026-09-23 this could
      // not be asked - `owned` names an original's surfaces rather than a
      // facet's, and answers "all of them" - and the ledger held only what
      // passes lost to an earlier holder. Now every pass records every claim it
      // would make with its fate (AnalyticFeatures::ClaimFate), so a declared
      // claim on a fitted or tessellated facet *is* the evidence that a
      // declaration was available there, and its fate is the reason it is not
      // written on it. This reads the catalogue; it measures nothing.
      //
      // Branches, taking a facet's most telling declared claim in this order:
      //   declined   the claimant covered it and its own rule refused it -
      //              a classification or a representation that could not hold
      //              the region, and the rule is named
      //   refused    proposed as a region, and the writer would not write it
      //   held       a declared claimant covered it and something less precise
      //              already had it - ground consumed by a misfitting claimant
      //   withdrawn  a declared claimant took it and gave it back
      //   none       no recogniser named a declaration for it at all: a missing
      //              declaration, or ground nothing could declare. Split by
      //              whether its maker declared anything, which is identity.
      {
        using AnalyticFeatures::ClaimFate;
        std::size_t below = 0, declined = 0, refused = 0, held = 0, withdrawn = 0, none = 0,
                    none_undeclaring = 0;
        std::map<std::string, std::size_t> reasons;
        for (std::size_t f = 0; f < facet_class.size() && f < loops.size(); f++) {
          if (!loop_valid[f] || loop_is_hole[f]) continue;
          if (facet_class[f] != 0 && facet_class[f] != 3) continue;
          below++;
          const AnalyticFeatures::Claim *best = nullptr;
          auto rank = [](ClaimFate fate) {
            switch (fate) {
            case ClaimFate::Declined:  return 0;
            case ClaimFate::Refused:   return 1;
            case ClaimFate::Held:      return 2;
            case ClaimFate::Withdrawn: return 3;
            default:                   return 4;
            }
          };
          for (const auto *claim : features.ledger.claimants(f)) {
            if (claim->honoured() || claim->surface_index >= surfaces.size()) continue;
            if (best == nullptr || rank(claim->fate) < rank(best->fate)) best = claim;
          }
          if (best == nullptr) {
            none++;
            const bool declares = f < faceOrigin.size() && owned.count(faceOrigin[f]) != 0 &&
                                  !owned.at(faceOrigin[f]).empty();
            if (!declares) none_undeclaring++;
            continue;
          }
          switch (best->fate) {
          case ClaimFate::Declined: declined++; break;
          case ClaimFate::Refused:  refused++; break;
          case ClaimFate::Held:     held++; break;
          default:                  withdrawn++; break;
          }
          const char *fate_name = best->fate == ClaimFate::Declined  ? "declined"
                                  : best->fate == ClaimFate::Refused ? "refused"
                                  : best->fate == ClaimFate::Held    ? "held elsewhere"
                                                                     : "withdrawn";
          reasons[std::string(fate_name) + " by " + (best->by != nullptr ? best->by : "something") +
                  ": " + (best->why != nullptr ? best->why : "no reason recorded")]++;
        }
        if (below > 0) {
          LOG(
            "STEP export:   why %1$d facets are fitted or left to the tessellation: %2$d were claimed "
            "for a declaration and declined by the claimant's own rule, %3$d refused by the writer, "
            "%4$d held first by something less precise, %5$d withdrawn, and %6$d were claimed for no "
            "declaration at all (%7$d of those from a maker that declares nothing)",
            int(below), int(declined), int(refused), int(held), int(withdrawn), int(none),
            int(none_undeclaring));
          std::vector<std::pair<std::size_t, std::string>> ranked;
          for (const auto& kv : reasons) ranked.emplace_back(kv.second, kv.first);
          std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
          });
          for (std::size_t i = 0; i < ranked.size() && i < 10; i++) {
            LOG("STEP export:     %1$d %2$s", int(ranked[i].first), ranked[i].second);
          }
        }
      }
      // **And what decided each written facet.** A claim's precision is the
      // surface it names; its evidence class is what put the facet on it, and a
      // rung is as exact as its evidence (doc/step-rungs.md). Counted, not yet
      // refused - refusing the mismatch is the handover's step 5 - but a claim
      // that states no class at all is malformed and is an error now, as a
      // claim that cannot name its surface is.
      {
        using AnalyticFeatures::EvidenceClass;
        std::size_t by_class[6] = {0, 0, 0, 0, 0, 0};
        std::size_t declared_on_mesh = 0;
        for (std::size_t f = 0; f < features.consumed.size(); f++) {
          if (features.consumed[f] == 0) continue;
          const AnalyticFeatures::Claim *holder = features.ledger.firstClaimant(f);
          if (holder == nullptr) continue;
          by_class[std::size_t(holder->evidence)]++;
          if (holder->precision == AnalyticFeatures::ClaimPrecision::DeclaredExact &&
              (holder->evidence == EvidenceClass::D3 || holder->evidence == EvidenceClass::D4)) {
            declared_on_mesh++;
          }
        }
        LOG(
          "STEP export:   what decided each written facet: %1$d D0, %2$d D1, %3$d D2, %4$d D3, %5$d D4 "
          "- "
          "%6$d of them on a declared exact surface by evidence from the tessellation",
          int(by_class[1]), int(by_class[2]), int(by_class[3]), int(by_class[4]), int(by_class[5]),
          int(declared_on_mesh));
        if (by_class[0] > 0) {
          LOG(message_group::Export_Error,
              "STEP export:   %1$d facets are held by a claim that states no evidence class - the claim "
              "is malformed; see AnalyticFeatures::EvidenceClass",
              int(by_class[0]));
        }
      }
    }
  }
}

}  // namespace recognition
