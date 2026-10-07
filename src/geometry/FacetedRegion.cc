/*! The solid a faceted generator's faces bound - see FacetedRegion.h. */
#include "geometry/FacetedRegion.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

#include "geometry/MachinePrecision.h"
#include "geometry/Surface.h"
#include "geometry/recognition/Exact.h"

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

/*! How far p is from the segment (a, b). */
double toSegment(const Vector3d& p, const Vector3d& a, const Vector3d& b)
{
  const Vector3d d = b - a;
  const double dd = d.dot(d);
  const double t = dd > 0 ? std::clamp((p - a).dot(d) / dd, 0.0, 1.0) : 0.0;
  return (a + d * t - p).norm();
}

Vector3d centreOf(const FacetedRegion::Face& f)
{
  return (f.a + f.b + f.c) / 3;
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
  bool stated = true;
  band_.reserve(faces_.size());
  for (const Face& f : faces_) {
    for (const auto& [u, w] : {std::pair(f.a, f.b), std::pair(f.b, f.c), std::pair(f.c, f.a)}) {
      edges[{keyOf(u), keyOf(w)}]++;
    }
    volume += f.a.dot(f.b.cross(f.c));
    reach_ = std::max({reach_, f.a.norm(), f.b.norm(), f.c.norm()});
    // How far the face's record may lie from it: a grid's own bound on its
    // tessellation, any other record's for this triangle.
    double band = 0;
    if (const auto *grid = dynamic_cast<const GridSurface *>(f.record.get())) {
      band = grid->tessellationBound() + grid->evaluationBound();
    } else if (f.record != nullptr) {
      stated = stated && f.record->facetBound(f.a, f.b, f.c, band);
    }
    band_.push_back(band);
  }
  closed_ = !faces_.empty() && volume != 0 && stated;
  for (const auto& [edge, n] : edges) {
    const auto back = edges.find({edge.second, edge.first});
    closed_ = closed_ && n == 1 && back != edges.end() && back->second == 1;
  }
  orientation_ = volume > 0 ? 1 : -1;
  // Where each record's piece ends: an edge whose neighbour is no face of it.
  std::map<std::pair<Key, Key>, std::size_t> owner;
  for (std::size_t i = 0; i < faces_.size(); i++) {
    const Face& f = faces_[i];
    for (const auto& [u, w] : {std::pair(f.a, f.b), std::pair(f.b, f.c), std::pair(f.c, f.a)})
      owner[{keyOf(u), keyOf(w)}] = i;
  }
  border_.assign(faces_.size(), {true, true, true});
  for (std::size_t i = 0; i < faces_.size(); i++) {
    const Face& f = faces_[i];
    const Vector3d corners[3] = {f.a, f.b, f.c};
    for (int k = 0; k < 3; k++) {
      const auto across = owner.find({keyOf(corners[(k + 1) % 3]), keyOf(corners[k])});
      border_[i][k] = f.record == nullptr || across == owner.end() ||
                      !sameRecord(faces_[across->second], f.record.get());
    }
  }
  if (!extent(box_)) box_.setEmpty();
}

bool FacetedRegion::sameRecord(const Face& f, const Surface *record) const
{
  return f.record != nullptr && record != nullptr &&
         (f.record.get() == record || f.record->sameAs(*record));
}

Vector3d FacetedRegion::outwardOf(const Face& f) const
{
  // Its winding, and the solid's orientation.
  return ((f.b - f.a).cross(f.c - f.a) * orientation_).normalized();
}

