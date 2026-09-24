#pragma once

/*! The claim ledger: what every recogniser wanted, and what became of it.
 *
 * Part of the recognition module - see doc/step-recognition-design.md. The
 * ledger is data, not control flow: recognisers write claims into it, one
 * component arbitrates, and every report is a query over it. Nothing here knows
 * about STEP or about any particular recogniser.
 */

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Surface;

namespace recognition {

/*! How precise a claim on a facet is - the same idea the corner arbiter's
 * `kCornerTiers` applies to a corner's constraints, one granularity up.
 *
 * A declared exact surface states where it is algebraically; a declared
 * interpolant was fitted through the model's own stations and publishes a band;
 * a fit is the approximation pass's own guess. Ordered, lower is more precise,
 * and this is what an arbiter will compare when ownership stops being decided
 * by which pass ran first. See doc/step-recognition-design.md. */
enum class ClaimPrecision { DeclaredExact, DeclaredInterpolant, Fitted };

/*! What *decided* that this facet belongs to this surface - the class of the
 * claim's evidence, which is a different thing from the precision of the
 * surface it names.
 *
 * doc/step-rungs.md: *a rung is as exact as its evidence*. A claim on a
 * declared cone is DeclaredExact by precision whatever decided it, and the R0
 * attempt of 2026-09-22 was exactly that - a declared surface in the sentence
 * and the tessellation band deciding. Carrying the two apart is what lets the
 * mismatch be counted, and later refused (the handover's step 5).
 *
 * `Unstated` comes first on purpose: a claim site that forgets to fill this in
 * value-initialises to it, and reads as unstated rather than as D0. */
enum class EvidenceClass {
  Unstated,
  D0,  // declarations alone: a declared point against its surface, float equality
  D1,  // the model's stations through an interpolant that publishes a band
  D2,  // fitted to declarations at a stated tolerance
  D3,  // fitted to the mesh
  D4,  // the tessellation itself: a band, a sagitta, a statistic over facets
};

/*! What became of a claim. Every claim a pass would make is recorded exactly
 * once, at the point its fate is decided, so that the ledger is the catalogue
 * of what was *wanted* and not only of what was taken.
 *
 * doc/step-recognition-design.md, migration step 1, extended from "what a pass
 * lost" to every claim a pass would make. The reason this matters, and has
 * cost weeks: `consumed` does not mean consumed by the most precise claimant;
 * a facet held even by a D0 claimant may be held by the wrong D0; and a wrong
 * holder can strand facets nothing is then able to allocate. None of that is
 * visible unless the claims that did *not* win are on the record. */
enum class ClaimFate {
  Honoured,   // the exporter acted on it: the facet is written on this surface
  Withdrawn,  // honoured, then given back - a band dropped at its rims
  Held,       // covered, but something already held the facet when this pass looked
  Declined,   // covered, and the claimant's own rule refused it (`why` names the rule)
  Refused,    // proposed as a region, and the writer would not write that region
};

/*! One recogniser's claim on one facet, with the evidence for it.
 *
 * `by` and `why` are string literals owned by the recogniser, never freed. */
struct Claim {
  std::size_t facet = 0;
  std::shared_ptr<const Surface> surface;
  ClaimPrecision precision = ClaimPrecision::Fitted;
  const char *by = nullptr;
  const char *why = nullptr;
  /*! What became of it.
   *
   * A claim that lost is still recorded - that record is the point - but it is
   * not a candidate for who holds the facet. Inferring this from *where* the
   * claim was recorded does not work: a pass abandons a region after covering
   * it, in several places, and each one is a way to record a claim nothing
   * honours. Measured: 288 such on lid10 before this was a field. */
  ClaimFate fate = ClaimFate::Held;
  /*! Which declared surface this claim is for, as an index into the surface
   * list the recognisers were given - `std::size_t(-1)` where the claimant
   * cannot name one.
   *
   * Needed to ask `Provenance` anything: it answers per (facet, surface index),
   * and a claim that cannot name its index cannot be arbitrated by the model's
   * own statement of who owns what. */
  std::size_t surface_index = std::size_t(-1);
  /*! The class of what decided membership: the *weakest* class in the set of
   * constraints that determined it. See EvidenceClass.
   *
   * One label, and deliberately only the first of three things determinacy
   * ranks by (doc/step-recognition-design.md, *Determinacy*): an entity is as
   * precise as the independent closed forms that fix it - the weakest class the
   * determining set needs, then the freedom it leaves, then independent
   * corroboration. The last two need the constraint set itself and an
   * algebraic independence test, and arrive with the arbiter (migration step
   * 4). A count of surface names would not do: a surface declared twice, or two
   * coaxial ones, fix no more than one. */
  EvidenceClass evidence = EvidenceClass::Unstated;

