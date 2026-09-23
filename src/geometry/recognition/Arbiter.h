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
#include <vector>

#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Ledger.h"

namespace recognition {

/*! Who claims, as the ledger names it, and what decides its membership.
 *
 * Precision is the surface's (a declaration or a fit); evidence is what put
 * the facet on it - see EvidenceClass. They are stated once per recogniser
 * here rather than at every site that records a claim. */
struct Claimant {
  const char *by;
  ClaimPrecision if_declared;          // DeclaredExact or DeclaredInterpolant
  EvidenceClass evidence_if_declared;  // what decides membership on a declaration
  EvidenceClass evidence_if_fitted;    // ... and on a surface the approximation fitted
};

/*! A declared Bezier patch: every corner on the model's own net - D1. */
inline constexpr Claimant kBezierPatch{"a Bezier patch", ClaimPrecision::DeclaredInterpolant,
                                       EvidenceClass::D1, EvidenceClass::D3};
/*! A declared sweep: the model's stations, interpolated, band published - D1. */
inline constexpr Claimant kDeclaredSweep{"a declared sweep", ClaimPrecision::DeclaredInterpolant,
                                         EvidenceClass::D1, EvidenceClass::D3};
/*! A trimmed quadric: OnQuadric::covers, which the tessellation band and the
 * facet's sagitta decide even at max_off = 1e-7 - D4, declared or not. */
inline constexpr Claimant kQuadricFace{"a quadric face", ClaimPrecision::DeclaredExact,
                                       EvidenceClass::D4, EvidenceClass::D4};

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

  /*! Record what became of `region`, claimed by `who` for `surface_index`.
   * Honoured, it takes its facets; any other fate is recorded and takes
   * nothing. Every facet of the region gets the claim. */
  void settle(const Claimant& who, const AnalyticFeatures::Patch& region, std::size_t surface_index,
              ClaimFate fate, const char *why)
  {
    const bool declared = surface_index < declared_count_;
    if (fate == ClaimFate::Honoured) {
      for (const std::size_t f : region.facets) {
        state_.consumed[f] = 1;
        state_.consumed_by[f] = who.by;
      }
    }
    for (const std::size_t f : region.facets) {
      state_.ledger.add({f, region.surface, declared ? who.if_declared : ClaimPrecision::Fitted, who.by,
                         why, fate, surface_index,
                         declared ? who.evidence_if_declared : who.evidence_if_fitted});
    }
  }

private:
  AnalyticFeatures::Result& state_;
  std::size_t declared_count_;
};

}  // namespace recognition
