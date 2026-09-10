/*Copyright(c) 2018, slugdev
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met :
1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
3. All advertising materials mentioning features or use of this software
must display the following acknowledgement :
This product includes software developed by slugdev.
4. Neither the name of the slugdev nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY SLUGDEV ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.IN NO EVENT SHALL SLUGDEV BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.*/

#include <limits>
#include "StepKernel.h"
#include "geometry/AnalyticFeatures.h"
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
StepKernel::StepKernel()
{
}

StepKernel::~StepKernel()
{
  // every entity registers itself in `entities` in its constructor, so this
  // frees exactly the entities allocated by this kernel, each of them once
  for (auto *entity : entities) delete entity;
  entities.clear();
}

namespace {

// Newell's method. In contrast to the cross product of the first two edges it
// is stable for concave corners (where the cross product points the wrong way)
// and for polygons whose first three vertices happen to be collinear (where
// the cross product collapses to zero). The magnitude is twice the area.
// A face of no area is three or more collinear points, and the mesh uses it to
// stitch a vertex sitting in the interior of another face's edge back into the
// surface. Sort its points along their common line: the two extremes are the
// span some neighbouring face still crosses in one edge, and everything between
// them is a T-junction that neighbour has to be told about.
//
// Records nothing when the points are not collinear to within weld_eps, which
// is deliberately far tighter than model_tol - these points are collinear to
// within rounding (about 1e-13 on the lid), so anything looser would start
// moving vertices that were never on the edge at all.
static void recordSliverSpan(const std::vector<Vector3d>& vertices, const std::vector<int>& loop,
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

// The axis the normal points along most; dropping it projects the plane onto
// the remaining two coordinates without ever collapsing it.
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

// Crossing number test. The half open comparison on the y coordinate makes a
// ray which passes exactly through a vertex count once instead of twice, which
// is what the 3D ray cast in mergeTriangles() gets wrong on concentric loops.
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

// Does `outer` hold every corner of `inner`, both projected along `drop`?
//
// One probe point says a loop *starts* inside a candidate; it does not say the
// loop is inside it. A hole whose corners fall outside the face carrying it is
// not a hole of that face, and OpenCASCADE says so - InvalidImbricationOfWires,
// on three faces of the bayonet lid, where the chosen parent held 1, 2 and 3 of
// the loop's corners rather than all of them. Every genuine hole on that model
// sits wholly inside its parent, so ask for all of them.
static bool loopContains(const std::vector<Vector3d>& vertices, const std::vector<int>& outer,
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

/*! The maximal stretches of a boundary cycle that lie in one plane.
 *
 * A plane section of a cylinder is an ellipse, which is exact and writable as
 * one - the third way a boundary can be honest about a curved surface, beside
 * running along the axis and running round it at constant height. The mesh does
 * not hand it over: Patch::Run splits the boundary wherever the neighbouring
 * face changes, so a single ellipse arrives as thirty-three runs of one edge
 * each. The stretches have to be found by walking the cycle.
 *
 * A cycle has no first vertex of its own - the flattening picked one - so a
 * left-to-right scan reports whatever stretch straddles that arbitrary seam as
 * two shorter ones, and the neighbouring face, whose seam is elsewhere, then
 * disagrees about where the curve begins. Two faces that disagree cannot share
 * an edge and the shell comes apart. So the search is over the cycle as a
 * circle: grow a plane from every vertex, take the longest, consume its edges,
 * repeat. That answer does not depend on where the cycle was cut.
 *
 * A stretch is only worth having if it covers at least two edges: one edge is a
 * chord, and a chord is what this is trying to get rid of. Indices returned may
 * run past the end of the cycle and are to be read modulo its length.
 */
struct PlaneStretch {
  std::size_t start = 0, count = 0;  // count is vertices, so count - 1 edges
  Vector3d normal;
};

std::vector<PlaneStretch> coplanarStretches(const std::vector<int>& cycle,
                                            const std::vector<Vector3d>& vertices, const Vector3d& axis,
                                            double min_cos_tilt)
{
  std::vector<PlaneStretch> out;
  const std::size_t n = cycle.size();
  if (n < 4) return out;
  auto at = [&](std::size_t k) -> const Vector3d& { return vertices[cycle[k % n]]; };
  // Scale the flatness test to the cycle, so it means the same thing on a coupon
  // measured in millimetres and on one measured in metres. From the bounding
  // box, not from a distance to some chosen vertex: two faces meeting along one
  // stretch see the cycle rotated and reversed, and they have to agree about the
  // stretch or the shell opens - so nothing here may depend on where the cycle
  // starts.
  Vector3d lo = at(0), hi = at(0);
  for (std::size_t k = 1; k < n; k++) {
    lo = lo.cwiseMin(at(k));
    hi = hi.cwiseMax(at(k));
  }
  const double tol = std::max(1e-12, 1e-9 * (hi - lo).norm());

  // The longest run of vertices starting at s that shares one plane, and that
  // plane. Collinear leading vertices do not fix a plane, so the seed keeps
  // reaching forward until it finds one that does.
  auto grow = [&](std::size_t s, Vector3d& normal) -> std::size_t {
    const Vector3d& p0 = at(s);
    std::size_t k = 2;
    Vector3d nn = Vector3d::Zero();
    for (; k < n; k++) {
      nn = (at(s + 1) - p0).cross(at(s + k) - p0);
      if (nn.norm() > tol) break;
    }
    if (k >= n) return 0;
    nn.normalize();
    std::size_t count = k + 1;
    while (count < n && fabs((at(s + count) - p0).dot(nn)) <= tol) count++;
    normal = nn;
    return count;
  };

  std::vector<std::size_t> len(n, 0);
  std::vector<Vector3d> nor(n, Vector3d::Zero());
  for (std::size_t s = 0; s < n; s++) len[s] = grow(s, nor[s]);

  std::vector<char> spent(n, 0);  // spent[i]: the edge from vertex i to i + 1
  for (;;) {
    std::size_t best_s = n, best_len = 0;
    for (std::size_t s = 0; s < n; s++) {
      if (len[s] < 3 || spent[s]) continue;
      std::size_t l = 1;
      while (l < len[s] && !spent[(s + l - 1) % n]) l++;
      if (l >= 3 && l > best_len) {
        best_len = l;
        best_s = s;
      }
    }
    if (best_s == n) break;
    for (std::size_t k = 0; k + 1 < best_len; k++) spent[(best_s + k) % n] = 1;
    // Perpendicular to the axis is a circle, which the arc pass already writes.
    // The floor below it belongs to the surface: on a cylinder any other tilt
    // gives an ellipse, but on a cone the section is one only while the plane
    // still crosses every generator - at the half angle it is a parabola and
    // past it a hyperbola, and neither of those closes. Either way the edges are
    // still spent, so the search does not offer the same stretch back.
    const double cos_tilt = fabs(nor[best_s].dot(axis));
    if (cos_tilt < min_cos_tilt || cos_tilt > 1 - 1e-9) continue;
    PlaneStretch st;
    st.start = best_s;
    st.count = best_len;
    st.normal = nor[best_s];
    out.push_back(st);
  }
  return out;
}

// The implicit-surface machinery these passes solve with - `surfaceImplicit`,
// `projectOntoBoth`, `isQuadric`, `declaredBand` - lives in AnalyticFeatures,
// beside `closestOnSurface`, because it is geometry rather than STEP and
// `export_step.cc` needs the same Newton for its corner placement.
using AnalyticFeatures::declaredBand;
using AnalyticFeatures::isQuadric;
using AnalyticFeatures::projectOntoBoth;
using AnalyticFeatures::surfaceImplicit;

using AnalyticFeatures::projectOntoSurfaceAndPlanes;

// Scratch, read straight after a call: the same worst-case split by whether the
// surface states where it is algebraically or answers by projecting.
double g_worst_quadric = 0, g_worst_fitted = 0;

/*! Why a crossing arc came out as it did.
 *
 * `intersectionArc` knows how close it got and used to throw the number away,
 * so a boundary that fell back to chords reported a count and nothing else -
 * and a count cannot tell a fit that missed by 3.4e-05 from one that missed by
 * a tenth of a millimetre. These are what turn that into a distribution.
 */
struct ArcOutcome {
  int degree = 0;           //!< the degree that answered, or the highest tried
  int sampling_failed = 0;  //!< degrees abandoned because a sample would not project
  double best_error = std::numeric_limits<double>::infinity();  //!< nearest any degree came
  double on_quadric = 0;      //!< of that, how much was against a surface stated algebraically
  double on_fitted = 0;       //!< and how much against one that answers by projecting
  int straddles_crease = -1;  //!< 1 if the arc's ends sit in different profile spans, 0 if not
};

/*! The arc of the curve where two declared quadrics cross, between two points
 *  already on it, as the control points of a Bezier.
 *
 * The curve two quadrics meet along is a quartic in general - two cylinders of
 * unequal radius crossing is the everyday case - and ISO 10303 has no entity for
 * it. OpenCASCADE's own export of such a solid writes a degree-7 B-spline of
 * some thirty control points, so approximating is not a shortcut here: it is
 * what the format offers. What matters is that the approximation is of the
 * *true* curve rather than of the mesh, that it is held to a stated tolerance,
 * and that both faces derive it from the same two declarations and so agree on
 * it exactly - which is what keeps the shell closed without either face knowing
 * what the other did.
 *
 * The degree is raised until the fit is inside `tol` rather than fixed, because
 * how much of the curve one mesh edge spans is not something this can know: a
 * cubic is enough over most of a bore's opening and nowhere near it where the
 * two surfaces run nearly tangent and the curve turns hard. Measured on
 * step-bored-cylinder, a cubic leaves the surfaces by 3.4e-05 - three hundred
 * times better than the chords it replaces, and still not exact.
 */
double intersectionArcError(const Surface *a, const Surface *b, const std::vector<Vector3d>& ctrl);

bool intersectionArc(const Surface *a, const Surface *b, const Vector3d& p0, const Vector3d& p1,
                     double tol, std::vector<Vector3d>& ctrl, ArcOutcome *why = nullptr)
{
  const double scale = std::max(1.0, std::max(p0.norm(), p1.norm()));
  // The band does NOT belong here, and putting it here was wrong once already.
  // A declared grid's band says how far the *mesh* stands off the surface, which
  // is why a mesh vertex is allowed to miss it by that much. It says nothing
  // about how precisely a point can be placed *on* the interpolant, which is
  // what this solves, nor about how closely a Bezier can follow the curve, which
  // is what the fit measures. Widening either to the band makes `intersectionArc`
  // accept a cubic on its first try and write a curve no better than the chord
  // it replaces - measured: identical boundary deviation to seven digits.
  const double solve_tol = 1e-14 * scale;
  const double fit_tol = tol;
  // A declared sweep's profile is a *polyline*, so the surface is creased along
  // every profile corner and its v derivative does not exist there. Where the
  // other surface crosses a crease the intersection curve has a kink, and no
  // polynomial of any degree follows a kink: raising it only moves the miss
  // around. So record whether this edge straddles one, because that is the
  // difference between a fit that wants more degree and one that wants
  // splitting at the crease.
  if (why != nullptr) {
    for (const Surface *sf : {a, b}) {
      const auto *grid = dynamic_cast<const GridSurface *>(sf);
      if (grid == nullptr) continue;
      // Where the creases are is something the surface *states*, in the same
      // description it hands the STEP writer: `splineForm` returns the v knots
      // and their multiplicities, and a knot the surface is only C0 across is a
      // crease by definition. Counting spans from `cols` would be deducing the
      // same thing from an internal, and getting the answer from the
      // declaration is the whole method this exporter is built on.
      int du = 0, dv = 0, nr = 0, nc = 0;
      std::vector<Vector3d> net;
      std::vector<double> ku, kv;
      std::vector<int> mu, mv;
      if (!grid->splineForm(du, dv, nr, nc, net, ku, mu, kv, mv)) continue;
      double u0 = 0, v0 = 0, u1 = 0, v1 = 0;
      if (!grid->project(p0, u0, v0) || !grid->project(p1, u1, v1)) continue;
      const double lo = std::min(v0, v1), hi = std::max(v0, v1);
      why->straddles_crease = 0;
      for (std::size_t k = 0; k < kv.size(); k++) {
        if (mv[k] < dv) continue;  // still C1 across this one
        if (kv[k] > lo + 1e-12 && kv[k] < hi - 1e-12) why->straddles_crease = 1;
      }
    }
  }
  for (int degree = 3; degree <= 9; degree++) {
    // Sample the true curve at the Bezier's own parameters, by pulling the
    // chord's point at each onto both surfaces.
    std::vector<Vector3d> on_curve(degree + 1);
    bool ok = true;
    on_curve[0] = p0;
    on_curve[degree] = p1;
    for (int i = 1; i < degree && ok; i++) {
      Vector3d q = p0 + (p1 - p0) * (double(i) / degree);
      ok = projectOntoBoth(a, b, q, solve_tol);
      on_curve[i] = q;
    }
    // A degree whose samples will not project is not the end of the fit. The
    // next degree samples at *different* parameters - degree 3 asks for 1/3 and
    // 2/3, degree 4 for 1/4, 1/2, 3/4 - so a chord point that lands somewhere
    // Newton cannot recover from says nothing at all about the one after it.
    // Returning here instead threw away every remaining degree on the strength
    // of a single sample, and it is the reason a boundary that had every corner
    // placed still fell back to chords on 47 edges.
    if (!ok) {
      if (why != nullptr) {
        why->sampling_failed++;
        why->degree = degree;
      }
      continue;
    }

    // Interpolate them: solve the Bernstein collocation system for the control
    // points. Small and dense, and well conditioned at these degrees.
    Eigen::MatrixXd m(degree + 1, degree + 1);
    Eigen::MatrixXd rhs(degree + 1, 3);
    for (int i = 0; i <= degree; i++) {
      const double t = double(i) / degree;
      double binom = 1;
      for (int j = 0; j <= degree; j++) {
        m(i, j) = binom * pow(t, j) * pow(1 - t, degree - j);
        binom = binom * (degree - j) / (j + 1);
      }
      rhs.row(i) = on_curve[i].transpose();
    }
    const Eigen::MatrixXd solved = m.colPivHouseholderQr().solve(rhs);
    ctrl.assign(degree + 1, Vector3d::Zero());
    for (int j = 0; j <= degree; j++) ctrl[j] = solved.row(j).transpose();
    g_worst_quadric = 0;
    g_worst_fitted = 0;
    const double err = intersectionArcError(a, b, ctrl);
    if (why != nullptr && err < why->best_error) {
      why->degree = degree;
      why->best_error = err;
      why->on_quadric = g_worst_quadric;
      why->on_fitted = g_worst_fitted;
    }
    if (err <= fit_tol) return true;
  }
  ctrl.clear();
  return false;
}

/*! How far a fitted arc leaves the two surfaces it is supposed to run along. */
double intersectionArcError(const Surface *a, const Surface *b, const std::vector<Vector3d>& ctrl)
{
  const int degree = int(ctrl.size()) - 1;
  double worst = 0;
  for (int k = 1; k < 4 * degree; k++) {
    const double t = double(k) / (4 * degree);
    Vector3d p = Vector3d::Zero();
    double binom = 1;
    for (int j = 0; j <= degree; j++) {
      p += ctrl[j] * (binom * pow(t, j) * pow(1 - t, degree - j));
      binom = binom * (degree - j) / (j + 1);
    }
    for (const Surface *s : {a, b}) {
      double f = 0;
      Vector3d g;
      double d = 0;
      if (!surfaceImplicit(s, p, f, g, &d)) return std::numeric_limits<double>::infinity();
      if (g.norm() < 1e-12) return std::numeric_limits<double>::infinity();
      worst = std::max(worst, d);  // the distance to the surface, not to its tangent plane
      // Which of the two the arc is failing is not a detail: a quadric answers
      // by algebra and a grid by projecting, so a large number here may be the
      // curve missing the surface or the surface failing to locate itself.
      if (isQuadric(s)) g_worst_quadric = std::max(g_worst_quadric, d);
      else g_worst_fitted = std::max(g_worst_fitted, d);
    }
  }
  return worst;
}

/*! The ellipse a plane cuts from a cylinder or a cone.
 *
 * `a` is the semi-axis along `major`, and is the longer of the two - which is
 * the order ISO 10303-42 gives an ELLIPSE its two radii in.
 *
 * A cylinder is the easy half: the section's centre is where the plane meets the
 * axis, the short semi-axis is the cylinder's own radius, and the long one is
 * that divided by the cosine of the tilt.
 *
 * A cone is worked in its plane of symmetry - the one containing the axis and
 * perpendicular to the cutting plane - because that is where the section's two
 * extreme points are. The cone shows there as its two outermost generators and
 * the cutting plane as a line, so the two crossings are the ends of the major
 * axis and the centre and `a` follow from them. `b` is the half chord through
 * the centre at right angles to that, and the cone's own quadratic gives it in
 * one square root: the direction is perpendicular to the axis and to the major
 * axis both, so every linear term in it drops out.
 */
struct SectionEllipse {
  Vector3d centre, major;
  double a = 0, b = 0;
};

bool planeSectionEllipse(const Surface *surface, const Vector3d& normal, const Vector3d& on_plane,
                         SectionEllipse& out)
{
  if (const auto *cyl = dynamic_cast<const CylinderSurface *>(surface)) {
    const Vector3d axis = cyl->normdir.normalized();
    const double cos_tilt = normal.dot(axis);
    if (fabs(cos_tilt) < 1e-9) return false;
    out.centre = cyl->refpt + axis * (normal.dot(on_plane - cyl->refpt) / cos_tilt);
    const Vector3d in_plane = axis - normal * cos_tilt;
    if (in_plane.norm() < 1e-12) return false;
    out.major = in_plane.normalized();
    out.a = cyl->r / fabs(cos_tilt);
    out.b = cyl->r;
    return true;
  }
  const auto *cone = dynamic_cast<const ConeSurface *>(surface);
  if (cone == nullptr || fabs(cone->slope) < 1e-12) return false;
  const Vector3d axis = cone->normdir.normalized();
  const Vector3d apex = cone->refpt + axis * (-cone->r / cone->slope);
  const double cos_a = 1.0 / sqrt(1.0 + cone->slope * cone->slope);
  const double sin_a = fabs(cone->slope) * cos_a;
  if (fabs(normal.dot(axis)) <= sin_a) return false;  // a parabola or a hyperbola
  Vector3d across = normal - axis * normal.dot(axis);
  if (across.norm() < 1e-12) return false;
  across.normalize();
  // Out from the apex is whichever way the radius grows.
  const Vector3d out_dir = cone->slope > 0 ? axis : -axis;
  Vector3d ends[2];
  for (int side = 0; side < 2; side++) {
    const Vector3d gen = out_dir * cos_a + across * (side == 0 ? sin_a : -sin_a);
    const double denom = normal.dot(gen);
    if (fabs(denom) < 1e-12) return false;
    const double along = normal.dot(on_plane - apex) / denom;
    if (along <= 0) return false;  // the other nappe, so not one closed section
    ends[side] = apex + gen * along;
  }
  out.centre = (ends[0] + ends[1]) * 0.5;
  const Vector3d half = (ends[0] - ends[1]) * 0.5;
  out.a = half.norm();
  if (out.a < 1e-12) return false;
  out.major = half / out.a;
  const Vector3d rel = out.centre - apex;
  const double up = rel.dot(axis);
  const double b2 = up * up / (cos_a * cos_a) - rel.squaredNorm();
  if (b2 <= 0) return false;
  out.b = sqrt(b2);
  return out.a >= out.b;
}

/*! How far off perpendicular a plane may tilt and still cut a closed section. */
double sectionTiltFloor(const Surface *surface)
{
  // A cylinder's rulings are parallel, so every plane but one along them closes;
  // a cone's meet, and past the half angle the section runs away to infinity.
  if (const auto *cone = dynamic_cast<const ConeSurface *>(surface)) {
    return fabs(cone->slope) / sqrt(1.0 + cone->slope * cone->slope);
  }
  return 1e-6;
}

/*! A quadric patch's boundary, as cycles, with the plane sections found on it.
 *
 * One place answers this because three ask it - whether a face may be written at
 * all, which sections its faces agree on, and the writer itself - and all three
 * have to agree exactly, because a curve one face writes and its neighbour does
 * not is a hole in the shell.
 *
 * A section's plane comes from one of two places, and which is not a detail.
 * Where the face across it is a *planar* one the plane is that face's own, known
 * exactly and known however short the shared stretch is - a single edge is
 * enough. Only where the neighbour is another quadric is there no plane to be
 * had from anywhere, and then it has to be fitted to three consecutive boundary
 * vertices and grown along the cycle. That fit is the weaker of the two and it
 * is used second: it needs two edges before it can see a plane at all, and a
 * cone cut by a slab meets that cut in a single edge on two of its four
 * regions - which is exactly the case that refused the whole surface while the
 * fit was the only way in.
 *
 * Cycles come back rolled so that no section straddles their seam, since the
 * writer reads a cycle left to right. Sections index into `verts` and never
 * wrap.
 */
struct BoundarySection {
  std::size_t start = 0, count = 0;  // count is vertices, so count - 1 edges
  Vector3d normal, on_plane;
  int loop = -1;          // the planar face across it, or -1 where another quadric is
  bool declared = false;  // the plane came from the model, not from the mesh
};

/*! A plane that cuts a quadric, taken from a face rather than fitted.
 *
 * `declared` says the model stated it, which is the difference that matters: a
 * declaration cannot drift, where a plane fitted to mesh vertices moves with the
 * tessellation, with whatever the boolean did, and with the tolerance the fit
 * was given. Everything derived from it inherits that - the section's centre and
 * its two semi-axes are all differences of the plane against the surface, and a
 * plane that is exact makes them exact too.
 */
struct CutPlane {
  Vector3d normal, on_plane;
  bool declared = false;
};

struct BoundaryCycle {
  std::vector<int> verts;
  std::vector<int> owner;  // per edge: the neighbouring planar loop, or -1
  std::vector<BoundarySection> sections;
};

bool surfaceAxis(const Surface *surface, Vector3d& axis)
{
  if (const auto *cyl = dynamic_cast<const CylinderSurface *>(surface)) {
    axis = cyl->normdir.normalized();
    return true;
  }
  if (const auto *cone = dynamic_cast<const ConeSurface *>(surface)) {
    axis = cone->normdir.normalized();
    return true;
  }
  return false;
}

void findSections(const Surface *surface, const std::vector<Vector3d>& vertices,
                  const std::vector<CutPlane>& planes, BoundaryCycle& cycle,
                  const std::set<std::pair<int, int>>& spoken_for)
{
  cycle.sections.clear();
  Vector3d axis;
  if (!surfaceAxis(surface, axis)) return;
  const double floor_tilt = sectionTiltFloor(surface);
  const std::size_t n = cycle.verts.size();
  if (n < 3) return;

  // Which of the surface's cutting planes each boundary edge lies in. A plane is
  // only usable if it makes a closed section: perpendicular to the axis it makes
  // a circle, which the arc pass writes, and past the floor a cone's section
  // runs away to infinity.
  std::vector<double> scale(planes.size(), 0);
  for (std::size_t p = 0; p < planes.size(); p++) {
    const double cos_tilt = fabs(planes[p].normal.dot(axis));
    if (cos_tilt < floor_tilt || cos_tilt > 1 - 1e-9) continue;
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
    bs.normal = planes[plane_of[e]].normal;
    bs.on_plane = planes[plane_of[e]].on_plane;
    bs.declared = planes[plane_of[e]].declared;
    bs.loop = cycle.owner[e];
    cycle.sections.push_back(bs);
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
    bs.normal = st.normal;
    bs.on_plane = on_plane;
    cycle.sections.push_back(bs);
    for (std::size_t k = 0; k + 1 < st.count; k++) taken_edge[(st.start + k) % n] = 1;
  }
}

/*! A patch's boundary cycles and who is across each edge, with no curves decided
 *  yet - which is what the crossing-curve search needs, since that runs first. */
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

std::map<std::size_t, BoundaryCycle> boundaryCycles(const AnalyticFeatures::Patch& patch,
                                                    const std::vector<Vector3d>& vertices,
                                                    const std::vector<std::vector<int>>& loops,
                                                    const std::vector<char>& loop_valid,
                                                    const std::vector<char>& taken,
                                                    const std::vector<CutPlane>& planes,
                                                    const std::set<std::pair<int, int>>& spoken_for)
{
  std::map<std::size_t, BoundaryCycle> out = rawBoundaryCycles(patch, loops, loop_valid, taken);
  for (auto& entry : out) {
    BoundaryCycle& cycle = entry.second;
    findSections(patch.surface.get(), vertices, planes, cycle, spoken_for);
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
    findSections(patch.surface.get(), vertices, planes, cycle, spoken_for);
  }
  return out;
}

/*! One run of loop edges replaced by a single arc. */
struct ArcSubstitution {
  std::size_t start = 0, count = 0;
  StepKernel::EdgeCurve *edge = nullptr;
  bool sense = true;  // orientation for this loop's own traversal
};

int uf_find(std::vector<int>& parent, int x)
{
  while (parent[x] != x) {
    parent[x] = parent[parent[x]];
    x = parent[x];
  }
  return x;
}

}  // namespace

StepKernel::EdgeCurve *StepKernel::create_line_edge_curve(StepKernel::Vertex *vert1,
                                                          StepKernel::Vertex *vert2, bool dir)
{
  // curve 1
  auto line_point1 = new Point(entities, vert1->point->pt);
  Vector3d v = vert2->point->pt - vert1->point->pt;
  const double len = v.norm();
  // A DIRECTION must have a non zero magnitude. Without this guard a zero
  // length edge is written as DIRECTION('',(0.,0.,0.)), which importers report
  // as a degenerated face.
  if (len < 1e-12) v = Vector3d(1, 0, 0);
  else v /= len;

  auto line_dir1 = new Direction(entities, v);
  auto line_vector1 = new Vector(entities, line_dir1, len > 1e-12 ? len : 1.0);
  auto line1 = new Line(entities, line_point1, line_vector1);
  //  auto surf_curve1 = new SurfaceCurve(entities, line1);
  return new EdgeCurve(entities, vert1, vert2, line1, dir);
}

void StepKernel::build_tri_body(
  const char *name, const std::vector<Vector3d>& mesh_vertices, const std::vector<IndexedFace>& faces,
  const std::vector<std::shared_ptr<Curve>>& curves,
  const std::vector<std::shared_ptr<Surface>>& surfaces, const std::vector<int32_t>& faceOrigin,
  const std::map<int, Vector3d>& cornerMoves, const std::map<int, std::size_t>& singleOwner,
  const std::map<int, std::pair<std::size_t, std::size_t>>& ownerSplit,
  const std::map<int32_t, std::vector<std::size_t>>& owned, const std::vector<int>& faceParents,
  const std::vector<Vector4d>& faceNormals, double tol, bool analytic, bool approximate)
{
  // A working copy, because the corner placement below moves junction corners
  // onto the curve their two declared owners cross along, once recognition has
  // run and it is known which faces would mind. Nothing else writes to this.
  std::vector<Vector3d> vertices = mesh_vertices;

  // `curves` and `surfaces` carry the analytic geometry the model was built
  // from: a ring of N quads is exactly the mesh of an N sided prism, so the
  // facets alone never say which was meant.
  //
  // `surfaces` is what the recogniser matches against. `curves` is deliberately
  // ignored, and it costs nothing today: the only Curve subclass is ArcCurve,
  // the only thing which produces one is import_step.cc, and every arc the
  // exporter writes is a rim it derived from the mesh itself once the band was
  // accepted. A declared arc would only add something for a circular edge whose
  // neighbouring wall is *not* recognised - an imported mesh being written back
  // out - which is a feature nobody has asked for yet.
  (void)curves;
  if (!surfaces.empty()) {
    int cylinders = 0, spheres = 0, tori = 0, patches = 0, grids = 0, cones = 0, planes = 0;
    for (const auto& surface : surfaces) {
      if (dynamic_cast<const PlaneSurface *>(surface.get()) != nullptr) planes++;
      else if (dynamic_cast<const CylinderSurface *>(surface.get()) != nullptr) cylinders++;
      else if (dynamic_cast<const SphereSurface *>(surface.get()) != nullptr) spheres++;
      else if (dynamic_cast<const TorusSurface *>(surface.get()) != nullptr) tori++;
      else if (dynamic_cast<const BezierPatchSurface *>(surface.get()) != nullptr) patches++;
      else if (dynamic_cast<const GridSurface *>(surface.get()) != nullptr) grids++;
      else if (dynamic_cast<const ConeSurface *>(surface.get()) != nullptr) cones++;
    }
    // Swept grids are named only when there are some. Every other kind is listed
    // unconditionally because a zero there is informative - a model that meant
    // to declare a cylinder and did not is what the line exists to expose - but
    // a grid is declared by hand and by name, so its absence says nothing, and
    // mentioning it always would churn every fixture quoting this line.
    std::string extra;
    if (grids > 0) extra = ", " + std::to_string(grids) + " swept grid";
    // A cone is named by hand for the same reason a grid is, so it is listed on
    // the same terms: only when there is one. Listing it always would say
    // nothing and would rewrite the EXPECT line of every fixture here.
    if (cones > 0) extra += ", " + std::to_string(cones) + " conical";
    char pn[64] = "";
    if (planes > 0) {
      snprintf(pn, sizeof(pn), ", and %d declared plane%s", planes, planes == 1 ? "" : "s");
    }
    const std::string plane_note(pn);
    LOG(
      // Planes are counted apart from the curved surfaces rather than added in.
      // This census is what the recognisers have to match facets against, and a
      // declared plane is not that - a merged planar face is already exact, so
      // nothing is recognised *onto* a plane. It is declared so that a face of
      // the model can be told from a chord of a tessellation, which happens
      // after the merge and reads this list directly.
      "STEP export: %1$d analytic surface%2$s available (%3$d cylindrical, %4$d spherical, "
      "%5$d toroidal, %6$d Bezier%7$s)%8$s",
      int(surfaces.size()) - planes, surfaces.size() - planes == 1 ? "" : "s", cylinders, spheres, tori,
      patches, extra, plane_note.c_str());
  }

  const double model_tol = tol > 0 ? tol : 1e-5;
  // twice the area of the smallest polygon still considered a face
  const double area_eps = 1e-12;
  const std::size_t face_cnt = faces.size();

  // Vertices which share the exact same coordinates have to end up as one
  // single VERTEX_POINT. Otherwise every face brings its own copy of each
  // corner and adjacent faces are no longer stitched along their common edge,
  // which is what makes importers report gaps in the shell.
  std::map<std::tuple<double, double, double>, int> point_map;
  std::vector<int> canonical(vertices.size(), 0);
  for (std::size_t i = 0; i < vertices.size(); i++) {
    auto key = std::make_tuple(vertices[i][0], vertices[i][1], vertices[i][2]);
    auto it = point_map.find(key);
    if (it == point_map.end()) {
      point_map.emplace(key, int(i));
      canonical[i] = int(i);
    } else {
      canonical[i] = it->second;
    }
  }

  std::vector<Vertex *> step_verts(vertices.size(), nullptr);
  auto get_vertex = [&](int ind) {
    if (step_verts[ind] == nullptr) {
      auto point = new Point(entities, vertices[ind]);
      step_verts[ind] = new Vertex(entities, point);
    }
    return step_verts[ind];
  };

  // Clean up the loops and derive a usable plane for each of them.
  std::vector<std::vector<int>> loops(face_cnt);
  std::vector<Vector3d> loop_normals(face_cnt, Vector3d(0, 0, 0));
  std::vector<char> loop_valid(face_cnt, 0);
  std::vector<char> loop_is_hole(face_cnt, 0);
  std::vector<int> parents(face_cnt, -1);
  int degenerated_cnt = 0;
  // Which gate rejected them, and how much area went with it. A skipped face
  // leaves every one of its edges used by one face only, so the shell stops
  // being closed - and "skipped 15 degenerated faces" does not say whether that
  // is 15 slivers a merge left behind or 15 real faces being dropped on the
  // floor. The two want opposite responses, so name them apart.
  int collapsed_cnt = 0, zero_area_cnt = 0;
  double lost_area = 0.0;
  // The corners of every sliver we drop. They are wanted again below: a sliver
  // of no area is how a mesh stitches a T-junction, and the vertex in the
  // middle of it has to be put back into the edge that runs past it.
  // Keyed by the two ends of the sliver's span, valued by the points in
  // between, in order along it.
  // Each middle point is kept with its position along the span, measured from
  // the lower-numbered end, so two slivers which share a span merge rather than
  // one replacing the other.
  std::map<std::pair<int, int>, std::vector<std::pair<double, int>>> sliver_spans;

  for (std::size_t i = 0; i < face_cnt; i++) {
    std::vector<int> loop;
    for (std::size_t j = 0; j < faces[i].size(); j++) {
      const int ind = canonical[faces[i][j]];
      // repeated points produce zero length edges
      if (!loop.empty() && loop.back() == ind) continue;
      loop.push_back(ind);
    }
    while (loop.size() >= 2 && loop.front() == loop.back()) loop.pop_back();
    if (loop.size() < 3) {
      // Fewer than three distinct points left: the loop closed on itself.
      degenerated_cnt++;
      collapsed_cnt++;
      continue;
    }

    Vector3d norm = polygonNormal(vertices, loop);
    if (norm.norm() < area_eps) {
      // zero area polygon, exporting it would create a face without a usable
      // surface normal
      degenerated_cnt++;
      zero_area_cnt++;
      lost_area += 0.5 * norm.norm();
      recordSliverSpan(vertices, loop, sliver_spans);
      continue;
    }
    norm.normalize();

    // All triangles of a mergeTriangles() bucket face the same way, so a merged
    // loop which winds the other way round can only be the boundary of a hole -
    // it is never a valid outer loop.
    parents[i] = faceParents[i];
    if (i < faceNormals.size()) {
      const Vector3d ref = faceNormals[i].head<3>();
      if (ref.squaredNorm() > 0.5 && ref.dot(norm) < 0) loop_is_hole[i] = 1;
    }
    if (parents[i] != -1) loop_is_hole[i] = 1;

    loops[i] = loop;
    loop_normals[i] = norm;
    loop_valid[i] = 1;
  }

  // Put the T-junctions back that the skipped slivers were holding shut.
  //
  // A face of no area is not noise. It is three or more collinear points, and
  // the mesh uses it to stitch a vertex sitting in the interior of another
  // face's edge back into the surface - which is why the longest edge of such
  // a sliver is exactly the sum of the others. Refusing to write it is right:
  // a face with no normal has no surface to be written on. Dropping it and
  // stopping there is not. The neighbour still spans the whole edge, the
  // vertex in the middle belongs to no face at all, and every edge the sliver
  // carried is left used once - which is exactly the "shell is not closed" an
  // importer reports, from a mesh that was manifold when it arrived.
  //
  // So split the edge that runs past the vertex. The neighbour's two halves
  // then pair with the two faces that met at the junction, and nothing needs
  // the sliver in order to say so.
  //
  // The tolerance is tight on purpose. These points are collinear to within
  // rounding - the perpendicular distance measured on the lid is around
  // 1e-13 - so there is no reason to reach for model_tol here, and good reason
  // not to: at 1e-5 this would start moving vertices that were never on the
  // edge in the first place.
  int split_cnt = 0;
  std::set<int> welded_verts;
  std::set<std::pair<int, int>> welded_spans;
  if (!sliver_spans.empty()) {
    for (std::size_t i = 0; i < face_cnt; i++) {
      if (!loop_valid[i]) continue;
      const std::size_t n = loops[i].size();
      std::vector<int> out;
      out.reserve(n);
      bool changed = false;
      for (std::size_t j = 0; j < n; j++) {
        const int p = loops[i][j];
        const int q = loops[i][(j + 1) % n];
        out.push_back(p);

        // Only an edge that *is* a sliver's span gets split, and only by that
        // sliver's own middle points. Splitting every edge which merely passes
        // through one of them is too much: two faces can share a span, and
        // both would then hand the same half-edge to a third face.
        const auto it = sliver_spans.find(p < q ? std::make_pair(p, q) : std::make_pair(q, p));
        if (it == sliver_spans.end()) continue;

        // Stored low-to-high along the span; this edge may run either way.
        const std::vector<std::pair<double, int>>& between = it->second;
        if (p < q) {
          for (auto m = between.begin(); m != between.end(); ++m) {
            if (out.back() == m->second) continue;
            out.push_back(m->second);
            welded_verts.insert(m->second);
          }
        } else {
          for (auto m = between.rbegin(); m != between.rend(); ++m) {
            if (out.back() == m->second) continue;
            out.push_back(m->second);
            welded_verts.insert(m->second);
          }
        }
        split_cnt++;
        welded_spans.insert(it->first);
        changed = true;
      }
      // Collinear insertions cannot change the plane, so loop_normals[i] and
      // everything derived from it downstream stay as they were.
      if (changed) loops[i] = out;
    }
  }

  // Work out which face each hole belongs to.
  //
  // mergeTriangles() records that in faceParents, but the search behind it is
  // not reliable in two ways. It keeps the last enclosing loop it happens to
  // find instead of the innermost one, so with concentric loops in one plane a
  // hole ends up on a face further out: the face it really belongs to is then
  // written without its hole and seals the bore, which is the membrane the CAD
  // system shows. And the containment test itself casts a ray through 3D space
  // using a normal taken from the first three vertices of the loop, which fails
  // outright on concentric circular loops (the ray runs exactly through a
  // vertex of the outer loop) and leaves the hole with no parent at all.
  //
  // So do not take faceParents at face value: project the coplanar loops and
  // pick the innermost one that encloses the hole.
  int reparented_cnt = 0, orphan_cnt = 0;
  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i] || !loop_is_hole[i]) continue;

