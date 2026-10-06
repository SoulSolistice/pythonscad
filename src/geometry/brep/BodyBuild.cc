/*! One solid's worth of STEP - see BodyBuild.h. */
#include "geometry/brep/BodyBuild.h"

#include <limits>
#include "geometry/AnalyticFeatures.h"
#include "geometry/recognition/Exact.h"
#include "geometry/recognition/Seam.h"
#include "geometry/recognition/Pipeline.h"
#include "geometry/recognition/Reports.h"
#include "geometry/brep/BoundaryCycles.h"
#include "geometry/brep/CornerLadder.h"
#include "geometry/brep/CrossingArc.h"
#include "geometry/brep/LoopGeometry.h"
#include "geometry/brep/PlaneSections.h"
#include "utils/printutils.h"
#include <algorithm>  // std::reverse
#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include <map>
#include <utility>
#include <array>
#include <set>
#include <functional>
#include <iomanip>  // put_time

using namespace step;

namespace {

// The implicit-surface machinery these passes solve with - `surfaceImplicit`,
// `projectOntoBoth`, `isQuadric`, `declaredBand` - lives in AnalyticFeatures,
// beside `closestOnSurface`, because it is geometry rather than STEP and
// `export_step.cc` needs the same Newton for its corner placement.
using AnalyticFeatures::declaredBand;
using AnalyticFeatures::isQuadric;
using AnalyticFeatures::projectOntoBoth;
using AnalyticFeatures::surfaceImplicit;

using AnalyticFeatures::projectOntoSurfaceAndPlanes;

}  // namespace

brep::Body step::BodyBuild::run()
{
  reportDeclaredSurfaces();

  buildLoops();

  recognise();

  placeCorners();

  emitBands();

  emitBezierFaces();

  redecideSections();

  decideCrossingRuns();

  emitSweepFaces();

  emitQuadricFaces();

  writeCrossingPcurves();

  emitPlanarFaces();

  emitCornerFans();

  assembleFaces();

  promoteCurves();

  checkBrep();

  checkAndReport();

  return std::move(brep_body);
}

// Corners are placed on their faces to the corner ladder's floor, 1e-9 of their
// size (CornerLadder.h - a chosen number itself, to go with open item 39): that
// is how exactly the file states where anything is, and an approximation finer
// than the corners it runs between gains nothing a reader can see. So the
// crossing curves are held to it, and say how closely they came
// (`crossing_fit`); and it is what the file declares as its uncertainty, what a
// reader may take two points as one within. That was 1e-5 whatever the file
// held until 2026-09-28.
double step::BodyBuild::resolution() const
{
  double size = 1.0;
  for (const Vector3d& v : vertices) size = std::max(size, v.norm());
  return 1e-9 * size;
}

int32_t step::BodyBuild::surfaceIndexOf(const Surface *record) const
{
  for (std::size_t i = 0; i < surfaces.size(); i++) {
    if (surfaces[i].get() == record) return int32_t(i);
  }
  for (std::size_t i = 0; i < surfaces.size() && record != nullptr; i++) {
    if (surfaces[i] != nullptr && surfaces[i]->sameAs(*record)) return int32_t(i);
  }
  return -1;
}

brep::FaceSource step::BodyBuild::sourceOf(std::shared_ptr<const Surface> record,
                                           std::vector<std::size_t> facets,
                                           std::optional<recognition::EvidenceClass> evidence,
                                           bool plane_from_corners) const
{
  using recognition::EvidenceClass;
  // The weakest class its facets' honoured claims state: a patch of D0 members
  // and D4 additions is D4. A facet no honoured claim holds leaves the face
  // unstated, which the B-rep's check names.
  EvidenceClass weakest = EvidenceClass::D0;
  if (!evidence) {
    for (const std::size_t f : facets) {
      EvidenceClass here = EvidenceClass::Unstated;
      for (const auto *claim : features.ledger.claimants(f)) {
        if (!claim->honoured()) continue;
        here = claim->evidence;
        break;
      }
      if (here == EvidenceClass::Unstated) {
        weakest = EvidenceClass::Unstated;
        break;
      }
      weakest = std::max(weakest, here);
    }
  }
  // And what the record itself is, which is a different fact: a maker's record
  // is D0 and one every name on which is derived D1 (mintDerivedTag); a fit, a
  // record the model never stated, D3; a plane from corners D4 until the B-rep
  // finds its corners determine it (brep::stateClasses).
  EvidenceClass record_class = EvidenceClass::D3;
  if (plane_from_corners) {
    record_class = EvidenceClass::D4;
  } else if (surfaceIndexOf(record.get()) >= 0) {
    bool all_derived = !record->tags.empty();
    for (const uint32_t tag : record->tags) all_derived = all_derived && isDerivedTag(tag);
    record_class = all_derived ? EvidenceClass::D1 : EvidenceClass::D0;
  }
  return FaceSource{std::move(record), std::move(facets), evidence ? *evidence : weakest,
                    plane_from_corners, record_class};
}

