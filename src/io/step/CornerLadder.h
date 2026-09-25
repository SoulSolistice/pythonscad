#pragma once

/*! The corner placement's two precision tables: which rung's candidate wins a
 * tie, and how much each kind of constraint on a corner is worth. Reordering
 * either is editing its table and nothing else. */

#include <cstddef>
#include <limits>

namespace step {

/*! The rungs of the corner placement, and the precision they rank at.
 *
 * Every rung that places a corner used to *claim* it: whichever ran first held
 * the corner, and every later rung skipped it. So the ladder's order was its
 * precision ranking, fixed in control flow, and the rungs written first - the
 * least precise, because they came before the exact ones existed - kept
 * winning. On lid10 the provenance rung held two corners on the curve two
 * surfaces share, while the point all three of their faces meet was one solve
 * away; on step-exact-trim, 32 corners reachable by two rungs went to whichever
 * ran first.
 *
 * Now a rung only *proposes*, and the arbiter in the file-faces pass decides.
 * It measures each candidate against every constraint the file asserts at the
 * corner - each analytic face's surface and each plane written there - and
 * takes the one satisfying the most, to the ladder's exactness floor of 1e-9.
 * That measurement is what enforces precision, and a table cannot overrule it:
 * a candidate on three of the corner's faces beats one on two, whatever its
 * rank.
 *
 * The rank below decides only between candidates that satisfy equally many -
 * two answers equally exact by the file's own standard. Reordering the ladder
 * is editing this table and nothing else. Lower wins.
 */
enum class CornerRung : int {
  FileFaces,   // every face the file writes at the corner, solved together
  Provenance,  // the two surfaces provenance names for the corner's originals
  MeshTriple,  // a declared surface and two of the mesh's facet planes
  MeshConic,   // a declared surface and one of the mesh's facet planes
  Mesh,        // where the mesh put it: no move at all
};
struct CornerRungSpec {
  CornerRung rung;
  int rank;
  const char *name;
};
constexpr CornerRungSpec kCornerRungs[] = {
  {CornerRung::FileFaces, 0, "every face the file writes there"},
  {CornerRung::Provenance, 1, "the two surfaces provenance names"},
  {CornerRung::MeshTriple, 2, "a declared surface and two mesh planes"},
  {CornerRung::MeshConic, 3, "a declared surface and one mesh plane"},
  {CornerRung::Mesh, 4, "where the mesh put it"},
};
inline int cornerRungRank(CornerRung rung)
{
  for (const auto& spec : kCornerRungs) {
    if (spec.rung == rung) return spec.rank;
  }
  return std::numeric_limits<int>::max();
}

/*! The constraints on a corner, and the precision each kind ranks at.
 *
 * Not all constraints are worth the same, so the arbiter does not count them:
 * it compares candidates tier by tier, from the most precise down, and a single
 * constraint of a higher tier outweighs any number of a lower one. That is what
 * lets it tell a declared cylinder from the chord a bore facet lies on - both
 * pass through the same corner, and only one is the model's statement.
 *
 *   DeclaredFace     an exact declaration at the corner: the surface of an
 *                    analytic face the model declared as a quadric or a closed-
 *                    form sweep, and a plane the model declared, whether across
 *                    that face's boundary or as a face written at the corner
 *   DeclaredInterpolant  a declare_grid sweep or a Bezier patch: declared, but
 *                    interpolated through the model's stations, publishing a
 *                    band - exactness takes precedence over it
 *   FittedFace       the surface of an analytic face the approximation fitted.
 *                    Less precise than a declaration, but the file asserts it:
 *                    a corner off it is a vertex off its own face's surface
 *   ProvenanceOwner  a declared surface the corner's makers put there, whether
 *                    or not a face of it is written at the corner - only owners
 *                    that contributed a facet at the corner are passed on, which
 *                    is the filter for provenance's known false owners. A hint
 *                    about the model, and below anything the file writes: ranked
 *                    above the fitted faces it took lid10 60 corners off them
 *   MeshPlane        the plane of a facet written as itself: the tessellation's
 *                    chord unless something above says otherwise, and a face the
 *                    corner placement is allowed to bend, since the writer fans it
 *
 * Reordering the tiers is editing this table and nothing else. Lower wins.
 */
enum class ConstraintTier : int {
  DeclaredFace,
  DeclaredInterpolant,
  FittedFace,
  ProvenanceOwner,
  MeshPlane
};
constexpr std::size_t kConstraintTierCount = 5;
struct ConstraintTierSpec {
  ConstraintTier tier;
  std::size_t rank;  // an index into the score, 0 the most precise
  bool hard;         // leaving it, where the corner was on it, disqualifies a candidate
  bool used;         // whether it enters the arbiter at all
};
// Three columns, because precision, safety and relevance are different
// questions. `rank` is where a kind of constraint stands by precision; `hard`
// says a candidate that takes the corner off such a constraint, when the corner
// was on it, is not a candidate at all; `used` says whether it enters the
// arbiter in the first place.
//
// MeshPlane is not used. A mesh plane is the tessellation's chord: it says
// nothing about where a point of the model is, and the writer fans a planar
// face whose corners the placement bends. Each other setting was measured and
// each was wrong:
//   - hard, as it was before the arbiter: corners held on chords, 0.096 inside
//     the declared bore on step-declare-grid-scad;
//   - ranked level with a declaration: two chord planes outvote the sweep, and
//     lid10 leaves 61 corners off their faces;
//   - ranked last but used: a far root on a vertex line of the bore lies on
//     *both* facet planes beside it, so it outscores the near one, and
//     step-declare-grid-scad moved a corner 1.9 onto its neighbour's point.
// Every defect met while mesh planes were used traced back to that scoring: the
// shells opened along edges between two corners the solver had put on one root,
// 1.4e-12 to 1.9 apart in the mesh, and a fan could not cut a ring with a side
// of no length. Unused, none of that occurs (open item 23).
constexpr ConstraintTierSpec kCornerTiers[kConstraintTierCount] = {
  {ConstraintTier::DeclaredFace, 0, false, true}, {ConstraintTier::DeclaredInterpolant, 1, false, true},
  {ConstraintTier::FittedFace, 2, false, true},   {ConstraintTier::ProvenanceOwner, 3, false, true},
  {ConstraintTier::MeshPlane, 4, false, false},
};
inline std::size_t tierRank(ConstraintTier tier)
{
  for (const auto& spec : kCornerTiers) {
    if (spec.tier == tier) return spec.rank;
  }
  return kConstraintTierCount - 1;
}
inline bool tierUsed(ConstraintTier tier)
{
  for (const auto& spec : kCornerTiers) {
    if (spec.tier == tier) return spec.used;
  }
  return false;
}
inline bool tierHard(ConstraintTier tier)
{
  for (const auto& spec : kCornerTiers) {
    if (spec.tier == tier) return spec.hard;
  }
  return false;
}

}  // namespace step