SolidRegion::Answer FacetedRegion::classify(const Vector3d& p) const
{
  if (!closed_) return Answer{};
  const double err = forwardError(64, p.norm() + reach_);
  // Beyond the box every face and every band lies in, out - without asking
  // each face.
  if (!box_.isEmpty() &&
      ((p.array() < box_.min().array() - err).any() || (p.array() > box_.max().array() + err).any()))
    return Answer{Where::Out, {}};
  // Which faces the point is near: a named one within its record's band; and
  // how near the nearest plane face is.
  const Surface *record = nullptr;
  std::size_t near_named = faces_.size();
  double near_named_d = 0;
  double plane_d = std::numeric_limits<double>::infinity();
  bool two_records = false;
  for (std::size_t i = 0; i < faces_.size(); i++) {
    const Face& f = faces_[i];
    const double d = (nearestOnTriangle(p, f.a, f.b, f.c) - p).norm();
    if (f.record == nullptr) {
      plane_d = std::min(plane_d, d);
      continue;
    }
    if (d > band_[i] + err) continue;
    if (record != nullptr && record != f.record.get() && !record->sameAs(*f.record)) two_records = true;
    if (near_named == faces_.size() || d < near_named_d) {
      near_named = i;
      near_named_d = d;
      record = f.record.get();
    }
  }
  if (two_records) return Answer{};
  if (near_named == faces_.size()) {
    if (plane_d <= err) {
      Answer a{Where::On, {}};
      stateSides(p, err, nullptr, Vector3d::Zero(), a);
      return a;
    }
    double turns = 0;
    for (const Face& f : faces_) turns += solidAngle(p, f.a, f.b, f.c);
    turns *= orientation_ / (4 * M_PI);
    // Off every face, the angles sum to a whole number of turns; anything else
    // is faces that do not bound what they were said to.
    if (std::fabs(turns - std::round(turns)) > 0.25) return Answer{};
    return Answer{std::round(turns) != 0 ? Where::In : Where::Out, {}};
  }
  Vector3d q;
  if (!record->closestPoint(p, q)) return Answer{};
  const Hit hit = inPiece(record, q, err);
  if (!hit.found) return Answer{};
  // On the record: as its residual says where it has one in closed form, as
  // far from its point as a grid's evaluation is off where not.
  bool on = false;
  if (const auto *grid = dynamic_cast<const GridSurface *>(record)) {
    on = (p - q).norm() <= grid->evaluationBound() + err;
  } else {
    const recognition::Residual residual = record->signedResidual(p);
    on = std::fabs(residual.value) <= residual.bound + err;
  }
  if (on) return onRecord(record, p, hit, err);
  if (plane_d <= band_[near_named] + err) return Answer{};
  // The face's outward normal: its winding, and the solid's orientation.
  const Face& face = faces_[near_named];
  const Vector3d n = (face.b - face.a).cross(face.c - face.a) * orientation_;
  const double side = (p - q).dot(n);
  if (side == 0) return Answer{};
  return Answer{side > 0 ? Where::Out : Where::In, {}};
}

SolidRegion::Answer FacetedRegion::classifyOn(const Vector3d& p,
                                              const std::vector<const Surface *>& on) const
{
  if (!closed_) return Answer{};
  // Taken onto a record already, the point need not be taken again: it is on
  // the solid where it is in the piece the faces stand for.
  const double err = forwardError(64, p.norm() + reach_);
  for (const Surface *s : on) {
    for (const Face& f : faces_) {
      if (!sameRecord(f, s)) continue;
      const Hit hit = inPiece(f.record.get(), p, err);
      if (hit.found) return onRecord(f.record.get(), p, hit, err);
      break;
    }
  }
  return classify(p);
}

FacetedRegion::Hit FacetedRegion::inPiece(const Surface *record, const Vector3d& q, double err) const
{
  // The piece a face stands for is what the record's nearest point takes it
  // to; q is in it where the record's normal at q meets the face within its
  // band - the face's point there is one q is the nearest point of.
  Hit out;
  Vector3d m;
  if (!record->normalAt(q, m)) {
    // A grid states no normal. Its nearest point is asked only within the
    // band of its own net (GridSurface::closestPoint), whose cells are the
    // faces named for it as the sweeps that declare one make them; a
    // polyhedron naming only some of a grid's cells is not told apart here.
    // Without a normal, it states no side.
    out.found = dynamic_cast<const GridSurface *>(record) != nullptr;
    return out;
  }
  m.normalize();
  for (std::size_t i = 0; i < faces_.size(); i++) {
    const Face& f = faces_[i];
    if (!sameRecord(f, record)) continue;
    const Vector3d n = (f.b - f.a).cross(f.c - f.a);
    const double across = m.dot(n);
    const double cosine = std::fabs(across) / n.norm();
    if (!(cosine > 0)) continue;
    const double s = (f.a - q).dot(n) / across;
    if (std::fabs(s) > band_[i] + err) continue;
    const Vector3d x = q + m * s;
    const double reach = err / cosine;
    if ((nearestOnTriangle(x, f.a, f.b, f.c) - x).norm() > reach) continue;
    if (!out.found) out.outward = m.dot(outwardOf(f)) >= 0 ? m : Vector3d(-m);
    out.found = true;
    // Inside the piece, not where it ends: no border edge within reach.
    const Vector3d corners[3] = {f.a, f.b, f.c};
    bool inner = true;
    for (int k = 0; k < 3; k++) {
      if (border_[i][k] && toSegment(x, corners[k], corners[(k + 1) % 3]) <= reach) inner = false;
    }
    out.inner = out.inner || inner;
  }
  return out;
}

