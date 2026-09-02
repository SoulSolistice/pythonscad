#include "core/Selection.h"
#include "utils/vector_math.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "geometry/Grid.h"

// this function resolves a 3x3 linear eqauation system
/*
 * res[0] * v1 + res[1] *v2 + res[2] * vf3 = pt
 */

bool linsystem(Vector3d v1, Vector3d v2, Vector3d v3, Vector3d pt, Vector3d& res, double *detptr)
{
  double det, ad11, ad12, ad13, ad21, ad22, ad23, ad31, ad32, ad33;
  det = v1[0] * (v2[1] * v3[2] - v3[1] * v2[2]) - v1[1] * (v2[0] * v3[2] - v3[0] * v2[2]) +
        v1[2] * (v2[0] * v3[1] - v3[0] * v2[1]);
  if (detptr != nullptr) *detptr = det;
  ad11 = v2[1] * v3[2] - v3[1] * v2[2];
  ad12 = v3[0] * v2[2] - v2[0] * v3[2];
  ad13 = v2[0] * v3[1] - v3[0] * v2[1];
  ad21 = v3[1] * v1[2] - v1[1] * v3[2];
  ad22 = v1[0] * v3[2] - v3[0] * v1[2];
  ad23 = v3[0] * v1[1] - v1[0] * v3[1];
  ad31 = v1[1] * v2[2] - v2[1] * v1[2];
  ad32 = v2[0] * v1[2] - v1[0] * v2[2];
  ad33 = v1[0] * v2[1] - v2[0] * v1[1];

  if (fabs(det) < 0.00001) return true;

  res[0] = (ad11 * pt[0] + ad12 * pt[1] + ad13 * pt[2]) / det;
  res[1] = (ad21 * pt[0] + ad22 * pt[1] + ad23 * pt[2]) / det;
  res[2] = (ad31 * pt[0] + ad32 * pt[1] + ad33 * pt[2]) / det;
  return false;
}

SelectedObject calculateLinePointDistance(const Vector3d& l1, const Vector3d& l2, const Vector3d& pt,
                                          double& dist_lat)
{
  SelectedObject ruler;
  ruler.type = SelectionType::SELECTION_LINE;
  Vector3d d = (l2 - l1);
  double l = d.norm();
  d.normalize();
  dist_lat = std::clamp((pt - l1).dot(d), 0.0, l);
  ruler.pt.push_back(l1 + d * dist_lat);
  ruler.pt.push_back(pt);
  return ruler;
}

Vector3d calculateLineLineVector(const Vector3d& l1b, const Vector3d& l1e, const Vector3d& l2b,
                                 const Vector3d& l2e, double& parametric_t, double& signed_distance)
{
  parametric_t = std::numeric_limits<double>::quiet_NaN();
  Vector3d v1 = l1e - l1b;
  Vector3d v2 = l2e - l2b;
  Vector3d n = v1.cross(v2);
  double t = n.norm();

  double v1_squaredNorm = v1.squaredNorm();
  double v2_squaredNorm = v2.squaredNorm();
  if (v1_squaredNorm < GRID_FINE * GRID_FINE || v2_squaredNorm < GRID_FINE * GRID_FINE) {
    // An input is indistinguishable from a point, so we can't usefully calculate a result.
    signed_distance = std::numeric_limits<double>::quiet_NaN();
    return Vector3d(signed_distance, signed_distance, signed_distance);
  }

  if (t < GRID_FINE) {
    // Lines are parallel (or collinear). `parametric_t` makes no sense.
    Vector3d c = l2b - l1b;
    Vector3d c_original = l1b - l2b;
    double v1_mag = sqrt(v1_squaredNorm);
    Vector3d cross_c_v1 = c.cross(v1);

    double dist_numerator = cross_c_v1.norm();
    double v1_norm = v1.norm();
    // Parallel lines are everywhere the same distance apart, and that distance
    // has no side to be signed by. This has to be assigned on every path out of
    // this branch: calculateLineLineDistance() passes an uninitialized double in
    // and returns it, so leaving it alone hands back whatever was on the stack -
    // 0 in one build, the previous call's distance in the next.
    signed_distance =
      (v1_norm < GRID_FINE) ? std::numeric_limits<double>::quiet_NaN() : dist_numerator / v1_norm;
    if (v1_norm < GRID_FINE) {
      // Line 1 is a point. This handles both line 2 is point and line 2 is a line.
      // Leave parametric_t as NaN because it's meaningless.
      double dummy;
      auto ret = calculateLinePointDistance(l2b, l2e, l1b, dummy);
      return (ret.pt[0] = ret.pt[1]);
    }
    // This handles line 2 being a point or line:
    return v1 * dist_numerator / (v1_norm * v1_norm);
  }

  n /= t;  // Normalize n.

  signed_distance = n.dot(l1b - l2b);

  // parametric_t logic remains the same
  parametric_t = (v2.cross(n)).dot(l2b - l1b) / t;

  // The vector pointing from Line 1 to Line 2 is actually -(signed_distance * n)
  // because signed_distance was calculated using (l1b - l2b).
  return -signed_distance * n;
}

