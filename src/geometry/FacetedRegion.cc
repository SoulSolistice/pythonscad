/*! The solid a faceted generator's faces bound - see FacetedRegion.h. */
#include "geometry/FacetedRegion.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <utility>

#include "geometry/MachinePrecision.h"
#include "geometry/Surface.h"

using recognition::forwardError;

namespace {

/*! The point of triangle (a, b, c) nearest p (Ericson, Real-Time Collision
 * Detection, 5.1.5). */
Vector3d nearestOnTriangle(const Vector3d& p, const Vector3d& a, const Vector3d& b, const Vector3d& c)
{
  const Vector3d ab = b - a, ac = c - a, ap = p - a;
  const double d1 = ab.dot(ap), d2 = ac.dot(ap);
  if (d1 <= 0 && d2 <= 0) return a;
  const Vector3d bp = p - b;
  const double d3 = ab.dot(bp), d4 = ac.dot(bp);
  if (d3 >= 0 && d4 <= d3) return b;
  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
  const Vector3d cp = p - c;
  const double d5 = ab.dot(cp), d6 = ac.dot(cp);
  if (d6 >= 0 && d5 <= d6) return c;
  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
  const double va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
    return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
  const double denom = 1 / (va + vb + vc);
  return a + ab * (vb * denom) + ac * (vc * denom);
}

/*! The solid angle triangle (a, b, c) subtends at p (Van Oosterom and
 * Strackee), signed by its orientation. */
double solidAngle(const Vector3d& p, const Vector3d& a, const Vector3d& b, const Vector3d& c)
{
  const Vector3d x = a - p, y = b - p, z = c - p;
  const double lx = x.norm(), ly = y.norm(), lz = z.norm();
  const double num = x.dot(y.cross(z));
  const double den = lx * ly * lz + x.dot(y) * lz + y.dot(z) * lx + z.dot(x) * ly;
  return 2 * std::atan2(num, den);
}

using Key = std::tuple<double, double, double>;
Key keyOf(const Vector3d& v)
{
  return {v[0], v[1], v[2]};
}

}  // namespace

FacetedRegion::FacetedRegion(std::vector<Face> faces) : faces_(std::move(faces))
{
  // Closed: every edge once each way, by the exact positions the faces share.
  std::map<std::pair<Key, Key>, int> edges;
  double volume = 0;
  for (const Face& f : faces_) {
    for (const auto& [u, w] : {std::pair(f.a, f.b), std::pair(f.b, f.c), std::pair(f.c, f.a)}) {
      edges[{keyOf(u), keyOf(w)}]++;
    }
    volume += f.a.dot(f.b.cross(f.c));
    reach_ = std::max({reach_, f.a.norm(), f.b.norm(), f.c.norm()});
  }
  closed_ = !faces_.empty() && volume != 0;
  for (const auto& [edge, n] : edges) {
    const auto back = edges.find({edge.second, edge.first});
    closed_ = closed_ && n == 1 && back != edges.end() && back->second == 1;
  }
  orientation_ = volume > 0 ? 1 : -1;
}

SolidRegion::Answer FacetedRegion::classify(const Vector3d& p) const
{
  if (!closed_) return Answer{};
  const double err = forwardError(64, p.norm() + reach_);
  // Which faces the point is near: a named one within its record's band, a
  // plane within the arithmetic's error.
  const GridSurface *record = nullptr;
  const Face *near_named = nullptr;
  double near_named_d = 0;
  bool near_plane = false, two_records = false;
  for (const Face& f : faces_) {
    const double d = (nearestOnTriangle(p, f.a, f.b, f.c) - p).norm();
    if (f.record == nullptr) {
      near_plane = near_plane || d <= err;
      continue;
    }
    const double band = f.record->tessellationBound() + f.record->evaluationBound() + err;
    if (d > band) continue;
    if (record != nullptr && record != f.record.get() && !record->sameAs(*f.record)) two_records = true;
    if (near_named == nullptr || d < near_named_d) {
      near_named = &f;
      near_named_d = d;
      record = f.record.get();
    }
  }
  if (two_records) return Answer{};
  if (near_named == nullptr) {
    if (near_plane) return Answer{Where::On, {}};
    double turns = 0;
    for (const Face& f : faces_) turns += solidAngle(p, f.a, f.b, f.c);
    turns *= orientation_ / (4 * M_PI);
    // Off every face, the angles sum to a whole number of turns; anything else
    // is faces that do not bound what they were said to.
    if (std::fabs(turns - std::round(turns)) > 0.25) return Answer{};
    return Answer{std::round(turns) != 0 ? Where::In : Where::Out, {}};
  }
  if (near_plane) return Answer{};
  Vector3d q;
  if (!record->closestPoint(p, q)) return Answer{};
  if ((p - q).norm() <= record->evaluationBound() + err) return Answer{Where::On, {record}};
  // The face's outward normal: its winding, and the solid's orientation.
  const Vector3d n = (near_named->b - near_named->a).cross(near_named->c - near_named->a) * orientation_;
  const double side = (p - q).dot(n);
  if (side == 0) return Answer{};
  return Answer{side > 0 ? Where::Out : Where::In, {}};
}

std::shared_ptr<const SolidRegion> FacetedRegion::transformed(const Transform3d& mat,
                                                              const Similarity& similarity) const
{
  if (mat.linear().determinant() == 0) return nullptr;
  std::vector<Face> moved;
  moved.reserve(faces_.size());
  std::map<const GridSurface *, std::shared_ptr<const GridSurface>> records;
  for (const Face& f : faces_) {
    Face g{mat * f.a, mat * f.b, mat * f.c, nullptr};
    if (f.record != nullptr) {
      auto& r = records[f.record.get()];
      if (r == nullptr) {
        auto copy = std::dynamic_pointer_cast<GridSurface>(f.record->clone());
        if (copy == nullptr || !copy->transform(mat, similarity)) return nullptr;
        r = copy;
      }
      g.record = r;
    }
    // A mirror turns every face inside out: the faces keep their winding, and
    // the signed volume's sign, recomputed, says which side is in.
    moved.push_back(std::move(g));
  }
  return std::make_shared<FacetedRegion>(std::move(moved));
}

bool FacetedRegion::extent(BoundingBox& out) const
{
  if (faces_.empty()) return false;
  out.setEmpty();
  double band = 0;
  for (const Face& f : faces_) {
    out.extend(f.a);
    out.extend(f.b);
    out.extend(f.c);
    if (f.record != nullptr)
      band = std::max(band, f.record->tessellationBound() + f.record->evaluationBound());
  }
  out.min() -= Vector3d::Constant(band);
  out.max() += Vector3d::Constant(band);
  return true;
}