SolidRegion::Answer FacetedRegion::onRecord(const Surface *record, const Vector3d& p, const Hit& hit,
                                            double err) const
{
  Answer a{Where::On, {record}};
  if (hit.outward.norm() == 0) return a;  // a record that states no normal states no side
  if (hit.inner) {
    // Inside its piece the record is the only bound through p, unless a
    // plane face passes through it as well.
    bool plane = false;
    for (const Face& f : faces_) {
      plane = plane || (f.record == nullptr && (nearestOnTriangle(p, f.a, f.b, f.c) - p).norm() <= err);
    }
    if (!plane) {
      a.sides = {Side{hit.outward, record}};
      return a;
    }
  }
  stateSides(p, err, record, hit.outward, a);
  // Where the piece ends and no plane face meets it, the gap between its
  // edge and the face beyond is a bound this does not state.
  if (!hit.inner && a.sides.size() == 1) a.sides.clear();
  return a;
}

void FacetedRegion::stateSides(const Vector3d& p, double err, const Surface *record,
                               const Vector3d& curved, Answer& a) const
{
  // The bounds through p, each with the directions of its faces from p.
  struct Bound {
    Vector3d n;
    const Surface *bound;
    std::vector<Vector3d> towards;
  };
  std::vector<Bound> bounds;
  const bool on_record = curved.norm() > 0;
  if (on_record) bounds.push_back(Bound{curved, record, {}});
  for (const Face& f : faces_) {
    if ((nearestOnTriangle(p, f.a, f.b, f.c) - p).norm() > err) continue;
    const Vector3d d = centreOf(f) - p;
    if (f.record != nullptr) {
      // A face of another record through p: whose side is that, this does
      // not say.
      if (!on_record || !sameRecord(f, record)) return;
      // The piece's direction from p: the face's, along the tangent plane.
      bounds.front().towards.push_back(d - curved * curved.dot(d));
      continue;
    }
    const Vector3d n = outwardOf(f);
    bool placed = false;
    for (Bound& b : bounds) {
      if (b.bound != nullptr || !recognition::parallel(b.n, n) || b.n.dot(n) < 0) continue;
      b.towards.push_back(d);
      placed = true;
      break;
    }
    if (!placed) bounds.push_back(Bound{n, nullptr, {d}});
  }
  if (bounds.empty() || (on_record && bounds.front().towards.empty())) return;
  // Convex where every face is on the inner side of every other bound,
  // concave where on the outer; a face on another's plane says neither.
  bool convex = true, concave = true;
  for (std::size_t i = 0; i < bounds.size(); i++) {
    for (std::size_t j = 0; j < bounds.size(); j++) {
      if (i == j) continue;
      for (const Vector3d& d : bounds[j].towards) {
        const double s = d.dot(bounds[i].n);
        if (std::fabs(s) <= err) return;
        (s < 0 ? concave : convex) = false;
      }
    }
  }
  if (!convex && !concave) return;
  a.all = convex;
  for (const Bound& b : bounds) a.sides.push_back(Side{b.n, b.bound});
}

std::shared_ptr<const SolidRegion> FacetedRegion::transformed(const Transform3d& mat,
                                                              const Similarity& similarity) const
{
  if (mat.linear().determinant() == 0) return nullptr;
  std::vector<Face> moved;
  moved.reserve(faces_.size());
  std::map<const Surface *, std::shared_ptr<const Surface>> records;
  for (const Face& f : faces_) {
    Face g{mat * f.a, mat * f.b, mat * f.c, nullptr};
    if (f.record != nullptr) {
      auto& r = records[f.record.get()];
      if (r == nullptr) {
        r = f.record->transformed(mat, similarity);
        if (r == nullptr) return nullptr;
      }
      g.record = r;
    }
    // A mirror turns every face inside out: the faces keep their winding, and
    // the signed volume's sign, recomputed, says which side is in.
    moved.push_back(std::move(g));
  }
  // A record moved into a kind that states no band - a cylinder under an
  // uneven scale is elliptic - leaves the solid unstated.
  auto out = std::make_shared<FacetedRegion>(std::move(moved));
  return out->closed() ? out : nullptr;
}

bool FacetedRegion::extent(BoundingBox& out) const
{
  if (faces_.empty()) return false;
  out.setEmpty();
  double band = 0;
  for (std::size_t i = 0; i < faces_.size(); i++) {
    const Face& f = faces_[i];
    out.extend(f.a);
    out.extend(f.b);
    out.extend(f.c);
    band = std::max(band, band_[i]);
  }
  out.min() -= Vector3d::Constant(band);
  out.max() += Vector3d::Constant(band);
  return true;
}