bool step::BodyBuild::boundWritten(std::size_t loop) const
{
  auto outer_written = [&](std::size_t k) {
    return k < face_cnt && loop_valid[k] && !loop_is_hole[k] && !consumed[k] &&
           split_for_corners.count(k) == 0;
  };
  if (loop >= face_cnt || !loop_valid[loop] || consumed[loop]) return false;
  if (!loop_is_hole[loop]) return outer_written(loop);
  // A hole is a bound of its face, written where that face is and nowhere
  // else (open item 39, R22, R26).
  return split_for_corners.count(loop) == 0 && parents[loop] >= 0 &&
         outer_written(std::size_t(parents[loop]));
}

const Surface *step::BodyBuild::namedFor(std::size_t facet) const
{
  if (facet >= faceSurface.size()) return nullptr;
  const int32_t s = faceSurface[facet];
  return s < 0 || std::size_t(s) >= surfaces.size() ? nullptr : surfaces[std::size_t(s)].get();
}

bool step::BodyBuild::flatWhereItStands(std::size_t face, const std::function<Vector3d(int)>& at) const
{
  std::vector<int> corners(loops[face].begin(), loops[face].end());
  for (std::size_t j = 0; j < face_cnt; j++) {
    if (loop_valid[j] && !consumed[j] && parents[j] == int(face))
      corners.insert(corners.end(), loops[j].begin(), loops[j].end());
  }
  std::vector<Vector3d> points;
  points.reserve(corners.size());
  for (const int w : corners) points.push_back(at(w));
  // The declared plane the face was made on: the first its unmoved corners lie
  // on, as the writer chooses one.
  for (const auto& declared : declared_planes) {
    if (!recognition::onPlane(declared.first, declared.second, mesh_vertices, loops[face])) continue;
    return recognition::flatWhereItStands(points, &declared.first, &declared.second);
  }
  return recognition::flatWhereItStands(points, nullptr, nullptr);
}

std::size_t step::BodyBuild::brepVertex(int ind)
{
  const auto key = std::make_tuple(vertices[ind][0], vertices[ind][1], vertices[ind][2]);
  const auto have = brep_vertex_at.find(key);
  if (have != brep_vertex_at.end()) return have->second;
  brep::Vertex vertex;
  vertex.point = vertices[ind];
  brep_body.vertices.push_back(vertex);
  brep_vertex_at.emplace(key, brep_body.vertices.size() - 1);
  return brep_body.vertices.size() - 1;
}

std::size_t step::BodyBuild::brepLine(std::size_t from, std::size_t to)
{
  brep::Edge edge;
  edge.start = from;
  edge.end = to;
  edge.same_sense = true;
  const Vector3d p = brep_body.vertices[from].point;
  Vector3d v = brep_body.vertices[to].point - p;
  v /= v.norm();
  edge.curve.form = brep::Line{p, v};
  brep_body.edges.push_back(std::move(edge));
  return brep_body.edges.size() - 1;
}

std::size_t step::BodyBuild::lineEdge(int a, int b, bool& forward)
{
  const auto key = std::make_pair(std::min(a, b), std::max(a, b));
  const std::size_t from = brepVertex(a);
  auto it = line_edges.find(key);
  if (it == line_edges.end()) {
    brep::Edge edge;
    edge.start = from;
    edge.end = brepVertex(b);
    // From its first vertex, along the unit vector to its second. Two
    // vertices are one wherever their points are (brepVertex), so the two
    // differ and the direction is theirs: a line of no length is not asked
    // for (the planar writer skips it) and no direction is invented for one
    // (open item 39, R17).
    const Vector3d p = brep_body.vertices[edge.start].point;
    edge.curve.form = brep::Line{p, (brep_body.vertices[edge.end].point - p).normalized()};
    edge.same_sense = true;
    brep_body.edges.push_back(std::move(edge));
    it = line_edges.emplace(key, brep_body.edges.size() - 1).first;
  }
  forward = brep_body.edges[it->second].start == from;
  return it->second;
}