    const int previous = parents[i];
    const int drop = dominantAxis(loop_normals[i]);
    // The loop's first vertex, and not a point interior to it. Probing with an
    // interior point finds more enclosing faces, which sounds strictly better
    // and is not: on concentric rings it parents a hole onto a face which does
    // not own it, and the solid comes apart into two shells. OpenCASCADE reads
    // the result as two solids and adds them - 68422 where step-nested-rings
    // measures 31901, and 998121 where the bayonet lid measures 223482 - and
    // rejects the faces with InvalidImbricationOfWires. Tried, measured,
    // reverted; see doc/step-export-development.md.
    const std::array<double, 2> probe = projectPoint(vertices[loops[i][0]], drop);
    std::vector<std::array<double, 2>> cand;
    int found = -1;
    double best_area = 0;

    for (std::size_t j = 0; j < face_cnt; j++) {
      if (j == i || !loop_valid[j] || loop_is_hole[j]) continue;
      // only loops of the same bucket, i.e. the same plane, can enclose it
      if (i >= faceNormals.size() || j >= faceNormals.size()) continue;
      if (faceNormals[i].head<3>().dot(faceNormals[j].head<3>()) < 0.9999) continue;
      if (fabs(faceNormals[i][3] - faceNormals[j][3]) > 1e-4) continue;

      projectLoop(vertices, loops[j], drop, cand);
      if (!pointInLoop2d(cand, probe)) continue;
      // One probe point says the loop starts inside this candidate; it does not
      // say the loop *is* inside it. See loopContains.
      if (!loopContains(vertices, loops[j], loops[i], drop)) continue;
      const double area = loopArea2d(cand);
      if (found == -1 || area < best_area) {
        found = int(j);
        best_area = area;
      }
    }

