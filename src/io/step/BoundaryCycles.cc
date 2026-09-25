/*! A quadric patch's boundary, as cycles - see BoundaryCycles.h. */
#include "io/step/BoundaryCycles.h"

#include <algorithm>
#include <cmath>

#include "geometry/recognition/Seam.h"
#include "io/step/PlaneSections.h"

namespace step {

using AnalyticFeatures::declaredBand;
using AnalyticFeatures::surfaceImplicit;

void findSections(const Surface *surface, const std::vector<Vector3d>& vertices,
                  const std::vector<CutPlane>& planes, BoundaryCycle& cycle,
                  const std::set<std::pair<int, int>>& spoken_for, bool bind_ends, SectionStats *stats)
{
  cycle.sections.clear();
  Vector3d axis;
  if (!surfaceAxis(surface, axis)) return;
  const double floor_tilt = sectionTiltFloor(surface);
  const std::size_t n = cycle.verts.size();
  if (n < 3) return;

  // A curve substituted for a run of chords has to pass through that run's own
  // endpoints. A section's curve is the surface met by the plane, so a vertex is
  // on it only if it is on *both* - and being in the plane is the only half this
  // used to ask. The other half is not a formality: lid10's draft cone is cut at
  // a hand over a degree past its half angle, where the section is an ellipse
  // 940 long and 58 wide, and a vertex a sagitta off the cone slides along that
  // major axis by a millimetre and a half. Measured on the committed build:
  // 38 edges whose vertices are up to 1.6196 off the curve their own edge is
  // written as, which is what SOLIDWORKS reports as fault code 7, "vertex does
  // not lie on edge's curve".
  //
  // The tolerance is the one the crossing-arc pass already holds its endpoints
  // to, and for the same reason: a declared surface owes its own band, and
  // anything else owes the scale of the point.
  std::vector<char> on_surface(n, 0);
  std::vector<double> stray(n, 0);
  for (std::size_t k = 0; k < n; k++) {
    const Vector3d& p = vertices[cycle.verts[k]];
    const double scale = std::max(1.0, p.norm());
    const double owed = std::max(1e-9 * scale, declaredBand(surface));
    double f = 0, d = 0;
    Vector3d g;
    on_surface[k] = surfaceImplicit(surface, p, f, g, &d) && g.norm() >= 1e-12 && d <= owed;
    stray[k] = d;
  }

  // It is the run's *ends* this binds, and only they: the vertices between them
  // are the chords' own, they are a sagitta off the surface because that is what
  // a tessellation is, and the curve replaces them rather than passing through
  // them. So a run whose end is off the surface is shortened until its end is on
  // it, and dropped if nothing is left. The edges it gives up stay chords, which
  // is what both faces already have.
  auto trim = [&](std::size_t& start, std::size_t& count) {
    if (!bind_ends) return;
    const std::size_t was = count;
    while (count >= 2 && !on_surface[start % n]) {
      if (stats != nullptr) stats->end_stray = std::max(stats->end_stray, stray[start % n]);
      start++;
      count--;
    }
    while (count >= 2 && !on_surface[(start + count - 1) % n]) {
      if (stats != nullptr)
        stats->end_stray = std::max(stats->end_stray, stray[(start + count - 1) % n]);
      count--;
    }
    if (count < 2) count = 0;
    if (stats == nullptr) return;
    if (count == 0) stats->dropped++;
    else if (count < was) stats->trimmed++;
  };

  // Which of the surface's cutting planes each boundary edge lies in. A plane is
  // only usable if it makes a closed section: perpendicular to the axis of a
  // surface whose sections there are circles it makes one, which the arc pass
  // writes, and past the floor a cone's section runs away to infinity.
  const auto *axial = dynamic_cast<const AxialSurface *>(surface);
  const bool circles_across = axial == nullptr || axial->circularSections();
  std::vector<double> scale(planes.size(), 0);
  for (std::size_t p = 0; p < planes.size(); p++) {
    const double cos_tilt = fabs(planes[p].normal.dot(axis));
    if (cos_tilt < floor_tilt || (circles_across && cos_tilt > 1 - 1e-9)) continue;
    SectionEllipse sec;
    if (!planeSectionEllipse(surface, planes[p].normal, planes[p].on_plane, sec)) continue;
    scale[p] = sec.a;
  }
  std::vector<int> plane_of(n, -1);
  for (std::size_t e = 0; e < n; e++) {
    // An edge already spoken for by a better curve is not offered a section. The
    // curve where this surface crosses another declared one lies on *both* of
    // them; a plane section lies on this one only, so wherever both are
    // available the crossing curve is the truer answer and takes the edge.
    const int eu = cycle.verts[e], ev = cycle.verts[(e + 1) % n];
    if (spoken_for.count({std::min(eu, ev), std::max(eu, ev)}) != 0) continue;
    for (std::size_t p = 0; p < planes.size(); p++) {
      if (scale[p] <= 0) continue;
      const double tol = 1e-9 * scale[p];
      const Vector3d& u = vertices[cycle.verts[e]];
      const Vector3d& v = vertices[cycle.verts[(e + 1) % n]];
      if (fabs((u - planes[p].on_plane).dot(planes[p].normal)) > tol) continue;
      if (fabs((v - planes[p].on_plane).dot(planes[p].normal)) > tol) continue;
      plane_of[e] = int(p);
      break;
    }
  }

  // Group into maximal runs sharing a plane *and* a neighbour. The plane is what
  // makes the curve; the neighbour is who has to be handed it, and a curve
  // spanning two neighbours could be handed to neither.
  std::vector<char> taken_edge(n, 0);
  for (std::size_t e = 0; e < n; e++) {
    // The fitted fallback below has to be told as well, or it takes back what
    // the crossing curve was given.
    const int eu = cycle.verts[e], ev = cycle.verts[(e + 1) % n];
    if (spoken_for.count({std::min(eu, ev), std::max(eu, ev)}) != 0) taken_edge[e] = 1;
  }
  for (std::size_t e = 0; e < n; e++) {
    if (plane_of[e] < 0 || taken_edge[e]) continue;
    const std::size_t prev = (e + n - 1) % n;
    if (plane_of[prev] == plane_of[e] && cycle.owner[prev] == cycle.owner[e] && e > 0) continue;
    std::size_t count = 1;
    while (count < n && plane_of[(e + count) % n] == plane_of[e] &&
           cycle.owner[(e + count) % n] == cycle.owner[e]) {
      count++;
    }
    BoundarySection bs;
    bs.start = e;
    bs.count = count + 1;
    trim(bs.start, bs.count);
    bs.normal = planes[plane_of[e]].normal;
    bs.on_plane = planes[plane_of[e]].on_plane;
    bs.declared = planes[plane_of[e]].declared;
    bs.loop = cycle.owner[e];
    if (bs.count >= 2) cycle.sections.push_back(bs);
    // The whole run is spoken for either way: what the trim gave up is a chord
    // by decision, not something for the fitted fallback below to pick up again.
    for (std::size_t k = 0; k < count; k++) taken_edge[(e + k) % n] = 1;
  }

  // And last, where no face anywhere declares the plane, fit one to the boundary
  // itself. That is the weaker way round - it needs two edges before it can see
  // a plane at all - and it exists for the case where nothing planar is in the
  // picture: two cylinders crossing meet along an ellipse that is nobody's face.
  for (const auto& st : coplanarStretches(cycle.verts, vertices, axis, floor_tilt)) {
    bool free_run = true;
    for (std::size_t k = 0; free_run && k + 1 < st.count; k++) {
      free_run = !taken_edge[(st.start + k) % n];
    }
    if (!free_run) continue;
    const Vector3d on_plane = vertices[cycle.verts[st.start % n]];
    SectionEllipse sec;
    if (!planeSectionEllipse(surface, st.normal, on_plane, sec)) continue;
    BoundarySection bs;
    bs.start = st.start;
    bs.count = st.count;
    trim(bs.start, bs.count);
    bs.normal = st.normal;
    bs.on_plane = on_plane;
    if (bs.count >= 2) cycle.sections.push_back(bs);
    for (std::size_t k = 0; k + 1 < st.count; k++) taken_edge[(st.start + k) % n] = 1;
  }
}

std::map<std::size_t, BoundaryCycle> rawBoundaryCycles(const AnalyticFeatures::Patch& patch,
                                                       const std::vector<std::vector<int>>& loops,
                                                       const std::vector<char>& loop_valid,
                                                       const std::vector<char>& taken)
{
  std::map<std::size_t, BoundaryCycle> out;
  for (const auto& run : patch.runs) {
    BoundaryCycle& cycle = out[run.bound];
    // Only a neighbour that is going to be written as a planar face can be
    // handed a curve; one another claim has already taken is not there to take
    // it, and the shell would open along it. `taken` is that test, and it has to
    // include the claims this same pass is about to make - two cylinders meeting
    // along an ellipse each see a facet loop of the other, which is not a planar
    // face at all and settles the curve between themselves.
    int who = -1;
    if (run.loop < loops.size() && loop_valid[run.loop] != 0 && taken[run.loop] == 0 &&
        loops[run.loop].size() >= 3) {
      who = int(run.loop);
    }
    for (std::size_t i = 0; i + 1 < run.verts.size(); i++) {
      cycle.verts.push_back(run.verts[i]);
      cycle.owner.push_back(who);
    }
  }
  return out;
}

bool sealWithSeam(const AnalyticFeatures::Patch& patch, const std::vector<Vector3d>& vertices,
                  const std::vector<CutPlane>& planes, const std::set<std::pair<int, int>>& spoken_for,
                  bool bind_ends, SectionStats *stats, std::map<std::size_t, BoundaryCycle>& cycles)
{
  const auto *axial = dynamic_cast<const AxialSurface *>(patch.surface.get());
  if (axial == nullptr || cycles.size() != 2) return false;
  auto it = cycles.begin();
  BoundaryCycle& first = it->second;
  BoundaryCycle& second = (++it)->second;
  if (!recognition::windsRound(*axial, first.verts, vertices) ||
      !recognition::windsRound(*axial, second.verts, vertices)) {
    return false;
  }
  auto rolled = [](const BoundaryCycle& c, std::size_t at) {
    BoundaryCycle out = c;
    std::rotate(out.verts.begin(), out.verts.begin() + long(at), out.verts.end());
    std::rotate(out.owner.begin(), out.owner.begin() + long(at), out.owner.end());
    out.sections.clear();
    out.seam = true;
    return out;
  };
  // No section may run across the seam vertex: a run ends there or covers the
  // whole rim, starting and ending there.
  auto clear_of_seam = [](const BoundaryCycle& c) {
    const std::size_t n = c.verts.size();
    for (const auto& bs : c.sections) {
      if (bs.start + bs.count > n && !(bs.start == 0 && bs.count == n + 1)) return false;
    }
    return true;
  };
  for (const auto& [i, j] : recognition::rulingSeams(*axial, first.verts, second.verts, vertices)) {
    BoundaryCycle a = rolled(first, i), b = rolled(second, j);
    findSections(patch.surface.get(), vertices, planes, a, spoken_for, bind_ends, stats);
    findSections(patch.surface.get(), vertices, planes, b, spoken_for, bind_ends, stats);
    if (!clear_of_seam(a) || !clear_of_seam(b)) continue;
    first = std::move(a);
    second = std::move(b);
    return true;
  }
  return false;
}

std::map<std::size_t, BoundaryCycle> boundaryCycles(
  const AnalyticFeatures::Patch& patch, const std::vector<Vector3d>& vertices,
  const std::vector<std::vector<int>>& loops, const std::vector<char>& loop_valid,
  const std::vector<char>& taken, const std::vector<CutPlane>& planes,
  const std::set<std::pair<int, int>>& spoken_for, bool bind_ends, SectionStats *stats)
{
  std::map<std::size_t, BoundaryCycle> out = rawBoundaryCycles(patch, loops, loop_valid, taken);
  // A face that goes round its axis between two rims is closed by a seam, and
  // each rim starts where the seam meets it; its sections are found from there.
  if (sealWithSeam(patch, vertices, planes, spoken_for, bind_ends, stats, out)) return out;
  for (auto& entry : out) {
    BoundaryCycle& cycle = entry.second;
    findSections(patch.surface.get(), vertices, planes, cycle, spoken_for, bind_ends, stats);
    // Move the seam onto an edge no section covers, so the writer meets each
    // section whole. Cutting at a section's own start is not enough: freeing one
    // that way pushes the next across the new seam wherever two meet end to end,
    // which is how the two ellipse arcs of a Steinmetz face meet at the pinch.
    const std::size_t n = cycle.verts.size();
    bool wraps = false;
    std::vector<char> covered(n, 0);
    for (const auto& bs : cycle.sections) {
      if (bs.start + bs.count > n) wraps = true;
      for (std::size_t k = 0; k + 1 < bs.count; k++) covered[(bs.start + k) % n] = 1;
    }
    if (!wraps) continue;
    std::size_t free_edge = n;
    for (std::size_t e = 0; e < n; e++) {
      if (!covered[e]) {
        free_edge = e;
        break;
      }
    }
    if (free_edge >= n) continue;  // every edge is a section, so nothing to cut at
    const long roll = long((free_edge + 1) % n);
    std::rotate(cycle.verts.begin(), cycle.verts.begin() + roll, cycle.verts.end());
    std::rotate(cycle.owner.begin(), cycle.owner.begin() + roll, cycle.owner.end());
    findSections(patch.surface.get(), vertices, planes, cycle, spoken_for, bind_ends, stats);
  }
  return out;
}

}  // namespace step
