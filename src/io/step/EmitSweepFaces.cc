/*! The declared sweeps, written - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>
#include "geometry/AnalyticFeatures.h"
#include "io/step/CrossingArc.h"

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
      std::vector<OrientedEdge *> loop;
      for (std::size_t i = 0; i < cycle.size(); i++) {
        const int a = cycle[i], b = cycle[(i + 1) % cycle.size()];
        bool dir = true;
        EdgeCurve *edge = nullptr;
        // The one edge a sweep shares with something that can give it a curve.
        // Same map, same key and the same EdgeCurve object as the quadric
        // emitter uses, so the two faces on this edge name one geometry however
        // the passes are ordered.
        const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
        const auto crossing = crossing_edges.find(key);
        if (crossing != crossing_edges.end()) {
          const auto found = crossing_curves.find(key);
          if (found != crossing_curves.end()) {
            edge = found->second;
            dir = edge->vert1 == get_vertex(a);
          } else {
            std::vector<Vector3d> ctrl;
            if (intersectionArc(crossing->second.first, crossing->second.second, vertices[key.first],
                                vertices[key.second], 1e-7, ctrl)) {
              std::vector<Point *> cp;
              cp.reserve(ctrl.size());
              for (const auto& c : ctrl) cp.push_back(new Point(entities, c));
              edge = new EdgeCurve(entities, get_vertex(key.first), get_vertex(key.second),
                                   new BSplineCurve(entities, "", cp), true);
              crossing_curves.emplace(key, edge);
              crossing_written++;
              dir = edge->vert1 == get_vertex(a);
            }
          }
        }
        if (edge == nullptr) {
          edge = get_line_from_map(edge_map, a, b, get_vertex(a), get_vertex(b), dir, merged_edge_cnt);
        }
        loop.push_back(new OrientedEdge(entities, edge, dir));
        face_edges_here.push_back(edge);
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
        [&]() {
          double ua = 0, va = 0, ub = 0, vb = 0;
          if (!parameters(edge->vert1->point->pt, ua, va)) return;
          if (!parameters(edge->vert2->point->pt, ub, vb)) return;
          const double su = ub - ua, sv = vb - va;
          const double plen = sqrt(su * su + sv * sv);
          // A DIRECTION has to have a magnitude, and an edge whose ends land on
          // the same parameters says nothing about where it runs anyway.
          if (plen < 1e-12) return;
          if (param_context == nullptr) param_context = new ParametricContext(entities);
          auto rep = new DefinitionalRepresentation(
            entities,
            new Line2d(entities, new Point2d(entities, ua, va),
                       new Vector2d(entities, new Direction2d(entities, su / plen, sv / plen), plen)),
            param_context);
          auto pcurve = new PCurve(entities, surface, rep);
          // An edge between two sweep faces is on both of them, and STEP allows
          // a SURFACE_CURVE to say so.
          if (auto *already = dynamic_cast<SurfaceCurve *>(edge->round)) {
            if (already->on.size() < 2) already->on.push_back(pcurve);
          } else {
            edge->round = new SurfaceCurve(entities, edge->round, {pcurve});
          }
        }();
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
