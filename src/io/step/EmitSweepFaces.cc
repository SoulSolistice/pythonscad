/*! The declared sweeps, written - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "geometry/MachinePrecision.h"
#include "io/step/CrossingArc.h"
#include "io/step/CrossingRuns.h"

using namespace step;

void StepKernel::BodyBuild::emitSweepFaces()
{
  for (const auto& patch : grid_faces) {
    const auto *grid = dynamic_cast<const GridSurface *>(patch.surface.get());
    if (grid == nullptr) continue;

    // Where to put the seam of a closed profile, which is free to choose.
    //
    // A face here is written over the rectangle 0 <= v <= 1, so its boundary
    // must not cross the seam. One that does has nowhere to go: the pcurve is a
    // straight line in (u, v), and from v = 0.0000 to v = 0.9956 it sweeps the
    // whole profile instead of stepping the 0.0044 across. A reader then repairs
    // a face whose boundary contradicts its own surface - OpenCASCADE builds a
    // seam edge nobody wrote and widens the vertices there until they merge,
    // 1.08 on lid10, and the LINE beside it comes back bounded by a point 0.7159
    // off its own curve. Open item 15.
    //
    // A boolean that buries one span of the profile leaves the seam somewhere to
    // go: written from the column after an unused span, the face's own facets
    // are contiguous in v and no boundary crosses anything. That costs no face,
    // no refused facet and no cut - the alternatives this replaces - and it is
    // exact, the surface being the same one described from another column.
    //
    // Which spans a face uses is asked of the facets' corners rather than their
    // middles: beside the seam the profile is creased, and a facet's centroid
    // projects onto the flank across the crease - 0.022 on lid10 for one whose
    // corners are at 0.98 and beyond.
    int seam_column = 0;
    const int spans = grid->closed_v ? grid->cols : grid->cols - 1;
    if (grid->closed_v && spans > 1) {
      std::vector<char> used(spans, 0);
      for (const std::size_t f : patch.facets) {
        if (f >= loops.size()) continue;
        double lo = 2, hi = -1;
        for (const int v : loops[f]) {
          double pu = 0, pv = 0;
          if (!grid->project(vertices[v], pu, pv)) continue;
          if (grid->onSeam(pu, pv)) continue;
          lo = std::min(lo, pv);
          hi = std::max(hi, pv);
        }
        if (hi < lo) continue;
        const int span = std::clamp(int(((lo + hi) / 2) * spans), 0, spans - 1);
        used[span] = 1;
      }
      for (int k = 0; k < spans; k++) {
        if (used[k] == 0) {
          seam_column = (k + 1) % grid->cols;
          break;
        }
      }
      if (seam_column != 0) reseamed++;
      else if (std::find(used.begin(), used.end(), 0) == used.end()) reseam_full++;
    }
    const double seam_shift = grid->seamShift(seam_column);
    // Every parameter this face reports is in the surface as it was *written*,
    // which is the same surface described from another column.
    auto parameters = [&](const Vector3d& p, double& pu, double& pv) {
      if (!grid->project(p, pu, pv)) return false;
      if (seam_shift > 0) {
        pv -= seam_shift;
        if (pv < 0) pv += 1.0;
        if (pv > 1) pv -= 1.0;
      }
      return true;
    };

    int du = 0, dv = 0, nu = 0, nv = 0;
    std::vector<Vector3d> ctrl;
    std::vector<double> knots_u, knots_v;
    std::vector<int> mults_u, mults_v;
    if (!grid->splineForm(du, dv, nu, nv, ctrl, knots_u, mults_u, knots_v, mults_v, seam_column)) {
      continue;
    }

    // Only the stretch of the surface this face lies on, between the corners
    // it turns along the sweep. The face was split there (formGridPatches) so
    // that no face spans one; the surface it is written on need not carry the
    // others either, and a whole rendered sweep copied onto every face made
    // the file twice the size for nothing. Exact, not a refit: an interior knot
    // of multiplicity `du` is where two clamped B-splines meet at a shared
    // control row, so the stretch is those rows with its end knots clamped,
    // and its parameters are the whole surface's - every pcurve holds as is.
    {
      std::vector<std::size_t> crease;  // indices into knots_u of full multiplicity
      for (std::size_t k = 1; k + 1 < knots_u.size(); k++) {
        if (mults_u[k] >= du) crease.push_back(k);
      }
      if (!crease.empty() && !patch.facets.empty()) {
        Vector3d centroid = Vector3d::Zero();
        for (const int v : loops[patch.facets.front()]) centroid += vertices[v];
        centroid /= double(loops[patch.facets.front()].size());
        double pu = 0, pv = 0;
        if (grid->project(centroid, pu, pv)) {
          std::size_t lo = 0, hi = knots_u.size() - 1;  // the stretch's end knots
          for (const std::size_t k : crease) {
            if (knots_u[k] <= pu) lo = k;
            else if (hi == knots_u.size() - 1) hi = k;
          }
          // Where each knot's run of the full vector starts.
          std::vector<int> first(knots_u.size(), 0);
          for (std::size_t k = 1; k < knots_u.size(); k++) first[k] = first[k - 1] + mults_u[k - 1];
          // The row an end knot interpolates: the first of its run less one, and
          // the ends of a clamped vector their own first and last rows.
          const int row0 = lo == 0 ? 0 : first[lo] - 1;
          const int row1 = hi == knots_u.size() - 1 ? nu - 1 : first[hi] - 1;
          std::vector<double> ku{knots_u[lo]};
          std::vector<int> mu{du + 1};
          for (std::size_t k = lo + 1; k < hi; k++) {
            ku.push_back(knots_u[k]);
            mu.push_back(mults_u[k]);
          }
          ku.push_back(knots_u[hi]);
          mu.push_back(du + 1);
          const int rows = row1 - row0 + 1;
          int total = 0;
          for (const int m : mu) total += m;
          // Only if the face stays on it. Where the mesh has no station at a
          // corner its facets straddle the crease - reported as they are
          // grouped - and a face reaching past the stretch keeps the whole
          // surface under it.
          const double on_crease = recognition::forwardError(16, 1.0);
          bool inside = true;
          for (const std::size_t f : patch.facets) {
            for (const int v : loops[f]) {
              double vu = 0, vv = 0;
              if (!grid->project(vertices[v], vu, vv)) continue;
              inside = inside && vu >= knots_u[lo] - on_crease && vu <= knots_u[hi] + on_crease;
            }
          }
          if (inside && rows > du && total == rows + du + 1) {
            ctrl =
              std::vector<Vector3d>(ctrl.begin() + long(row0) * nv, ctrl.begin() + long(row1 + 1) * nv);
            nu = rows;
            knots_u = std::move(ku);
            mults_u = std::move(mu);
          }
        }
      }
    }

    std::vector<std::vector<Point *>> net;
    for (int i = 0; i < nu; i++) {
      std::vector<Point *> row;
      for (int j = 0; j < nv; j++) {
        row.push_back(new Point(entities, ctrl[std::size_t(i) * nv + j]));
      }
      net.push_back(row);
    }
    auto surface = new BSplineSurface(entities, "", du, dv, net);
    surface->setKnots(knots_u, mults_u, knots_v, mults_v);

    // The boundary, one cycle at a time. Patch::Run carries them in order with
    // consecutive runs sharing an endpoint, so a cycle is its runs' vertices
    // with the joins written once.
    std::map<std::size_t, std::vector<int>> cycles;
    for (const auto& run : patch.runs) {
      std::vector<int>& cycle = cycles[run.bound];
      for (std::size_t i = 0; i + 1 < run.verts.size(); i++) cycle.push_back(run.verts[i]);
    }

    std::vector<FaceBound *> bounds;
    std::vector<EdgeCurve *> face_edges_here;
    // Which cycle is the outer bound is a question about the surface's own
    // parameters, not about the model: a hole in a face is a hole in its
    // parameter rectangle. The largest area there is the one that contains the
    // others.
    std::size_t outer = 0;
    double widest = -1.0;
    std::map<std::size_t, double> area;
    for (const auto& entry : cycles) {
      std::vector<std::pair<double, double>> uv;
      for (const int v : entry.second) {
        double pu = 0, pv = 0;
        if (!parameters(vertices[v], pu, pv)) continue;
        uv.emplace_back(pu, pv);
      }
      double twice = 0.0;
      for (std::size_t i = 0; i < uv.size(); i++) {
        const auto& a = uv[i];
        const auto& b = uv[(i + 1) % uv.size()];
        twice += a.first * b.second - b.first * a.second;
      }
      area[entry.first] = fabs(twice) / 2;
      if (area[entry.first] > widest) {
        widest = area[entry.first];
        outer = entry.first;
      }
    }

    for (const auto& entry : cycles) {
      const std::vector<int>& cycle = entry.second;
      if (cycle.size() < 3) continue;
      const std::size_t n = cycle.size();
      // From where no crossing piece straddles the start, since each is one edge.
      const std::size_t start = walkStart(cycle);
      std::vector<OrientedEdge *> loop;
      for (std::size_t step = 0; step < n;) {
        const std::size_t i = (start + step) % n;
        const int a = cycle[i], b = cycle[(i + 1) % n];
        // The edges of the file this stretch is: the chain of the crossing
        // piece it belongs to, decided once for both faces, or one mesh edge.
        struct Link {
          EdgeCurve *edge;
          bool dir;
          const CurveFit *fit;  // the curve's own fit, or null for a chord or one Bezier
        };
        std::vector<Link> links;
        std::size_t spans = 1;  // mesh edges these edges of the file cover
        const CrossingPiece *piece = pieceOf(a, b);
        if (piece != nullptr && !piece->edges.empty()) {
          bool ok = false;
          const bool forward = walksForward(*piece, cycle, i, ok);
          if (ok) {
            for (std::size_t k = 0; k < piece->edges.size(); k++) {
              const std::size_t at = forward ? k : piece->edges.size() - 1 - k;
              const CurveFit *fit = piece->fits[at].ctrl.empty() ? nullptr : &piece->fits[at];
              links.push_back({piece->edges[at], forward, fit});
            }
            spans = piece->verts.size() - 1;
          } else {
            crossing_unwalked++;
          }
        }
        if (links.empty()) {
          bool dir = true;
          EdgeCurve *edge =
            get_line_from_map(edge_map, a, b, get_vertex(a), get_vertex(b), dir, merged_edge_cnt);
          links.push_back({edge, dir, nullptr});
        }
        step += spans;
        for (const Link& link : links) {
          EdgeCurve *edge = link.edge;
          loop.push_back(new OrientedEdge(entities, edge, link.dir));
          face_edges_here.push_back(edge);
          auto attach = [&](PCurve *pcurve) {
            // An edge between two sweep faces is on both of them, and STEP
            // allows a SURFACE_CURVE to say so.
            if (auto *already = dynamic_cast<SurfaceCurve *>(edge->round)) {
              if (already->on.size() < 2) already->on.push_back(pcurve);
            } else {
              edge->round = new SurfaceCurve(entities, edge->round, {pcurve});
            }
          };
          if (link.fit != nullptr) {
            // A fitted curve across the surface, and its pcurve is that curve's
            // own interpolation carried into the sweep's parameters: the same
            // points, parameters and knots, so the two agree at every
            // interpolated point. A straight line in (u, v), which is what one
            // mesh edge is given below, is no description of a flank.
            std::vector<Eigen::Vector2d> uv;
            for (const auto& p : link.fit->points) {
              double pu = 0, pv = 0;
              if (!parameters(p, pu, pv)) break;
              uv.emplace_back(pu, pv);
            }
            const std::vector<Eigen::Vector2d> ctrl2 = uv.size() == link.fit->points.size()
                                                         ? interpolateLike(*link.fit, uv)
                                                         : std::vector<Eigen::Vector2d>{};
            if (!ctrl2.empty()) {
              std::vector<Point2d *> cp;
              for (const auto& c : ctrl2) cp.push_back(new Point2d(entities, c.x(), c.y()));
              auto flat = new BSplineCurve2d(entities, cp);
              flat->setKnots(CurveFit::degree, link.fit->knots, link.fit->mults);
              if (param_context == nullptr) param_context = new ParametricContext(entities);
              attach(new PCurve(entities, surface,
                                new DefinitionalRepresentation(entities, flat, param_context)));
            }
            continue;
          }
          // Where this edge runs across the surface, written down rather than
          // left to be guessed. A swept surface cannot be projected onto
          // reliably - see the PCURVE classes in StepKernel.h - and this is the
          // one face shape in this exporter that is trimmed by a polyline on
          // such a surface.
          //
          // Taken from the edge's own two vertices, not from a and b: an edge
          // already in the map may run the other way, and a pcurve that
          // disagrees with the direction of the curve it belongs to is worse
          // than none.
          double ua = 0, va = 0, ub = 0, vb = 0;
          if (!parameters(edge->vert1->point->pt, ua, va)) continue;
          if (!parameters(edge->vert2->point->pt, ub, vb)) continue;
          const double su = ub - ua, sv = vb - va;
          const double plen = sqrt(su * su + sv * sv);
          // A DIRECTION has to have a magnitude, and an edge whose ends land on
          // the same parameters says nothing about where it runs anyway.
          if (plen < 1e-12) continue;
          if (param_context == nullptr) param_context = new ParametricContext(entities);
          attach(new PCurve(
            entities, surface,
            new DefinitionalRepresentation(
              entities,
              new Line2d(entities, new Point2d(entities, ua, va),
                         new Vector2d(entities, new Direction2d(entities, su / plen, sv / plen), plen)),
              param_context)));
        }
      }
      auto edge_loop = new EdgeLoop(entities, loop);
      bounds.push_back(new FaceBound(entities, edge_loop, true, entry.first == outer));
    }
    if (bounds.empty()) continue;

    // Which way the face points. du x dv has no reason to agree with the mesh,
    // so it is compared against a facet the region actually contains.
    bool outward = true;
    if (!patch.facets.empty()) {
      const std::vector<int>& facet = loops[patch.facets.front()];
      Vector3d centroid = Vector3d::Zero();
      for (const int v : facet) centroid += vertices[v];
      centroid /= double(facet.size());
      double pu = 0, pv = 0;
      grid->project(centroid, pu, pv);
      const double h = 1e-5;
      const Vector3d d_u =
        grid->evaluate(std::min(1.0, pu + h), pv) - grid->evaluate(std::max(0.0, pu - h), pv);
      const Vector3d d_v =
        grid->evaluate(pu, std::min(1.0, pv + h)) - grid->evaluate(pu, std::max(0.0, pv - h));
      outward = d_u.cross(d_v).dot(loop_normals[patch.facets.front()]) > 0;
    }

    sfaces_extra.push_back(new Face(entities, bounds, surface, outward));
    face_edges_extra.push_back(face_edges_here);
  }
}