double calculateLineLineDistance(const Vector3d& l1b, const Vector3d& l1e, const Vector3d& l2b,
                                 const Vector3d& l2e, double& parametric_t)
{
  double dist;
  calculateLineLineVector(l1b, l1e, l2b, l2e, parametric_t, dist);
  return dist;
}

SelectedObject calculateSegSegDistance(const Vector3d& l1b, const Vector3d& l1e, const Vector3d& l2b,
                                       const Vector3d& l2e)
{
  SelectedObject ruler;
  ruler.type = SelectionType::SELECTION_LINE;

  // The closest points on two segments, by the standard method: solve for the
  // closest points on the two infinite lines, and where that answer falls off
  // the end of a segment, pin that parameter to the end and re-solve the other
  // one against it. Clamping both parameters independently is not the same thing
  // and does not give the closest points - it was what this function did, and it
  // measured to the far end of a segment whenever the unclamped answer lay
  // outside. Parallel and degenerate inputs fall out of the same cases rather
  // than needing a branch of their own, so there is no unsolvable input and this
  // always returns two points: Measurement.cc reads both without checking.
  const Vector3d d1 = l1e - l1b;
  const Vector3d d2 = l2e - l2b;
  const Vector3d r = l1b - l2b;
  const double a = d1.squaredNorm();  // squared length of segment 1
  const double e = d2.squaredNorm();  // squared length of segment 2
  const double f = d2.dot(r);
  const double eps = GRID_FINE * GRID_FINE;

  double s = 0.0;  // parameter along segment 1
  double t = 0.0;  // parameter along segment 2
  if (a <= eps && e <= eps) {
    // Both segments are points, so the endpoints are the answer.
  } else if (a <= eps) {
    t = std::clamp(f / e, 0.0, 1.0);
  } else {
    const double c = d1.dot(r);
    if (e <= eps) {
      s = std::clamp(-c / a, 0.0, 1.0);
    } else {
      const double b = d1.dot(d2);
      const double denom = a * e - b * b;  // zero exactly when the segments are parallel
      // Parallel segments have no single closest pair, so any point of the
      // overlap will do: take the start of segment 1 and let the clamping below
      // slide it onto segment 2's range.
      s = (denom > 0.0) ? std::clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.0;
      t = (b * s + f) / e;
      if (t < 0.0) {
        t = 0.0;
        s = std::clamp(-c / a, 0.0, 1.0);
      } else if (t > 1.0) {
        t = 1.0;
        s = std::clamp((b - c) / a, 0.0, 1.0);
      }
    }
  }

  ruler.pt.push_back(l1b + d1 * s);
  ruler.pt.push_back(l2b + d2 * t);

  return ruler;
}

SelectedObject calculatePointFaceDistance(const Vector3d& pt, const Vector3d& p1, const Vector3d& p2,
                                          const Vector3d& p3)
{
  SelectedObject ruler;
  ruler.type = SelectionType::SELECTION_LINE;
  ruler.pt.push_back(pt);
  Vector3d n = (p2 - p1).cross(p3 - p1).normalized();
  double dist = fabs((pt - p1).dot(n));
  ruler.pt.push_back(pt + n * dist);
  return ruler;
}
