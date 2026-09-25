/*! The plane geometry of the mesh's own loops - see LoopGeometry.h. */
#include "io/step/LoopGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "geometry/AnalyticFeatures.h"

namespace step {

Vector3d polygonNormal(const std::vector<Vector3d>& vertices, const std::vector<int>& poly)
{
  Vector3d norm(0, 0, 0);
  const size_t n = poly.size();
  for (size_t i = 0; i < n; i++) {
    const Vector3d& a = vertices[poly[i]];
    const Vector3d& b = vertices[poly[(i + 1) % n]];
    norm[0] += (a[1] - b[1]) * (a[2] + b[2]);
    norm[1] += (a[2] - b[2]) * (a[0] + b[0]);
    norm[2] += (a[0] - b[0]) * (a[1] + b[1]);
  }
  return norm;
}

void recordSliverSpan(const std::vector<Vector3d>& vertices, const std::vector<int>& loop,
                      std::map<std::pair<int, int>, std::vector<std::pair<double, int>>>& spans)
{
  const double weld_eps = 1e-9;

  std::vector<int> pts;
  for (const int ind : loop) {
    if (std::find(pts.begin(), pts.end(), ind) == pts.end()) pts.push_back(ind);
  }
  if (pts.size() < 3) return;

  // The longest chord is the span; measure everything along it.
  std::size_t a = 0, b = 1;
  double best = -1.0;
  for (std::size_t i = 0; i < pts.size(); i++) {
    for (std::size_t j = i + 1; j < pts.size(); j++) {
      const double d = (vertices[pts[j]] - vertices[pts[i]]).squaredNorm();
      if (d > best) {
        best = d;
        a = i;
        b = j;
      }
    }
  }
  if (best <= 0.0) return;

  const Vector3d& from = vertices[pts[a]];
  const Vector3d axis = vertices[pts[b]] - from;
  const double axis_len = axis.norm();

  std::vector<std::pair<double, int>> ordered;
  for (std::size_t i = 0; i < pts.size(); i++) {
    if (i == a || i == b) continue;
    const Vector3d off = vertices[pts[i]] - from;
    if (axis.cross(off).norm() > weld_eps * axis_len) return;  // not collinear
    ordered.emplace_back(off.dot(axis) / axis.squaredNorm(), pts[i]);
  }
  if (ordered.empty()) return;
  std::sort(ordered.begin(), ordered.end());

  // Key on the span, and measure every middle point from its lower-numbered
  // end so entries from different slivers are directly comparable.
  const int lo = pts[a], hi = pts[b];
  const bool forward = lo < hi;
  auto& slot = spans[forward ? std::make_pair(lo, hi) : std::make_pair(hi, lo)];
  for (const auto& o : ordered) {
    const double at = forward ? o.first : 1.0 - o.first;
    bool seen = false;
    for (const auto& have : slot) {
      if (have.second == o.second) {
        seen = true;
        break;
      }
    }
    if (!seen) slot.emplace_back(at, o.second);
  }
  std::sort(slot.begin(), slot.end());
}

int dominantAxis(const Vector3d& norm)
{
  const double ax = fabs(norm[0]), ay = fabs(norm[1]), az = fabs(norm[2]);
  if (ax >= ay && ax >= az) return 0;
  return ay >= az ? 1 : 2;
}

std::array<double, 2> projectPoint(const Vector3d& pt, int drop)
{
  return {pt[(drop + 1) % 3], pt[(drop + 2) % 3]};
}

void projectLoop(const std::vector<Vector3d>& vertices, const std::vector<int>& loop, int drop,
                 std::vector<std::array<double, 2>>& out)
{
  out.clear();
  out.reserve(loop.size());
  for (const int ind : loop) out.push_back(projectPoint(vertices[ind], drop));
}

double loopArea2d(const std::vector<std::array<double, 2>>& poly)
{
  double area = 0;
  for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    area += (poly[j][0] + poly[i][0]) * (poly[j][1] - poly[i][1]);
  }
  return fabs(area) * 0.5;
}