    if (found != -1) {
      parents[i] = found;
      if (found != previous) reparented_cnt++;
    } else if (previous != -1 && loop_valid[previous] && !loop_is_hole[previous] &&
               loopContains(vertices, loops[previous], loops[i], drop)) {
      // Keep what mergeTriangles found, but only if it actually holds the loop.
      // Without that condition this branch puts back the very parent the search
      // above just rejected, which is how three faces of the bayonet lid kept
      // their InvalidImbricationOfWires after the search learned to check.
      parents[i] = previous;
    } else {
      // Nothing encloses it, which is the evidence that it is not the boundary
      // of a hole in anything: it is an outer bound. It used to be marked
      // invalid and dropped here, and dropping a face is never an option - its
      // edges are then used by one face instead of two and the shell is open
      // along every one of them. An analytic export of the bayonet lid came out
      // with 94 such edges over 61 faces, all of them in the one annulus where
      // this fired, and the only sign was a line on stdout nobody reads. See
      // *The defects the checks exist for* in doc/step-export-development.md.
      parents[i] = -1;
      loop_is_hole[i] = 0;
      // Keep the winding the loop arrived with. It used to be reversed to agree
      // with the bucket's mesh normal, on the reasoning that a face should agree
      // with the neighbours it shares edges with - but the bucket normal is the
      // wrong reference for exactly the loops which reach this branch. They are
      // the ones nothing encloses, and on the bayonet lid they are notches at
      // the rim whose own winding is what the mesh said; turning them over is
      // what made them disagree with their neighbours. Reversing produced 15
      // edges used twice in the same direction there, and not reversing
      // produces none.
      orphan_cnt++;
    }
  }

  const int welded_span_cnt = int(welded_spans.size());
  if (degenerated_cnt > 0) {
    LOG(message_group::Export_Warning,
        "STEP export: skipped %1$d degenerated face%2$s - %3$d collapsed to fewer "
        "than three distinct points, %4$d had no area (%5$.3g in total)",
        degenerated_cnt, degenerated_cnt == 1 ? "" : "s", collapsed_cnt, zero_area_cnt, lost_area);
  }
  if (zero_area_cnt > welded_span_cnt) {
    // A sliver whose span no face turned out to cross is one this pass could
    // not put back, and its edges stay used once.
    LOG(message_group::Export_Warning,
        "STEP export: %1$d skipped sliver%2$s had no neighbouring edge to weld into, so "
        "the shell may not be closed there",
        zero_area_cnt - welded_span_cnt, zero_area_cnt - welded_span_cnt == 1 ? "" : "s");
  }
  if (!welded_verts.empty()) {
    const int wv = int(welded_verts.size());
    LOG(message_group::Export_Warning,
        "STEP export: welded %1$d T-junction vertex%2$s back into %3$d edge%4$s, so the "
        "faces beside the skipped slivers still pair up",
        wv, wv == 1 ? "" : "es", split_cnt, split_cnt == 1 ? "" : "s");
  }
  if (reparented_cnt > 0) {
    LOG(message_group::Export_Warning, "STEP export: moved %1$d hole%2$s to the enclosing face",
        reparented_cnt, reparented_cnt == 1 ? "" : "s");
  }
  if (orphan_cnt > 0) {
    LOG(message_group::Export_Warning,
        "STEP export: kept %1$d reversed loop%2$s without an enclosing face as %3$s own face",
        orphan_cnt, orphan_cnt == 1 ? "" : "s", orphan_cnt == 1 ? "its" : "their");
  }

  std::vector<Face *> sfaces_extra;
  std::vector<std::vector<EdgeCurve *>> face_edges_extra;

  // Declared here rather than with the loop building below because a partial
  // cylinder's two end edges are ordinary straight edges shared with a
  // neighbouring planar face, so both have to come from the same map.
  std::map<std::pair<int, int>, EdgeCurve *> edge_map;
  int merged_edge_cnt = 0;

  // Recognise the bands of facets that were modelled as a surface of
  // revolution. The recogniser is format neutral and lives in
  // geometry/AnalyticFeatures - everything below this point is the STEP
  // specific half, turning its answer into entities.
  AnalyticFeatures::Result features;
  std::vector<AnalyticFeatures::Patch> bezier_patches;
  // Runs whose curve is exactly a circular arc, keyed by their vertices - which
  // is how the emitter shares one EdgeCurve between the two faces that meet
  // there. Decided per run rather than per face; see where it is filled.
  std::set<std::set<int>> circular_runs;
  // Declared sweeps whose claimed region can be written as one face: a strip,
  // whose boundary stays inside the surface's parameter rectangle. One closing
  // around its profile crosses the surface's seam and is left faceted.
  std::vector<AnalyticFeatures::Patch> grid_faces;
  std::vector<AnalyticFeatures::Patch> quadric_faces;
  // The plane sections that both of their two faces agree to write as one
  // conic, each named by the set of mesh vertices it runs through. A curve one
  // face writes and its neighbour does not is a hole in the shell, so this is a
  // joint decision and not one a face can take for itself.
  std::set<std::set<int>> section_curves;
  // The planes the model declared, kept where the writer can reach them. A
  // declared plane cannot drift with the mesh, and a section derived from one is
  // exact rather than merely close, so it is preferred wherever it agrees with
  // the face at hand.
  std::vector<std::pair<Vector3d, Vector3d>> declared_planes;  // point, unit normal
  // Edges that lie on the curve where two declared quadrics cross, and which two
  // they are. Both faces derive the arc from the same pair of declarations, so
  // they agree on it exactly and the shell closes without either having to know
  // what the other did - which is what makes a curve taken from declarations
  // different in kind from one fitted to the mesh.
  std::map<std::pair<int, int>, std::pair<const Surface *, const Surface *>> crossing_edges;
  std::set<std::pair<int, int>> crossing_keys;  // the same, as the sections' skip set
  double crossing_fit = 0;                      // the worst any of those arcs leaves either surface
  // What the fitter knew and used to discard. A count of chorded edges cannot
  // tell a fit that missed by 3.4e-05 from one that missed by a tenth of a
  // millimetre, and those want different work.
  std::size_t arc_refused = 0, arc_no_fit = 0, arc_creased = 0, arc_uncreased = 0;
  std::vector<double> arc_errors, arc_on_quadric, arc_on_fitted;
  // Deciding which plane sections get written, over a given set of faces. This
  // is a function rather than a step because it has to be asked **twice**, on
  // two different meshes: once while it is being settled which faces are
  // analytic at all, and again immediately before they are written. The corner
  // placement runs between those two points and moves vertices - by up to 0.07
  // on step-band-family - so a section agreed on the first mesh need not exist
  // on the second, and a curve one face writes and its neighbour does not is a
  // hole in the shell. Measured before this was split in two: 132 edges with one
  // face, every one of them an ellipse the other side had stopped seeing.
  std::function<void(const std::vector<const AnalyticFeatures::Patch *>&)> decide_sections;
  // Which facet loops are not available to be written as planar faces. Filled
  // by the quadric pass and read again by its writer, so both sides of every
  // section agree about who is across it.
  std::vector<char> taken;
  // The planes that cut each quadric surface, gathered once so that every face
  // of that surface sees the same list and they agree about the curve between
  // them. Filled by the quadric pass, read again by its writer.
  std::map<const Surface *, std::vector<CutPlane>> section_planes;
  // Parallel to `bezier_patches`: the exact quadric that patch lies on, or null
  // where it is written as the spline it is in general. See quadricOfPatch.
  std::vector<std::shared_ptr<Surface>> patch_quadric;
  features.consumed.assign(face_cnt, 0);  // nothing collapsed unless it says so
  if (analytic) {
    // Say so even when there is nothing, so that a model whose declarations
    // never arrived cannot be mistaken for a build that predates them. The
    // availability line above prints only when the list is non-empty, which
    // makes those two cases look identical - silence.
    if (surfaces.empty()) LOG("STEP export: no analytic surfaces were declared");
    // How sharp an edge still counts as one surface. It is the whole of the
    // intent judgement the approximation pass makes, so it is read once, here,
    // and used by both the fitting and the measuring below.
    double smooth_angle = 25.0;
    if (const char *env = getenv("OPENSCAD_STEP_SMOOTH_ANGLE")) smooth_angle = atof(env);
    smooth_angle *= M_PI / 180.0;
    AnalyticFeatures::Mesh mesh;
    mesh.vertices = &vertices;
    mesh.loops = &loops;
    mesh.valid = &loop_valid;
    mesh.is_hole = &loop_is_hole;
    mesh.normals = &loop_normals;
    features = AnalyticFeatures::recogniseSurfacesOfRevolution(mesh, surfaces, model_tol);

    // The approximation pass, and the only place in this exporter where a
    // surface is written that the model never declared.
    //
    // It runs on what the declared pass left over, fits a cylinder to each
    // smooth region there, and then simply *declares* it - after which the
    // ordinary recogniser does everything else, including refusing the fit if
    // the mesh does not lie on it after all. That is the whole design: the
    // approximation contributes a declaration, not a face, so nothing
    // downstream has to trust it.
    //
    // What makes declaring on the model's behalf defensible is the region
    // rather than the fit. Regions are grown across edges meeting at less than
    // the smoothing angle, so a hexagonal prism - which is the same mesh as a
    // six sided tessellation of a cylinder, and the reason the exact path
    // refuses to guess - never forms one.
    std::vector<std::shared_ptr<Surface>> effective = surfaces;
    AnalyticFeatures::Provenance provenance;
    provenance.face_origin = &faceOrigin;
    provenance.owned = &owned;
    provenance.declared = surfaces.size();
    if (approximate) {
      const std::vector<AnalyticFeatures::SmoothRegion> candidates =
        AnalyticFeatures::uncoveredRegions(mesh, features.consumed, smooth_angle);
      std::size_t fitted = 0, recovered = 0, turned = 0, tried = 0, coned = 0;
      std::map<std::string, int> turned_refusals;
      double coarsest = 0;
      for (const auto& region : candidates) {
        if (region.facets.size() < 3) continue;
        tried++;
        std::shared_ptr<Surface> guess = AnalyticFeatures::fitCylinder(mesh, region, model_tol);
        if (guess != nullptr) {
          fitted++;
          coarsest = std::max(coarsest, region.band);
          addSurfaceUnique(effective, guess);
          continue;
        }
        // A cone, which is a quadric fitCylinder cannot describe and correctly
        // refuses - its normals make a constant angle with the axis rather than
        // lying in a plane. The reference lid's hose socket is bored as a cone
        // over most of its depth, so without this most of its wall area has no
        // quadric to be written on at all.
        guess = AnalyticFeatures::fitCone(mesh, region, model_tol);
        if (guess != nullptr) {
          coned++;
          coarsest = std::max(coarsest, region.band);
          addSurfaceUnique(effective, guess);
          continue;
        }
        // Not a quadric, but perhaps still a sweep. `regularity` is what says
        // whether that question can even be asked: fitting needs the facets'
        // ordering, a mesh straight from a generator still has it at every
        // interior vertex, and a mesh a boolean has been through does not.
        // Below the threshold there is nothing to recover and the region is
        // left alone rather than fitted to something plausible.
        // Or a surface of revolution the model turned but could not name. A
        // cone and a sphere are declared here as rings rather than as shapes,
        // so this contributes one cylinder per ring and the band pass makes
        // the cones out of them.
        const char *not_turned = "no reason was given";
        const std::vector<std::shared_ptr<Surface>> rings =
          AnalyticFeatures::fitRevolved(mesh, region, model_tol, &not_turned);
        if (rings.empty()) turned_refusals[not_turned]++;
        if (!rings.empty()) {
          turned++;
          coarsest = std::max(coarsest, region.band);
          for (const auto& ring : rings) addSurfaceUnique(effective, ring);
          continue;
        }
        if (region.regularity < 0.95 || region.interior_vertices == 0) continue;
        const char *why = "no reason was given";
        guess = AnalyticFeatures::gridFromRegion(mesh, region, model_tol, &why);
        if (guess == nullptr) {
          // A region this regular is one the measurement said could be fitted,
          // so failing to recover it is worth a line rather than a silence.
          LOG("STEP export: a region of %1$d facets kept its ordering but was not recovered: %2$s",
              int(region.facets.size()), why);
          continue;
        }
        recovered++;
        coarsest = std::max(coarsest, region.band);
        addSurfaceUnique(effective, guess);
      }
      for (const auto& entry : turned_refusals) {
        LOG("STEP export: %1$d regions are not turned surfaces because %2$s", int(entry.second),
            entry.first.c_str());
      }
      if (tried > 0) {
        LOG(
          "STEP export: approximation took %1$d of %2$d uncovered regions - %3$d as cylinders, "
          "%4$d as cones, %5$d as rings of a turned surface, %6$d as swept grids - the coarsest "
          "tessellated to %7$.4f",
          int(fitted + coned + turned + recovered), int(tried), int(fitted), int(coned), int(turned),
          int(recovered), coarsest);
      }
      if (fitted + turned + recovered > 0) {
        // Re-run with the fits in hand. Cheaper than threading them through the
        // pass that has already run, and it means a fitted surface goes through
        // exactly the checks a declared one does.
        features = AnalyticFeatures::recogniseSurfacesOfRevolution(mesh, effective, model_tol);
      }
    }
    for (const auto& line : features.report) LOG("STEP export: %1$s", line);

    // Bezier patches are recognised here and written further down, with the
    // B_SPLINE_SURFACE_WITH_KNOTS faces. The report is emitted either way, and
    // before the writing: it says whether the fillet declarations reached the
    // exporter and whether the regions and their boundary runs came out right,
    // which a file with no B-spline face in it cannot distinguish from a model
    // that declared none. A patch that is silently not recognised looks exactly
    // like one that was never declared - the same trap the band report exists
    // for.
    std::vector<std::string> patch_report;
    std::vector<AnalyticFeatures::Patch> patches =
      AnalyticFeatures::recogniseBezierPatches(mesh, effective, features.consumed, patch_report);
    for (const auto& line : patch_report) LOG("STEP export: %1$s", line);
    std::size_t curved_runs = 0, straight_runs = 0, mesh_edges = 0, covered = 0, live = 0;
    for (const auto& patch : patches) {
      if (!patch.alive) continue;
      live++;
      covered += patch.facets.size();
      for (const auto& run : patch.runs) {
        (run.straight ? straight_runs : curved_runs)++;
        if (!run.verts.empty()) mesh_edges += run.verts.size() - 1;
      }
    }
    if (live > 0) {
      LOG(
        "STEP export: their boundaries are %1$d curved runs over %2$d mesh edges, and "
        "%3$d straight edges",
        int(curved_runs), int(mesh_edges), int(straight_runs));

      // What each run borders decides whether it can be collapsed at all, and
      // it is the last thing the emission needs that has never been seen on a
      // real model. A run left unresolved is one the substitution cannot make.
      int whole = 0, part = 0, shared = 0, stuck = 0;
      for (const auto& patch : patches) {
        if (!patch.alive) continue;
        for (const auto& run : patch.runs) {
          switch (run.kind) {
          case AnalyticFeatures::Patch::Run::WHOLE_LOOP:  whole++; break;
          case AnalyticFeatures::Patch::Run::LOOP_RUN:    part++; break;
          case AnalyticFeatures::Patch::Run::OTHER_PATCH: shared++; break;
          default:                                        stuck++; break;
          }
        }
      }
      LOG(
        "STEP export: those runs border %1$d whole faces, %2$d stretches of a face, "
        "%3$d other patches, %4$d unresolved",
        whole, part, shared, stuck);
      LOG("STEP export: written as %1$d faces instead of %2$d", int(face_cnt - covered + live),
          int(face_cnt));
    }
    // Only patches whose every boundary can be substituted are written. One
    // that cannot stays faceted, which is always a valid export.
    for (auto& patch : patches) {
      if (!patch.alive) continue;
      for (const auto& run : patch.runs) {
        if (run.kind == AnalyticFeatures::Patch::Run::UNRESOLVED) {
          patch.alive = false;
          patch.dropped = "one of its boundaries borders more than one face";
          break;
        }
      }
      if (!patch.alive) continue;
      for (const std::size_t f : patch.facets) features.consumed[f] = 1;
    }

    // Which of the surviving patches are exactly quadrics.
    //
    // Since the fillet's Beziers went rational an edge strip is an exact
    // cylinder quadrant and a corner an exact sphere octant, and writing them
    // as such is the difference between a face a CAD kernel can offset, thread
    // and pattern and one it merely tolerates. Everything else - a varying
    // radius, faces that are not perpendicular - is a genuine spline and stays
    // one.
    patch_quadric.assign(patches.size(), nullptr);
    std::map<std::string, int> quadric_refusals;
    for (std::size_t p = 0; p < patches.size(); p++) {
      if (!patches[p].alive) continue;
      const auto *bez = dynamic_cast<const BezierPatchSurface *>(patches[p].surface.get());
      if (bez == nullptr) continue;
      const char *why = "no reason was given";
      patch_quadric[p] = AnalyticFeatures::quadricOfPatch(*bez, model_tol, &why);
      if (patch_quadric[p] == nullptr) quadric_refusals[why]++;
    }

    // A curved run shared with two patches becomes one EdgeCurve used by both,
    // so the two have to agree on what kind of curve it is.
    //
    // This used to be settled by withdrawing the quadric: a quadric face was
    // bounded by CIRCLEs and a spline face by curves read off its own net, so a
    // patch whose partner was not a quadric gave up being one, to a fixed point
    // because withdrawing one could withdraw its partner's partner in turn. It
    // cost real faces - OCCT reported six of step-fillet-refusals' thirty
    // splines as exact cylinders of radius sqrt(3), which is exactly the set
    // this rule withdrew.
    //
    // The question it was answering is better asked of the *run*. runCircle
    // reads the declared control net, weights and all, so it says whether that
    // boundary is exactly a circular arc rather than whether the face beside it
    // is a quadric. When both sides say so about the same circle, a CIRCLE lies
    // on both surfaces exactly and bounding either with it is sound - and a
    // kernel offsets and patterns along a circle where it merely tolerates a
    // spline. When they disagree, or only one side is a patch at all, the run
    // stays a spline and it is the *curve* that gives way rather than the face.
    for (std::size_t p = 0; p < patches.size(); p++) {
      if (!patches[p].alive) continue;
      for (const auto& run : patches[p].runs) {
        if (run.straight) continue;
        Vector3d centre, normal;
        double radius = 0;
        if (!AnalyticFeatures::runCircle(patches[p], run, vertices, centre, normal, radius)) {
          continue;
        }
        if (run.kind == AnalyticFeatures::Patch::Run::OTHER_PATCH) {
          // Both nets have to describe the same circle, or the two faces would
          // be bounded by a curve only one of them lies on.
          if (run.patch >= patches.size() || !patches[run.patch].alive) continue;
          const AnalyticFeatures::Patch& other = patches[run.patch];
          if (run.partner >= other.runs.size()) continue;
          Vector3d c2, n2;
          double r2 = 0;
          if (!AnalyticFeatures::runCircle(other, other.runs[run.partner], vertices, c2, n2, r2)) {
            continue;
          }
          const double scale = std::max(1.0, radius);
          if (fabs(r2 - radius) > 1e-9 * scale || (c2 - centre).norm() > 1e-9 * scale) continue;
        }
        circular_runs.insert(std::set<int>(run.verts.begin(), run.verts.end()));
      }
    }

    // Why the rest are not, counted by reason. A patch refused for a reason
    // nobody expected is the only way to tell a genuine spline from a quadric
    // the recogniser is failing to see, and this suite had exactly that: OCCT
    // named six exact cylinders among the splines while this pass reported a
    // bare zero.
    for (const auto& entry : quadric_refusals) {
      LOG("STEP export: %1$d patches are not quadrics because %2$s", int(entry.second),
          entry.first.c_str());
    }

    int quad_cyl = 0, quad_sph = 0;
    for (std::size_t p = 0; p < patches.size(); p++) {
      if (patch_quadric[p] == nullptr) continue;
      if (dynamic_cast<const CylinderSurface *>(patch_quadric[p].get()) != nullptr) quad_cyl++;
      else quad_sph++;
    }
    if (live > 0) {
      LOG(
        "STEP export: %1$d of %2$d patches are exactly quadrics - %3$d cylindrical, "
        "%4$d spherical",
        quad_cyl + quad_sph, int(live), quad_cyl, quad_sph);
    }
    bezier_patches = patches;

    // What is left, and what it would take to do better.
    //
    // Everything above writes a surface only where the model declared one and
    // the mesh fits it exactly. What remains is the geometry OpenSCAD never
    // held the mathematics for - a polyhedron() over a computed point list, a
    // helical thread - and it is written as planes, which is always correct and
    // on the reference part is 99.8% of the uncovered area.
    //
    // `band` is what makes this measurable rather than a matter of taste. The
    // mesh does not say where the true surface is; it says the true surface
    // passes through these vertices and cannot stray further than the sagitta
    // of the facets between them. A fitted surface inside that band asserts
    // nothing the mesh does not already allow. One outside it is inventing
    // geometry - which is exactly what a B-spline fitted through this project's
    // own thread did at the run-out, overshooting 0.378mm where the band is
    // 0.109mm, while scoring 1e-13 against the vertices it interpolated.
    // What a declared sweep claims. This is the other half of the measurement
    // below: uncoveredRegions() says the mesh has lost the ordering a fit would
    // need, and a GridSurface is the generator handing that ordering back. The
    // question it has to answer is whether the declaration still matches the
    // mesh once the booleans have run - a record which matches nothing is
    // harmless but useless, and looks identical to one that was never made.
    for (const auto& surface : effective) {
      const auto *grid = dynamic_cast<const GridSurface *>(surface.get());
      if (grid == nullptr) continue;
      std::size_t whole = 0, partly = 0;
      std::vector<Vector3d> unused;  // pointMember's scratch argument, as elsewhere
      for (std::size_t i = 0; i < loops.size(); i++) {
        if (!loop_valid[i] || loop_is_hole[i]) continue;
        std::size_t corners = 0;
        for (const int v : loops[i]) {
          corners += const_cast<GridSurface *>(grid)->pointMember(unused, vertices[v]);
        }
        if (corners == loops[i].size()) whole++;
        else if (corners > 0) partly++;
      }
      LOG(
        // The band is printed because it is the tolerance being trusted. A
        // declared sweep is smooth and the mesh is its tessellation, so a
        // vertex the boolean created lies on a facet rather than on the
        // surface, up to the sagitta of a station away. That is the widest a
        // claim here can be wrong by, and it is the model's own resolution
        // rather than a constant somebody chose.
        "STEP export: a declared %1$dx%2$d %3$s sweep claims %4$d facets whole, %5$d cut across "
        "it, within its tessellation band of %6$.4f",
        grid->rows, grid->cols, grid->interpolated() ? "cubic" : "linear", int(whole), int(partly),
        grid->tessellationBand());
    }

    // What those claimed facets look like as a face: one sheet, and a boundary
    // split into the runs that would each have to become one curve. That is the
    // step between claiming facets and writing them, and it is reported before
    // anything is written for the same reason the rest of this is - a sweep
    // that cannot be made into a face has to say so, rather than looking like a
    // sweep that was never declared.
    std::vector<std::string> grid_report;
    const std::vector<AnalyticFeatures::Patch> grid_patches =
      AnalyticFeatures::recogniseGridPatches(mesh, effective, features.consumed, grid_report);
    for (const auto& line : grid_report) LOG("STEP export: %1$s", line);

    // Where the claimed region sits on the sweep, which is the question a face
    // has to answer and a count of facets does not. A sweep whose profile is
    // declared closed is a tube, and its surface is closed across v: the region
    // covering every span of the profile wraps that seam, and a face on a
    // surface written as an open rectangle cannot be bounded across it. One
    // covering only some spans is a strip, whose boundary stays inside the
    // rectangle and can be written as it stands.
    std::size_t wrapping = 0, strips = 0;
    for (const auto& patch : grid_patches) {
      if (!patch.alive) continue;
      const auto *g = dynamic_cast<const GridSurface *>(patch.surface.get());
      if (g == nullptr) continue;
      const int segs = g->closed_v ? g->cols : g->cols - 1;
      if (segs < 1) continue;
      std::vector<char> span_used(segs, 0);
      for (const std::size_t f : patch.facets) {
        Vector3d centroid = Vector3d::Zero();
        for (const int v : loops[f]) centroid += vertices[v];
        centroid /= double(loops[f].size());
        double pu = 0, pv = 0;
        if (!g->project(centroid, pu, pv)) continue;
        int span = int(pv * segs);
        span = std::max(0, std::min(segs - 1, span));
        span_used[span] = 1;
      }
      int used = 0;
      for (const char c : span_used) used += c ? 1 : 0;
      const bool wraps = used == segs && g->closed_v;
      if (wraps) wrapping++;
      else {
        strips++;
        // Written only under the approximation flag. Every other analytic face
        // this exporter writes carries a surface the mesh lies on exactly; a
        // declared sweep is matched to within its tessellation band, which is
        // the model's own resolution and not zero.
        if (approximate) {
          grid_faces.push_back(patch);
          for (const std::size_t f : patch.facets) features.consumed[f] = 1;
        }
      }
      LOG("STEP export: its facets lie over %1$d of the profile's %2$d spans - %3$s", used, segs,
          wraps ? "the region closes around the profile, so its face crosses the surface's seam"
                : "the region is a strip, whose boundary stays inside the surface's rectangle");
    }

    if (!grid_faces.empty()) {
      std::size_t written_facets = 0;
      for (const auto& patch : grid_faces) written_facets += patch.facets.size();
      LOG("STEP export: %1$d declared sweep%2$s written as one face each, replacing %3$d facets",
          int(grid_faces.size()), grid_faces.size() == 1 ? "" : "s", int(written_facets));
    }
    if (wrapping > 0 || strips > grid_faces.size()) {
      // Which sweep is blocked on what is worth saying apart. A strip is
      // bounded by the mesh's own edges and needs only the approximation flag;
      // one that closes around its profile crosses a seam, which is a different
      // piece of work.
      LOG(message_group::Export_Warning,
          "STEP export: %1$d declared sweep%2$s left faceted - %3$d wrap the surface's seam, "
          "%4$d await the approximation flag",
          int(wrapping + strips - grid_faces.size()),
          wrapping + strips - grid_faces.size() == 1 ? "" : "s", int(wrapping),
          int(strips - grid_faces.size()));
    }

    // A quadric the band pass could not write, because its trim is not a plane
    // section: a bored cylinder, whose rim is a quartic and whose face is
    // therefore lost for want of a bound. Runs last and over what nothing else
    // claimed, so it can only ever add faces.
    //
    // Which tier it runs in is decided by proof rather than by the flag. A
    // region cut from a declared surface is mostly exact - a boolean puts its
    // new vertices on the facet planes, so it is the fringe where the cut
    // landed that strays off the surface and not the interior - and a face
    // whose every corner is on the surface to 1e-7 asserts nothing the mesh
    // does not already state. Those are written by the exact pass. Spending
    // the tessellation band, which is what makes a face this exporter cannot
    // prove, still needs the approximation flag.
    {
      const double max_off = approximate ? std::numeric_limits<double>::infinity() : 1e-7;
      std::vector<std::string> quadric_report;
      const std::vector<AnalyticFeatures::Patch> found = AnalyticFeatures::recogniseQuadricPatches(
        mesh, effective, features.consumed, provenance, smooth_angle, max_off, quadric_report);
      for (const auto& line : quadric_report) LOG("STEP export: %1$s", line);
      // Corners on the surface are not enough to call a face exact. The
      // boundary between two of them is a straight edge in the mesh, and a
      // straight edge lies on a quadric only if it runs along the axis, or
      // around it at a constant height where it can be written as an arc.
      // Anything else is a chord under a surface that bulges away from it by
      // the sagitta - truthful edge, truthful surface, inconsistent pair, and
      // it is the pair a kernel has to widen its tolerance over.
      //
      // So the exact tier asks for both. Measured on the reference lid, asking
      // only for the corners bought nine more analytic faces and cost 39 edges
      // sagging up to 0.107 off the cylinder they bounded, on an export that
      // had none at all.
      auto boundary_lies_on_surface = [&](const AnalyticFeatures::Patch& patch,
                                          const std::set<std::set<int>>& agreed) {
        Vector3d axis, origin;
        double radius = 0, slope = 0;
        if (const auto *cyl = dynamic_cast<const CylinderSurface *>(patch.surface.get())) {
          axis = cyl->normdir.normalized();
          origin = cyl->refpt;
          radius = cyl->r;
        } else if (const auto *con = dynamic_cast<const ConeSurface *>(patch.surface.get())) {
          axis = con->normdir.normalized();
          origin = con->refpt;
          radius = con->r;
          slope = con->slope;
        } else {
          return false;
        }
        for (const auto& entry : boundaryCycles(patch, vertices, loops, loop_valid, taken,
                                                section_planes[patch.surface.get()], crossing_keys)) {
          const BoundaryCycle& cycle = entry.second;
          const std::size_t n = cycle.verts.size();
          if (n < 3) continue;
          // Edges a plane section covers are exact as the conic written for
          // them, so they are not asked to lie on the surface as chords - but
          // only where the face on the other side writes the same conic, which
          // is what `agreed` records, decided over all the faces at once.
          std::vector<char> covered(n, 0);
          for (const auto& bs : cycle.sections) {
            std::set<int> key;
            for (std::size_t k = 0; k < bs.count; k++) key.insert(cycle.verts[(bs.start + k) % n]);
            if (agreed.find(key) == agreed.end()) continue;
            for (std::size_t k = 0; k + 1 < bs.count; k++) covered[(bs.start + k) % n] = 1;
          }
          // How far a point is off the surface, radially. A cone's radius is a
          // function of height, which is the whole of the difference.
          auto off_surface = [&](const Vector3d& p) {
            const Vector3d rel = p - origin;
            const double along = rel.dot(axis);
            const double want = radius + slope * along;
            return fabs((rel - axis * along).norm() - want);
          };
          for (std::size_t i = 0; i < n; i++) {
            if (covered[i]) continue;
            // An edge on the curve where this surface crosses another declared
            // one is written as that curve, so it is not asked to be a chord.
            const int u = cycle.verts[i], v = cycle.verts[(i + 1) % n];
            if (crossing_edges.count({std::min(u, v), std::max(u, v)}) != 0) continue;
            const Vector3d a = vertices[cycle.verts[i]] - origin;
            const Vector3d b = vertices[cycle.verts[(i + 1) % n]] - origin;
            const Vector3d along = b - a;
            const double rise = fabs(along.dot(axis));
            // Round the axis at constant height is exact as the arc the
            // post-pass will write. A straight edge is exact when the surface
            // contains the whole line, and the midpoint settles that on its own:
            // a line meets a quadric twice unless it lies in it, so a third
            // point on the surface means every point of it is. That is the test
            // rather than "parallel to the axis", which is only a cylinder's
            // answer - a cone's rulings run to its apex, and calling those
            // chords refused four regions of step-cut-cone for edges that were
            // exactly on the cone all along.
            const Vector3d mid = origin + (a + b) * 0.5;
            if (rise > 1e-7 && off_surface(mid) > 1e-7) return false;
          }
        }
        return true;
      };

      for (const auto& sf : effective) {
        const auto *pl = dynamic_cast<const PlaneSurface *>(sf.get());
        if (pl != nullptr) declared_planes.emplace_back(pl->refpt, pl->normdir.normalized());
      }
      decide_sections = [&](const std::vector<const AnalyticFeatures::Patch *>& standing) {
        // Whose facets are not there to be written as a planar face: what an
        // earlier pass consumed, plus what this one is standing to claim.
        taken = features.consumed;
        for (const auto *patch : standing) {
          for (const std::size_t f : patch->facets) taken[f] = 1;
        }
        // A loop a band will use as its rim is spoken for too. It is still
        // written as a planar face, so it does not look consumed, but the band
        // replaces a stretch of it with a circle and the writer takes the band's
        // answer - so a second curve handed to the same loop is dropped in
        // silence.
        for (const auto& rim : features.rims) {
          for (const AnalyticFeatures::RimRef *side : {&rim.first, &rim.second}) {
            if (side->kind == AnalyticFeatures::RimRef::WHOLE_LOOP ||
                side->kind == AnalyticFeatures::RimRef::LOOP_RUN) {
              if (side->loop < taken.size()) taken[side->loop] = 1;
            }
          }
        }
        // And the planes those faces cut each surface with. Gathered per surface
        // rather than per region, because a plane that cuts a cone cuts all of
        // it: the region meeting the cut along a single edge could never have
        // seen the plane from that edge alone, and refused the whole surface for
        // it.
        section_planes.clear();
        for (const auto *patch : standing) {
          std::vector<CutPlane>& list = section_planes[patch->surface.get()];
          for (const auto& run : patch->runs) {
            if (run.loop >= loops.size() || loop_valid[run.loop] == 0) continue;
            if (taken[run.loop] != 0 || loops[run.loop].size() < 3) continue;
            CutPlane cp;
            cp.normal = loop_normals[run.loop].normalized();
            cp.on_plane = vertices[loops[run.loop][0]];
            // A declared plane in preference to the mesh's own. The fit moves
            // with the tessellation and with whatever the boolean left behind;
            // the declaration cannot move at all, and the section derived from
            // it is exact rather than merely close.
            for (const auto& pl : declared_planes) {
              if (fabs(fabs(pl.second.dot(cp.normal)) - 1.0) > 1e-6) continue;
              if (fabs((cp.on_plane - pl.first).dot(pl.second)) > 1e-6) continue;
              cp.normal = cp.normal.dot(pl.second) > 0 ? pl.second : Vector3d(-pl.second);
              cp.on_plane = pl.first;
              cp.declared = true;
              break;
            }
            bool have = false;
            for (const auto& seen : list) {
              have = fabs(fabs(seen.normal.dot(cp.normal)) - 1.0) < 1e-9 &&
                     fabs((cp.on_plane - seen.on_plane).dot(seen.normal)) < 1e-9;
              if (have) break;
            }
            if (!have) list.push_back(cp);
          }
        }
        // Which faces meet along each boundary edge, so that an edge on two
        // declared quadrics can be recognised from either side and written the
        // same way from both.
        std::map<std::pair<int, int>, std::vector<const Surface *>> along;
        auto walk_into_along = [&](const AnalyticFeatures::Patch& patch) {
          for (const auto& entry : rawBoundaryCycles(patch, loops, loop_valid, taken)) {
            const std::vector<int>& verts = entry.second.verts;
            const std::size_t n = verts.size();
            for (std::size_t i = 0; i < n; i++) {
              const int u = verts[i], v = verts[(i + 1) % n];
              along[{std::min(u, v), std::max(u, v)}].push_back(patch.surface.get());
            }
          }
        };
        for (const auto *patch : standing) walk_into_along(*patch);
        // The declared sweeps too. An edge where a sweep meets a cylinder is a
        // crossing of two declarations exactly as much as one between two
        // cylinders is, and leaving the sweeps out of this map is what made the
        // question unaskable rather than answered - `along` would only ever see
        // one owner for such an edge and drop it at the size test below.
        //
        // They are safe to add only because the grid emitter now reads
        // `crossing_curves` as well: an edge admitted here and taken by the
        // cylinder's face while the sweep's face still wrote a chord would give
        // one edge two geometries, which is how this opened the shell on 420
        // edges the first time it was tried.
        for (const auto& patch : grid_faces) walk_into_along(patch);
        crossing_edges.clear();
        double worst_arc = 0;
        for (const auto& entry : along) {
          if (entry.second.size() != 2) continue;
          const Surface *a = entry.second[0], *b = entry.second[1];
          if (a == b) continue;
          const Vector3d& p0 = vertices[entry.first.first];
          const Vector3d& p1 = vertices[entry.first.second];
          const double scale = std::max(1.0, std::max(p0.norm(), p1.norm()));
          // Both ends have to be on both surfaces already. Where a boolean made
          // the vertex that is exactly what it is - the two makers crossing -
          // and where it is not, no arc of theirs belongs here.
          bool on = true;
          for (const Surface *s : {a, b}) {
            for (const Vector3d *p : {&p0, &p1}) {
              double f = 0;
              Vector3d g;
              const double owed = std::max(1e-9 * scale, declaredBand(s));
              double d = 0;
              if (!surfaceImplicit(s, *p, f, g, &d) || g.norm() < 1e-12 || d > owed) {
                on = false;
              }
            }
          }
          if (!on) continue;
          std::vector<Vector3d> ctrl;
          // The same 1e-7 the exact tier holds every other boundary to.
          ArcOutcome why;
          if (!intersectionArc(a, b, p0, p1, 1e-7, ctrl, &why)) {
            arc_refused++;
            if (why.degree == 0 || why.best_error == std::numeric_limits<double>::infinity()) {
              arc_no_fit++;  // no degree ever got as far as a curve to measure
            } else {
              arc_errors.push_back(why.best_error);
              arc_on_quadric.push_back(why.on_quadric);
              arc_on_fitted.push_back(why.on_fitted);
              if (why.straddles_crease == 1) arc_creased++;
              else if (why.straddles_crease == 0) arc_uncreased++;
            }
            continue;
          }
          // Where the crossing curve is *planar* it is a conic, and a conic has
          // an entity of its own: the plane-section pass writes it exactly,
          // where this would write a fitted B-spline through it. Two equal
          // cylinders crossing meet in a pair of true ellipses, and taking them
          // here would be trading an exact curve for an approximation of the
          // same curve. Planarity is the whole test, and it is a property of the
          // two surfaces rather than of anything this pass chose.
          Vector3d centroid = Vector3d::Zero();
          for (const auto& c : ctrl) centroid += c;
          centroid /= double(ctrl.size());
          Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
          for (const auto& c : ctrl) cov += (c - centroid) * (c - centroid).transpose();
          const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(cov);
          // The smallest eigenvalue is the sum of squared distances to the best
          // plane, so its root over the count is the RMS off it.
          const double flat = sqrt(std::max(0.0, es.eigenvalues()(0)) / double(ctrl.size()));
          // ...but only between two of them. The sentence above is a statement
          // about quadrics: two of those meeting in a planar curve meet in a
          // conic, which ELLIPSE states exactly. A *sweep* cut by a plane is
          // not a conic and no pass in this exporter writes one for it, so
          // skipping it here does not hand the edge to something better - it
          // hands it back to the chord it was. Measured on the band family at
          // $fn 32: all 640 edges where the sweep meets the bore pass every
          // other gate and every one of them is planar to 1e-9, because each
          // spans a single facet and a short arc of anything is flat.
          if (flat <= 1e-9 * scale && isQuadric(a) && isQuadric(b)) continue;
          worst_arc = std::max(worst_arc, intersectionArcError(a, b, ctrl));
          crossing_fit = std::max(crossing_fit, worst_arc);
          crossing_edges.emplace(entry.first, std::make_pair(a, b));
        }
        crossing_keys.clear();
        for (const auto& e : crossing_edges) crossing_keys.insert(e.first);
        // A section is written where both its faces will write it. A planar face
        // across it is not a claimant - it never asks for a curve - so it counts
        // as the second, and is handed the same edge when the faces are written.
        std::map<std::set<int>, int> claims;
        for (const auto *patch : standing) {
          for (const auto& entry : boundaryCycles(*patch, vertices, loops, loop_valid, taken,
                                                  section_planes[patch->surface.get()], crossing_keys)) {
            const BoundaryCycle& cycle = entry.second;
            const std::size_t n = cycle.verts.size();
            for (const auto& bs : cycle.sections) {
              std::set<int> key;
              for (std::size_t k = 0; k < bs.count; k++) key.insert(cycle.verts[(bs.start + k) % n]);
              claims[key] += bs.loop >= 0 ? 2 : 1;
            }
          }
        }
        section_curves.clear();
        for (const auto& claim : claims) {
          if (claim.second == 2) section_curves.insert(claim.first);
        }
      };

      // All of a surface or none of it, and that decision and the sections are
      // circular - a face may be writable only because a section covers its
      // chords, and the section is only there while that face stands - so they
      // are settled together, by dropping whatever the last round refused and
      // asking again. Refusals only ever grow, so it stops.
      std::map<const Surface *, bool> surface_ok;
      {
        std::vector<char> standing(found.size(), 0);
        for (std::size_t i = 0; i < found.size(); i++) standing[i] = found[i].alive ? 1 : 0;
        for (;;) {
          std::vector<const AnalyticFeatures::Patch *> live;
          for (std::size_t i = 0; i < found.size(); i++) {
            if (standing[i]) live.push_back(&found[i]);
          }
          decide_sections(live);
          surface_ok.clear();
          for (std::size_t i = 0; i < found.size(); i++) {
            if (!standing[i]) continue;
            const Surface *key = found[i].surface.get();
            const bool ok = boundary_lies_on_surface(found[i], section_curves);
            auto it = surface_ok.find(key);
            if (it == surface_ok.end()) surface_ok.emplace(key, ok);
            else it->second = it->second && ok;
          }
          // Under the approximation flag a face is written whatever its boundary
          // does, so nothing is dropped and one round settles it.
          if (approximate) break;
          bool changed = false;
          for (std::size_t i = 0; i < found.size(); i++) {
            if (standing[i] && !surface_ok[found[i].surface.get()]) {
              standing[i] = 0;
              changed = true;
            }
          }
          if (!changed) break;
        }
      }

      std::size_t claimed_facets = 0, refused = 0, unbounded = 0;
      for (const auto& patch : found) {
        if (!patch.alive) {
          refused++;
          continue;
        }
        if (!approximate && !surface_ok[patch.surface.get()]) {
          unbounded++;
          continue;
        }
        quadric_faces.push_back(patch);
        claimed_facets += patch.facets.size();
        for (const std::size_t f : patch.facets) features.consumed[f] = 1;
      }
      if (!quadric_faces.empty()) {
        LOG(
          "STEP export: %1$d trimmed quadric%2$s written as one face each, replacing %3$d facets "
          "(%4$s)",
          int(quadric_faces.size()), quadric_faces.size() == 1 ? "" : "s", int(claimed_facets),
          approximate ? "within their tessellation band" : "exactly on their surface");
      }
      if (refused > 0) {
        LOG("STEP export: %1$d trimmed quadric region%2$s could not be bounded", int(refused),
            refused == 1 ? "" : "s");
      }
      if (unbounded > 0) {
        LOG(
          "STEP export: %1$d trimmed quadric region%2$s left faceted - the surface is exact but "
          "some of it is bounded off itself, and half a surface is worse than none",
          int(unbounded), unbounded == 1 ? "" : "s");
      }
    }

    if (approximate) {
      // What is left after everything, including the fits above. A region of
      // one or two facets is not among them: a single flat facet is already
      // written exactly as a PLANE, and reporting it as something left faceted
      // says a surface was lost where none was.
      std::vector<AnalyticFeatures::SmoothRegion> regions;
      for (auto& region : AnalyticFeatures::uncoveredRegions(mesh, features.consumed, smooth_angle)) {
        if (region.facets.size() >= 3) regions.push_back(std::move(region));
      }
      std::size_t left = 0;
      double worst_band = 0.0;
      for (const auto& region : regions) {
        left += region.facets.size();
        worst_band = std::max(worst_band, region.band);
      }
      if (regions.empty()) {
        LOG("STEP export: approximation found nothing left to fit");
      } else {
        LOG(
          "STEP export: %1$d smooth region%2$s left faceted, %3$d facets in all; the "
          "tessellation leaves at most %4$.4f to fit inside",
          int(regions.size()), regions.size() == 1 ? "" : "s", int(left), worst_band);
        const std::size_t show = std::min<std::size_t>(regions.size(), 3);
        for (std::size_t i = 0; i < show; i++) {
          LOG(
            "STEP export:   region of %1$d facets, area %2$.1f, band %3$.4f (typical %4$.4f), "
            "worst dihedral %5$.1f degrees",
            int(regions[i].facets.size()), regions[i].area, regions[i].band, regions[i].median_band,
            regions[i].worst_dihedral * 180.0 / M_PI);
          // Whether a fit is even available here, which is a different question
          // from whether it would be accurate.
          LOG(
            "STEP export:     grid %1$.0f% regular over %2$d interior vertices at valence "
            "%3$d - %4$s",
            regions[i].regularity * 100.0, int(regions[i].interior_vertices),
            int(regions[i].modal_valence),
            regions[i].interior_vertices == 0
              ? "too thin to tell - every vertex is on the boundary"
              : (regions[i].regularity > 0.95
                   ? "the generator's ordering survives, a fit could be made"
                   : "the ordering is gone, only a declaration could describe this"));
        }
        // These are the ones the fit could not take, and saying so is the point -
        // a pass which quietly wrote nothing here would look exactly like one
        // which found nothing to write.
        LOG(message_group::Export_Warning,
            "STEP export: %1$d region%2$s faceted, no fit having been found - which is always "
            "a valid export",
            int(regions.size()), regions.size() == 1 ? " stays" : "s stay");
      }
    }
  }
  const std::vector<AnalyticFeatures::Band>& bands = features.bands;
  const std::vector<std::pair<AnalyticFeatures::RimRef, AnalyticFeatures::RimRef>>& rims = features.rims;
  const std::vector<char>& consumed = features.consumed;
  std::map<std::size_t, std::size_t> split_for_corners;  // face -> fan apex, see below

  // Put the junction corners on the curve their two owners cross along - but
  // only once it is known which faces are analytic, and only if no face that
  // keeps a PLANE would be bent by it.
  //
  // A corner two declared surfaces made belongs on their intersection, and
  // reportOwnership has computed that point all along. Where it may be applied
  // is the whole difficulty, and two earlier places are ruled out by
  // measurement, both recorded in doc/step-export-development.md, *Corner
  // placement*: before mergeTriangles it keeps the neighbours planar and
  // destroys the merge, at twelve to sixty-two times the faces; after the merge
  // but before recognition it bends the faceted neighbours by 0.0331 where they
  // are polygons.
  //
  // Here is the place that costs neither. Recognition has already run on the
  // untouched, fully merged mesh, so the claims are what they were; and an
  // analytic face does not care whether its corners are coplanar, only a PLANE
  // does. On step-bored-cone 52 faces use a movable corner and every one of
  // them is written analytic, so nothing is bent and nothing is split.
  //
  // All of them or none, and per export rather than per corner. A boundary half
  // moved is worse than one not moved at all - doc/step-export-development.md,
  // *Half a fix is worse than none*, measured that at 83 faulty faces against 9
  // - so where a polygon that keeps its plane would be bent, this declines the
  // lot and the export is exactly what it was.
  // The other half of the placement: a corner one declared surface shares with
  // a plane the mesh carries and nothing declared.
  //
  // Here rather than beside the two-owner case in reportOwnership, because
  // *which* plane is only answerable here. Asked of raw triangles it cannot be
  // done: a frustum cap shares an original with its wall, and no distance
  // threshold separates a cut facet near the rim from a wall facet, or a thin
  // base sliver from either - three attempts, all in
  // doc/step-export-development.md. mergeTriangles has since answered it
  // exactly, because a merged face that keeps a PLANE *is* a plane: the planes
  // at a corner are the distinct ones among the faces using it.
  //
  // One plane, and the corner goes on the conic where it meets the declared
  // surface. Two, and it is a triple point - the cut and the sliver of base a
  // tilted cut leaves behind - which belongs on neither conic and stays.
  std::map<int, Vector3d> moves = cornerMoves;
  std::size_t on_conic = 0, on_triple = 0, on_own = 0;  // what each path added to `moves`

  // A corner of a trimmed quadric that nothing else holds goes on that quadric.
  //
  // The placement above reaches a corner two surfaces made. It does not reach a
  // corner the *mesh* made in the middle of a facet, which a trim then left on
  // the boundary of the face written on that quadric: nothing declared it, so
  // provenance names no owners for it and no crossing curve applies. It is
  // nonetheless a corner of an analytic face, sitting at the inradius of the
  // polygon the surface was tessellated as - 20(1 - cos(pi/32)) = 0.0963 inside
  // the cylinder its own face is written on.
  //
  // On step-band-family that is the last 39 corners, at the height where the
  // ridge tapers to nothing and the trim runs along the bore's own facets
  // instead of across them. They are moved onto the surface their face names,
  // and only where no other exact declared surface already holds them - a
  // corner on two of them belongs where both agree, which is the crossing curve
  // the paths above compute, not on either one alone.
  if (approximate) {
    std::size_t onto_own = 0;
    double worst_own = 0;
    for (const auto& patch : quadric_faces) {
      const Surface *surf = patch.surface.get();
      if (surf == nullptr) continue;
      for (const auto& run : patch.runs) {
        for (const int v : run.verts) {
          if (v < 0 || std::size_t(v) >= vertices.size()) continue;
          if (moves.count(v) != 0) continue;
          Vector3d q;
          if (!AnalyticFeatures::closestOnSurface(surf, vertices[v], q)) continue;
          const double off = (q - vertices[v]).norm();
          if (off <= 1e-9) continue;
          bool held = false;
          for (const auto& other : surfaces) {
            if (other.get() == surf) continue;
            if (dynamic_cast<const GridSurface *>(other.get()) != nullptr) continue;
            Vector3d qo;
            if (AnalyticFeatures::closestOnSurface(other.get(), vertices[v], qo) &&
                (qo - vertices[v]).norm() <= 1e-9) {
              held = true;
              break;
            }
          }
          if (held) continue;
          moves.emplace(v, q);
          worst_own = std::max(worst_own, off);
          onto_own++;
        }
      }
    }
    on_own = onto_own;
    if (onto_own > 0) {
      LOG(
        "STEP export: %1$d corners of a trimmed quadric that nothing else holds are placed on the "
        "surface their own face is written on, moving at most %2$.4f",
        int(onto_own), worst_own);
    }
  }

  // A welded vertex stays where it is.
  //
  // Those are the insertions that keep the shell conformal: a vertex one face
  // has on an edge another face spans in one line, added to both so they meet
  // edge for edge. It therefore sits on the *boundary* of faces whose loops do
  // not contain it, and the bend gate below - which reads each face's own loop -
  // cannot see what moving it does to them. On step-declare-grid-two-bands one
  // such vertex was moved 1.73e-07 off the flat bottom it lies on the rim of,
  // and every loop in the file was still within 1e-9 of its own plane: the face
  // asserted a plane a corner of it was off, and nothing internal could tell.
  {
    std::size_t held = 0;
    for (auto it = moves.begin(); it != moves.end();) {
      if (welded_verts.count(it->first) != 0) {
        it = moves.erase(it);
        held++;
      } else {
        ++it;
      }
    }
    if (held > 0) {
      LOG(
        "STEP export: %1$d corners are left where the mesh put them: they were welded onto an "
        "edge to keep the shell conformal, and sit on faces whose loops do not name them",
        int(held));
    }
  }
  if (approximate && !singleOwner.empty()) {
    std::map<int, std::vector<std::size_t>> faces_at;
    for (std::size_t i = 0; i < loops.size(); i++) {
      if (!loop_valid[i] || consumed[i] || loops[i].size() < 3) continue;
      for (const int v : loops[i]) faces_at[v].push_back(i);
    }
    std::size_t placed = 0, triple = 0, placed_triple = 0, plane_gave_up = 0;
    // Why the surface-against-line placement gives up, kept apart: these are
    // five unrelated reasons and one number for all of them says nothing about
    // which to work on. `t_offsurf` is the corner's own surface refusing to
    // answer, `t_faces` is the corner not having exactly two mesh planes to
    // cross - not a solver failure at all - `t_parallel` is those two planes
    // being parallel, and `t_solve` is the only one the projection owns.
    std::size_t t_offsurf = 0, t_faces = 0, t_parallel = 0, t_solve = 0;
    std::size_t ts_noconv = 0, ts_offsurf = 0, ts_plane = 0, ts_far = 0;
    double near_min = 0, near_med = 0, near_max = 0;
    std::vector<double> near_misses;
    double worst_plane = 0, worst_triple = 0;
    for (const auto& entry : singleOwner) {
      const int v = entry.first;
      if (v < 0 || std::size_t(v) >= vertices.size()) continue;
      if (moves.count(v) != 0 || entry.second >= surfaces.size()) continue;
      const auto at = faces_at.find(v);
      if (at == faces_at.end()) continue;
      Vector3d pn(0, 0, 0);
      double pd = 0, reach_here = 0;
      bool one = true, any = false;
      for (const std::size_t i : at->second) {
        const Vector3d n = loop_normals[i].normalized();
        const double d = n.dot(vertices[loops[i][0]]);
        for (const int w : loops[i]) {
          reach_here = std::max(reach_here, (vertices[w] - vertices[v]).norm());
        }
        if (!any) {
          pn = n;
          pd = d;
          any = true;
        } else if ((n - pn).norm() > 1e-6 || fabs(d - pd) > 1e-6) {
          one = false;
          break;
        }
      }
      if (!any) continue;
      if (!one) {
        // More than one plane, which is a corner where three exact things meet:
        // the declared surface and two faces of the model. That point is
        // computable, and until now it was declined outright - "belongs on
        // neither conic and stays" - which left 48 corners on step-exact-trim,
        // 6 on step-shared-arc and 2 on step-cut-cone where the mesh put them.
        //
        // The planes have to be sorted first, because not every plane at the
        // corner is a face: some are the tessellation's own chords of the very
        // surface being placed on, and a chord is not a constraint. On
        // step-exact-trim 16 of the 48 corners have three planes and one of
        // them is a chord; taking all three would over-determine the point and
        // decline it again.
        //
        // A chord's plane normal is parallel to the surface's normal there, a
        // face's is not. The bound is the tessellation's own angular half-step:
        // a facet of an n-gon lies within pi/n of the surface normal at any of
        // its corners, and nothing coarser than a hexagon is a tessellation, so
        // cos(pi/6) admits every chord. Measured, chords read 0.885 to 1.000
        // and faces 0.000 to 0.643 - the closest pair being step-band-family's
        // 0.885 against step-cut-cone's 0.643.
        //
        // That margin is the weakest thing here and it is an inference standing
        // in for something the model knew: a declared plane would make this a
        // lookup. See doc/step-export-development.md, *Corner placement*.
        std::vector<std::pair<Vector3d, double>> faces_here;
        double reach_all = 0;
        {
          const Surface *os = surfaces[entry.second].get();
          Vector3d q0;
          if (!AnalyticFeatures::closestOnSurface(os, vertices[v], q0)) {
            triple++;
            t_offsurf++;
            continue;
          }
          const double off0 = (vertices[v] - q0).norm();
          for (const std::size_t i : at->second) {
            const Vector3d n = loop_normals[i].normalized();
            const double d = n.dot(vertices[loops[i][0]]);
            // How far the corner may travel: to its nearest neighbour and no
            // further. The face's whole extent is far too loose - the line two
            // faces cross along can meet the surface twice, and on
            // step-shared-arc the far one is 2.0 away and takes a cone that was
            // exact to 1.41 out. A corner is correcting its own tessellation,
            // so it belongs nearer than the vertex next to it.
            for (const int w : loops[i]) {
              if (w == v) continue;
              const double e = (vertices[w] - vertices[v]).norm();
              if (e > 1e-12) reach_all = reach_all == 0 ? e : std::min(reach_all, e);
            }
            bool have = false;
            for (const auto& w : faces_here) {
              if ((w.first - n).norm() < 1e-6 && fabs(w.second - d) < 1e-6) have = true;
            }
            if (have) continue;
            Vector3d q1;
            if (!AnalyticFeatures::closestOnSurface(os, vertices[v] + n * 1e-4, q1)) continue;
            const double along = fabs(((vertices[v] + n * 1e-4) - q1).norm() - off0) / 1e-4;
            if (along >= cos(M_PI / 6)) continue;  // a chord of this surface
            faces_here.emplace_back(n, d);
          }
        }
        if (faces_here.size() != 2) {
          triple++;
          t_faces++;
          continue;
        }
        // Where the two faces cross is a line; where that line meets the
        // surface is the corner. Projecting onto the line is exact, so this
        // alternates between two closed forms rather than three.
        const Vector3d n1 = faces_here[0].first, n2 = faces_here[1].first;
        const double d1 = faces_here[0].second, d2 = faces_here[1].second;
        const Vector3d dir = n1.cross(n2);
        if (dir.norm() < 1e-9) {
          triple++;
          t_parallel++;
          continue;
        }
        const Surface *own3 = surfaces[entry.second].get();
        Vector3d p3 = vertices[v], q3;
        // Newton on all three at once - the surface and both planes. This used
        // to alternate between projecting onto the surface and onto the line,
        // which converges linearly at cos^2(theta) per cycle and so, under its
        // 64-iteration cap, only for lines meeting the surface above about 30
        // degrees. It gave up on 280 corners across the fixture set, 260 of
        // them on lid10, and none of them because the geometry was not there.
        const double solve_tol = 1e-12 * std::max(1.0, vertices[v].norm());
        double near3 = 0;
        const bool ok3 =
          projectOntoSurfaceAndPlanes(own3, n1, d1, n2, d2, p3, solve_tol, reach_all, &near3);
        const bool got3 = ok3 && AnalyticFeatures::closestOnSurface(own3, p3, q3);
        if (!got3) {
          ts_noconv++;
          if (std::isfinite(near3)) near_misses.push_back(near3);
        } else if ((q3 - p3).norm() > 1e-6) ts_offsurf++;
        else if (fabs(n1.dot(p3) - d1) > 1e-9 || fabs(n2.dot(p3) - d2) > 1e-9) ts_plane++;
        else if ((p3 - vertices[v]).norm() > reach_all) {
          ts_far++;
          (void)reach_all;
        }
        if (!got3 || (q3 - p3).norm() > 1e-6 || fabs(n1.dot(p3) - d1) > 1e-9 ||
            fabs(n2.dot(p3) - d2) > 1e-9 || (p3 - vertices[v]).norm() > reach_all) {
          triple++;
          t_solve++;
          continue;
        }
        worst_triple = std::max(worst_triple, (p3 - vertices[v]).norm());
        moves.emplace(v, p3);
        placed_triple++;
        continue;
      }
      const Surface *own = surfaces[entry.second].get();
      Vector3d p = vertices[v], qa;
      bool ok = true;
      for (int iter = 0; iter < 64; iter++) {
        if (!AnalyticFeatures::closestOnSurface(own, p, qa)) {
          ok = false;
          break;
        }
        const Vector3d qb = qa - pn * (pn.dot(qa) - pd);
        if ((qb - p).norm() < 1e-12) {
          p = qb;
          break;
        }
        p = qb;
      }
      if (!ok || !AnalyticFeatures::closestOnSurface(own, p, qa)) {
        plane_gave_up++;
        continue;
      }
      // Tight, because the file will be read as though it were exact. Accepting
      // 1e-6 here put a corner 1.73e-07 off a plane it was placed *in*, which is
      // outside OpenCASCADE's own Precision::Confusion of 1e-7 - so the face
      // asserted a plane its corner was not on, by the kernel's own reckoning.
      // The alternating projection converges, so there is no reason to accept
      // less than it converges to.
      if ((qa - p).norm() > 1e-9 || fabs(pn.dot(p) - pd) > 1e-9) {
        plane_gave_up++;
        continue;
      }
      const double travel = (p - vertices[v]).norm();
      if (travel > reach_here) {
        plane_gave_up++;
        continue;
      }
      worst_plane = std::max(worst_plane, travel);
      moves.emplace(v, p);
      placed++;
    }
    on_conic = placed;
    on_triple = placed_triple;
    if (placed_triple > 0) {
      LOG(
        "STEP export: %1$d corners where a declared surface meets two faces of the model are "
        "placed where all three cross, moving at most %2$.4f; %3$d are not - %4$d off their own "
        "surface, %5$d without two faces to cross, %6$d on parallel faces, %7$d the solve did not reach",
        int(placed_triple), worst_triple, int(triple), int(t_offsurf), int(t_faces), int(t_parallel),
        int(t_solve));
      if (!near_misses.empty()) {
        std::sort(near_misses.begin(), near_misses.end());
        near_min = near_misses.front();
        near_med = near_misses[near_misses.size() / 2];
        near_max = near_misses.back();
      }
      LOG(
        "STEP export:    of those %1$d: %2$d no answer, %3$d off the surface, %4$d off a plane, %5$d "
        "past the nearest neighbour; closest the line came to the surface inside the window "
        "%6$.3e/%7$.3e/%8$.3e min/med/max",
        int(t_solve), int(ts_noconv), int(ts_offsurf), int(ts_plane), int(ts_far), near_min, near_med,
        near_max);
    }
    if (placed > 0) {
      LOG(
        "STEP export: %1$d corners a declared surface shares with one plane of the mesh reach "
        "the conic where the two cross, moving at most %2$.4f; %3$d do not and stay where the "
        "mesh put them",
        int(placed), worst_plane, int(plane_gave_up));
    }
  }

  // Keep every exact thing the corner is already on.
  //
  // A corner owned by a declared quadric and a declared sweep was being put
  // where the two cross - but the two are not worth the same. The quadric
  // states where its surface is; the sweep was interpolated through the model's
  // stations and publishes a tessellation band saying how well. Aiming at their
  // crossing curve spends an exact surface to satisfy a fitted one.
  //
  // It shows up as a plane. The mesh's flat bottom on step-declare-grid.py is a
  // 95-corner face, and one of its corners is owned by the wall cylinder and
  // the ridge's sweep; placed on their crossing curve it leaves z = 0 by
  // 0.0423, and a face that big cannot be fanned without turning the part's
  // flat bottom into 93 triangles, three of them off level and one by 21
  // degrees. The corner was exactly on that plane, 0.0103 off the cylinder -
  // 19.2(1 - cos(pi/96)), the wall's own sagitta - and 0.0378 off the sweep,
  // which had declared a band of 0.1290. Nothing was wrong with it that the
  // sweep was entitled to complain about.
  //
  // So: a plane the mesh carries *and a declaration vouches for* is kept, and
  // the corner is placed on that plane crossed with its exact owner instead.
  // Measured over the fixture set, 1533 merged planes are bent by a move and
  // exactly 2 are vouched for - the flat bottoms of step-declare-grid.py and
  // -strip, matching a declaration's own plane to 0.00e+00 - so this reaches
  // the case it is for and no other.
  //
  // The direction is what makes it safe: a declaration only ever *confirms* a
  // plane the mesh already has, and never invents one. refpt is not reliably a
  // rim - declare_cylinder takes the caller's centre, and a fitted cylinder's
  // refpt is wherever the fit landed - so a plane the mesh does not carry must
  // not be conjured out of one.
  if (approximate && !moves.empty() && !ownerSplit.empty()) {
    std::map<int, std::vector<std::size_t>> faces_at;
    for (std::size_t i = 0; i < loops.size(); i++) {
      if (!loop_valid[i] || consumed[i] || loops[i].size() < 3) continue;
      for (const int v : loops[i]) faces_at[v].push_back(i);
    }
    std::size_t rerouted = 0, dropped = 0;
    double worst_reroute = 0;
    for (const auto& sp : ownerSplit) {
      const int v = sp.first;
      if (moves.count(v) == 0) continue;
      if (v < 0 || std::size_t(v) >= vertices.size()) continue;
      if (sp.second.first >= surfaces.size() || sp.second.second >= surfaces.size()) continue;
      const auto at = faces_at.find(v);
      if (at == faces_at.end()) continue;
      // The planes at this corner that are faces of the model rather than
      // tessellation, told apart by the declared surface itself.
      //
      // A chord facet of a surface is spanned by two directions tangent to it,
      // so its plane's normal is parallel to the surface's own normal there. A
      // plane that cuts *across* the surface - a cylinder's cap - has a normal
      // perpendicular to it. So the question is only whether the plane's normal
      // is closer to parallel or to perpendicular, which is a midpoint rather
      // than a tuned constant, and the two cases are nowhere near it: measured
      // over the fixtures, cutting planes read 0.000000 to 0.000003 and chords
      // read 0.885 to 1.000.
      //
      // This asks the declaration and not the mesh, which is the point. An
      // earlier version compared the plane against the one a declared surface's
      // refpt is anchored at; that works only where the anchor happens to be
      // the rim, so it vouched for a cylinder's bottom cap and not its top -
      // primitives.cc pushes one record for r1 == r2 - and never for a sweep's
      // end caps. This needs no anchor and no second declaration.
      const Surface *exact = surfaces[sp.second.first].get();
      Vector3d q_at;
      if (!AnalyticFeatures::closestOnSurface(exact, vertices[v], q_at)) continue;
      const double off_at = (vertices[v] - q_at).norm();
      std::vector<std::pair<Vector3d, double>> vouched;
      for (const std::size_t i : at->second) {
        const double nn = loop_normals[i].norm();
        if (nn < 1e-12) continue;
        const Vector3d nhat = loop_normals[i] / nn;
        const double d = nhat.dot(vertices[loops[i][0]]);
        if (fabs(nhat.dot(vertices[v]) - d) > tol) continue;  // the corner is not on it
        // How much of a step along the plane's normal the surface takes back,
        // which is |n . nS| without needing a normal from the Surface API.
        const double delta = 1e-4;
        Vector3d q_off;
        if (!AnalyticFeatures::closestOnSurface(exact, vertices[v] + nhat * delta, q_off)) continue;
        const double along = fabs(((vertices[v] + nhat * delta) - q_off).norm() - off_at) / delta;
        if (along >= 0.5) continue;  // parallel to the surface's normal: a chord
        bool have = false;
        for (const auto& w : vouched) {
          if ((w.first - nhat).norm() < 1e-6 && fabs(w.second - d) < tol) have = true;
        }
        if (!have) vouched.emplace_back(nhat, d);
      }
      if (vouched.empty()) continue;  // nothing exact is being given up
      // Already in every one of them? Then the move keeps them and stands.
      bool leaves = false;
      for (const auto& w : vouched) {
        if (fabs(w.first.dot(moves[v]) - w.second) > 1e-9) leaves = true;
      }
      if (!leaves) continue;
      // Two vouched planes and the exact owner is one constraint too many; a
      // corner on two planes is a triple point already, and it stays.
      if (vouched.size() > 1) {
        moves.erase(v);
        dropped++;
        continue;
      }
      // Place it where the exact owner crosses the plane, nearest to where the
      // mesh put it. Both are exact, so this is the answer rather than a
      // compromise between one and a fit.
      const Surface *own = exact;
      const Vector3d pn = vouched[0].first;
      const double pd = vouched[0].second;
      Vector3d p = vertices[v], q;
      bool ok = true;
      for (int iter = 0; iter < 64; iter++) {
        if (!AnalyticFeatures::closestOnSurface(own, p, q)) {
          ok = false;
          break;
        }
        const Vector3d next = q - pn * (pn.dot(q) - pd);
        if ((next - p).norm() < 1e-12) {
          p = next;
          break;
        }
        p = next;
      }
      if (!ok || !AnalyticFeatures::closestOnSurface(own, p, q) || (q - p).norm() > 1e-6 ||
          fabs(pn.dot(p) - pd) > 1e-9) {
        moves.erase(v);
        dropped++;
        continue;
      }
      // And the fit only has to agree to within what it declared.
      const auto *fit = dynamic_cast<const GridSurface *>(surfaces[sp.second.second].get());
      if (fit != nullptr && AnalyticFeatures::closestOnSurface(fit, p, q) &&
          (q - p).norm() > fit->membershipTolerance()) {
        moves.erase(v);
        dropped++;
        continue;
      }
      worst_reroute = std::max(worst_reroute, (p - vertices[v]).norm());
      moves[v] = p;
      rerouted++;
    }
    if (rerouted > 0 || dropped > 0) {
      LOG(
        "STEP export: %1$d corners are placed on a plane a declaration vouches for crossed with "
        "their exact owner rather than on the fit, moving at most %2$.4f; %3$d stay where the "
        "mesh put them",
        int(rerouted), worst_reroute, int(dropped));
    }
  }

  // Under the approximation flag only. The exact tier asserts nothing the mesh
  // does not already state, and moving a vertex is such an assertion - without
  // this gate step-bored-cone's analytic export comes out with a PLANE
  // disagreeing with the winding of its own bound.
  if (approximate && !moves.empty()) {
    auto at_moved = [&](int v) -> const Vector3d& {
      const auto m = moves.find(v);
      return m == moves.end() ? vertices[v] : m->second;
    };
    std::size_t analytic_faces = 0, bent = 0, turned = 0;
    // Whose moves the refusal actually costs, as against how many faces caused
    // it. The two numbers reported were the count of faces and the count of
    // *all* moves, and neither said how local the damage was.
    std::set<int> bent_verts;
    std::map<std::size_t, std::size_t> splittable;
    for (std::size_t i = 0; i < loops.size(); i++) {
      if (!loop_valid[i]) continue;
      bool uses = false;
      for (const int v : loops[i]) {
        if (moves.count(v) != 0) uses = true;
      }
      if (!uses) continue;
      if (consumed[i]) {
        analytic_faces++;
        continue;
      }
      // A triangle stays planar wherever its corners are and can still turn
      // over: where a corner crosses the line of the opposite edge the winding
      // reverses, and the face then contradicts the normal the shell was
      // oriented by. Splitting cannot rescue one - every triangle of a fan
      // turns with it - so a face that would turn stops the move outright.
      Vector3d after(0, 0, 0);
      for (std::size_t k = 0; k < loops[i].size(); k++) {
        after += at_moved(loops[i][k]).cross(at_moved(loops[i][(k + 1) % loops[i].size()]));
      }
      if (after.dot(loop_normals[i]) <= 0) {
        turned++;
        for (const int w : loops[i]) bent_verts.insert(w);
        continue;
      }
      // Whether it is actually taken out of its plane, rather than whether it
      // has more than three corners. A corner cut by a plane moves *along* that
      // plane - the conic it lands on lies in it - so its face is not bent at
      // all, and assuming otherwise declined the whole of step-cut-cone for a
      // move that could not have touched it.
      // Against the plane the *face* will assert, which is the outer loop's -
      // its point and its normal, exactly as written below - and over every
      // corner that face carries, its holes included.
      //
      // Measuring each loop against its own plane is not the same thing and
      // lets this through: two loops coplanar in the mesh drift apart by a
      // fraction of the move, and a hole's far corner is then off the outer
      // loop's plane by that drift times the face's extent. On
      // step-declare-grid-two-bands that put a corner 1.73e-07 off a disc of
      // radius 19.2 - outside OpenCASCADE's own Precision::Confusion, so the
      // face asserted a plane its corner was not on, while every loop was
      // within 1e-9 of its own.
      const Vector3d& p0 = at_moved(loops[i][0]);
      double out_of_plane = 0;
      for (const int w : loops[i]) {
        out_of_plane = std::max(out_of_plane, fabs((at_moved(w) - p0).dot(loop_normals[i])));
      }
      for (std::size_t j = 0; j < loops.size(); j++) {
        if (!loop_valid[j] || consumed[j] || parents[j] != int(i)) continue;
        for (const int w : loops[j]) {
          out_of_plane = std::max(out_of_plane, fabs((at_moved(w) - p0).dot(loop_normals[i])));
        }
      }
      if (out_of_plane <= 1e-9) continue;
      // Bent, and a bent polygon can be fanned out into triangles instead of
      // being left alone: a triangle is planar wherever its corners are. Only
      // one with a hole cannot, because a fan from one corner triangulates a
      // simple loop and an inner bound would have to be threaded into it.
      bool has_hole = false;
      for (std::size_t j = 0; j < loops.size(); j++) {
        if (loop_valid[j] && !consumed[j] && parents[j] == int(i)) has_hole = true;
      }
      if (has_hole || loops[i].size() < 4) {
        bent++;
        for (const int w : loops[i]) bent_verts.insert(w);
        continue;
      }
      // Which corner the fan starts from is not free, and corner 0 is not the
      // answer. A fan triangulates the polygon only where every ear it cuts is
      // wound the way the polygon is; an ear wound the other way lies *outside*
      // it, and the face written for it contradicts the loop it is bounded by.
      //
      // The move is what makes this bite. Before it these polygons are convex
      // and every corner would do - measured on step-declare-grid-scad, all
      // seven apexes of the 7-gons. After it, three corners of a bore facet sit
      // on the trim line the ridge left, nearly in a line, and a fan from one of
      // them cuts an ear of area -4.0e-05: inverted, and only just. So the apex
      // is chosen for the ear it leaves worst, and the polygon is split only if
      // some corner leaves none of them inverted. On the same fixture that is 2
      // to 3 corners of each polygon, at a worst ear of 0.19 to 0.72 against
      // corner 0's -4.0e-05.
      const Vector3d nhat = loop_normals[i].normalized();
      const std::size_t n = loops[i].size();
      std::size_t best_apex = 0;
      double best_worst_ear = -1;
      for (std::size_t a = 0; a < n; a++) {
        double worst_ear = std::numeric_limits<double>::max();
        for (std::size_t k = 1; k + 1 < n; k++) {
          const Vector3d& q0 = at_moved(loops[i][a]);
          const Vector3d& q1 = at_moved(loops[i][(a + k) % n]);
          const Vector3d& q2 = at_moved(loops[i][(a + k + 1) % n]);
          worst_ear = std::min(worst_ear, 0.5 * (q1 - q0).cross(q2 - q0).dot(nhat));
        }
        if (worst_ear > best_worst_ear) {
          best_worst_ear = worst_ear;
          best_apex = a;
        }
      }
      if (best_worst_ear <= 0) {
        bent++;
        for (const int w : loops[i]) bent_verts.insert(w);
      } else {
        splittable.emplace(i, best_apex);
      }
    }
    // A face that turns over cannot be rescued by splitting - every triangle of
    // the fan turns with it - so it stops the move outright, and so does a
    // polygon that cannot be split. Otherwise the fans go ahead: all of the
    // corners move or none of them do.
    bent += turned;
    std::size_t touched = 0;
    for (const auto& m : moves) {
      if (bent_verts.count(m.first) != 0) touched++;
    }
    if (bent == 0) split_for_corners = splittable;
    if (bent > 0) {
      // All of them or none of them, and the ratio below says how blunt that
      // is: on the band family at $fn 64 it is fourteen corners that touch a
      // bent face against 1249 held for someone else's problem, and those 1249
      // are the whole reason that coupon's boundary stays chorded while $fn
      // 32's does not.
      //
      // Refusing only the implicated corners was built and measured on
      // 2026-09-10, settling until nothing bent. It took f04 from 1 crossing
      // curve to 1198 and lid10 from 1 to 589 - and broke both: `2 edges used
      // by only one face` on the one, two VERTEX_POINTs on the same
      // coordinates on the other, neither caught by the 50-test suite because
      // neither coupon is a fixture. So "half a boundary moved is worse than
      // none" carries more than planarity, and what else it carries has to be
      // found before this can be made local. Open item 11.
      LOG(
        "STEP export: %1$d corners are left where the mesh put them: moving them would bend "
        "%2$d face%3$s that keeps a plane, and half a boundary moved is worse than none - of "
        "those corners %4$d actually touch a bent face, so %5$d are refused for someone else's",
        int(moves.size()), int(bent), bent == 1 ? "" : "s", int(touched), int(moves.size() - touched));
    } else if (analytic_faces > 0) {
      double worst = 0;
      for (const auto& m : moves) {
        if (m.first < 0 || std::size_t(m.first) >= vertices.size()) continue;
        worst = std::max(worst, (m.second - vertices[m.first]).norm());
        vertices[m.first] = m.second;
      }
      // Three populations, not one. `moves` starts as the corners two declared
      // owners cross at and then takes the conic and triple-point placements
      // above, so calling the total "their two declared owners" was false
      // wherever the other paths fired: on step-exact-trim provenance names two
      // owners for *zero* vertices and this line still claimed 40 of them.
      // Each is named now, and they sum to the total by construction - every
      // path checks `moves.count(v)` before inserting.
      LOG(
        "STEP export: %1$d corners moved, by at most %2$.4f - %3$d where two declared owners "
        "cross, %4$d onto a conic with one plane, %5$d where three exact things meet, %6$d onto "
        "the quadric their own face is written on; %7$d analytic faces now bound themselves on "
        "their own surface",
        int(moves.size()), worst, int(cornerMoves.size()), int(on_conic), int(on_triple), int(on_own),
        int(analytic_faces));
      if (!split_for_corners.empty()) {
        std::size_t added = 0;
        for (const auto& sc : split_for_corners) added += loops[sc.first].size() - 3;
        LOG(
          "STEP export: %1$d face%2$s that keep%3$s a plane fanned into triangles to follow them, "
          "adding %4$d face%5$s",
          int(split_for_corners.size()), split_for_corners.size() == 1 ? "" : "s",
          split_for_corners.size() == 1 ? "s" : "", int(added), added == 1 ? "" : "s");
      }
    }
  }

  // Emit the recognised bands: one CYLINDRICAL_SURFACE or CONICAL_SURFACE face
  // each, bounded by a circle or an arc at either rim.
  //
  // A band which covers the full turn is periodic, so it cannot be bounded by
  // the rims alone - the loop walks up a seam and back down it, the same edge
  // used once in each direction. One which does not is bounded by an arc at
  // either rim and the band's two end edges, and needs no seam.
  std::map<std::size_t, std::pair<EdgeCurve *, bool>> rim_of_loop;  // whole loop -> circle
  std::map<std::size_t, std::vector<ArcSubstitution>> arc_subs;     // runs inside a loop
  std::map<std::set<int>, EdgeCurve *> shared_rim_edges;            // rim vertices -> circle

  for (std::size_t i = 0; i < bands.size(); i++) {
    const AnalyticFeatures::Band& band = bands[i];
    if (!band.alive) continue;

    // A torus is closed in both directions, so its face is bounded by nothing
    // but its own two seams: the major circle through one profile station and
    // the meridian circle at one longitude, each a closed circle through the
    // same vertex and each used once in either direction. That is the same four
    // edge loop a periodic cylinder uses - the doubly periodic case needs no
    // new shape, only a second seam - and it needs nothing from the mesh beyond
    // that one vertex, since both circles come out of the declared record.
    // Only the complete one. A run with a free rim at either end - a rounded
    // corner of a revolved profile, which sweeps a quarter of a torus - is a
    // ring like any other band: two rim circles and one seam, written by the
    // generic path below with a ToroidalSurface under it.
    const auto *whole_torus = dynamic_cast<const TorusSurface *>(band.zone.get());
    if (whole_torus != nullptr && band.seam_bottom != band.seam_top) whole_torus = nullptr;
    if (const auto *tor = whole_torus) {
      const Vector3d centre = tor->refpt;
      const Vector3d axis = tor->normdir.normalized();
      const Vector3d corner = vertices[band.seam_bottom];
      const Vector3d rel = corner - centre;
      const double along = rel.dot(axis);
      const Vector3d radial = (rel - axis * along).normalized();
      // the point on the tube's centre circle nearest the corner
      const Vector3d tube = centre + radial * tor->r_major;

      auto placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
        auto point = new Point(entities, origin);
        auto dir_axis = new Direction(entities, dir);
        auto dir_ref = new Direction(entities, towards);
        return new Axis2Placement(entities, dir_axis, dir_ref, point);
      };

      auto surface =
        new ToroidalSurface(entities, "", placement(centre, axis, radial), tor->r_major, tor->r_minor);
      Vertex *vert = get_vertex(band.seam_bottom);

      // the major circle: round the axis, through the corner
      auto major = new Circle(entities, "", placement(centre + axis * along, axis, radial),
                              (rel - axis * along).norm());
      auto edge_major = new EdgeCurve(entities, vert, vert, major, true);

      // the meridian circle: round the tube, in the plane the axis and the
      // radial direction span
      auto meridian = new Circle(
        entities, "", placement(tube, radial.cross(axis), (corner - tube).normalized()), tor->r_minor);
      auto edge_meridian = new EdgeCurve(entities, vert, vert, meridian, true);

      std::vector<OrientedEdge *> loop{
        new OrientedEdge(entities, edge_major, true),
        new OrientedEdge(entities, edge_meridian, true),
        new OrientedEdge(entities, edge_major, false),
        new OrientedEdge(entities, edge_meridian, false),
      };
      auto edge_loop = new EdgeLoop(entities, loop);
      std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};
      sfaces_extra.push_back(new Face(entities, bounds, surface, band.outward));
      face_edges_extra.push_back({edge_major, edge_meridian});
      continue;
    }

    // A sphere which reaches both poles closes on itself, so like the complete
    // torus above it is bounded by its seam and by nothing else. What differs is
    // that a sphere is periodic in one direction only: the seam is a meridian
    // running pole to pole - half of a great circle - used once in either
    // direction, and the poles are where the two usages meet. There is no rim,
    // because the two polar caps that used to bound the run are part of this
    // face now.
    //
    // The poles come from the surface record - centre +/- r along the axis - and
    // not from the mesh, which has a vertex at neither of them. Nothing else
    // refers to them, so inventing them here costs no other loop a rewrite: the
    // same argument the seam itself rests on.
    //
    // OCCT reads this back as one Sphere with two degenerate edges, which are
    // the zero length edges it inserts at the poles itself, and measures the
    // volume as (4/3)pi r^3.
    if (band.pole_closed) {
      const auto *sph = dynamic_cast<const SphereSurface *>(band.zone.get());
      if (sph != nullptr) {
        const Vector3d centre = sph->refpt;
        const Vector3d axis = band.axis.normalized();
        // the longitude the seam runs down, which is also the direction the
        // surface's own parameterisation is measured from
        const Vector3d rel = vertices[band.seam_bottom] - centre;
        const Vector3d radial = (rel - axis * rel.dot(axis)).normalized();

        auto placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
          auto point = new Point(entities, origin);
          auto dir_axis = new Direction(entities, dir);
          auto dir_ref = new Direction(entities, towards);
          return new Axis2Placement(entities, dir_axis, dir_ref, point);
        };

        auto surface = new SphericalSurface(entities, "", placement(centre, axis, radial), sph->r);
        auto south = new Vertex(entities, new Point(entities, centre - axis * sph->r));
        auto north = new Vertex(entities, new Point(entities, centre + axis * sph->r));

        // Parameterised from the south pole, so that a quarter turn reaches the
        // seam's own longitude and a half turn the north pole. `radial x axis`
        // is the normal that makes that the positive sweep; the other way round
        // the arc runs down the far side of the sphere, and the face is bounded
        // by the wrong half of the great circle.
        auto meridian =
          new Circle(entities, "", placement(centre, radial.cross(axis), Vector3d(-axis)), sph->r);
        auto edge_seam = new EdgeCurve(entities, south, north, meridian, true);

        std::vector<OrientedEdge *> loop{
          new OrientedEdge(entities, edge_seam, true),
          new OrientedEdge(entities, edge_seam, false),
        };
        auto edge_loop = new EdgeLoop(entities, loop);
        std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};
        sfaces_extra.push_back(new Face(entities, bounds, surface, band.outward));
        face_edges_extra.push_back({edge_seam});
        continue;
      }
    }

    const Vector3d top_centre = band.base + band.axis * band.height;
    const bool is_cone = band.isCone();

    auto placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
      auto point = new Point(entities, origin);
      auto dir_axis = new Direction(entities, dir);
      auto dir_ref = new Direction(entities, towards);
      return new Axis2Placement(entities, dir_axis, dir_ref, point);
    };

    // a radial direction to measure the surface's own parameterisation from
    const int ref_vertex = band.closed ? band.seam_bottom : band.bottom_set.front();
    const Vector3d rel = vertices[ref_vertex] - band.base;
    const Vector3d ref = (rel - band.axis * band.axis.dot(rel)).normalized();

    SurfaceType *surface = nullptr;
    if (const auto *tor = dynamic_cast<const TorusSurface *>(band.zone.get())) {
      // A partial torus: the placement comes from the record, not from the
      // band's rims. Its own axis and a radial direction through the seam are
      // what the parameterisation is measured from, exactly as for the complete
      // one above.
      const Vector3d axis = tor->normdir.normalized();
      const Vector3d rel_seam = vertices[band.seam_bottom] - tor->refpt;
      const Vector3d radial = (rel_seam - axis * rel_seam.dot(axis)).normalized();
      surface = new ToroidalSurface(entities, "", placement(tor->refpt, axis, radial), tor->r_major,
                                    tor->r_minor);
    } else if (band.zone != nullptr) {
      const auto *sph = dynamic_cast<const SphereSurface *>(band.zone.get());
      surface = new SphericalSurface(entities, "", placement(sph->refpt, band.axis, ref), sph->r);
    } else if (!is_cone) {
      surface =
        new CylindricalSurface(entities, "", placement(band.base, band.axis, ref), band.r_bottom);
    } else {
      // ISO 10303 wants a half angle in (0, pi/2), so a cone which narrows
      // along the axis is written from its other end instead.
      const bool widens = band.r_top > band.r_bottom;
      const Vector3d origin = widens ? band.base : top_centre;
      const Vector3d dir = widens ? band.axis : Vector3d(-band.axis);
      const double r0 = widens ? band.r_bottom : band.r_top;
      const double half_angle = atan(fabs(band.r_top - band.r_bottom) / band.height);
      surface = new ConicalSurface(entities, "", placement(origin, dir, ref), r0, half_angle);
    }

    // the rims
    EdgeCurve *rim_edge[2] = {nullptr, nullptr};
    bool rim_sense[2] = {true, true};
    for (int side = 0; side < 2; side++) {
      const bool bottom = side == 0;
      const AnalyticFeatures::RimRef& rim = bottom ? rims[i].first : rims[i].second;
      const std::vector<int>& level = bottom ? band.bottom_set : band.top_set;
      const Vector3d centre = bottom ? band.base : top_centre;
      const double radius = bottom ? band.r_bottom : band.r_top;
      const std::set<int> key(level.begin(), level.end());

      const auto shared = shared_rim_edges.find(key);
      if (shared != shared_rim_edges.end()) {
        // the other side of a shared rim already made the circle
        rim_edge[side] = shared->second;
        rim_sense[side] = rim.wall_ccw;
        continue;
      }

      if (band.closed) {
        const int seam = bottom ? band.seam_bottom : band.seam_top;
        const Vector3d seam_rel = vertices[seam] - centre;
        RoundType *curve = nullptr;
        if (!bottom && band.top_tilted) {
          // The rim is the section of the cylinder by a plane which is not
          // perpendicular to the axis, so it is an ellipse. Its minor axis has
          // the cylinder's own radius and lies where the cut plane meets the
          // plane through the centre perpendicular to it; its major axis runs
          // up the steepest ascent of the cut and is longer by exactly the
          // secant of the tilt.
          //
          // Unlike a circle the reference direction is not free - it is what
          // says which semi-axis is which - so the seam vertex does not sit at
          // the start of the parameterisation here. A closed edge does not
          // require it to.
          const Vector3d n = band.top_normal;
          const double cos_tilt = n.dot(band.axis);
          const Vector3d major = (band.axis - n * cos_tilt).normalized();
          curve = new Ellipse(entities, "", placement(centre, n, major), radius / cos_tilt, radius);
        } else {
          curve = new Circle(entities, "", placement(centre, band.axis, seam_rel.normalized()), radius);
        }
        Vertex *vert = get_vertex(seam);
        // a full circle is one edge whose two ends are the same vertex
        rim_edge[side] = new EdgeCurve(entities, vert, vert, curve, true);
        rim_sense[side] = rim.wall_ccw;
        if (rim.kind == AnalyticFeatures::RimRef::OTHER_BAND)
          shared_rim_edges.emplace(key, rim_edge[side]);
        else rim_of_loop.emplace(rim.loop, std::make_pair(rim_edge[side], !rim.wall_ccw));
      } else {
        // An arc, from one end of the rim to the other. A CIRCLE is counter
        // clockwise about its own axis, so the arc is always written that way
        // and the face's own direction is carried by rim_sense.
        const int from = rim.ccw_start, to = rim.ccw_end;
        auto circle = new Circle(
          entities, "", placement(centre, band.axis, (vertices[from] - centre).normalized()), radius);
        rim_edge[side] = new EdgeCurve(entities, get_vertex(from), get_vertex(to), circle, true);
        rim_sense[side] = rim.wall_ccw;
        if (rim.kind == AnalyticFeatures::RimRef::OTHER_BAND_ARC) {
          // shared with another partial band: one arc, two curved faces, no
          // planar loop anywhere along it
          shared_rim_edges.emplace(key, rim_edge[side]);
        } else {
          arc_subs[rim.loop].push_back({rim.start, rim.count, rim_edge[side], !rim.wall_ccw});
        }
      }
    }

    std::vector<OrientedEdge *> loop;
    std::vector<EdgeCurve *> face_edges_here;
    if (band.closed) {
      // The seam of a periodic face has to lie *on* the surface. Up a cylinder
      // or a cone that is a straight ruling; over a sphere it is a meridian,
      // and the straight line between the same two vertices is a chord that
      // sags off the surface by far more than the modelling tolerance - 0.05mm
      // on a radius 10 sphere at $fn=32. So a spherical zone seams with an arc
      // of a great circle instead.
      //
      // Nothing else refers to the seam: it is used twice by this one face and
      // by nothing at all in the mesh, so replacing the line costs no
      // neighbouring loop a rewrite.
      EdgeCurve *edge_seam = nullptr;
      if (const auto *tor = dynamic_cast<const TorusSurface *>(band.zone.get())) {
        // Over a torus the seam is a meridian: an arc of the tube's own circle,
        // about the point on the tube's centre circle at the seam's longitude.
        // Same argument as the sphere below - a straight chord sags off the
        // surface - and the same construction, one level in.
        const Vector3d axis = tor->normdir.normalized();
        const Vector3d rel_seam = vertices[band.seam_bottom] - tor->refpt;
        const Vector3d radial = (rel_seam - axis * rel_seam.dot(axis)).normalized();
        const Vector3d tube = tor->refpt + radial * tor->r_major;
        const Vector3d from = (vertices[band.seam_bottom] - tube).normalized();
        const Vector3d to = (vertices[band.seam_top] - tube).normalized();
        const Vector3d normal = from.cross(to);
        auto meridian =
          new Circle(entities, "", placement(tube, normal.normalized(), from), tor->r_minor);
        edge_seam = new EdgeCurve(entities, get_vertex(band.seam_bottom), get_vertex(band.seam_top),
                                  meridian, true);
      } else if (band.zone != nullptr) {
        const auto *sph = dynamic_cast<const SphereSurface *>(band.zone.get());
        const Vector3d from = (vertices[band.seam_bottom] - sph->refpt).normalized();
        const Vector3d to = (vertices[band.seam_top] - sph->refpt).normalized();
        // this normal is the one that makes the sweep from `from` to `to`
        // counter clockwise and shorter than half a turn, which a zone's
        // latitude range always is - the flat cap at either end sees to that
        const Vector3d normal = from.cross(to);
        auto meridian =
          new Circle(entities, "", placement(sph->refpt, normal.normalized(), from), sph->r);
        edge_seam = new EdgeCurve(entities, get_vertex(band.seam_bottom), get_vertex(band.seam_top),
                                  meridian, true);
      } else {
        edge_seam =
          create_line_edge_curve(get_vertex(band.seam_bottom), get_vertex(band.seam_top), true);
      }
      loop.push_back(new OrientedEdge(entities, rim_edge[0], rim_sense[0]));
      loop.push_back(new OrientedEdge(entities, edge_seam, true));
      loop.push_back(new OrientedEdge(entities, rim_edge[1], rim_sense[1]));
      loop.push_back(new OrientedEdge(entities, edge_seam, false));
      face_edges_here = {rim_edge[0], rim_edge[1], edge_seam};
    } else {
      // walk along the bottom rim, up the end edge, back along the top rim,
      // down the other end edge - so the ends are where one rim's traversal
      // finishes and the other's begins
      const int b_from = rims[i].first.traversalStart();
      const int b_to = rims[i].first.traversalEnd();
      const int t_from = rims[i].second.traversalStart();
      const int t_to = rims[i].second.traversalEnd();

      bool up_dir = true, down_dir = true;
      EdgeCurve *edge_up = get_line_from_map(edge_map, b_to, t_from, get_vertex(b_to),
                                             get_vertex(t_from), up_dir, merged_edge_cnt);
      EdgeCurve *edge_down = get_line_from_map(edge_map, t_to, b_from, get_vertex(t_to),
                                               get_vertex(b_from), down_dir, merged_edge_cnt);
      loop.push_back(new OrientedEdge(entities, rim_edge[0], rim_sense[0]));
      loop.push_back(new OrientedEdge(entities, edge_up, up_dir));
      loop.push_back(new OrientedEdge(entities, rim_edge[1], rim_sense[1]));
      loop.push_back(new OrientedEdge(entities, edge_down, down_dir));
      face_edges_here = {rim_edge[0], rim_edge[1], edge_up, edge_down};
    }

    auto edge_loop = new EdgeLoop(entities, loop);
    std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};

    // The surface normal of a cylinder or a cone points away from its axis, so
    // a bore - where the material is outside it - is the opposite sense.
    sfaces_extra.push_back(new Face(entities, bounds, surface, band.outward));
    face_edges_extra.push_back(face_edges_here);
  }

  // ---- Bezier patches ----------------------------------------------------
  //
  // A patch face is bounded by its runs, each of which is one curve read off
  // the same control net the surface is written from - so the curve provably
  // lies on the surface rather than approximately. A seam between two patches
  // is one EdgeCurve used by both, in opposite senses; a run cutting into a
  // planar neighbour is spliced into that loop through the same substitution
  // the arcs use.
  {
    std::map<std::set<int>, EdgeCurve *> run_edges;  // run vertices -> its curve
    auto placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
      auto point = new Point(entities, origin);
      auto dir_axis = new Direction(entities, dir);
      auto dir_ref = new Direction(entities, towards);
      return new Axis2Placement(entities, dir_axis, dir_ref, point);
    };
    for (std::size_t pi = 0; pi < bezier_patches.size(); pi++) {
      const AnalyticFeatures::Patch& patch = bezier_patches[pi];
      if (!patch.alive) continue;
      const auto *bez = dynamic_cast<const BezierPatchSurface *>(patch.surface.get());
      if (bez == nullptr) continue;
      const std::shared_ptr<Surface> quadric = pi < patch_quadric.size() ? patch_quadric[pi] : nullptr;

      SurfaceType *surface = nullptr;
      if (const auto *cyl = dynamic_cast<const CylinderSurface *>(quadric.get())) {
        // The reference direction is the radius through the start of the first
        // rail, so the face occupies theta from zero forwards rather than a
        // stretch wrapping through the surface's own seam. quadricOfPatch has
        // already oriented the axis to make that sweep the positive one.
        const Vector3d axis = cyl->normdir.normalized();
        const Vector3d rel = bez->control(0, 0) - cyl->refpt;
        const Vector3d ref = (rel - axis * axis.dot(rel)).normalized();
        surface = new CylindricalSurface(entities, "", placement(cyl->refpt, axis, ref), cyl->r);
      } else if (const auto *sph = dynamic_cast<const SphereSurface *>(quadric.get())) {
        // The polar axis is the patch's apex, so the octant is the (theta, phi)
        // rectangle below the pole and its three bounding arcs are two
        // meridians and one arc of the equator - no seam crosses it.
        const Vector3d axis = sph->normdir.normalized();
        const Vector3d rel = bez->control(0, 0) - sph->refpt;
        const Vector3d ref = (rel - axis * axis.dot(rel)).normalized();
        surface = new SphericalSurface(entities, "", placement(sph->refpt, axis, ref), sph->r);
      } else {
        std::vector<std::vector<Point *>> net;
        std::vector<std::vector<double>> wnet;
        for (int i = 0; i <= bez->degree_u; i++) {
          std::vector<Point *> row;
          std::vector<double> row_w;
          for (int j = 0; j <= bez->degree_v; j++) {
            row.push_back(new Point(entities, bez->control(i, j)));
            row_w.push_back(bez->weight(i, j));
          }
          net.push_back(row);
          if (bez->isRational()) wnet.push_back(row_w);
        }
        // A fillet's patch is rational - its middle weight is what makes the arc a
        // circle rather than a parabola - and the weights have to be written, or
        // the face describes a different surface from the mesh it replaces.
        surface = new BSplineSurface(entities, "", bez->degree_u, bez->degree_v, net, wnet);
      }

      std::vector<OrientedEdge *> loop;
      std::vector<EdgeCurve *> face_edges_here;
      for (const auto& run : patch.runs) {
        const std::set<int> key(run.verts.begin(), run.verts.end());
        Vertex *from = get_vertex(run.verts.front());
        Vertex *to = get_vertex(run.verts.back());
        EdgeCurve *edge = nullptr;
        const auto known = run_edges.find(key);
        if (known != run_edges.end()) {
          edge = known->second;
        } else if (run.straight) {
          edge = create_line_edge_curve(from, to, true);
          run_edges.emplace(key, edge);
        } else {
          // On a quadric face the run is a circular arc lying on that quadric,
          // and writing it as a CIRCLE rather than as a spline off the net is
          // the other half of the same change: a kernel offsets and patterns
          // along a circle, and only tolerates a spline. The two describe the
          // same curve, so the substitution into the neighbouring planar loop
          // is unchanged. The classification above guarantees the patch on the
          // far side of a shared run agrees about which of the two it is.
          Vector3d centre, normal;
          double radius = 0;
          if (circular_runs.count(key) &&
              AnalyticFeatures::runCircle(patch, run, vertices, centre, normal, radius)) {
            const Vector3d ref = (vertices[run.verts.front()] - centre).normalized();
            auto circle = new Circle(entities, "", placement(centre, normal, ref), radius);
            edge = new EdgeCurve(entities, from, to, circle, true);
          } else {
            std::vector<Point *> cp;
            std::vector<double> cw;
            for (const auto& c : AnalyticFeatures::runControlPoints(patch, run, vertices, &cw)) {
              cp.push_back(new Point(entities, c));
            }
            auto curve = new BSplineCurve(entities, "", cp, cw);
            edge = new EdgeCurve(entities, from, to, curve, true);
          }
          run_edges.emplace(key, edge);
        }
        // The curve was built running from whichever patch reached it first.
        const bool sense = edge->vert1 == from;
        loop.push_back(new OrientedEdge(entities, edge, sense));
        face_edges_here.push_back(edge);

        // and the neighbouring planar face gives up the segments it replaces
        // The region boundary is walked in the direction its own facets go, so
        // the face on the far side necessarily goes the other way. Two faces
        // traversing a shared edge the same way is precisely what leaves a
        // shell open.
        if (run.kind == AnalyticFeatures::Patch::Run::WHOLE_LOOP) {
          rim_of_loop[run.loop] = {edge, !sense};
        } else if (run.kind == AnalyticFeatures::Patch::Run::LOOP_RUN) {
          arc_subs[run.loop].push_back({run.start, run.count, edge, !sense});
        }
      }

      // Which way the patch faces: compare its own normal with the mesh it
      // replaces, since du x dv has no reason to agree with the facets.
      //
      // A quadric's normal is the radial one - away from the axis of a cylinder,
      // away from the centre of a sphere - so a fillet, where the material is
      // on the far side of the surface from the axis, is the opposite sense.
      // Same question as the bands answer with Band::outward, one facet at a
      // time. The facet centroid is used rather than a corner, which on a
      // corner patch can be the apex.
      bool outward = true;
      if (!patch.facets.empty()) {
        const std::vector<int>& facet = loops[patch.facets[0]];
        const Vector3d& mesh_normal = loop_normals[patch.facets[0]];
        if (quadric != nullptr && !facet.empty()) {
          Vector3d centroid = Vector3d::Zero();
          for (const int v : facet) centroid += vertices[v];
          centroid /= double(facet.size());
          Vector3d radial;
          if (const auto *cyl = dynamic_cast<const CylinderSurface *>(quadric.get())) {
            const Vector3d axis = cyl->normdir.normalized();
            const Vector3d rel = centroid - cyl->refpt;
            radial = rel - axis * axis.dot(rel);
          } else {
            radial = centroid - quadric->refpt;
          }
          outward = radial.dot(mesh_normal) > 0;
        } else {
          double u = 0.5, v = 0.5;
          const_cast<BezierPatchSurface *>(bez)->project(vertices[facet[0]], u, v);
          const double h = 1e-6;
          const Vector3d du =
            bez->evaluate(std::min(1.0, u + h), v) - bez->evaluate(std::max(0.0, u - h), v);
          const Vector3d dv =
            bez->evaluate(u, std::min(1.0, v + h)) - bez->evaluate(u, std::max(0.0, v - h));
          outward = du.cross(dv).dot(mesh_normal) > 0;
        }
      }

      auto edge_loop = new EdgeLoop(entities, loop);
      std::vector<FaceBound *> bounds{new FaceBound(entities, edge_loop, true, true)};
      sfaces_extra.push_back(new Face(entities, bounds, surface, outward));
      face_edges_extra.push_back(face_edges_here);
    }
  }

  // ---- declared sweeps ---------------------------------------------------
  //
  // One face per claimed region, on the B-spline the declaration describes,
  // bounded by the mesh's own straight edges.
  //
  // That last part is the whole design and it is not a compromise. The obvious
  // move is to fit a curve along each boundary run, the way a fillet's rails
  // become arcs - but a fillet's rail lies in the flat face beside it, and a
  // trimmed sweep's boundary does not. Its neighbours here are the planar
  // facets the boolean left, so a curve on the sweep lies in none of them, and
  // a shared edge between the two can only be the chord it already is. Taking
  // the edges from the same map every other face uses is therefore both the
  // simplest thing and the only one that keeps the shell closed: the neighbour
  // is not asked to give anything up.
  //
  // One neighbour is not a planar facet, and it is the exception that sentence
  // does not cover: where the sweep meets a *declared* surface, the two cross
  // along a curve both of them can derive from their own declarations, and
  // `crossing_edges` holds exactly those edges. Such an edge has to be taken
  // from `crossing_curves` here for the same reason it is taken there - it is
  // one edge, and the two faces on it must name one geometry. Whichever emitter
  // reaches it first builds it and the other reuses the object, which is why
  // that map is declared above both of them rather than beside either.
  std::map<std::pair<int, int>, EdgeCurve *> crossing_curves;
  /*! One face's view of a crossing edge: the surface entity it wrote and the
   *  placement that entity carries, which is what a PCURVE's parameters mean.
   *
   * Recorded per face rather than per surface because the reference direction is
   * chosen from each face's own boundary, so two faces on one cylinder do not
   * share a parameter origin and a pcurve written against the wrong one is off
   * by a rotation.
   */
  struct CrossingSide {
    SurfaceType *surface = nullptr;
    Vector3d base, axis, ref;
    double slope = 0;  // a cone's, in its written direction; zero for a cylinder
  };
  std::map<std::pair<int, int>, std::vector<CrossingSide>> crossing_sides;
  int crossing_written = 0;

  // One per file - nothing about a 2D parametric context varies.
  ParametricContext *param_context = nullptr;
  // Settle the sections and the crossing curves before *either* emitter runs.
  // This used to sit after the grid faces were written, which meant the grid
  // emitter read a set that had not been decided yet and the quadric emitter
  // read one that had: the same edge then got a crossing curve from one face
  // and a chord from the other, and the shell came apart on twice the number
  // of crossings - 1278 edges used once, on the band family at $fn 32. Its own
  // comment says it has to run after the corner placement, and it still does;
  // the placement is a long way above here.
  // A trimmed quadric, bounded by the mesh's own boundary rather than by
  // circles. The band pass writes a cylinder between two circular rims and
  // writes it better - a CIRCLE is a curve a kernel can offset and pattern
  // along - so this only ever sees what that pass could not take: a face whose
  // trim is not a plane section and has no conic to bound it with.
  //
  // The bound is the polyline the neighbouring faceted faces already use, for
  // the same reason a declared sweep's is: those faces have to close against
  // this one edge for edge, and a curve of our own devising there would open
  // the shell. The surface is exact; only its boundary is the mesh's.
  // One curve per plane section, shared by the two faces that meet along it -
  // the same rule the band pass follows for a shared rim. Without it each face
  // would write its own ellipse and the shell would come apart along them.
  // Ask again, now that the corner placement has moved what it is going to move.
  // The faces are settled; what is being re-decided is only which sections both
  // of their faces can still see, and it has to be decided on the geometry that
  // is about to be written rather than on the geometry it was chosen from.
  if (decide_sections) {
    const std::set<std::set<int>> agreed_before = section_curves;
    std::vector<const AnalyticFeatures::Patch *> live;
    live.reserve(quadric_faces.size());
    for (const auto& patch : quadric_faces) live.push_back(&patch);
    decide_sections(live);
    std::size_t lost = 0;
    for (const auto& key : agreed_before) {
      if (section_curves.find(key) == section_curves.end()) lost++;
    }
    if (lost > 0) {
      // Say so rather than let it pass: this is the one place the two meshes can
      // disagree, and in the exact tier it means a face was called exact partly
      // because a section covered chords which are now written as chords after
      // all. A plane fitted to mesh facets is what does not survive: the
      // placement moves a corner onto the surface the model declared, which is
      // exactly off the facet plane it happened to share with a neighbour. A
      // declared plane survives, because that is what the corner was moved onto.
      LOG(message_group::Export_Warning,
          "STEP export: %1$d plane section%2$s agreed before the corners were placed and %3$s not "
          "after, so %4$s boundary is written as chords%5$s",
          int(lost), lost == 1 ? "" : "s", lost == 1 ? "does" : "do",
          lost == 1 ? "that much of a" : "that much of the",
          approximate ? "" : " under a face called exact");
    }
  }

  for (const auto& patch : grid_faces) {
    const auto *grid = dynamic_cast<const GridSurface *>(patch.surface.get());
    if (grid == nullptr) continue;

    int du = 0, dv = 0, nu = 0, nv = 0;
    std::vector<Vector3d> ctrl;
    std::vector<double> knots_u, knots_v;
    std::vector<int> mults_u, mults_v;
    if (!grid->splineForm(du, dv, nu, nv, ctrl, knots_u, mults_u, knots_v, mults_v)) continue;

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
        if (!grid->project(vertices[v], pu, pv)) continue;
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
          if (!grid->project(edge->vert1->point->pt, ua, va)) return;
          if (!grid->project(edge->vert2->point->pt, ub, vb)) return;
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

  std::map<std::set<int>, EdgeCurve *> section_edges;
  std::set<std::set<int>> section_subs;  // sections already handed to a planar face
  int sections_declared = 0, sections_fitted = 0;
  auto conic_placement = [&](const Vector3d& origin, const Vector3d& dir, const Vector3d& towards) {
    return new Axis2Placement(entities, new Direction(entities, dir), new Direction(entities, towards),
                              new Point(entities, origin));
  };
  for (const auto& patch : quadric_faces) {
    const auto *cyl = dynamic_cast<const CylinderSurface *>(patch.surface.get());
    const auto *cone = dynamic_cast<const ConeSurface *>(patch.surface.get());
    if (cyl == nullptr && cone == nullptr) continue;
    const Vector3d axis = (cyl != nullptr ? cyl->normdir : cone->normdir).normalized();
    const Vector3d base = cyl != nullptr ? cyl->refpt : cone->refpt;

    const std::map<std::size_t, BoundaryCycle> boundary = boundaryCycles(
      patch, vertices, loops, loop_valid, taken, section_planes[patch.surface.get()], crossing_keys);
    if (boundary.empty()) continue;

    // The reference direction is a radius through the first boundary vertex that
    // has one, so the face starts where its own boundary does rather than at
    // some seam of the surface's own. "That has one" is not defensiveness: a
    // cone's face can be bounded through its apex, which is on the axis and has
    // no radius at all, and taking that one wrote a zero DIRECTION into the
    // file - a placement no reader can use, on a face that was correct in every
    // other respect.
    Vector3d ref = Vector3d::Zero();
    for (const auto& entry : boundary) {
      for (const int v : entry.second.verts) {
        const Vector3d rel = vertices[v] - base;
        const Vector3d radial = rel - axis * axis.dot(rel);
        if (radial.norm() > 1e-9) {
          ref = radial.normalized();
          break;
        }
      }
      if (ref.norm() > 0.5) break;
    }
    if (ref.norm() < 0.5) ref = AnalyticFeatures::perpendicular(axis);
    auto point = new Point(entities, base);
    auto dir_ref = new Direction(entities, ref);
    SurfaceType *surface = nullptr;
    if (cyl != nullptr) {
      auto dir_axis = new Direction(entities, axis);
      surface = new CylindricalSurface(entities, "",
                                       new Axis2Placement(entities, dir_axis, dir_ref, point), cyl->r);
    } else {
      // ISO 10303 wants a half angle in (0, pi/2) and a radius growing along the
      // placement's axis, so a cone narrowing that way is written from its other
      // end - the same convention the band pass follows.
      auto dir_axis = new Direction(entities, cone->slope > 0 ? axis : Vector3d(-axis));
      surface = new ConicalSurface(entities, "", new Axis2Placement(entities, dir_axis, dir_ref, point),
                                   cone->r, atan(fabs(cone->slope)));
    }

    // Which cycle is the outer bound is a question about the surface's own
    // parameters: a hole in a face is a hole in its (theta, z) rectangle.
    std::size_t outer = 0;
    double widest = -1.0;
    const Vector3d ref2 = axis.cross(ref);
    for (const auto& entry : boundary) {
      std::vector<std::pair<double, double>> uv;
      for (const int v : entry.second.verts) {
        const Vector3d rel = vertices[v] - base;
        double t = atan2(rel.dot(ref2), rel.dot(ref));
        if (t < 0) t += 2 * M_PI;
        uv.emplace_back(t * (cyl != nullptr ? cyl->r : cone->r), axis.dot(rel));
      }
      double twice = 0.0;
      for (std::size_t i = 0; i < uv.size(); i++) {
        const auto& a = uv[i];
        const auto& b = uv[(i + 1) % uv.size()];
        twice += a.first * b.second - b.first * a.second;
      }
      if (fabs(twice) / 2 > widest) {
        widest = fabs(twice) / 2;
        outer = entry.first;
      }
    }

    std::vector<FaceBound *> bounds;
    std::vector<EdgeCurve *> face_edges_here;
    for (const auto& entry : boundary) {
      const std::vector<int>& cycle = entry.second.verts;
      const std::size_t n = cycle.size();
      if (n < 3) continue;
      // Where the boundary is a plane section, write the ellipse it is rather
      // than the chords the mesh carries. Everything else stays a straight edge:
      // this replaces a stretch of the boundary, never the whole of it.
      std::vector<int> starts_at(n, -1);
      for (std::size_t k = 0; k < entry.second.sections.size(); k++) {
        const BoundarySection& bs = entry.second.sections[k];
        // A section the roll could not free stays a run of chords, which is what
        // the boundary test then refuses the surface for; so does one the face
        // on the other side is not writing - see `section_curves`.
        std::set<int> key;
        for (std::size_t c = 0; c < bs.count; c++) key.insert(cycle[(bs.start + c) % n]);
        if (bs.start + bs.count > n) continue;
        if (section_curves.find(key) == section_curves.end()) continue;
        starts_at[bs.start] = int(k);
      }
      std::vector<OrientedEdge *> loop;
      for (std::size_t i = 0; i < n;) {
        if (starts_at[i] >= 0) {
          const BoundarySection& bs = entry.second.sections[starts_at[i]];
          const int a = cycle[bs.start], b = cycle[bs.start + bs.count - 1];
          std::set<int> key;
          for (std::size_t k = 0; k < bs.count; k++) key.insert(cycle[bs.start + k]);
          EdgeCurve *edge = nullptr;
          bool dir = true;
          const auto found = section_edges.find(key);
          if (found != section_edges.end()) {
            edge = found->second;
            dir = edge->vert1 == get_vertex(a);
          } else {
            SectionEllipse sec;
            if (!planeSectionEllipse(patch.surface.get(), bs.normal, bs.on_plane, sec)) {
              starts_at[i] = -1;  // fall back to the chords, which is what was there
              continue;
            }
            auto ell =
              new Ellipse(entities, "", conic_placement(sec.centre, bs.normal, sec.major), sec.a, sec.b);
            // An ELLIPSE runs counter clockwise about its own axis, so the edge
            // says whether it agrees - decided by whether the section's middle
            // lies on the counter clockwise side of its two ends.
            const Vector3d minor = bs.normal.cross(sec.major);
            auto param = [&](const Vector3d& q) {
              const Vector3d rel = q - sec.centre;
              return atan2(rel.dot(minor) / sec.b, rel.dot(sec.major) / sec.a);
            };
            auto ccw = [](double from, double to) {
              double d = to - from;
              while (d < 0) d += 2 * M_PI;
              while (d >= 2 * M_PI) d -= 2 * M_PI;
              return d;
            };
            const double ta = param(vertices[a]), tb = param(vertices[b]);
            if (bs.count > 2) {
              // A vertex of the section's own interior settles it exactly.
              const double tm = param(vertices[cycle[bs.start + bs.count / 2]]);
              dir = ccw(ta, tm) < ccw(ta, tb);
            } else {
              // A section of one edge has no interior vertex to ask, and asking
              // for one returns its far end - which made the test read "equal",
              // took the same branch every time, and sent half the arcs the long
              // way round. What decides it instead is that the edge is one
              // segment of a tessellation: it spans at most a third of the way
              // round even at the coarsest $fn, so the arc that replaces it is
              // the shorter one.
              dir = ccw(ta, tb) <= M_PI;
            }
            edge = new EdgeCurve(entities, get_vertex(dir ? a : b), get_vertex(dir ? b : a), ell, true);
            section_edges.emplace(key, edge);
            if (bs.declared) sections_declared++;
            else sections_fitted++;
          }
          loop.push_back(new OrientedEdge(entities, edge, dir));
          face_edges_here.push_back(edge);
          // And the planar face across it gives up the segments the curve
          // replaces, the same way a band's rim does. The region's boundary is
          // walked the way its own facets go, so the far face necessarily goes
          // the other way; two faces traversing a shared edge the same way is
          // precisely what leaves a shell open.
          if (bs.loop >= 0 && section_subs.insert(key).second) {
            const std::vector<int>& other = loops[bs.loop];
            const std::size_t m = other.size();
            const std::size_t span = bs.count - 1;
            for (std::size_t j = 0; j < m; j++) {
              bool fwd = true, rev = true;
              for (std::size_t c = 0; c <= span; c++) {
                fwd = fwd && other[(j + c) % m] == cycle[bs.start + c];
                rev = rev && other[(j + c) % m] == cycle[bs.start + span - c];
              }
              if (!fwd && !rev) continue;
              if (span == m) rim_of_loop[bs.loop] = {edge, !dir};
              else arc_subs[bs.loop].push_back({j, span, edge, !dir});
              break;
            }
          }
          i = bs.start + bs.count - 1;
          continue;
        }
        const int a = cycle[i], b = cycle[(i + 1) % n];
        bool dir = true;
        EdgeCurve *edge = nullptr;
        const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
        const auto crossing = crossing_edges.find(key);
        if (crossing != crossing_edges.end()) {
          const auto found = crossing_curves.find(key);
          if (found != crossing_curves.end()) {
            edge = found->second;
            dir = edge->vert1 == get_vertex(a);
          } else {
            std::vector<Vector3d> ctrl;
            const Vector3d& p0 = vertices[key.first];
            const Vector3d& p1 = vertices[key.second];
            if (intersectionArc(crossing->second.first, crossing->second.second, p0, p1, 1e-7, ctrl)) {
              std::vector<Point *> cp;
              for (const auto& c : ctrl) cp.push_back(new Point(entities, c));
              auto curve = new BSplineCurve(entities, "", cp);
              edge = new EdgeCurve(entities, get_vertex(key.first), get_vertex(key.second), curve, true);
              crossing_curves.emplace(key, edge);
              crossing_written++;
              dir = edge->vert1 == get_vertex(a);
            }
          }
          if (edge != nullptr) {
            // What this face's parameters mean, kept until the other face has
            // written its own and both pcurves can be built.
            CrossingSide side;
            side.surface = surface;
            side.base = base;
            side.axis = cone != nullptr && cone->slope < 0 ? Vector3d(-axis) : axis;
            side.ref = ref;
            side.slope = cone != nullptr ? fabs(cone->slope) : 0.0;
            crossing_sides[key].push_back(side);
          }
        }
        if (edge == nullptr) {
          edge = get_line_from_map(edge_map, a, b, get_vertex(a), get_vertex(b), dir, merged_edge_cnt);
        }
        loop.push_back(new OrientedEdge(entities, edge, dir));
        face_edges_here.push_back(edge);
        i++;
      }
      if (loop.size() < 2) continue;
      auto edge_loop = new EdgeLoop(entities, loop);
      bounds.push_back(new FaceBound(entities, edge_loop, true, entry.first == outer));
    }
    if (bounds.empty()) continue;

    // Which way the face points. A cylinder's own normal is radially outward,
    // so the mesh decides by whether one of its facets agrees.
    bool outward = true;
    if (!patch.facets.empty()) {
      const std::vector<int>& facet = loops[patch.facets.front()];
      Vector3d centroid = Vector3d::Zero();
      for (const int v : facet) centroid += vertices[v];
      centroid /= double(facet.size());
      const Vector3d rel = centroid - base;
      const Vector3d radial = (rel - axis * axis.dot(rel)).normalized();
      outward = radial.dot(loop_normals[patch.facets.front()]) > 0;
    }

    sfaces_extra.push_back(new Face(entities, bounds, surface, outward));
    face_edges_extra.push_back(face_edges_here);
  }

  // Say where each crossing curve runs across the two surfaces it lies on, now
  // that both faces have been written and their placements are known.
  //
  // The 3D curve stays definitive - .CURVE_3D., as it is everywhere else here -
  // so a reader that ignores the pcurves sees the file it saw before. What they
  // add is that a curve on two surfaces is *said* to be on them: STEP calls that
  // a SURFACE_CURVE, and a kernel that would otherwise re-project a B-spline
  // onto a quadric and decide for itself is told the answer instead.
  int pcurved = 0;
  for (const auto& entry : crossing_curves) {
    const auto sides = crossing_sides.find(entry.first);
    if (sides == crossing_sides.end() || sides->second.size() != 2) continue;
    auto *bez = dynamic_cast<BSplineCurve *>(entry.second->round);
    if (bez == nullptr || bez->pts.size() < 2) continue;
    const int degree = int(bez->pts.size()) - 1;

    // The curve at its own collocation parameters, which are the points it was
    // fitted through, so the pcurve and the 3D curve agree there exactly.
    std::vector<Vector3d> on_curve(degree + 1);
    for (int i = 0; i <= degree; i++) {
      const double t = double(i) / degree;
      Vector3d p = Vector3d::Zero();
      double binom = 1;
      for (int j = 0; j <= degree; j++) {
        p += bez->pts[j]->pt * (binom * pow(t, j) * pow(1 - t, degree - j));
        binom = binom * (degree - j) / (j + 1);
      }
      on_curve[i] = p;
    }

    Eigen::MatrixXd m(degree + 1, degree + 1);
    for (int i = 0; i <= degree; i++) {
      const double t = double(i) / degree;
      double binom = 1;
      for (int j = 0; j <= degree; j++) {
        m(i, j) = binom * pow(t, j) * pow(1 - t, degree - j);
        binom = binom * (degree - j) / (j + 1);
      }
    }
    const Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(m);

    std::vector<PCurve *> on_both;
    for (const CrossingSide& side : sides->second) {
      const Vector3d ref2 = side.axis.cross(side.ref);
      Eigen::MatrixXd rhs(degree + 1, 2);
      double previous = 0;
      for (int i = 0; i <= degree; i++) {
        const Vector3d rel = on_curve[i] - side.base;
        double u = atan2(rel.dot(ref2), rel.dot(side.ref));
        // A surface of revolution's parameter wraps, and a curve that steps over
        // the seam has to keep counting rather than jump a turn.
        if (i > 0) {
          while (u - previous > M_PI) u -= 2 * M_PI;
          while (u - previous < -M_PI) u += 2 * M_PI;
        }
        previous = u;
        rhs(i, 0) = u;
        rhs(i, 1) = rel.dot(side.axis);
      }
      const Eigen::MatrixXd solved = qr.solve(rhs);
      std::vector<Point2d *> cp;
      for (int j = 0; j <= degree; j++) {
        cp.push_back(new Point2d(entities, solved(j, 0), solved(j, 1)));
      }
      if (param_context == nullptr) param_context = new ParametricContext(entities);
      auto rep =
        new DefinitionalRepresentation(entities, new BSplineCurve2d(entities, cp), param_context);
      on_both.push_back(new PCurve(entities, side.surface, rep));
    }
    entry.second->round = new SurfaceCurve(entities, bez, on_both);
    pcurved++;
  }
  if (pcurved > 0) {
    LOG(
      "STEP export: %1$d of those carr%2$s a pcurve on each of the two surfaces, so the curve "
      "is written as lying on them rather than left to be projected back",
      pcurved, pcurved == 1 ? "ies" : "y");
  }

  // Build the loops, their edges and the carrier planes.
  std::vector<FaceBound *> face_bounds(face_cnt, nullptr);
  std::vector<Plane *> planes(face_cnt, nullptr);
  int planes_declared = 0;
  std::vector<std::vector<EdgeCurve *>> loop_edges(face_cnt);

  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i] || consumed[i] || split_for_corners.count(i) != 0) continue;
    const std::vector<int>& loop = loops[i];
    const int n = int(loop.size());

    std::vector<OrientedEdge *> oriented_edges;

    // a rim of a recognised band is one circular edge instead of n straight
    // ones, shared with the band's face
    const auto rim = rim_of_loop.find(i);
    if (rim != rim_of_loop.end()) {
      oriented_edges.push_back(new OrientedEdge(entities, rim->second.first, rim->second.second));
      loop_edges[i].push_back(rim->second.first);
    } else if (arc_subs.count(i) != 0) {
      // one or more runs of this loop's edges belong to a partial band and are
      // replaced, in place, by the single arc that band is bounded by
      const std::vector<ArcSubstitution>& subs = arc_subs[i];
      std::vector<int> starts_here(n, -1);
      std::vector<char> covered(n, 0);
      for (std::size_t s = 0; s < subs.size(); s++) {
        starts_here[subs[s].start % n] = int(s);
        for (std::size_t c = 0; c < subs[s].count; c++) covered[(subs[s].start + c) % n] = 1;
      }
      for (int j = 0; j < n; j++) {
        if (starts_here[j] >= 0) {
          const ArcSubstitution& sub = subs[starts_here[j]];
          oriented_edges.push_back(new OrientedEdge(entities, sub.edge, sub.sense));
          loop_edges[i].push_back(sub.edge);
          continue;
        }
        if (covered[j] != 0) continue;
        const int ind = loop[j];
        const int indn = loop[(j + 1) % n];
        bool edge_dir = true;
        EdgeCurve *edge_curve = get_line_from_map(edge_map, ind, indn, get_vertex(ind), get_vertex(indn),
                                                  edge_dir, merged_edge_cnt);
        oriented_edges.push_back(new OrientedEdge(entities, edge_curve, edge_dir));
        loop_edges[i].push_back(edge_curve);
      }
    } else
      for (int j = 0; j < n; j++) {
        const int ind = loop[j];
        const int indn = loop[(j + 1) % n];
        bool edge_dir = true;
        EdgeCurve *edge_curve = get_line_from_map(edge_map, ind, indn, get_vertex(ind), get_vertex(indn),
                                                  edge_dir, merged_edge_cnt);
        oriented_edges.push_back(new OrientedEdge(entities, edge_curve, edge_dir));
        loop_edges[i].push_back(edge_curve);
      }

    // create the plane. Where the model declared one that this face lies in,
    // that is the plane: a declaration cannot drift, and a fit to mesh vertices
    // moves with the tessellation, with whatever a boolean left behind, and with
    // every vertex a later pass touches. The face's own vertices are unchanged
    // either way - the declaration is only accepted when every one of them is
    // already in it, so this replaces the *coefficients* and nothing else.
    Vector3d norm = loop_normals[i];
    Vector3d on_plane = vertices[loop[0]];
    {
      double extent = 0;
      for (int j = 0; j < n; j++) {
        extent = std::max(extent, (vertices[loop[j]] - on_plane).norm());
      }
      const double tol = std::max(1e-12, 1e-9 * std::max(1.0, extent));
      for (const auto& declared : declared_planes) {
        if (fabs(fabs(declared.second.dot(norm)) - 1.0) > 1e-9) continue;
        bool holds = true;
        for (int j = 0; holds && j < n; j++) {
          holds = fabs((vertices[loop[j]] - declared.first).dot(declared.second)) <= tol;
        }
        if (!holds) continue;
        // Keep the mesh's sense: a declaration says which plane, not which way
        // the face looks out of it.
        norm = declared.second.dot(norm) > 0 ? declared.second : Vector3d(-declared.second);
        on_plane = declared.first + norm * (vertices[loop[0]] - declared.first).dot(norm);
        // Per face, not per loop: a face with a hole in it computes a plane for
        // its inner bound too, and counting those says a ring was written twice.
        if (!loop_is_hole[i]) planes_declared++;
        break;
      }
    }
    Vector3d ref(0, 0, 0);
    double ref_len = 0;
    for (int j = 0; j < n; j++) {
      const Vector3d dir = vertices[loop[(j + 1) % n]] - vertices[loop[j]];
      if (dir.norm() > ref_len) {
        ref_len = dir.norm();
        ref = dir;
      }
    }
    ref -= norm * norm.dot(ref);
    if (ref.norm() < 1e-12) ref = AnalyticFeatures::perpendicular(norm);
    else ref.normalize();

    auto plane_point = new Point(entities, on_plane);
    auto plane_dir_1 = new Direction(entities, norm);
    auto plane_dir_2 = new Direction(entities, ref);
    auto plane_axis = new Axis2Placement(entities, plane_dir_1, plane_dir_2, plane_point);
    planes[i] = new Plane(entities, plane_axis);

    auto edge_loop = new EdgeLoop(entities, oriented_edges);
    face_bounds[i] = new FaceBound(entities, edge_loop, true, !loop_is_hole[i]);
  }

  if (planes_declared > 0) {
    LOG(
      "STEP export: %1$d planar face%2$s written on a plane the model declared rather than one "
      "fitted to its own corners",
      planes_declared, planes_declared == 1 ? "" : "s");
  }

  // The fans for the polygons the corner placement bent.
  //
  // Each triangle takes its boundary edges from the same map every other face
  // uses, so the shell still stitches along them; the diagonals are new and
  // used by exactly the two triangles that share them. Each triangle carries
  // the plane its own three corners lie on, which is the point of the fan -
  // the polygon's plane is the one they no longer all sit on.
  for (const auto& sc : split_for_corners) {
    const std::size_t i = sc.first, apex = sc.second;
    const std::vector<int>& loop = loops[i];
    const std::size_t n = loop.size();
    for (std::size_t k = 1; k + 1 < n; k++) {
      const int tri[3] = {loop[apex], loop[(apex + k) % n], loop[(apex + k + 1) % n]};
      const Vector3d e1 = vertices[tri[1]] - vertices[tri[0]];
      const Vector3d e2 = vertices[tri[2]] - vertices[tri[0]];
      if (e1.cross(e2).norm() < area_eps) continue;
      std::vector<OrientedEdge *> oriented;
      std::vector<EdgeCurve *> edges_here;
      for (int j = 0; j < 3; j++) {
        const int a = tri[j], b = tri[(j + 1) % 3];
        bool dir = true;
        EdgeCurve *e =
          get_line_from_map(edge_map, a, b, get_vertex(a), get_vertex(b), dir, merged_edge_cnt);
        oriented.push_back(new OrientedEdge(entities, e, dir));
        edges_here.push_back(e);
      }
      // The triangle's own plane, not the polygon's. A triangle is planar
      // wherever its corners are, which is the whole reason for fanning one
      // out - but only if the PLANE written under it is the one its corners
      // actually lie on. Taking the polygon's normal instead left every fan
      // triangle asserting a plane its corners had just moved off, and
      // step-declare-grid-scad carried the entire 0.096 on its planes while its
      // cylinders and its sweep were already exact.
      //
      // And it is written the way the bound winds, never turned to agree with
      // the polygon. Turning it is what an inverted ear used to produce: a
      // PLANE exactly opposite the loop bounding it, which is not a face.
      // Choosing the apex is what makes that unnecessary - every ear of the
      // chosen fan winds with the polygon - so a disagreement here is a bug
      // rather than a case to paper over, and the triangle is dropped.
      Vector3d tn = e1.cross(e2);
      if (tn.dot(loop_normals[i]) <= 0) continue;
      tn.normalize();
      Vector3d ref = e1 - tn * tn.dot(e1);
      if (ref.norm() < 1e-12) ref = AnalyticFeatures::perpendicular(tn);
      else ref.normalize();
      auto axis = new Axis2Placement(entities, new Direction(entities, tn), new Direction(entities, ref),
                                     new Point(entities, vertices[tri[0]]));
      std::vector<FaceBound *> bounds;
      bounds.push_back(new FaceBound(entities, new EdgeLoop(entities, oriented), true, true));
      sfaces_extra.push_back(new Face(entities, bounds, new Plane(entities, axis), true));
      face_edges_extra.push_back(edges_here);
    }
  }

  // Combine every outer loop with the loops of its holes into one ADVANCED_FACE.
  std::vector<Face *> sfaces;
  std::vector<std::vector<EdgeCurve *>> face_edges;
  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i] || loop_is_hole[i] || consumed[i] || split_for_corners.count(i) != 0) {
      continue;
    }
    std::vector<FaceBound *> singface;
    singface.push_back(face_bounds[i]);
    std::vector<EdgeCurve *> edges = loop_edges[i];
    for (std::size_t j = 0; j < face_cnt; j++) {
      if (!loop_valid[j] || consumed[j] || parents[j] != int(i)) continue;
      singface.push_back(face_bounds[j]);
      edges.insert(edges.end(), loop_edges[j].begin(), loop_edges[j].end());
    }

    sfaces.push_back(new Face(entities, singface, planes[i], true));
    face_edges.push_back(edges);
  }

  // the recognised cylinders are faces of the same shell
  for (std::size_t i = 0; i < sfaces_extra.size(); i++) {
    sfaces.push_back(sfaces_extra[i]);
    face_edges.push_back(face_edges_extra[i]);
  }

  // Every edge bounding a recovered curved face was still a straight line: the
  // exporter drops a cylinder behind the mesh's polyline boundary, so the edge
  // is the chord where the surface is the arc, and the gap between them is the
  // sagitta. Measured on the band family, 366 of 388 line-on-cylinder edges lay
  // off the cylinder they bounded, by up to 0.17mm - doc/step-export-development.md
  // has the tables. Every kernel that reads such a file has to widen its
  // tolerance to swallow that; two do it silently and one declines, and none of
  // them is wrong to object.
  //
  // The 22 edges that were already exact say what the fix is. A ruling lies on a
  // cylinder by construction, so the segments running up and down the axis need
  // nothing, and it is the ones running around it - at constant height, chords
  // of a circle - that should be written as arcs of that circle instead.
  //
  // An edge belongs to two faces, so promoting it is only sound when both can
  // contain the arc. A plane can when its normal is the axis, which is what a
  // flat top or an annulus is; anything else declines, and the edge stays the
  // line it was. That is what keeps this from ever moving an edge off a face
  // that was exact already, which would trade one kernel's complaint for
  // everyone's.
  std::map<EdgeCurve *, std::vector<Face *>> edge_faces;
  for (std::size_t i = 0; i < face_edges.size(); i++) {
    for (auto *edge : face_edges[i]) edge_faces[edge].push_back(sfaces[i]);
  }

  // The axis and radius a quadric face implies for an edge, if it implies one:
  // the section at the edge's own height, which exists only when both ends are
  // at the same height and the same distance from the axis.
  auto section_of = [&](const SurfaceType *surface, const Vector3d& p1, const Vector3d& p2,
                        Vector3d& axis, Vector3d& centre, double& radius) {
    Vector3d ax, org;
    double apex_r = 0.0, slope = 0.0;
    if (const auto *cyl = dynamic_cast<const CylindricalSurface *>(surface)) {
      ax = cyl->axis->dir1->pt.normalized();
      org = cyl->axis->point->pt;
      apex_r = cyl->r;
    } else if (const auto *con = dynamic_cast<const ConicalSurface *>(surface)) {
      ax = con->axis->dir1->pt.normalized();
      org = con->axis->point->pt;
      apex_r = con->r;
      slope = tan(con->half_angle);
    } else {
      return false;
    }
    const double h1 = (p1 - org).dot(ax), h2 = (p2 - org).dot(ax);
    if (fabs(h1 - h2) > tol) return false;
    const Vector3d c = org + ax * h1;
    const double r1 = (p1 - c).norm(), r2 = (p2 - c).norm();
    const double want = apex_r + slope * h1;
    if (fabs(r1 - r2) > tol || fabs(r1 - want) > tol) return false;
    axis = ax;
    centre = c;
    radius = want;
    return true;
  };

  int arcs_promoted = 0, arcs_declined = 0;
  for (auto& entry : edge_faces) {
    EdgeCurve *edge = entry.first;
    auto *was_line = dynamic_cast<Line *>(edge->round);
    if (was_line == nullptr) continue;
    const Vector3d p1 = edge->vert1->point->pt;
    const Vector3d p2 = edge->vert2->point->pt;
    if ((p1 - p2).norm() < 1e-12) continue;

    // A quadric among the faces proposes the arc; every face then gets a veto.
    Vector3d axis, centre;
    double radius = 0.0;
    bool proposed = false;
    for (auto *face : entry.second) {
      if (section_of(face->surface, p1, p2, axis, centre, radius)) {
        proposed = true;
        break;
      }
    }
    if (!proposed) continue;

    bool agreed = true;
    for (auto *face : entry.second) {
      if (const auto *pl = dynamic_cast<const Plane *>(face->surface)) {
        // A circle about `axis` lies in a plane only if the plane is
        // perpendicular to it, and its endpoints are in the plane already.
        const Vector3d n = pl->axis->dir1->pt.normalized();
        agreed = fabs(fabs(n.dot(axis)) - 1) < 1e-9;
      } else {
        Vector3d other_axis, other_centre;
        double other_radius = 0.0;
        agreed = section_of(face->surface, p1, p2, other_axis, other_centre, other_radius) &&
                 (other_centre - centre).norm() < tol && fabs(other_radius - radius) < tol;
      }
      if (!agreed) break;
    }
    if (!agreed) {
      arcs_declined++;
      continue;
    }

    const Vector3d from = (p1 - centre).normalized();
    const Vector3d to = (p2 - centre).normalized();
    // The normal that makes the sweep from `from` to `to` counter clockwise and
    // shorter than half a turn, which one segment of a tessellated boundary
    // always is.
    const Vector3d normal = from.cross(to);
    if (normal.norm() < 1e-12) {
      arcs_declined++;
      continue;
    }
    auto arc_point = new Point(entities, centre);
    auto arc_axis = new Direction(entities, normal.normalized());
    auto arc_ref = new Direction(entities, from);
    edge->round =
      new Circle(entities, "", new Axis2Placement(entities, arc_axis, arc_ref, arc_point), radius);
    edge->dir = true;
    // The line and the three entities it alone referred to would otherwise sit
    // in the file unreferenced. `get_line_from_map` gives every edge its own,
    // so nothing else can be pointing at them.
    was_line->live = false;
    was_line->vector->live = false;
    was_line->vector->dir->live = false;
    was_line->point->live = false;
    arcs_promoted++;
  }
  if (arcs_promoted > 0 || arcs_declined > 0) {
    LOG("STEP export: %1$d edge%2$s written as an arc on the surface it bounds, %3$d left straight",
        arcs_promoted, arcs_promoted == 1 ? "" : "s", arcs_declined);
  }
  if (crossing_written > 0) {
    LOG(
      "STEP export: %1$d edge%2$s written as the curve where two declared surfaces cross, "
      "fitted to within %3$.2e of both - a chord and a plane section each lie on only one",
      crossing_written, crossing_written == 1 ? "" : "s", crossing_fit);
  }
  if (arc_refused > 0) {
    std::sort(arc_errors.begin(), arc_errors.end());
    const double lo = arc_errors.empty() ? 0 : arc_errors.front();
    const double med = arc_errors.empty() ? 0 : arc_errors[arc_errors.size() / 2];
    const double hi = arc_errors.empty() ? 0 : arc_errors.back();
    LOG(
      "EXPORT-WARNING: STEP export: %1$d edge%2$s could not be written as that curve and fell "
      "back to a chord - %3$d never fitted at any degree, the rest came within "
      "%4$.2e/%5$.2e/%6$.2e min/med/max of both surfaces against the %7$.0e asked for",
      int(arc_refused), arc_refused == 1 ? "" : "s", int(arc_no_fit), lo, med, hi, 1e-7);
    std::sort(arc_on_quadric.begin(), arc_on_quadric.end());
    std::sort(arc_on_fitted.begin(), arc_on_fitted.end());
    if (!arc_on_quadric.empty()) {
      LOG(
        "STEP export:    of that miss, %1$.2e median is against a surface stated algebraically "
        "and %2$.2e median against one that answers by projecting",
        arc_on_quadric[arc_on_quadric.size() / 2], arc_on_fitted[arc_on_fitted.size() / 2]);
      LOG("STEP export:    %1$d of them run across a crease in the declared profile and %2$d do not",
          int(arc_creased), int(arc_uncreased));
    }
  }
  if (sections_declared + sections_fitted > 0) {
    // Where each section's plane came from, because it is the difference
    // between a curve that is exact and one that is only as good as the mesh
    // it was fitted to. A declared plane cannot move; a fitted one moves with
    // the tessellation, with what the boolean left behind, and with the
    // tolerance the fit was given - and the section's centre and both its
    // semi-axes are differences taken against that plane.
    LOG(
      "STEP export: %1$d plane section%2$s written as the conic it is - %3$d on a plane the "
      "model declared, %4$d on one taken from the mesh",
      sections_declared + sections_fitted, sections_declared + sections_fitted == 1 ? "" : "s",
      sections_declared, sections_fitted);
  }

  // A CLOSED_SHELL has to be a single connected shell, so split disconnected
  // bodies into one MANIFOLD_SOLID_BREP each instead of stuffing all of them
  // into one shell that can never close.
  std::vector<int> component(sfaces.size(), 0);
  for (std::size_t i = 0; i < sfaces.size(); i++) component[i] = int(i);
  std::map<EdgeCurve *, int> edge_owner;
  for (std::size_t i = 0; i < face_edges.size(); i++) {
    for (auto *edge : face_edges[i]) {
      auto it = edge_owner.find(edge);
      if (it == edge_owner.end()) {
        edge_owner.emplace(edge, int(i));
        continue;
      }
      const int a = uf_find(component, it->second);
      const int b = uf_find(component, int(i));
      if (a != b) component[a] = b;
    }
  }
  std::map<int, std::vector<Face *>> shell_faces;
  for (std::size_t i = 0; i < sfaces.size(); i++) {
    shell_faces[uf_find(component, int(i))].push_back(sfaces[i]);
  }

  // units and modelling tolerance
  auto length_unit = new SiUnit(entities, SiUnit::LENGTH);
  auto angle_unit = new SiUnit(entities, SiUnit::PLANE_ANGLE);
  auto solid_angle_unit = new SiUnit(entities, SiUnit::SOLID_ANGLE);
  auto uncertainty = new UncertaintyMeasure(entities, length_unit, model_tol);
  auto geom_context =
    new GeometricContext(entities, uncertainty, length_unit, angle_unit, solid_angle_unit);

  // create the base csys
  auto base_point = new Point(entities, Vector3d(0, 0, 0));
  auto base_dir_1 = new Direction(entities, Vector3d(0, 0, 1));
  auto base_dir_2 = new Direction(entities, Vector3d(1, 0, 0));
  auto base_axis = new Axis2Placement(entities, base_dir_1, base_dir_2, base_point);

  // product structure
  const std::string body_name = name != nullptr ? name : "";
  auto app_context = new ApplicationContext(entities);
  new ApplicationProtocolDefinition(entities, app_context);
  auto prod_context = new ProductContext(entities, app_context);
  auto product = new Product(entities, body_name, prod_context);
  new ProductRelatedProductCategory(entities, product);
  auto prod_formation = new ProductDefinitionFormation(entities, product);
  auto prod_def_context = new ProductDefinitionContext(entities, app_context);
  auto product_def = new ProductDefinition(entities, prod_formation, prod_def_context);
  auto product_def_shape = new ProductDefinitionShape(entities, product_def);
  auto shape_repr = new ShapeRepresentation(entities, body_name, base_axis, geom_context);
  new ShapeDefinition_Representation(entities, product_def_shape, shape_repr);

  // build the model
  std::vector<ManifoldSolid *> solids;
  for (auto& shell : shell_faces) {
    auto closed_shell = new Shell(entities, shell.second);
    closed_shell->isOpen = false;
    solids.push_back(new ManifoldSolid(entities, 0, closed_shell));
  }

  if (!solids.empty()) {
    auto adv_brep_shape_pres = new AdvancesBrepRepresentation(entities, body_name, solids, geom_context);
    new ShapeRepresentationRelationShip(entities, shape_repr, adv_brep_shape_pres);
  }
}

