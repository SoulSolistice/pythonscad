/*! Where a face that goes all the way round its axis can be closed by a seam -
 * see Seam.h. */
#include "geometry/recognition/Seam.h"

#include <algorithm>
#include <cmath>

namespace recognition {

bool isRuling(const Surface& s, const Vector3d& a, const Vector3d& b)
{
  return s.residual(a).exact() && s.residual(b).exact() && s.residual((a + b) * 0.5).exact();
}

bool windsRound(const AxialSurface& s, const std::vector<int>& cycle,
                const std::vector<Vector3d>& vertices)
{
  const std::size_t n = cycle.size();
  if (n < 3) return false;
  double turn = 0.0;
  for (std::size_t k = 0; k < n; k++) {
    double d = s.angle(vertices[cycle[(k + 1) % n]]) - s.angle(vertices[cycle[k]]);
    if (d > M_PI) d -= 2 * M_PI;
    if (d < -M_PI) d += 2 * M_PI;
    turn += d;
  }
  return fabs(turn) > M_PI;
}

namespace {

/*! Whether an edge of a rim other than the seam's own ends crosses the seam's
 * angle strictly between its two heights. */
bool crossesSeam(const AxialSurface& s, const std::vector<int>& cycle, int from, int to,
                 const std::vector<Vector3d>& vertices, double theta, double lo, double hi)
{
  const Vector3d axis = s.axis();
  const std::size_t n = cycle.size();
  for (std::size_t k = 0; k < n; k++) {
    const int a = cycle[k], b = cycle[(k + 1) % n];
    if (a == from || b == from || a == to || b == to) continue;
    double ta = s.angle(vertices[a]) - theta, tb = s.angle(vertices[b]) - theta;
    // Each measured from the seam, into (-pi, pi]: the edge crosses the seam's
    // angle where the two fall on either side of zero, the short way round.
    while (ta <= -M_PI) ta += 2 * M_PI;
    while (ta > M_PI) ta -= 2 * M_PI;
    while (tb <= -M_PI) tb += 2 * M_PI;
    while (tb > M_PI) tb -= 2 * M_PI;
    if ((ta > 0) == (tb > 0) || fabs(ta - tb) >= M_PI) continue;
    const double ha = (vertices[a] - s.refpt).dot(axis), hb = (vertices[b] - s.refpt).dot(axis);
    const double h = ha + (hb - ha) * (ta / (ta - tb));
    if (h > lo && h < hi) return true;
  }
  return false;
}

}  // namespace

std::vector<std::pair<std::size_t, std::size_t>> rulingSeams(const AxialSurface& s,
                                                             const std::vector<int>& first,
                                                             const std::vector<int>& second,
                                                             const std::vector<Vector3d>& vertices)
{
  std::vector<std::pair<std::size_t, std::size_t>> out;
  const Vector3d axis = s.axis();
  for (std::size_t i = 0; i < first.size(); i++) {
    for (std::size_t j = 0; j < second.size(); j++) {
      const Vector3d& p = vertices[first[i]];
      const Vector3d& q = vertices[second[j]];
      if (!isRuling(s, p, q)) continue;
      const double hp = (p - s.refpt).dot(axis), hq = (q - s.refpt).dot(axis);
      const double lo = std::min(hp, hq), hi = std::max(hp, hq), theta = s.angle(p);
      if (crossesSeam(s, first, first[i], second[j], vertices, theta, lo, hi) ||
          crossesSeam(s, second, first[i], second[j], vertices, theta, lo, hi)) {
        continue;
      }
      out.emplace_back(i, j);
    }
  }
  return out;
}

}  // namespace recognition