  /*! Whether the exporter acted on it - the one question `consumed` answers. */
  [[nodiscard]] bool honoured() const { return fate == ClaimFate::Honoured; }
};

/*! Every claim every recogniser made, in the order they were made.
 *
 * The point of recording a claim that *lost* is that today it leaves no trace
 * at all: whoever runs first takes a facet and it is invisible to everyone
 * after, so "why is this facet a plane?" has to be reconstructed with an
 * instrumented build. With the ledger, contention is `claimants(f).size() > 1`
 * - computed once, by the component whose job it is.
 *
 * The ledger decides nothing yet. `consumed` still does. `firstClaimant` exists
 * so that the ledger's answer under the *current* policy can be held against
 * what `consumed` produced, which is the shadow check that makes it safe to
 * flip the source of truth later. */
class ClaimLedger
{
public:
  void add(const Claim& claim)
  {
    by_facet_[claim.facet].push_back(claims_.size());
    claims_.push_back(claim);
  }
  /*! A claimant gave the facet back.
   *
   * A band that is dropped puts its facets back, and something else then takes
   * them. Without this the ledger names a holder that had already let go - the
   * shadow check caught exactly that on lid10 (162 facets) and the bayonet (42)
   * the first time it ran. The claims stay in `all()`, because what was claimed
   * and withdrawn is part of the record; they simply stop being candidates. */
  void release(std::size_t facet)
  {
    const auto it = by_facet_.find(facet);
    if (it == by_facet_.end()) return;
    for (const std::size_t i : it->second) {
      if (claims_[i].fate == ClaimFate::Honoured) claims_[i].fate = ClaimFate::Withdrawn;
    }
  }
  /*! Whether `by` already has a claim on this facet for this surface, in any
   * fate. A pass that tries the same facet more than once - the band pass walks
   * a strip from every seed on it - records one claim, not one per attempt. */
  [[nodiscard]] bool has(std::size_t facet, const char *by, std::size_t surface_index) const
  {
    const auto it = by_facet_.find(facet);
    if (it == by_facet_.end() || by == nullptr) return false;
    for (const std::size_t i : it->second) {
      const Claim& c = claims_[i];
      if (c.surface_index == surface_index && c.by != nullptr && std::string(c.by) == by) return true;
    }
    return false;
  }
  [[nodiscard]] std::vector<const Claim *> claimants(std::size_t facet) const
  {
    std::vector<const Claim *> out;
    const auto it = by_facet_.find(facet);
    if (it == by_facet_.end()) return out;
    out.reserve(it->second.size());
    for (const std::size_t i : it->second) out.push_back(&claims_[i]);
    return out;
  }
  /*! Who would hold the facet under the policy in force today: whoever claimed
   * it first. Null where nothing claimed it. */
  [[nodiscard]] const Claim *firstClaimant(std::size_t facet) const
  {
    const auto it = by_facet_.find(facet);
    if (it == by_facet_.end()) return nullptr;
    for (const std::size_t i : it->second) {
      if (claims_[i].honoured()) return &claims_[i];
    }
    return nullptr;
  }
  /*! Facets held by a claimant less precise than another that also covered
   * them.
   *
   * This, and not `contested`, is what deciding by call order costs. A facet
   * two recognisers want is no defect at all if the more precise one has it;
   * the defect is a declared exact surface losing to a fit, or to an
   * interpolant, because it happened to run second. Each one of these is a face
   * written from a worse surface than the model provides.
   *
   * `worst_loser`, when given, receives what the most precise unhonoured
   * claimant was, so a report can name the kind of loss rather than count it. */
  [[nodiscard]] std::size_t misheld(ClaimPrecision *best_lost = nullptr) const
  {
    std::size_t n = 0;
    for (const auto& kv : by_facet_) {
      const Claim *holder = nullptr;
      const Claim *best = nullptr;
      for (const std::size_t i : kv.second) {
        const Claim& c = claims_[i];
        if (c.honoured() && holder == nullptr) holder = &c;
        if (best == nullptr || c.precision < best->precision) best = &c;
      }
      if (holder == nullptr || best == nullptr) continue;
      if (best->precision >= holder->precision) continue;
      n++;
      if (best_lost != nullptr && (n == 1 || best->precision < *best_lost)) {
        *best_lost = best->precision;
      }
    }
    return n;
  }
  /*! Facets whose best claimants are of *equal* precision, and different
   * recognisers.
   *
   * `misheld` catches a holder beaten on precision. It says nothing about a
   * tie, and a tie is not harmless: two claimants equally precise by this
   * ranking are still separated by call order, and nothing anywhere states
   * which of them is right. On lid10 the ties are the bulk of the contention -
   * a band's wall against a quadric face, both declared exact - so a zero from
   * `misheld` must never be read as "the correct recogniser took it". It means
   * only that no strictly better claimant lost. */
  [[nodiscard]] std::size_t tied() const
  {
    std::size_t n = 0;
    for (const auto& kv : by_facet_) {
      const Claim *holder = nullptr;
      for (const std::size_t i : kv.second) {
        if (claims_[i].honoured()) {
          holder = &claims_[i];
          break;
        }
      }
      if (holder == nullptr) continue;
      for (const std::size_t i : kv.second) {
        const Claim& c = claims_[i];
        if (&c == holder || c.honoured()) continue;
        if (c.precision != holder->precision) continue;
        if (c.by != nullptr && holder->by != nullptr && std::string(c.by) == holder->by) continue;
        n++;
        break;
      }
    }
    return n;
  }
  [[nodiscard]] std::size_t contested() const
  {
    std::size_t n = 0;
    for (const auto& kv : by_facet_) {
      if (kv.second.size() > 1) n++;
    }
    return n;
  }
  [[nodiscard]] const std::vector<Claim>& all() const { return claims_; }

private:
  std::vector<Claim> claims_;
  std::map<std::size_t, std::vector<std::size_t>> by_facet_;
};

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

/*! A band's wall: every corner on the declared surface to 1e-7 and the rims
 * matched to it at 1e-7, no band or statistic - D0 on a declaration. Which
 * facets a band takes is still what a ring can express (R2, D4); its declines
 * are the record of that. */
inline constexpr Claimant kBandWall{"a band's wall", ClaimPrecision::DeclaredExact, EvidenceClass::D0,
                                    EvidenceClass::D3};
/*! A band's polar cap, absorbed because the gap it spans is under one ring step
 * of the tessellation - the tessellation deciding, however exact the sphere. */
inline constexpr Claimant kBandCap{"a band's cap", ClaimPrecision::DeclaredExact, EvidenceClass::D4,
                                   EvidenceClass::D4};
/*! A declared Bezier patch: every corner on the model's own net - D1. */
inline constexpr Claimant kBezierPatch{"a Bezier patch", ClaimPrecision::DeclaredInterpolant,
                                       EvidenceClass::D1, EvidenceClass::D3};
/*! A declared sweep: the model's stations, interpolated, band published - D1. */
inline constexpr Claimant kDeclaredSweep{"a declared sweep", ClaimPrecision::DeclaredInterpolant,
                                         EvidenceClass::D1, EvidenceClass::D3};
/*! A trimmed quadric. D4 by default - the tessellation band admits a facet, or
 * nothing declared who made it - and D0 facet by facet where its maker owns the
 * surface (identity) and every corner is on it at machine precision; the
 * proposal says which (QuadricProposal::decided). */
inline constexpr Claimant kQuadricFace{"a quadric face", ClaimPrecision::DeclaredExact,
                                       EvidenceClass::D4, EvidenceClass::D4};

/*! Record one claim by `who` on `facet` for the surface at `surface_index`,
 * whose first `declared_count` entries are the model's own. The one rule for
 * what precision and evidence class a claim carries - every site that records a
 * claim, honoured or lost, goes through it.
 *
 * `decided` is the class of what decided this facet's membership, where the
 * claimant knows it facet by facet; left `Unstated`, the claimant's own stated
 * class applies. A fitted surface's claim takes the claimant's fitted class
 * whatever is passed - nothing declared it, so nothing about it is D0. */
inline void recordClaim(ClaimLedger& ledger, const Claimant& who, std::size_t facet,
                        const std::shared_ptr<const Surface>& surface, std::size_t surface_index,
                        std::size_t declared_count, ClaimFate fate, const char *why,
                        EvidenceClass decided = EvidenceClass::Unstated)
{
  const bool declared = surface_index < declared_count;
  const EvidenceClass evidence = !declared                            ? who.evidence_if_fitted
                                 : decided != EvidenceClass::Unstated ? decided
                                                                      : who.evidence_if_declared;
  ledger.add({facet, surface, declared ? who.if_declared : ClaimPrecision::Fitted, who.by, why, fate,
              surface_index, evidence});
}

}  // namespace recognition