StepKernel::EdgeCurve *StepKernel::get_line_from_map(
  std::map<std::pair<int, int>, StepKernel::EdgeCurve *>& edge_map, int ind1, int ind2,
  StepKernel::Vertex *vert1, StepKernel::Vertex *vert2, bool& edge_dir, int& merge_cnt)
{
  // Keyed on the (deduplicated) vertex indices: the previous key was built from
  // the raw coordinates, which only matches when the two faces meeting at the
  // edge store bit identical doubles.
  const auto key = std::make_pair(std::min(ind1, ind2), std::max(ind1, ind2));
  edge_dir = true;

  auto it = edge_map.find(key);
  if (it != edge_map.end()) {
    edge_dir = (it->second->vert1 == vert1);
    merge_cnt++;
    return it->second;
  }

  StepKernel::EdgeCurve *edge_curve = create_line_edge_curve(vert1, vert2, true);
  edge_map.emplace(key, edge_curve);
  return edge_curve;
}

std::string StepKernel::read_line(std::ifstream& stp_file, bool skip_all_space)
{
  std::string line_str;
  bool leading_space = true;
  bool in_squote = false;
  bool in_dquote = false;
  bool in_comment = false;
  char old_char = '\0';
  while (stp_file) {
    char get_char = ' ';
    stp_file.get(get_char);
    if (old_char == '/' && get_char == '*' && !in_comment) {
      in_comment = true;
      line_str = line_str.substr(0, line_str.size() - 1);
    }
    if (old_char == '*' && get_char == '/' && in_comment) {
      in_comment = false;
      continue;
    }

    old_char = get_char;
    if (in_comment) continue;

    if (get_char == '\'' && !in_dquote) in_squote = !in_squote;
    if (get_char == '\"' && !in_squote) in_dquote = !in_dquote;
    if (get_char == ';' && !in_squote && !in_dquote) break;

    if (get_char == '\n' || get_char == '\r' || get_char == '\t') continue;

    if (leading_space && (get_char == ' ' || get_char == '\t')) continue;
    if (!skip_all_space) leading_space = false;
    line_str.push_back(get_char);
  }
  return line_str;
}