bool pointInLoop2d(const std::vector<std::array<double, 2>>& poly, const std::array<double, 2>& pt)
{
  bool inside = false;
  for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    if ((poly[i][1] > pt[1]) != (poly[j][1] > pt[1])) {
      const double x =
        (poly[j][0] - poly[i][0]) * (pt[1] - poly[i][1]) / (poly[j][1] - poly[i][1]) + poly[i][0];
      if (pt[0] < x) inside = !inside;
    }
  }
  return inside;
}

bool loopContains(const std::vector<Vector3d>& vertices, const std::vector<int>& outer,
                  const std::vector<int>& inner, int drop)
{
  // A hole which touches its own outer bound is not a hole. Three on the
  // bayonet lid share exactly one vertex with the loop carrying them - pinched
  // to the boundary at a point - and a point on the boundary is where an
  // even-odd ray is ambiguous, so the containment test below calls all of its
  // corners inside and OpenCASCADE still refuses the face. Vertex indices are
  // exact where the geometry is not, so ask them first.
  for (const int v : inner) {
    if (std::find(outer.begin(), outer.end(), v) != outer.end()) return false;
  }
  std::vector<std::array<double, 2>> poly;
  projectLoop(vertices, outer, drop, poly);
  for (const int v : inner) {
    if (!pointInLoop2d(poly, projectPoint(vertices[v], drop))) return false;
  }
  return true;
}

