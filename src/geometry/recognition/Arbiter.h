#pragma once

/*! The arbiter: the one place a facet becomes held.
 *
 * doc/step-recognition-design.md, *Arbitrate*: "One component takes the ledger
 * and assigns each facet an owner ... recording the winner *and the losers*."
 * Before this, three stages each set `consumed`, `consumed_by` and an honoured
 * claim by hand, with their own copies of which precision and which evidence
 * class a claim carries. Now a stage says what became of a region and the
 * arbiter records it - and a region it honours takes its facets.
 *
 * The *policy* - who may be granted what - is still today's: pass order, then
 * surface order, whatever an earlier honoured grant holds is held. That is
 * migration step 3's precondition, reproduced exactly; changing it is step 4,
 * and it will be changed here and nowhere else.
 */

#include <cstddef>
#include <map>
#include <tuple>
#include <vector>

#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Ledger.h"

namespace recognition {

class Arbiter
{
public:
  /*! Arbitrate over `state` - its `consumed`, `consumed_by` and ledger - for a
   * surface list whose first `declared_count` entries are the model's own. */
  Arbiter(AnalyticFeatures::Result& state, std::size_t declared_count)
    : state_(state), declared_count_(declared_count)
  {
  }

  /*! What the policy in force holds: every facet with an honoured claim.
   *
   * Read from the ledger, not from `consumed` - migration step 3, *flip the
   * source of truth*. `consumed` is kept as a cache the writer reads, and
   * `cacheAgrees` says whether it still says the same thing; the shadow check
   * proved it did on every export before the flip. */
  [[nodiscard]] std::vector<char> held() const
  {
    std::vector<char> out(state_.consumed.size(), 0);
    for (const Claim& claim : state_.ledger.all()) {
      if (claim.honoured() && claim.facet < out.size()) out[claim.facet] = 1;
    }
    return out;
  }

  /*! Whether the `consumed` cache still holds exactly what the ledger does. */
  [[nodiscard]] bool cacheAgrees() const { return held() == state_.consumed; }

  /*! What decided that `facet` belongs to the surface at `surface_index`, as
   * `who` proposed it - the class of its membership, stated by the proposal
   * and read by every claim recorded for it here. Pure input: a proposal's,
   * set before any grant. It is also what the policy will rank on once the
   * grant stops being call order (migration step 4): D0 ground first. */
  void stateMembership(const Claimant& who, std::size_t surface_index, std::size_t facet,
                       EvidenceClass decided)
  {
    membership_[std::make_tuple(who.by, surface_index, facet)] = decided;
  }

  /*! Record what became of `region`, claimed by `who` for `surface_index`.
   * Honoured, it takes its facets; any other fate is recorded and takes
   * nothing. Every facet of the region gets the claim, with the class of what
   * decided its membership where the proposal stated one. */
  void settle(const Claimant& who, const AnalyticFeatures::Patch& region, std::size_t surface_index,
              ClaimFate fate, const char *why)
  {
    if (fate == ClaimFate::Honoured) {
      for (const std::size_t f : region.facets) {
        state_.consumed[f] = 1;
        state_.consumed_by[f] = who.by;
      }
    }
    for (const std::size_t f : region.facets) {
      const auto it = membership_.find(std::make_tuple(who.by, surface_index, f));
      recordClaim(state_.ledger, who, f, region.surface, surface_index, declared_count_, fate, why,
                  it != membership_.end() ? it->second : EvidenceClass::Unstated);
    }
  }

private:
  AnalyticFeatures::Result& state_;
  std::size_t declared_count_;
  std::map<std::tuple<const char *, std::size_t, std::size_t>, EvidenceClass> membership_;
};

}  // namespace recognition
