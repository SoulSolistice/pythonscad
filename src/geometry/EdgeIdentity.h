#pragma once

/*! Which maker's edge each edge of a 2D outline is - the 2D channel's identity.
 *
 * The 3D channel learned on 2026-09-24 that ownership inferred from positions
 * fails silently where two surfaces pass through the same points, and that
 * identity carried from the generator does not (ManifoldUtils::mintOriginals).
 * The 2D channel had the same gap one level down: whether an outline edge is a
 * straight segment or a chord of a recorded circle was *measured*, by asking
 * whether both its ends lie on the circle - and a D-shaft's flat has both ends
 * on its circle and is not a chord, while the cut chords beside it have one end
 * off it and are. Only the maker knows, so the maker says (doc/step-export-wip.md,
 * *Inferred where a declaration was possible*, item 2).
 *
 * Every edge a generator makes gets an id, unique for the life of the process,
 * and the polygon keeps what each id is (EdgeDecl). A 2D boolean or offset runs
 * through Clipper, which knows nothing of either; so each vertex rides through
 * it with its two edges' ids packed into Clipper's per-vertex Z, a crossing Clipper
 * makes is named by the two edges that cross there (the Z callback), and on the
 * way out each edge of the result is the one edge its two ends both name. Where
 * that is not exactly one edge - Clipper simplified a vertex away, merged two
 * collinear edges, or the vertex came from a producer that names nothing - the
 * edge is unknown, and the classifier measures it as it always did. Identity
 * only ever replaces a measurement; it never adds a guess.
 */

#include <cstddef>
#include <cstdint>
#include <unordered_map>

/*! What a profile edge is. */
enum class EdgeKind {
  Line,    //!< a straight segment
  Arc,     //!< a chord of a circle - see EdgeDecl::record
  Bezier,  //!< a chord of a Bezier segment - see EdgeDecl::record
};

/*! One maker's edge, unique for the life of the process; 0 is "not known". At
 * most 2^31 - 1, so that two fit in Clipper's signed 64-bit Z. */
using EdgeId = std::uint32_t;

/*! What a maker declared an edge to be. */
struct EdgeDecl {
  EdgeKind kind = EdgeKind::Line;
  /*! The record the edge is a chord of, as an index into its polygon's `arcs`
   * or `beziers`; -1 where it is known to be curved and the curve has no record
   * - a circle a shear has made an ellipse, whose chords are still not lines. */
  int record = -1;
};

using EdgeDecls = std::unordered_map<EdgeId, EdgeDecl>;

/*! `n` fresh ids, consecutive; returns the first. Thread-safe. */
EdgeId reserveEdgeIds(std::size_t n);

/*! A vertex's two edges, as Clipper's Z carries them. */
std::int64_t packEdges(EdgeId a, EdgeId b);

/*! The one edge two vertices both name - the edge between them - or 0 where
 * they name none or more than one. */
EdgeId commonEdge(std::int64_t za, std::int64_t zb);