std::vector<std::array<int, 3>> triangulateWithHoles(const std::vector<std::vector<int>>& rings,
                                                     const std::function<Vector3d(int)>& at,
                                                     const Vector3d& normal)
{
  std::vector<std::array<int, 3>> out;
  if (rings.empty() || rings[0].size() < 3 || normal.norm() < 1e-12) return out;
  const Vector3d n = normal.normalized();
  Vector3d e1 = AnalyticFeatures::perpendicular(n);
  if (e1.norm() < 1e-12) return out;
  e1.normalize();
  const Vector3d e2 = n.cross(e1);
  auto uv = [&](int v) { return std::make_pair(at(v).dot(e1), at(v).dot(e2)); };

  auto ring_area = [&](const std::vector<int>& ring) {
    double twice = 0;
    for (std::size_t k = 0; k < ring.size(); k++) {
      const auto a = uv(ring[k]), b = uv(ring[(k + 1) % ring.size()]);
      twice += a.first * b.second - b.first * a.second;
    }
    return twice / 2;
  };

  // The outer ring counter clockwise in this basis, every hole clockwise: that
  // is what makes a bridge leave one simple ring rather than two.
  std::vector<int> ring = rings[0];
  if (ring_area(ring) < 0) std::reverse(ring.begin(), ring.end());
  std::vector<std::vector<int>> holes;
  for (std::size_t h = 1; h < rings.size(); h++) {
    if (rings[h].size() < 3) return out;
    std::vector<int> hole = rings[h];
    if (ring_area(hole) > 0) std::reverse(hole.begin(), hole.end());
    holes.push_back(hole);
  }
  // Rightmost first, so a hole is never bridged across one still outside.
  std::sort(holes.begin(), holes.end(), [&](const std::vector<int>& a, const std::vector<int>& b) {
    double xa = -std::numeric_limits<double>::max(), xb = xa;
    for (const int v : a) xa = std::max(xa, uv(v).first);
    for (const int v : b) xb = std::max(xb, uv(v).first);
    return xa > xb;
  });

  auto in_triangle = [&](const std::pair<double, double>& p, const std::pair<double, double>& a,
                         const std::pair<double, double>& b, const std::pair<double, double>& c) {
    auto side = [](const std::pair<double, double>& u, const std::pair<double, double>& v,
                   const std::pair<double, double>& w) {
      return (v.first - u.first) * (w.second - u.second) - (v.second - u.second) * (w.first - u.first);
    };
    const double s1 = side(a, b, p), s2 = side(b, c, p), s3 = side(c, a, p);
    return (s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0);
  };

  for (const auto& hole : holes) {
    // The hole's rightmost corner, and a ray from it to the right: the first
    // edge of the ring that ray meets carries the corner to bridge to.
    std::size_t m = 0;
    for (std::size_t k = 1; k < hole.size(); k++) {
      if (uv(hole[k]).first > uv(hole[m]).first) m = k;
    }
    const auto M = uv(hole[m]);
    double best_x = std::numeric_limits<double>::max();
    std::size_t hit = ring.size();
    for (std::size_t k = 0; k < ring.size(); k++) {
      const auto a = uv(ring[k]), b = uv(ring[(k + 1) % ring.size()]);
      if ((a.second > M.second) == (b.second > M.second)) continue;
      const double t = (M.second - a.second) / (b.second - a.second);
      const double x = a.first + t * (b.first - a.first);
      if (x < M.first || x >= best_x) continue;
      best_x = x;
      hit = (uv(ring[k]).first > uv(ring[(k + 1) % ring.size()]).first) ? k : (k + 1) % ring.size();
    }
    if (hit >= ring.size()) return out;
    // A corner of the ring inside the triangle (M, the crossing, the candidate)
    // would be cut off by the bridge, so the visible one is the best of those.
    const std::pair<double, double> P(best_x, M.second);
    std::size_t bridge = hit;
    double best_angle = std::numeric_limits<double>::max();
    for (std::size_t k = 0; k < ring.size(); k++) {
      const auto q = uv(ring[k]);
      if (q.first < M.first) continue;
      if (!in_triangle(q, M, P, uv(ring[hit]))) continue;
      const double dx = q.first - M.first, dy = q.second - M.second;
      const double len = sqrt(dx * dx + dy * dy);
      const double angle = len > 0 ? fabs(dy) / len : 0;
      if (angle < best_angle) {
        best_angle = angle;
        bridge = k;
      }
    }
    // Splice: out to the bridge corner, round the hole from its rightmost, back
    // along the bridge, then on round the ring.
    std::vector<int> joined;
    for (std::size_t k = 0; k <= bridge; k++) joined.push_back(ring[k]);
    for (std::size_t k = 0; k < hole.size(); k++) joined.push_back(hole[(m + k) % hole.size()]);
    joined.push_back(hole[m]);
    for (std::size_t k = bridge; k < ring.size(); k++) joined.push_back(ring[k]);
    ring.swap(joined);
  }

  // Ear clipping. The scale sets what counts as no area at all; nothing else
  // here needs a tolerance.
  double extent = 0;
  for (std::size_t k = 0; k < ring.size(); k++) {
    for (std::size_t j = k + 1; j < ring.size(); j++) {
      const auto a = uv(ring[k]), b = uv(ring[j]);
      extent = std::max(extent, fabs(a.first - b.first) + fabs(a.second - b.second));
    }
  }
  const double tiny = 1e-12 * std::max(1.0, extent * extent);

  std::vector<int> work = ring;
  std::size_t guard = work.size() * work.size() + 16;
  while (work.size() > 3 && guard-- > 0) {
    bool cut = false;
    for (std::size_t k = 0; k < work.size(); k++) {
      const std::size_t kp = (k + work.size() - 1) % work.size();
      const std::size_t kn = (k + 1) % work.size();
      const auto a = uv(work[kp]), b = uv(work[k]), c = uv(work[kn]);
      const double area = 0.5 * ((b.first - a.first) * (c.second - a.second) -
                                 (b.second - a.second) * (c.first - a.first));
      if (area <= tiny) continue;  // reflex, or nothing to cut
      bool clear = true;
      for (std::size_t j = 0; j < work.size() && clear; j++) {
        if (j == k || j == kp || j == kn) continue;
        if (work[j] == work[k] || work[j] == work[kp] || work[j] == work[kn]) continue;
        if (in_triangle(uv(work[j]), a, b, c)) clear = false;
      }
      if (!clear) continue;
      out.push_back({work[kp], work[k], work[kn]});
      work.erase(work.begin() + long(k));
      cut = true;
      break;
    }
    if (!cut) {
      out.clear();
      return out;  // no ear anywhere: say so rather than write a wrong face
    }
  }
  if (work.size() == 3) out.push_back({work[0], work[1], work[2]});
  return out;
}

int uf_find(std::vector<int>& parent, int x)
{
  while (parent[x] != x) {
    parent[x] = parent[parent[x]];
    x = parent[x];
  }
  return x;
}

}  // namespace step