void StepKernel::read_step(std::string file_name)
{
  std::ifstream stp_file;
  stp_file.open(file_name);
  if (!stp_file) {
    LOG(message_group::Export_Error, "Cannot open %1$s", file_name);
    return;
  }
  // read the first line to get the iso stuff
  std::string iso_line = read_line(stp_file, true);

  bool data_section = false;
  std::vector<Entity *> ents;
  std::map<int, Entity *> ent_map;
  std::vector<std::string> args;
  while (stp_file) {
    std::string cur_str = read_line(stp_file, false);
    if (cur_str == "DATA") {
      data_section = true;
      continue;
    }
    if (!data_section) continue;

    if (cur_str == "ENDSEC") {
      data_section = false;
      break;
    }
    // parse the id
    int id = -1;
    if (cur_str.size() > 0 && cur_str[0] == '#' && cur_str.find('=')) {
      auto equal_pos = cur_str.find('=');
      //			auto paren_pos = cur_str.find('(');
      auto id_str = cur_str.substr(1, equal_pos - 1);
      id = std::atoi(id_str.c_str());
      auto func_start = cur_str.find_first_not_of("\t ", equal_pos + 1);
      auto func_end = cur_str.find_first_of("\t (", func_start + 1);
      auto func_name = cur_str.substr(func_start, func_end - func_start);
      bool unimplemented = false;

      // now parse the args
      auto arg_end = cur_str.find_last_of(')');
      auto arg_start = cur_str.find_first_not_of("\t (", func_end + 1);
      auto arg_str = cur_str.substr(arg_start, arg_end - arg_start);
      Entity *ent = 0;
      if (func_name == "CARTESIAN_POINT") ent = new Point(entities);
      else if (func_name == "DIRECTION") ent = new Direction(entities);
      else if (func_name == "AXIS2_PLACEMENT_3D") ent = new Axis2Placement(entities);
      else if (func_name == "PLANE") ent = new Plane(entities);
      else if (func_name == "EDGE_LOOP") ent = new EdgeLoop(entities);
      else if (func_name == "FACE_BOUND") ent = new FaceBound(entities);
      else if (func_name == "FACE_OUTER_BOUND") {
        auto face_bound = new FaceBound(entities);
        face_bound->outer = true;
        ent = face_bound;
      } else if (func_name == "ADVANCED_FACE") ent = new Face(entities);
      else if (func_name == "FACE_SURFACE") ent = new Face(entities);
      else if (func_name == "OPEN_SHELL") ent = new Shell(entities);
      else if (func_name == "CLOSED_SHELL") ent = new Shell(entities);
      else if (func_name == "SHELL_BASED_SURFACE_MODEL") ent = new ShellModel(entities);
      else if (func_name == "MANIFOLD_SURFACE_SHAPE_REPRESENTATION") ent = new ManifoldShape(entities);
      else if (func_name == "MANIFOLD_SOLID_BREP") ent = new ManifoldSolid(entities);
      else if (func_name == "VERTEX_POINT") ent = new Vertex(entities);
      else if (func_name == "SURFACE_CURVE") ent = new SurfaceCurve(entities);
      else if (func_name == "EDGE_CURVE") ent = new EdgeCurve(entities);
      else if (func_name == "ORIENTED_EDGE") ent = new OrientedEdge(entities);
      else if (func_name == "VECTOR") ent = new Vector(entities);
      else if (func_name == "LINE") ent = new Line(entities);
      else if (func_name == "CIRCLE") ent = new Circle(entities);
      else if (func_name == "ELLIPSE") ent = new Ellipse(entities);
      else if (func_name == "CYLINDRICAL_SURFACE") ent = new CylindricalSurface(entities);
      else if (func_name == "CONICAL_SURFACE") ent = new ConicalSurface(entities);
      else if (func_name == "PCURVE") unimplemented = true;
      else if (func_name == "DEFINITIONAL_REPRESENTATION") unimplemented = true;
      else if (func_name == "UNCERTAINTY_MEASURE_WITH_UNIT") unimplemented = true;
      else if (func_name == "PRODUCT_TYPE") unimplemented = true;
      else if (func_name == "APPLICATION_PROTOCOL_DEFINITION") unimplemented = true;
      else if (func_name == "APPLICATION_CONTEXT") unimplemented = true;
      else if (func_name == "SHAPE_DEFINITION_REPRESENTATION") unimplemented = true;
      else if (func_name == "PRODUCT") unimplemented = true;
      else if (func_name == "PRODUCT_DEFINITION_SHAPE") unimplemented = true;
      else if (func_name == "PRODUCT_DEFINITION") unimplemented = true;
      else if (func_name == "PRODUCT_DEFINITION_FORMATION") unimplemented = true;
      else if (func_name == "MECHANICAL_CONTEXT") unimplemented = true;
      else if (func_name == "PRODUCT_DEFINITION_CONTEXT") unimplemented = true;
      else if (func_name == "ADVANCED_BREP_SHAPE_REPRESENTATION") unimplemented = true;
      else if (func_name == "PERSON") unimplemented = true;
      else if (func_name == "DATE_TIME_ROLE") unimplemented = true;
      else if (func_name == "LOCAL_TIME") unimplemented = true;
      else if (func_name == "APPROVAL_ROLE") unimplemented = true;
      else if (func_name == "APPROVAL") unimplemented = true;
      else if (func_name == "COORDINATED_UNIVERSAL_TIME_OFFSET") unimplemented = true;
      else if (func_name == "CC_DESIGN_PERSON_AND_ORGANIZATION_ASSIGNMENT") unimplemented = true;
      else if (func_name == "DATE_AND_TIME") unimplemented = true;
      else if (func_name == "APPROVAL_DATE_TIME") unimplemented = true;
      else if (func_name == "SECURITY_CLASSIFICATION_LEVEL") unimplemented = true;
      else if (func_name == "APPROVAL_STATUS") unimplemented = true;
      else if (func_name == "CC_DESIGN_APPROVAL") unimplemented = true;
      else if (func_name == "ORGANIZATION") unimplemented = true;
      else if (func_name == "PERSON_AND_ORGANIZATION") unimplemented = true;
      else if (func_name == "CALENDAR_DATE") unimplemented = true;
      else if (func_name == "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE") unimplemented = true;
      else if (func_name == "PERSON_AND_ORGANIZATION_ROLE") unimplemented = true;
      else if (func_name == "PRODUCT_RELATED_PRODUCT_CATEGORY") unimplemented = true;
      else if (func_name == "CC_DESIGN_DATE_AND_TIME_ASSIGNMENT") unimplemented = true;
      else if (func_name == "SECURITY_CLASSIFICATION") unimplemented = true;
      else if (func_name == "APPROVAL_PERSON_ORGANIZATION") unimplemented = true;
      else if (func_name == "DESIGN_CONTEXT") unimplemented = true;
      else if (func_name == "CC_DESIGN_SECURITY_CLASSIFICATION") unimplemented = true;
      else if (func_name == "PRODUCT_CONTEXT") unimplemented = true;
      else if (func_name == "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION")
        unimplemented = true;
      else if (func_name == "STYLED_ITEM") unimplemented = true;
      else if (func_name == "PRESENTATION_STYLE_ASSIGNMENT") unimplemented = true;
      else if (func_name == "COLOUR_RGB") unimplemented = true;
      else if (func_name == "FILL_AREA_STYLE") unimplemented = true;
      else if (func_name == "SURFACE_STYLE_USAGE") unimplemented = true;
      else if (func_name == "SURFACE_SIDE_STYLE") unimplemented = true;
      else if (func_name == "SURFACE_STYLE_FILL_AREA") unimplemented = true;
      else if (func_name == "FILL_AREA_STYLE_COLOUR") unimplemented = true;
      else if (func_name == "CURVE_STYLE") unimplemented = true;
      else if (func_name == "DRAUGHTING_PRE_DEFINED_CURVE_FONT") unimplemented = true;
      else if (func_name == "SHAPE_REPRESENTATION") unimplemented = true;
      else if (func_name == "SHAPE_REPRESENTATION_RELATIONSHIP") unimplemented = true;
      else if (func_name == "PERSONAL_ADDRESS") unimplemented = true;
      else if (func_name == "PLANE_ANGLE_MEASURE_WITH_UNIT") unimplemented = true;
      else if (func_name == "PRODUCT_CATEGORY") unimplemented = true;
      else if (func_name == "PRODUCT_CATEGORY_RELATIONSHIP") unimplemented = true;
      else if (func_name == "(LENGTH_UNIT") unimplemented = true;
      else if (func_name == "(NAMED_UNIT") unimplemented = true;
      else if (func_name == "(GEOMETRIC_REPRESENTATION_CONTEXT") unimplemented = true;
      else if (func_name == "(") unimplemented = true;
      if (!ent) {
        if (unimplemented) {
          ent = new Line(entities);  // TODO fix
        } else {
          LOG(message_group::Export_Warning, "Unknown Type %1$s", func_name);
          ent = new Line(entities);
        }
      }

      if (ent) {
        ent->id = id;
        ent_map[id] = ent;
        ents.push_back(ent);
        args.push_back(arg_str);
      }
    }
    //		std::cout << cur_str << "\n";
  }
  // processes all the arguments
  for (size_t i = 0; i < ents.size(); i++) {
    ents[i]->parse_args(ent_map, args[i]);
  }
  stp_file.close();
  this->entities = ents;
}
