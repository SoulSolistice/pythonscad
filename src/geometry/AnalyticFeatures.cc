#include "geometry/AnalyticFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

#include "geometry/Surface.h"

namespace AnalyticFeatures {

Vector3d perpendicular(const Vector3d& norm)
{
  const Vector3d axis = fabs(norm[0]) < 0.9 ? Vector3d(1, 0, 0) : Vector3d(0, 1, 0);
  return norm.cross(axis).normalized();
}

bool fitCircleCentre(const std::vector<Vector3d>& vertices, const std::vector<int>& ids,
                     const Vector3d& axis, double level, Vector3d& centre)
{
  if (ids.size() < 3) return false;

  const Vector3d u = perpendicular(axis);
  const Vector3d w = axis.cross(u);
  const Vector3d origin = vertices[ids[0]];

  Matrix3d ata = Matrix3d::Zero();
  Vector3d atb = Vector3d::Zero();
  for (const int id : ids) {
    const Vector3d rel = vertices[id] - origin;
    const Vector3d row(2 * rel.dot(u), 2 * rel.dot(w), 1.0);
    const double val = rel.dot(u) * rel.dot(u) + rel.dot(w) * rel.dot(w);
    ata += row * row.transpose();
    atb += row * val;
  }

  Eigen::FullPivLU<Matrix3d> lu(ata);
  if (!lu.isInvertible()) return false;
  const Vector3d sol = lu.solve(atb);
  if (!sol.allFinite()) return false;

  centre = origin + sol[0] * u + sol[1] * w;
  centre -= axis * (axis.dot(centre) - level);
  return true;
}

double distanceToAxis(const Vector3d& pt, const Vector3d& base, const Vector3d& axis)
{
  const Vector3d rel = pt - base;
  return (rel - axis * axis.dot(rel)).norm();
}

bool Band::isCone() const
{
  return fabs(r_bottom - r_top) > 1e-9 * std::max(r_bottom, r_top);
}

namespace {

std::string format(const char *fmt, ...)
{
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  return buf;
}

}  // namespace

namespace {

/*! An edge of a facet, as an unordered vertex pair. */
using EdgeKey = std::pair<int, int>;
EdgeKey edgeKey(int a, int b)
{
  return a < b ? EdgeKey(a, b) : EdgeKey(b, a);
}

/*! The four edges of the parameter square, as arguments to
 * BezierPatchSurface::boundary: u=0, u=1, v=0, v=1. */
constexpr bool EDGE_ALONG_U[4] = {false, false, true, true};
constexpr bool EDGE_FAR[4] = {false, true, false, true};

/*! Distance from a point to one boundary curve of a patch, by sampling then
 * refining. The curve is degree 2 at most and only boundary vertices are ever
 * tested, so this does not need to be clever. */
double distanceToBoundary(const BezierPatchSurface& patch, int e, const Vector3d& pt)
{
  const std::vector<Vector3d> cp = patch.boundary(EDGE_ALONG_U[e], EDGE_FAR[e]);
  const std::vector<double> cw = patch.boundaryWeights(EDGE_ALONG_U[e], EDGE_FAR[e]);
  const bool rational = cw.size() == cp.size();
  auto at = [&](double t) {
    // The boundary of a rational patch is a rational curve, so de Casteljau has
    // to run on (w*P, w) and divide at the end. Evaluating it as a polynomial
    // curve measures the distance to the parabola through the same control
    // points, which is 6% of the radius away from the circular arc the mesh
    // actually sits on - against a tolerance of 1e-7 of the model size. Every
    // boundary segment of every fillet patch then belongs to no edge, and the
    // whole fillet is written faceted while nothing anywhere reports an error.
    std::vector<Vector3d> w = cp;
    std::vector<double> ww(cp.size(), 1.0);
    if (rational) {
      for (std::size_t i = 0; i < cp.size(); i++) {
        w[i] = cp[i] * cw[i];
        ww[i] = cw[i];
      }
    }
    for (std::size_t k = w.size(); k > 1; k--) {
      for (std::size_t i = 0; i + 1 < k; i++) {
        w[i] = w[i] * (1 - t) + w[i + 1] * t;
        ww[i] = ww[i] * (1 - t) + ww[i + 1] * t;
      }
    }
    return ww[0] == 0.0 ? w[0] : Vector3d(w[0] / ww[0]);
  };
  double best = -1, bt = 0;
  for (int i = 0; i <= 64; i++) {
    const double t = i / 64.0;
    const double d = (at(t) - pt).norm();
    if (best < 0 || d < best) {
      best = d;
      bt = t;
    }
  }
  for (double step = 1.0 / 64; step > 1e-13; step *= 0.5) {
    for (const double t : {bt - step, bt + step}) {
      const double c = std::min(1.0, std::max(0.0, t));
      const double d = (at(c) - pt).norm();
      if (d < best) {
        best = d;
        bt = c;
      }
    }
  }
  return best;
}

/*! Which boundary curves of the patch a vertex lies on.
 *
 * By distance to the curve rather than by its parameters, because the
 * parameters cannot answer it at a corner of the square: a corner is on two
 * edges at once, and a corner fillet's apex - the whole of its collapsed `u = 1`
 * edge - is the far end of *both* rails. Classifying by parameter put the apex
 * inside one rail's run, so that run spanned two different curves and the edge
 * from the apex to the other rail was about to be replaced by the wrong one. */
unsigned boundarySet(const BezierPatchSurface& patch, const Vector3d& pt, double tol)
{
  unsigned mask = 0;
  for (int e = 0; e < 4; e++) {
    if (patch.degenerateAt(EDGE_ALONG_U[e], EDGE_FAR[e])) continue;
    if (distanceToBoundary(patch, e, pt) <= tol) mask |= 1u << e;
  }
  return mask;
}

}  // namespace

namespace {

/*! The circular arc a rational quadratic Bezier draws, from its control points
 * alone.
 *
 * A quadratic Bezier through (a, b, c) with the middle weight at cos(theta/2)
 * is exactly a circular arc - see BezierWeight() in core/FilletNode.cc, which
 * is where every patch this file sees comes from. The arc leaves `a` along
 * `b - a` and arrives at `c` along `c - b`, so its centre is the point on the
 * perpendicular to the first tangent at `a` which is also equidistant from `c`:
 * with u the in-plane unit normal to that tangent and d = a - c, the centre is
 * a + s*u where |d + s*u|^2 = s^2, so s = -|d|^2 / (2 u.d).
 *
 * Nothing here checks the *weight*. It cannot: three points and a weight that
 * is not cos(theta/2) draw some other conic through the same points, and this
 * returns the circle they would have drawn. The caller is required to measure
 * the patch against the result, which is what turns a guess into a fit. */
bool arcCircle(const Vector3d& a, const Vector3d& b, const Vector3d& c, Vector3d& centre,
               Vector3d& normal, double& radius)
{
  const Vector3d t0 = b - a, t1 = c - b;
  const Vector3d n = t0.cross(t1);
  if (n.norm() < 1e-12) return false;  // collinear: a straight rail, not an arc
  normal = n.normalized();
  const Vector3d u = normal.cross(t0).normalized();
  const Vector3d d = a - c;
  const double denom = 2.0 * u.dot(d);
  if (fabs(denom) < 1e-12) return false;
  const double s = -d.squaredNorm() / denom;
  centre = a + u * s;
  radius = fabs(s);
  return radius > 1e-9;
}

/*! How far `pt` is off the quadric, for the two kinds this file recovers. */
double deviationFrom(const Surface& surface, const Vector3d& pt)
{
  if (const auto *cyl = dynamic_cast<const CylinderSurface *>(&surface)) {
    return fabs(distanceToAxis(pt, cyl->refpt, cyl->normdir.normalized()) - cyl->r);
  }
  if (const auto *sph = dynamic_cast<const SphereSurface *>(&surface)) {
    return fabs((pt - sph->refpt).norm() - sph->r);
  }
  return std::numeric_limits<double>::infinity();
}

}  // namespace

std::shared_ptr<Surface> quadricOfPatch(const BezierPatchSurface& bez, double tol, const char **why)
{
  auto refuse = [&](const char *reason) -> std::shared_ptr<Surface> {
    if (why != nullptr) *why = reason;
    return nullptr;
  };
  // Only the two shapes FilletNode draws. A patch of any other degree was
  // declared by something else and has no reason to be a quadric.
  const bool strip = bez.degree_u == 2 && bez.degree_v == 1;
  const bool corner = bez.degree_u == 2 && bez.degree_v == 2;
  if (!strip && !corner) return refuse("it is not a shape fillet() draws");
  // A polynomial patch is a parabola, not an arc.
  if (!bez.isRational()) return refuse("it is polynomial, so its arcs are parabolas");

  std::shared_ptr<Surface> candidate;

  if (strip) {
    // The two rails are the columns of the net. Where the edge is straight and
    // the radius constant they are equal circles on a common axis, and the
    // ruled surface between them is exactly that cylinder.
    //
    // Two rails of *different* radii on a common axis would be a cone, and
    // there is no cone among the declarable surface types - a band expresses
    // one as a frustum, which needs two rims this patch does not have. Such a
    // strip stays a B-spline, which is exact and describes it correctly.
    Vector3d c0, n0, c1, n1;
    double r0 = 0, r1 = 0;
    if (!arcCircle(bez.control(0, 0), bez.control(1, 0), bez.control(2, 0), c0, n0, r0)) {
      return refuse("one rail is not a circular arc");
    }
    if (!arcCircle(bez.control(0, 1), bez.control(1, 1), bez.control(2, 1), c1, n1, r1)) {
      return refuse("the other rail is not a circular arc");
    }
    if (fabs(r0 - r1) > tol) return refuse("the two rails have different radii, so it is a cone");
    Vector3d axis = c1 - c0;
    if (axis.norm() < tol) return refuse("the two rails coincide");
    axis.normalize();
    // Both rails perpendicular to the line joining their centres is what makes
    // the pair coaxial rather than merely parallel.
    if (fabs(fabs(axis.dot(n0)) - 1.0) > 1e-9 || fabs(fabs(axis.dot(n1)) - 1.0) > 1e-9) {
      return refuse("the rails are parallel but not coaxial");
    }
    // Orient the axis so the rail sweeps counter clockwise about it. The face
    // is then a region of the surface's own parameterisation running forwards
    // from the reference direction rather than one wrapping through the seam.
    if ((bez.control(0, 0) - c0).cross(bez.control(2, 0) - c0).dot(axis) < 0) axis = -axis;
    candidate = std::make_shared<CylinderSurface>(c0, axis, r0);
  } else {
    // A corner. Its apex row is a single point, and its first row and first
    // column are two arcs meeting there - concentric and of one radius exactly
    // when the patch is an octant of that sphere.
    const Vector3d apex = bez.control(2, 0);
    if ((bez.control(2, 1) - apex).norm() > tol || (bez.control(2, 2) - apex).norm() > tol) {
      return refuse("its apex row is not a single point");
    }
    Vector3d cu, nu, cv, nv;
    double ru = 0, rv = 0;
    if (!arcCircle(bez.control(0, 0), bez.control(0, 1), bez.control(0, 2), cv, nv, rv) ||
        !arcCircle(bez.control(0, 0), bez.control(1, 0), bez.control(2, 0), cu, nu, ru)) {
      return refuse("one of its meridians is not a circular arc");
    }
    if (fabs(ru - rv) > tol) return refuse("its two meridians have different radii");
    if ((cu - cv).norm() > tol) return refuse("its two meridians are not concentric");
    // The apex is where the two meridians meet, so it is the pole of the
    // parameterisation that keeps this face inside one (theta, phi) rectangle:
    // the row at u = 0 is then the equator arc and the columns are meridians
    // climbing to it. A sphere looks the same from every direction, so this
    // choice costs nothing geometrically and buys a face with no seam in it.
    const Vector3d polar = apex - cu;
    if (polar.norm() < tol) return refuse("its apex sits on its own centre");
    candidate = std::make_shared<SphereSurface>(cu, polar.normalized(), ru);
  }

  // The geometry gate. Everything above reads the *boundary* of the net, and a
  // patch whose two rails are concentric arcs can still bulge off the quadric
  // in between - a rail rotated relative to its partner rules a hyperboloid
  // through the same two circles. Only measuring the surface itself settles it,
  // and the weights are what the evaluation uses, so a patch whose middle
  // weight is not cos(theta/2) fails here rather than being written as the
  // circle it is not.
  constexpr int STEPS = 6;
  for (int i = 0; i <= STEPS; i++) {
    for (int j = 0; j <= STEPS; j++) {
      const Vector3d pt = bez.evaluate(double(i) / STEPS, double(j) / STEPS);
      if (deviationFrom(*candidate, pt) > tol) {
        return refuse("its boundary fits but its middle bulges off the quadric");
      }
    }
  }
  return candidate;
}

std::vector<Vector3d> runControlPoints(const Patch& patch, const Patch::Run& run,
                                       const std::vector<Vector3d>& vertices,
                                       std::vector<double> *weights_out)
{
  if (weights_out != nullptr) weights_out->clear();
  const auto *bez = dynamic_cast<const BezierPatchSurface *>(patch.surface.get());
  if (bez == nullptr || run.edge < 0 || run.edge > 3 || run.verts.empty()) return {};
  const bool along_u = EDGE_ALONG_U[run.edge], far = EDGE_FAR[run.edge];
  std::vector<Vector3d> cp = bez->boundary(along_u, far);
  std::vector<double> cw = bez->boundaryWeights(along_u, far);
  if (cp.empty()) return cp;
  // A Bezier interpolates its end control points, so which end the curve starts
  // at is decided by comparing the first of them with the run's first vertex.
  // The weights are reversed with the points, off the same decision rather than
  // a second one made from the same coordinates.
  const Vector3d& first = vertices[run.verts.front()];
  if ((cp.front() - first).norm() > (cp.back() - first).norm()) {
    std::reverse(cp.begin(), cp.end());
    std::reverse(cw.begin(), cw.end());
  }
  if (weights_out != nullptr) *weights_out = std::move(cw);
  return cp;
}

bool runCircle(const Patch& patch, const Patch::Run& run, const std::vector<Vector3d>& vertices,
               Vector3d& centre, Vector3d& normal, double& radius)
{
  std::vector<double> cw;
  const std::vector<Vector3d> cp = runControlPoints(patch, run, vertices, &cw);
  if (cp.size() != 3) return false;  // only a quadratic boundary is an arc
  // runControlPoints orders the net so the curve starts at run.verts.front(),
  // and arcCircle's normal is the one the sweep from the first control point to
  // the last turns counter clockwise about - which is the direction a STEP
  // CIRCLE is always written in, so the caller can use it unchanged.
  if (!arcCircle(cp[0], cp[1], cp[2], centre, normal, radius)) return false;

  // arcCircle reads the control points only, so it answers "the circle these
  // three points would draw" for a curve which may be some other conic - a
  // glyph outline is full of them. The weight is what decides, and rather than
  // compare it against cos(theta/2) the curve is measured: at the midpoint the
  // two differ by the whole sagitta, so one sample separates them.
  const double w = cw.size() == 3 ? cw[1] : 1.0;
  const double b0 = 0.25, b1 = 0.5 * w, b2 = 0.25;
  const Vector3d mid = (cp[0] * b0 + cp[1] * b1 + cp[2] * b2) / (b0 + b1 + b2);
  return fabs((mid - centre).norm() - radius) <= 1e-9 * std::max(1.0, radius);
}

namespace {

/*! Length of the facet across an edge: how far the facet reaches from that edge.
 *
 * This is the `c` in the sagitta, and it has to be the span the surface curves
 * over rather than the edge's own length - a long thin facet curves over its
 * width, not its length. */
double reachAcross(const std::vector<Vector3d>& verts, const std::vector<int>& loop, int a, int b)
{
  const Vector3d& pa = verts[a];
  const Vector3d& pb = verts[b];
  Vector3d along = pb - pa;
  const double len = along.norm();
  if (len < 1e-12) return 0.0;
  along /= len;
  double reach = 0.0;
  for (const int v : loop) {
    const Vector3d rel = verts[v] - pa;
    reach = std::max(reach, (rel - along * rel.dot(along)).norm());
  }
  return reach;
}

}  // namespace

std::shared_ptr<Surface> fitCylinder(const Mesh& mesh, const SmoothRegion& region, double tol)
{
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<Vector3d>& normals = *mesh.normals;
  if (region.facets.size() < 3) return nullptr;

  // The axis is the direction every facet normal is perpendicular to, so it is
  // the direction the normals' scatter matrix is *smallest* in. Weighting by
  // area keeps a swarm of tiny facets from outvoting the shape.
  Eigen::Matrix3d scatter = Eigen::Matrix3d::Zero();
  std::set<int> verts;
  for (const std::size_t f : region.facets) {
    const std::vector<int>& loop = loops[f];
    if (loop.size() < 3) continue;
    for (const int v : loop) verts.insert(v);
    double area = 0;
    for (std::size_t i = 1; i + 1 < loop.size(); i++) {
      area +=
        (vertices[loop[i]] - vertices[loop[0]]).cross(vertices[loop[i + 1]] - vertices[loop[0]]).norm() /
        2;
    }
    const Vector3d n = normals[f].normalized();
    scatter += area * n * n.transpose();
  }
  if (verts.size() < 6) return nullptr;

  Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(scatter);
  if (solver.info() != Eigen::Success) return nullptr;
  const Vector3d axis = solver.eigenvectors().col(0).normalized();
  const double lo = solver.eigenvalues()[0], mid = solver.eigenvalues()[1], hi = solver.eigenvalues()[2];
  if (!(hi > 0)) return nullptr;
  // Two conditions, and they refuse different things. The normals must lie in a
  // plane - or there is no axis - and they must genuinely spread within it, or
  // the region is flat and every axis in its plane fits equally well.
  if (lo > 1e-6 * hi) return nullptr;
  if (mid < 1e-3 * hi) return nullptr;

  // A least squares circle through the vertices projected onto the plane
  // perpendicular to the axis: x^2 + y^2 + Dx + Ey + F = 0, linear in D, E, F.
  const Vector3d ref = perpendicular(axis);
  const Vector3d ref2 = axis.cross(ref);
  const Vector3d origin = vertices[*verts.begin()];
  Eigen::Matrix3d ata = Eigen::Matrix3d::Zero();
  Vector3d atb = Vector3d::Zero();
  for (const int v : verts) {
    const Vector3d rel = vertices[v] - origin;
    const double x = rel.dot(ref), y = rel.dot(ref2);
    const Vector3d row(x, y, 1.0);
    ata += row * row.transpose();
    atb += row * -(x * x + y * y);
  }
  if (fabs(ata.determinant()) < 1e-18) return nullptr;
  const Vector3d sol = ata.inverse() * atb;
  const double cx = -sol[0] / 2, cy = -sol[1] / 2;
  const double rsq = cx * cx + cy * cy - sol[2];
  if (!(rsq > 0)) return nullptr;
  const double r = sqrt(rsq);
  if (!(r > tol)) return nullptr;

  const Vector3d centre = origin + ref * cx + ref2 * cy;
  for (const int v : verts) {
    const Vector3d rel = vertices[v] - centre;
    const double radial = (rel - axis * rel.dot(axis)).norm();
    if (fabs(radial - r) > tol) return nullptr;
  }
  return std::make_shared<CylinderSurface>(centre, axis, r);
}

std::shared_ptr<Surface> gridFromRegion(const Mesh& mesh, const SmoothRegion& region, double tol,
                                        const char **why)
{
  auto refuse = [&](const char *reason) -> std::shared_ptr<Surface> {
    if (why != nullptr) *why = reason;
    return nullptr;
  };
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  if (region.facets.size() < 2) return refuse("it is a single facet");

  // Recover the quads. A generator's sweep is a quad grid; whether it reaches
  // here as quads or as pairs of triangles depends on what merged the facets,
  // so both are accepted.
  //
  // Which edge between two triangles is the diagonal is the whole difficulty,
  // and the obvious answer is wrong. "The longest edge of both" is the
  // hypotenuse rule, and it holds for a grid of rectangles and fails for a grid
  // of skewed parallelograms - a twisted extrusion's walls, where the short
  // diagonal is shorter than the sides, and where near the boundary a side can
  // be the longest edge a triangle has. Both failures were measured on
  // step-approximate-report before this was written the second time.
  //
  // What does hold is that a swept grid's quads are nearly parallelograms. So
  // every interior edge is scored by how close the quad across it would come to
  // one, and the pairing is taken greedily from the best score down. That is a
  // heuristic, and it does not have to be right: the layout below either comes
  // out a rectangle of coordinates or it does not, so a wrong pairing is
  // refused rather than believed.
  std::map<EdgeKey, std::vector<std::size_t>> users;
  for (const std::size_t f : region.facets) {
    const std::vector<int>& loop = loops[f];
    for (std::size_t i = 0; i < loop.size(); i++) {
      users[edgeKey(loop[i], loop[(i + 1) % loop.size()])].push_back(f);
    }
  }

  std::vector<std::vector<int>> quads;
  std::set<std::size_t> paired;
  std::set<std::size_t> triangles;
  for (const std::size_t f : region.facets) {
    const std::vector<int>& loop = loops[f];
    if (loop.size() == 4) {
      quads.push_back(loop);
      paired.insert(f);
      continue;
    }
    if (loop.size() != 3) {
      return refuse("it has a facet that is neither a triangle nor a quad");
    }
    triangles.insert(f);
  }

  struct Candidate {
    double score;
    std::size_t a, b;
    std::vector<int> quad;
  };
  std::vector<Candidate> candidates;
  for (const auto& entry : users) {
    if (entry.second.size() != 2) continue;
    const std::size_t f = entry.second[0], g = entry.second[1];
    if (!triangles.count(f) || !triangles.count(g)) continue;
    auto opposite = [&](std::size_t face) {
      for (const int v : loops[face]) {
        if (v != entry.first.first && v != entry.first.second) return v;
      }
      return -1;
    };
    const int a = opposite(f), b = opposite(g);
    if (a < 0 || b < 0) continue;
    // The quad in cyclic order, and the order has to come from the mesh rather
    // than from the edge key. The key is sorted by vertex index, so taking it as
    // written gives each quad an arbitrary handedness, and the layout below -
    // which turns the same way at every step - then disagrees with itself
    // wherever two neighbours were written opposite ways round. That is what it
    // did on every wall of step-approximate-report.
    //
    // The shared edge is interior to the quad, so the quad's own boundary is
    // the four outer edges: with f running u->v, they are v->a, a->u from f and
    // u->b, b->v from g.
    int u = entry.first.first, v = entry.first.second;
    bool found = false;
    for (std::size_t i = 0; i < loops[f].size() && !found; i++) {
      const int x = loops[f][i], y = loops[f][(i + 1) % loops[f].size()];
      if (x == u && y == v) found = true;
      else if (x == v && y == u) {
        std::swap(u, v);
        found = true;
      }
    }
    if (!found) continue;
    const Vector3d p0 = vertices[v], p1 = vertices[a];
    const Vector3d p2 = vertices[u], p3 = vertices[b];
    const double perimeter = (p1 - p0).norm() + (p2 - p1).norm() + (p3 - p2).norm() + (p0 - p3).norm();
    if (!(perimeter > 0)) continue;
    const double skew = ((p1 - p0) - (p2 - p3)).norm() + ((p2 - p1) - (p3 - p0)).norm();
    candidates.push_back({skew / perimeter, f, g, {v, a, u, b}});
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& x, const Candidate& y) { return x.score < y.score; });

  // Greedy from the best score down, and then repaired. Greedy alone strands
  // triangles - it did, on every wall of step-approximate-report - because
  // taking the best pair available can leave a neighbour with none, and on a
  // grid where every quad scores about the same the order is close to
  // arbitrary. The repair is the standard one: walk an augmenting path from
  // each stranded triangle, breaking and remaking pairs along it.
  std::map<std::size_t, std::vector<std::size_t>> neighbours;
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> quad_of;
  for (std::size_t c = 0; c < candidates.size(); c++) {
    neighbours[candidates[c].a].push_back(candidates[c].b);
    neighbours[candidates[c].b].push_back(candidates[c].a);
    quad_of[{candidates[c].a, candidates[c].b}] = c;
    quad_of[{candidates[c].b, candidates[c].a}] = c;
  }
  std::map<std::size_t, std::size_t> partner;
  for (const auto& candidate : candidates) {
    if (partner.count(candidate.a) || partner.count(candidate.b)) continue;
    partner[candidate.a] = candidate.b;
    partner[candidate.b] = candidate.a;
  }
  // The repair is a heuristic, not a maximum matching. Triangle adjacency is not
  // bipartite in general, and a plain augmenting path without blossom
  // contraction can miss a path that exists around an odd cycle. That costs a
  // quad, never a wrong one: a triangle left unpaired refuses the grid and the
  // region goes out faceted.
  //
  // The depth limit is the part that matters. Each level inserts a triangle into
  // `seen` before recursing, so depth is bounded only by the number of triangles
  // in the region - tens of thousands on a large sweep, one stack frame each.
  // Running out of stack would take the process down; giving up returns the same
  // faceted fallback an unpairable triangle already produces.
  const std::size_t augment_max_depth = 4096;
  std::function<bool(std::size_t, std::set<std::size_t>&, std::size_t)> augment =
    [&](std::size_t u, std::set<std::size_t>& seen, std::size_t depth) {
    if (depth >= augment_max_depth) return false;
    for (const std::size_t v : neighbours[u]) {
      if (seen.count(v)) continue;
      seen.insert(v);
      const auto held = partner.find(v);
      if (held == partner.end()) {
        partner[v] = u;
        partner[u] = v;
        return true;
      }
      const std::size_t displaced = held->second;
      partner.erase(displaced);
      partner.erase(v);
      if (augment(displaced, seen, depth + 1)) {
        partner[v] = u;
        partner[u] = v;
        return true;
      }
      partner[v] = displaced;
      partner[displaced] = v;
    }
    return false;
  };
  for (const std::size_t f : triangles) {
    if (partner.count(f)) continue;
    std::set<std::size_t> seen{f};
    if (!augment(f, seen, 0)) return refuse("a triangle pairs with no neighbour into a quad");
  }
  for (const auto& entry : partner) {
    if (entry.first > entry.second) continue;  // once per pair
    const auto it = quad_of.find({entry.first, entry.second});
    if (it == quad_of.end()) return refuse("a pairing has no quad behind it");
    quads.push_back(candidates[it->second].quad);
    paired.insert(entry.first);
    paired.insert(entry.second);
  }
  if (quads.empty()) return refuse("no quads could be recovered");

  // Lay the quads out on integer coordinates, by flood fill from one of them.
  // A quad sharing an edge with a placed quad has two of its corners placed
  // already, and those two decide where the other two go; a grid that really is
  // one comes out as a rectangle of coordinates with nothing on top of anything
  // else, and one that is not disagrees with itself and is refused here.
  std::map<EdgeKey, std::vector<std::size_t>> quad_users;
  for (std::size_t q = 0; q < quads.size(); q++) {
    for (std::size_t i = 0; i < 4; i++) {
      quad_users[edgeKey(quads[q][i], quads[q][(i + 1) % 4])].push_back(q);
    }
  }

  std::map<int, std::pair<int, int>> coord;
  std::vector<char> placed(quads.size(), 0);
  auto place = [&](int v, int i, int j) {
    const auto it = coord.find(v);
    if (it == coord.end()) {
      coord[v] = {i, j};
      return true;
    }
    return it->second.first == i && it->second.second == j;
  };
  {
    const std::vector<int>& seed = quads[0];
    coord[seed[0]] = {0, 0};
    coord[seed[1]] = {1, 0};
    coord[seed[2]] = {1, 1};
    coord[seed[3]] = {0, 1};
    placed[0] = 1;
  }
  for (bool moved = true; moved;) {
    moved = false;
    for (std::size_t q = 0; q < quads.size(); q++) {
      if (placed[q]) continue;
      const std::vector<int>& quad = quads[q];
      int known = -1;
      for (int i = 0; i < 4; i++) {
        if (coord.count(quad[i]) && coord.count(quad[(i + 1) % 4])) {
          known = i;
          break;
        }
      }
      if (known < 0) continue;
      // The placed edge runs from `known` to `known+1`; the quad continues on
      // the far side of it, so the other two corners are that edge translated
      // by the perpendicular step.
      const std::pair<int, int> a = coord[quad[known]];
      const std::pair<int, int> b = coord[quad[(known + 1) % 4]];
      const int di = b.first - a.first, dj = b.second - a.second;
      if (abs(di) + abs(dj) != 1) return refuse("the quads do not step by one, so they are not a grid");
      const int pi = -dj, pj = di;  // turn left, into the new quad
      if (!place(quad[(known + 2) % 4], b.first + pi, b.second + pj)) {
        return refuse("the layout disagrees with itself, so the region folds or wraps");
      }
      if (!place(quad[(known + 3) % 4], a.first + pi, a.second + pj)) {
        return refuse("the layout disagrees with itself, so the region folds or wraps");
      }
      placed[q] = 1;
      moved = true;
    }
  }
  for (const char c : placed) {
    if (!c) return refuse("the quads are not one connected sheet");
  }

  int lo_i = 0, hi_i = 0, lo_j = 0, hi_j = 0;
  bool first = true;
  for (const auto& entry : coord) {
    const int i = entry.second.first, j = entry.second.second;
    lo_i = first ? i : std::min(lo_i, i);
    hi_i = first ? i : std::max(hi_i, i);
    lo_j = first ? j : std::min(lo_j, j);
    hi_j = first ? j : std::max(hi_j, j);
    first = false;
  }
  const int rows = hi_i - lo_i + 1, cols = hi_j - lo_j + 1;
  if (rows < 2 || cols < 2) return refuse("the recovered grid is one row or one column");
  if (std::size_t(rows) * cols != coord.size())
    return refuse("the recovered grid is not a full rectangle");

  std::vector<Vector3d> net(std::size_t(rows) * cols, Vector3d::Zero());
  std::vector<char> filled(std::size_t(rows) * cols, 0);
  for (const auto& entry : coord) {
    const std::size_t at = std::size_t(entry.second.first - lo_i) * cols + (entry.second.second - lo_j);
    net[at] = vertices[entry.first];
    filled[at] = 1;
  }
  for (const char c : filled) {
    if (!c) return refuse("the recovered grid has a gap in it");
  }

  // The grid is only worth declaring if the surface it describes passes through
  // the points it was built from, which is what GridSurface's own interpolation
  // decides. Cubic along the sweep needs four stations; below that the fit is
  // the mesh again and buys nothing.
  if (rows < 4) return refuse("the sweep has fewer than four stations, so there is no cubic to fit");
  auto grid = std::make_shared<GridSurface>(rows, cols, net, false);

  // One guard, and it matters more than it looks. The band is not only a
  // measurement here - it is the tolerance membership is then answered at - so
  // a wild recovery would come with a wild band and admit everything. Against
  // what, though, has to be a property of the grid itself rather than of the
  // caller's bookkeeping: a surface that bows by a quarter of a cell is not
  // describing this tessellation, whatever the region record happens to say.
  double scale = 0;
  int cells = 0;
  for (int i = 0; i + 1 < rows; i++) {
    for (int j = 0; j + 1 < cols; j++) {
      scale += (grid->at(i + 1, j + 1) - grid->at(i, j)).norm();
      cells++;
    }
  }
  if (cells == 0 || !(scale > 0)) return refuse("the recovered grid has no extent");
  scale /= cells;
  if (grid->tessellationBand() > 0.25 * scale + tol) {
    return refuse("the interpolant bows by more than a quarter of a cell");
  }
  return grid;
}

std::vector<SmoothRegion> uncoveredRegions(const Mesh& mesh, const std::vector<char>& consumed,
                                           double smooth_angle)
{
  const std::vector<Vector3d>& verts = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;
  const std::vector<Vector3d>& normals = *mesh.normals;

  std::vector<char> eligible(loops.size(), 0);
  for (std::size_t i = 0; i < loops.size(); i++) {
    eligible[i] = valid[i] && !is_hole[i] && (i >= consumed.size() || !consumed[i]);
  }

  // edge -> the eligible facets using it
  std::map<EdgeKey, std::vector<std::size_t>> users;
  for (std::size_t i = 0; i < loops.size(); i++) {
    if (!eligible[i]) continue;
    const std::vector<int>& loop = loops[i];
    for (std::size_t j = 0; j < loop.size(); j++) {
      users[edgeKey(loop[j], loop[(j + 1) % loop.size()])].push_back(i);
    }
  }

  std::vector<SmoothRegion> out;
  std::vector<char> seen(loops.size(), 0);
  for (std::size_t seed = 0; seed < loops.size(); seed++) {
    if (!eligible[seed] || seen[seed]) continue;
    SmoothRegion region;
    std::vector<double> bands;
    std::vector<std::size_t> stack{seed};
    seen[seed] = 1;
    while (!stack.empty()) {
      const std::size_t f = stack.back();
      stack.pop_back();
      region.facets.push_back(f);
      const std::vector<int>& loop = loops[f];
      for (std::size_t j = 0; j < loop.size(); j++) {
        const int a = loop[j], b = loop[(j + 1) % loop.size()];
        const auto it = users.find(edgeKey(a, b));
        if (it == users.end()) continue;
        for (const std::size_t g : it->second) {
          if (g == f) continue;
          const double dot = std::clamp(normals[f].dot(normals[g]), -1.0, 1.0);
          const double dihedral = acos(dot);
          if (dihedral > smooth_angle) continue;
          // The band this edge leaves open: the sagitta of a circular cross
          // section through the two facets. Measured whether or not the
          // neighbour is new, because a region's band is a property of all its
          // interior edges.
          const double reach =
            std::max(reachAcross(verts, loops[f], a, b), reachAcross(verts, loops[g], a, b));
          region.worst_dihedral = std::max(region.worst_dihedral, dihedral);
          const double sagitta = (reach / 2.0) * tan(dihedral / 4.0);
          region.band = std::max(region.band, sagitta);
          bands.push_back(sagitta);
          if (seen[g]) continue;
          seen[g] = 1;
          stack.push_back(g);
        }
      }
    }
    for (const std::size_t f : region.facets) {
      const std::vector<int>& loop = loops[f];
      Vector3d acc = Vector3d::Zero();
      for (std::size_t j = 1; j + 1 < loop.size(); j++) {
        acc += (verts[loop[j]] - verts[loop[0]]).cross(verts[loop[j + 1]] - verts[loop[0]]);
      }
      region.area += acc.norm() / 2.0;
    }
    // The grid, or what is left of it. An interior vertex is one none of whose
    // incident edges is on the region's boundary; its valence is how many of
    // the region's facets meet there.
    {
      std::map<EdgeKey, int> edge_use;
      std::map<int, int> valence;
      for (const std::size_t f : region.facets) {
        const std::vector<int>& loop = loops[f];
        for (std::size_t j = 0; j < loop.size(); j++) {
          edge_use[edgeKey(loop[j], loop[(j + 1) % loop.size()])]++;
          valence[loop[j]]++;
        }
      }
      std::set<int> on_boundary;
      for (const auto& entry : edge_use) {
        if (entry.second == 1) {
          on_boundary.insert(entry.first.first);
          on_boundary.insert(entry.first.second);
        }
      }
      std::map<int, int> histogram;
      for (const auto& entry : valence) {
        if (on_boundary.count(entry.first) == 0) histogram[entry.second]++;
      }
      std::size_t total = 0, best = 0;
      for (const auto& entry : histogram) {
        total += entry.second;
        if (std::size_t(entry.second) > best) {
          best = entry.second;
          region.modal_valence = entry.first;
        }
      }
      region.interior_vertices = total;
      region.regularity = total ? double(best) / double(total) : 0.0;
    }
    if (!bands.empty()) {
      std::nth_element(bands.begin(), bands.begin() + bands.size() / 2, bands.end());
      region.median_band = bands[bands.size() / 2];
    }
    out.push_back(std::move(region));
  }
  std::sort(out.begin(), out.end(),
            [](const SmoothRegion& a, const SmoothRegion& b) { return a.area > b.area; });
  return out;
}

namespace {

/*! The boundary of a region of facets, as directed cycles.
 *
 * Directed in the sense the region's own facets traverse it. That is what makes
 * a collapsed face agree with the mesh it replaces, and it is what forces the
 * neighbour on the far side of every boundary segment to traverse it the other
 * way round - the invariant that keeps the shell closed. An undirected walk
 * gives an arbitrary direction, and half the faces then share an edge in the
 * same direction as their neighbour.
 *
 * There can be more than one. A fillet patch is a disc and has exactly one, but
 * a declared sweep closed around its profile is a tube: trim it against a wall
 * and what is left is an annulus, whose boundary is two cycles and neither of
 * them closes over the other. Walking only the first reported that the boundary
 * did not close, which was true of the walk rather than of the region.
 *
 * Returns null on success, or why the region has no such boundary - which is
 * always a reason to leave it faceted. */
const char *boundaryCycles(const std::vector<std::vector<int>>& loops,
                           const std::vector<std::size_t>& facets, std::vector<std::vector<int>>& cycles)
{
  // Edges used by one of the region's facets rather than two. Anything else
  // means the region is not a simple sheet.
  std::map<EdgeKey, int> uses;
  for (const std::size_t f : facets) {
    const std::vector<int>& loop = loops[f];
    for (std::size_t i = 0; i < loop.size(); i++) {
      uses[edgeKey(loop[i], loop[(i + 1) % loop.size()])]++;
    }
  }
  std::map<int, int> next;  // boundary vertex -> the next one along
  std::size_t boundary_edges = 0;
  for (const std::size_t f : facets) {
    const std::vector<int>& loop = loops[f];
    for (std::size_t i = 0; i < loop.size(); i++) {
      const int a = loop[i], b = loop[(i + 1) % loop.size()];
      if (uses[edgeKey(a, b)] != 1) continue;
      next[a] = b;
      boundary_edges++;
    }
  }
  if (boundary_edges == 0 || next.size() != boundary_edges) {
    return "the facets on this patch do not form a simple sheet";
  }

  cycles.clear();
  std::set<int> walked;
  std::size_t seen = 0;
  for (const auto& entry : next) {
    if (walked.count(entry.first)) continue;
    std::vector<int> cycle;
    int cur = entry.first;
    do {
      cycle.push_back(cur);
      walked.insert(cur);
      cur = next[cur];
    } while (cur != entry.first && !walked.count(cur) && cycle.size() <= boundary_edges);
    if (cur != entry.first) return "the patch boundary does not close";
    seen += cycle.size();
    cycles.push_back(std::move(cycle));
  }
  if (seen != boundary_edges) return "the patch boundary does not close";
  return nullptr;
}

}  // namespace

std::vector<Patch> recogniseBezierPatches(const Mesh& mesh,
                                          const std::vector<std::shared_ptr<Surface>>& surfaces,
                                          const std::vector<char>& consumed,
                                          std::vector<std::string>& report)
{
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;

  std::vector<Patch> patches;
  std::vector<char> taken(loops.size(), 0);

  for (const auto& surface : surfaces) {
    const auto *bez = dynamic_cast<const BezierPatchSurface *>(surface.get());
    if (bez == nullptr || bez->net.empty()) continue;

    // A Bezier lies inside the convex hull of its control net, so a box round
    // the net rejects almost every facet in the model without projecting
    // anything. That matters: projection is a Newton solve from a grid of
    // starts, and a filleted cube has thousands of facets and dozens of
    // patches.
    Vector3d lo = bez->net.front(), hi = bez->net.front();
    for (const auto& p : bez->net) {
      lo = lo.cwiseMin(p);
      hi = hi.cwiseMax(p);
    }
    const double slack = 1e-6 * std::max(1.0, (hi - lo).norm());
    lo.array() -= slack;
    hi.array() += slack;

    Patch patch;
    patch.surface = surface;
    for (std::size_t f = 0; f < loops.size(); f++) {
      if (!loop_valid[f] || is_hole[f] || consumed[f] || taken[f]) continue;
      bool on = true;
      for (const int v : loops[f]) {
        const Vector3d& p = vertices[v];
        if ((p.array() < lo.array()).any() || (p.array() > hi.array()).any()) {
          on = false;
          break;
        }
      }
      if (!on) continue;
      for (const int v : loops[f]) {
        std::vector<Vector3d> unused;
        if (!const_cast<BezierPatchSurface *>(bez)->pointMember(unused, vertices[v])) {
          on = false;
          break;
        }
      }
      if (on) patch.facets.push_back(f);
    }
    if (patch.facets.empty()) continue;

    std::vector<int> edge_of;
    std::vector<std::vector<int>> cycles;
    const char *why = boundaryCycles(loops, patch.facets, cycles);
    // A Bezier patch is a disc: one boundary, or it is not the sheet it claims
    // to be.
    if (why == nullptr && cycles.size() != 1) why = "the patch boundary does not close";
    if (why != nullptr) {
      patch.alive = false;
      patch.dropped = why;
      patches.push_back(std::move(patch));
      continue;
    }
    const std::vector<int>& cycle = cycles.front();
    // Each *segment* of the boundary is assigned a curve, not each vertex: a
    // segment lies on exactly one, while its endpoints may lie on two.
    const double curve_tol = 1e-7 * std::max(1.0, (hi - lo).norm());
    std::vector<unsigned> on(cycle.size());
    for (std::size_t i = 0; i < cycle.size(); i++) {
      on[i] = boundarySet(*bez, vertices[cycle[i]], curve_tol);
    }
    bool classified = true;
    for (std::size_t i = 0; i < cycle.size(); i++) {
      const unsigned both = on[i] & on[(i + 1) % cycle.size()];
      if (both == 0) {
        classified = false;
        break;
      }
      // A segment whose ends share two curves is a whole edge of the square
      // seen end to end; take the lowest, consistently.
      int e = 0;
      while (((both >> e) & 1u) == 0) e++;
      edge_of.push_back(e);
    }
    if (!classified) {
      patch.alive = false;
      patch.dropped = "a boundary segment lies on none of the patch's edges";
      patches.push_back(std::move(patch));
      continue;
    }

    // Split the cycle into maximal runs sharing one edge of the parameter
    // square. A vertex sitting exactly on a corner reports whichever edge came
    // first, so let it join the run already in progress.
    // Maximal runs of consecutive segments on the same curve. `edge_of[i]` is
    // the segment from cycle[i] to cycle[i+1], so a run of segments is a run of
    // vertices one longer.
    const std::size_t n = cycle.size();
    std::size_t begin = 0;
    while (begin < n && edge_of[begin] == edge_of[(begin + n - 1) % n]) begin++;
    if (begin == n) begin = 0;  // the whole boundary is one curve
    for (std::size_t i = 0; i < n;) {
      const int id = edge_of[(begin + i) % n];
      Patch::Run run;
      run.edge = id;
      run.straight = id <= 1 ? bez->degree_v == 1 : bez->degree_u == 1;
      std::size_t j = i;
      run.verts.push_back(cycle[(begin + j) % n]);
      while (j < n && edge_of[(begin + j) % n] == id) {
        j++;
        run.verts.push_back(cycle[(begin + j) % n]);
      }
      patch.runs.push_back(std::move(run));
      i = j;
    }

    for (const std::size_t f : patch.facets) taken[f] = 1;
    patches.push_back(std::move(patch));
  }

  // Resolve what each boundary run borders. Only possible once every patch is
  // known, because a run may be shared with another one.
  {
    // Hole loops belong here. A patch's facets are always outer bounds - a hole
    // is not a sheet of anything - but what a patch *borders* may perfectly well
    // be one: the counter of a glyph is a hole in the letter's cap face, and
    // every patch around the inside of an O runs along it. Skipping holes left
    // those runs with one user instead of two and so unresolved, which drops the
    // patch; an O kept eight of its nineteen. The band path has always used every
    // valid loop for the same lookup, and the emitter already substitutes into a
    // hole loop and builds a FaceBound for it.
    std::map<EdgeKey, std::vector<std::size_t>> edge_loops;
    for (std::size_t f = 0; f < loops.size(); f++) {
      if (!loop_valid[f]) continue;
      const std::vector<int>& loop = loops[f];
      for (std::size_t i = 0; i < loop.size(); i++) {
        edge_loops[edgeKey(loop[i], loop[(i + 1) % loop.size()])].push_back(f);
      }
    }
    std::map<std::size_t, std::size_t> patch_of_loop;
    for (std::size_t pi = 0; pi < patches.size(); pi++) {
      if (!patches[pi].alive) continue;
      for (const std::size_t f : patches[pi].facets) patch_of_loop[f] = pi;
    }

    for (std::size_t pi = 0; pi < patches.size(); pi++) {
      if (!patches[pi].alive) continue;
      for (auto& run : patches[pi].runs) {
        // Which face is on the other side of each segment. When that face
        // belongs to another patch the answer is the *patch*, not the face: a
        // run of eleven segments against a corner patch borders eleven
        // different triangles of it, so asking for one neighbouring loop
        // rejects every rail on a filleted solid.
        std::set<std::size_t> other_loops, other_patches;
        bool ok = run.verts.size() >= 2;
        for (std::size_t i = 0; ok && i + 1 < run.verts.size(); i++) {
          const auto it = edge_loops.find(edgeKey(run.verts[i], run.verts[i + 1]));
          if (it == edge_loops.end() || it->second.size() != 2) {
            ok = false;
            break;
          }
          const std::size_t a = it->second[0], b = it->second[1];
          const auto own = patch_of_loop.find(a);
          const std::size_t other = (own != patch_of_loop.end() && own->second == pi) ? b : a;
          const auto op = patch_of_loop.find(other);
          if (op != patch_of_loop.end()) other_patches.insert(op->second);
          else other_loops.insert(other);
        }
        if (!ok) continue;  // stays UNRESOLVED
        if (!other_patches.empty()) {
          if (other_patches.size() != 1 || !other_loops.empty()) continue;
          if (*other_patches.begin() == pi) continue;  // folded back on itself
          run.kind = Patch::Run::OTHER_PATCH;
          run.patch = *other_patches.begin();
          continue;
        }
        if (other_loops.size() != 1) continue;
        const std::size_t other = *other_loops.begin();
        // A stretch of one neighbouring face. Find where it starts, so the
        // caller can replace exactly those segments and leave the rest.
        const std::vector<int>& loop = loops[other];
        const std::size_t n = loop.size();
        run.count = run.verts.size() - 1;
        for (std::size_t j = 0; j < n; j++) {
          bool fwd = true, rev = true;
          for (std::size_t c = 0; c < run.count; c++) {
            const int a = loop[(j + c) % n], b = loop[(j + c + 1) % n];
            fwd = fwd && a == run.verts[c] && b == run.verts[c + 1];
            // the neighbour walking the same segments the other way round
            rev = rev && a == run.verts[run.count - c] && b == run.verts[run.count - c - 1];
          }
          if (!fwd && !rev) continue;
          run.kind = run.count == n ? Patch::Run::WHOLE_LOOP : Patch::Run::LOOP_RUN;
          run.loop = other;
          run.start = j;
          run.reversed = !fwd;
          break;
        }
      }
    }
  }

  // Two patches sharing a seam must agree on it exactly. They will share one
  // EdgeCurve, so a seam that is one run for one of them and two for the other
  // cannot be written at all.
  std::size_t seam_mismatch = 0, shared_seams = 0;
  for (std::size_t pi = 0; pi < patches.size(); pi++) {
    if (!patches[pi].alive) continue;
    for (auto& run : patches[pi].runs) {
      if (run.kind != Patch::Run::OTHER_PATCH) continue;
      shared_seams++;
      const Patch& other = patches[run.patch];
      run.partner = std::size_t(-1);
      for (std::size_t ri = 0; ri < other.runs.size(); ri++) {
        const std::vector<int>& theirs = other.runs[ri].verts;
        if (theirs.size() != run.verts.size()) continue;
        bool same = true, flipped = true;
        for (std::size_t k = 0; k < theirs.size(); k++) {
          same = same && theirs[k] == run.verts[k];
          flipped = flipped && theirs[k] == run.verts[theirs.size() - 1 - k];
        }
        if (same || flipped) {
          run.partner = ri;
          break;
        }
      }
      if (run.partner == std::size_t(-1)) {
        run.kind = Patch::Run::UNRESOLVED;
        seam_mismatch++;
      }
    }
  }

  std::size_t live = 0, facets = 0;
  for (const auto& p : patches) {
    if (!p.alive) {
      report.push_back(
        format("a Bezier patch of %d facets was left faceted: %s", int(p.facets.size()), p.dropped));
      continue;
    }
    live++;
    facets += p.facets.size();
  }
  if (live > 0) {
    report.push_back(
      format("%d Bezier patch%s cover %d facets", int(live), live == 1 ? "" : "es", int(facets)));
  }
  if (shared_seams > 0) {
    // Stated even when nothing is wrong. Reporting only the failures makes a
    // clean run and a binary built before the check look identical - both print
    // nothing - which is the same ambiguity the availability line had.
    report.push_back(format("%d of %d shared seams agree between the two patches meeting there",
                            int(shared_seams - seam_mismatch), int(shared_seams)));
  }
  return patches;
}

std::vector<std::shared_ptr<Surface>> fitRevolved(const Mesh& mesh, const SmoothRegion& region,
                                                  double tol, const char **why)
{
  auto refuse = [&](const char *reason) {
    if (why != nullptr) *why = reason;
    return std::vector<std::shared_ptr<Surface>>();
  };
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  if (region.facets.size() < 3) return refuse("it is too small to be a surface of revolution");

  // The axis, which is the whole difficulty and has no single source.
  //
  // Two closed-form fits were tried and both are degenerate on the shapes that
  // matter. The rims give it directly - a frustum's two, a sphere's two cap
  // circles - and a sphere's cap meets the band beside it at the angle of one
  // ring, 11 degrees on a 32x16 sphere, so the caps join the region and it has
  // no boundary at all. A screw-axis fit over the facet normals, solving
  // n . (a x (c - p)) = 0 as a null space, is degenerate the other way: a cone
  // and a sphere both have every normal line through one point, and the null
  // space comes out three dimensional rather than one. Measured, not assumed.
  //
  // So the axis is *proposed* and then verified. Every source of a plausible
  // direction contributes a candidate, and the ring test below - every vertex
  // on the circle its height puts it on - is what accepts one. That test is
  // strict enough that a wrong candidate cannot survive it, which is what lets
  // the proposals be as rough as they like.
  const std::vector<Vector3d>& normals = *mesh.normals;
  Vector3d anchor = Vector3d::Zero();
  double anchor_n = 0;
  std::set<int> region_verts;
  for (const std::size_t f : region.facets) {
    for (const int v : loops[f]) {
      if (!region_verts.insert(v).second) continue;
      anchor += vertices[v];
      anchor_n += 1;
    }
  }
  if (!(anchor_n > 2)) return refuse("it has too few vertices");
  anchor /= anchor_n;

  std::vector<Vector3d> candidates;
  auto propose = [&](const Vector3d& dir) {
    if (dir.norm() < 1e-9) return;
    const Vector3d unit = dir.normalized();
    for (const auto& had : candidates) {
      if (fabs(fabs(had.dot(unit)) - 1.0) < 1e-6) return;
    }
    candidates.push_back(unit);
  };

  // A rim, where the region has one. This is the frustum's case and the
  // cheapest to trust: the plane of a boundary circle is perpendicular to the
  // axis by construction.
  std::vector<std::vector<int>> cycles;
  if (boundaryCycles(loops, region.facets, cycles) == nullptr) {
    for (const auto& cycle : cycles) {
      if (cycle.size() < 3) continue;
      Vector3d centre = Vector3d::Zero();
      for (const int v : cycle) centre += vertices[v];
      centre /= double(cycle.size());
      Vector3d area = Vector3d::Zero();
      for (std::size_t i = 0; i < cycle.size(); i++) {
        area += (vertices[cycle[i]] - centre).cross(vertices[cycle[(i + 1) % cycle.size()]] - centre);
      }
      propose(area);
    }
  }

  // A cap that joined the region. A turned surface is tessellated into quads,
  // so a face with more corners than that is the polygon closing one end, and
  // its normal is the axis.
  for (const std::size_t f : region.facets) {
    if (loops[f].size() > 4) propose(normals[f]);
  }

  // And the apex, for a cone. Every tangent plane of a cone contains its apex,
  // which is a linear least squares over the facet planes; the rulings from
  // there make a constant angle with the axis, so their mean direction is it.
  {
    Eigen::Matrix3d nn = Eigen::Matrix3d::Zero();
    Vector3d nb = Vector3d::Zero();
    for (const std::size_t f : region.facets) {
      const std::vector<int>& loop = loops[f];
      if (loop.empty()) continue;
      const Vector3d n = normals[f].normalized();
      nn += n * n.transpose();
      nb += n * n.dot(vertices[loop[0]]);
    }
    if (fabs(nn.determinant()) > 1e-12) {
      const Vector3d apex = nn.inverse() * nb;
      Vector3d mean = Vector3d::Zero();
      for (const int v : region_verts) {
        const Vector3d rel = vertices[v] - apex;
        if (rel.norm() > tol) mean += rel.normalized();
      }
      propose(mean);
    }
  }
  if (candidates.empty()) return refuse("nothing proposes an axis for it");

  // Each candidate in turn, accepted only by the ring test.
  const char *last = "no candidate axis described a turned surface";
  auto tryAxis = [&](const Vector3d& axis) {
    auto give_up = [&](const char *reason) {
      last = reason;
      return std::vector<std::shared_ptr<Surface>>();
    };
    // Which ring each vertex belongs to: its height along the axis. A frustum has
    // only the two rims; a sphere has one ring per row of its tessellation, and
    // every one of them has to be declared or the bands between them are not
    // bounded by anything the recogniser can match.
    // Keyed by quantised height, but carrying the true mean height as well. The
    // key is only how vertices are grouped; using it as the height in its own
    // right rounds every ring to the quantum, which a cylinder does not care
    // about - it is infinite along its axis - and which defeats the sphere test
    // below outright, since that measures heights against a radius.
    struct Ring {
      double radius = 0, height = 0;
      int count = 0;
    };
    std::map<long long, Ring> rings;
    const double quantum = std::max(tol, 1e-9) * 100;
    std::set<int> seen;
    for (const std::size_t f : region.facets) {
      for (const int v : loops[f]) {
        if (!seen.insert(v).second) continue;
        const Vector3d rel = vertices[v] - anchor;
        const double along = rel.dot(axis);
        Ring& ring = rings[(long long)llround(along / quantum)];
        ring.radius += (rel - axis * along).norm();
        ring.height += along;
        ring.count += 1;
      }
    }
    if (rings.size() < 2) return give_up("its vertices do not lie on rings");

    // Every vertex on the ring its height puts it in, which is what makes this a
    // surface of revolution rather than a swarm that happens to have two round
    // ends.
    for (const std::size_t f : region.facets) {
      for (const int v : loops[f]) {
        const Vector3d rel = vertices[v] - anchor;
        const double along = rel.dot(axis);
        const Ring& ring = rings[(long long)llround(along / quantum)];
        const double radius = ring.radius / ring.count;
        if (fabs((rel - axis * along).norm() - radius) > tol) {
          return give_up("a vertex is off the ring its height puts it on");
        }
      }
    }

    std::vector<std::shared_ptr<Surface>> found;
    for (const auto& entry : rings) {
      const double radius = entry.second.radius / entry.second.count;
      if (!(radius > tol)) continue;  // an apex is not a rim; there is no circle there
      const Vector3d at = anchor + axis * (entry.second.height / entry.second.count);
      found.push_back(std::make_shared<CylinderSurface>(at, axis, radius));
    }
    if (found.size() < 2) return give_up("fewer than two rings carry a circle");

    // And the sphere the rings may lie on, which is worth one more test because
    // of what the band pass does with it. The rings alone already collapse a
    // tessellated sphere into a stack of exact cones - that is how a declared
    // sphere collapses too - but a SphereSurface among the declarations lets the
    // zone pass absorb that whole stack into one SPHERICAL_SURFACE. On a 32x16
    // sphere it is the difference between fifteen faces and one.
    //
    // For a ring at height h and radius r on a sphere centred at t along the
    // axis, (h - t)^2 + r^2 is the same for every ring, which is linear in t.
    if (rings.size() >= 3) {
      std::vector<std::pair<double, double>> hr;
      for (const auto& entry : rings) {
        hr.emplace_back(entry.second.height / entry.second.count,
                        entry.second.radius / entry.second.count);
      }
      const double h0 = hr.front().first, r0 = hr.front().second;
      const double h1 = hr.back().first, r1 = hr.back().second;
      if (fabs(h0 - h1) > tol) {
        const double t = (h0 * h0 + r0 * r0 - h1 * h1 - r1 * r1) / (2 * (h0 - h1));
        const double rsq = (h0 - t) * (h0 - t) + r0 * r0;
        if (rsq > 0) {
          const double radius = sqrt(rsq);
          bool spherical = true;
          for (const auto& entry : hr) {
            const double want = (entry.first - t) * (entry.first - t) + entry.second * entry.second;
            spherical = spherical && fabs(sqrt(want) - radius) <= tol;
          }
          if (spherical) {
            found.push_back(std::make_shared<SphereSurface>(anchor + axis * t, axis, radius));
          }
        }
      }
    }
      return found;
    };


  for (const auto& axis : candidates) {
    std::vector<std::shared_ptr<Surface>> found = tryAxis(axis);
    if (!found.empty()) return found;
  }
  return refuse(last);
}

std::vector<Patch> recogniseGridPatches(const Mesh& mesh,
                                        const std::vector<std::shared_ptr<Surface>>& surfaces,
                                        const std::vector<char>& consumed,
                                        std::vector<std::string>& report)
{
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& is_hole = *mesh.is_hole;

  std::vector<Patch> patches;
  std::vector<char> taken(loops.size(), 0);
  std::size_t corners_only = 0;               // every corner on the sweep, the middle not
  std::size_t cut_into = 0;                   // faces a wrapping claim had to be cut into
  double worst_miss = 0, missed_against = 0;  // how far off, and what was allowed

  // Which faces sit on either side of every edge of the mesh. Needed before the
  // boundaries are split rather than after, because for a declared grid the
  // split *is* by neighbour - see below.
  std::map<EdgeKey, std::vector<std::size_t>> edge_loops;
  for (std::size_t f = 0; f < loops.size(); f++) {
    if (!loop_valid[f]) continue;
    const std::vector<int>& loop = loops[f];
    for (std::size_t i = 0; i < loop.size(); i++) {
      edge_loops[edgeKey(loop[i], loop[(i + 1) % loop.size()])].push_back(f);
    }
  }

  for (const auto& surface : surfaces) {
    const auto *grid = dynamic_cast<const GridSurface *>(surface.get());
    if (grid == nullptr || grid->net.empty()) continue;

    // The declared points bound the sweep, so a box round them rejects most of
    // the model without projecting anything - the same reason the Bezier path
    // takes a box round its control net first. The band is added because
    // membership itself is only accurate to the band.
    Vector3d lo = grid->net.front(), hi = grid->net.front();
    for (const auto& p : grid->net) {
      lo = lo.cwiseMin(p);
      hi = hi.cwiseMax(p);
    }
    const double slack = grid->tessellationBand() + 1e-6 * std::max(1.0, (hi - lo).norm());
    lo.array() -= slack;
    hi.array() += slack;

    // Which facets lie on the sweep, and where along the profile each one sits.
    // The second answer is needed as early as the first: a region covering every
    // span of a closed profile closes around it, and no face on a surface
    // written as an open rectangle can be bounded across its own seam.
    const int segs = grid->closed_v ? grid->cols : grid->cols - 1;
    std::vector<std::size_t> claimed;
    std::vector<int> span_of;
    for (std::size_t f = 0; f < loops.size(); f++) {
      if (!loop_valid[f] || is_hole[f] || consumed[f] || taken[f]) continue;
      bool on = true;
      for (const int v : loops[f]) {
        const Vector3d& p = vertices[v];
        if ((p.array() < lo.array()).any() || (p.array() > hi.array()).any()) {
          on = false;
          break;
        }
      }
      if (!on) continue;
      for (const int v : loops[f]) {
        std::vector<Vector3d> unused;  // pointMember's scratch argument, as elsewhere
        if (!const_cast<GridSurface *>(grid)->pointMember(unused, vertices[v])) {
          on = false;
          break;
        }
      }
      if (!on) continue;
      // Corners are not enough. Every corner of the facet closing a declared
      // profile is a point the generator emitted, so position alone claims it
      // even when the declaration does not cover that strip - which is exactly
      // what a grid declared open does. The centroid is the cheapest point that
      // is not a declared point, and asking whether it lies on the surface asks
      // about the facet rather than about its corners.
      Vector3d centroid = Vector3d::Zero();
      for (const int v : loops[f]) centroid += vertices[v];
      centroid /= double(loops[f].size());
      double pu = 0, pv = 0;
      if (!grid->project(centroid, pu, pv)) {
        corners_only++;
        continue;
      }
      const double miss = (grid->evaluate(pu, pv) - centroid).norm();
      if (miss > grid->membershipTolerance()) {
        corners_only++;
        worst_miss = std::max(worst_miss, miss);
        missed_against = grid->membershipTolerance();
        continue;
      }
      claimed.push_back(f);
      span_of.push_back(segs > 0 ? std::max(0, std::min(segs - 1, int(pv * segs))) : 0);
    }
    if (claimed.empty()) continue;

    // Build one Patch out of a set of facets: its boundary cycles, and the runs
    // those split into. Called once for the whole claim, or once per group when
    // the claim has to be cut - see below.
    auto buildPatch = [&](const std::vector<std::size_t>& facets) {
      Patch patch;
      patch.surface = surface;
      patch.facets = facets;

      std::vector<std::vector<int>> cycles;
      if (const char *why = boundaryCycles(loops, patch.facets, cycles)) {
        patch.alive = false;
        patch.dropped = why;
        return patch;
      }

      // Where a Bezier patch and a declared grid part company.
      //
      // A Bezier covers its whole parameter square, so every boundary segment
      // lies on one of the square's four edges and the split into runs follows
      // the geometry: this stretch is the curve u=0, that one is v=1. A declared
      // grid is trimmed wherever the boolean cut it, and its boundary therefore
      // lies nowhere in particular - which is the whole reason the facets had to
      // be claimed by projection rather than by position.
      //
      // So the split follows the *topology* instead: a run is a maximal stretch
      // of consecutive boundary segments with the same thing on the far side.
      // That is the property a run actually needs - every segment of it is
      // replaced in one neighbouring face, by one curve, or the shell opens -
      // and it is the one the parameter square was standing in for all along.
      const std::set<std::size_t> mine(patch.facets.begin(), patch.facets.end());
      bool ok = true;
      for (std::size_t ci = 0; ci < cycles.size() && ok; ci++) {
        const std::vector<int>& cycle = cycles[ci];
        const std::size_t n = cycle.size();
        std::vector<std::size_t> across(n, std::size_t(-1));
        for (std::size_t i = 0; i < n; i++) {
          const auto it = edge_loops.find(edgeKey(cycle[i], cycle[(i + 1) % n]));
          if (it == edge_loops.end() || it->second.size() != 2) {
            ok = false;
            break;
          }
          across[i] = mine.count(it->second[0]) ? it->second[1] : it->second[0];
        }
        if (!ok) break;

        std::size_t begin = 0;
        while (begin < n && across[begin] == across[(begin + n - 1) % n]) begin++;
        if (begin == n) begin = 0;  // one neighbour all the way round
        for (std::size_t i = 0; i < n;) {
          const std::size_t neighbour = across[(begin + i) % n];
          Patch::Run run;
          // A grid's run lies on no edge of a parameter square and is straight
          // only by accident, so neither field means anything here; the emitter
          // reads `kind`, `verts` and `bound`.
          run.bound = ci;
          std::size_t j = i;
          run.verts.push_back(cycle[(begin + j) % n]);
          while (j < n && across[(begin + j) % n] == neighbour) {
            j++;
            run.verts.push_back(cycle[(begin + j) % n]);
          }
          patch.runs.push_back(std::move(run));
          i = j;
        }
      }
      if (!ok) {
        patch.alive = false;
        patch.dropped = "a boundary segment is not shared with exactly one other face";
      }
      return patch;
    };

    // Does the claim close around the profile? Only a closed profile can, and
    // only when every span of it carries facets.
    bool wraps = grid->closed_v && segs > 0;
    if (wraps) {
      std::vector<char> used(segs, 0);
      for (const int span : span_of) used[span] = 1;
      for (const char c : used) wraps = wraps && c != 0;
    }

    if (!wraps) {
      Patch patch = buildPatch(claimed);
      if (patch.alive) {
        for (const std::size_t f : patch.facets) taken[f] = 1;
      }
      patches.push_back(std::move(patch));
      continue;
    }

    // Cut it into arcs of spans, each of which stays inside the rectangle.
    //
    // The alternative is to write the surface as closed across v and carry a
    // seam edge round the loop, the way a full cylindrical band is written.
    // That works when the region is the whole tube; here it is whatever the
    // boolean left of one, with a trim boundary meeting the seam wherever it
    // happens to. Cutting is the operation that does not depend on the shape of
    // that boundary: each arc of spans is a sheet in its own right and its own
    // face, and the cut runs along mesh edges the two arcs already share, so
    // nothing is asked of the neighbours.
    //
    // Halves first, because two faces are better than four and a ridge whose
    // profile is a simple quadrilateral splits cleanly; one face per span if a
    // half is not a sheet.
    std::vector<std::vector<int>> attempts;
    if (segs >= 4) attempts.push_back({0, segs / 2});
    std::vector<int> each;
    for (int i = 0; i < segs; i++) each.push_back(i);
    attempts.push_back(each);

    for (std::size_t a = 0; a < attempts.size(); a++) {
      const std::vector<int>& starts = attempts[a];
      std::vector<Patch> built;
      bool all_alive = true;
      for (std::size_t g = 0; g < starts.size() && all_alive; g++) {
        const int from = starts[g];
        const int to = g + 1 < starts.size() ? starts[g + 1] : segs;
        std::vector<std::size_t> group;
        for (std::size_t i = 0; i < claimed.size(); i++) {
          if (span_of[i] >= from && span_of[i] < to) group.push_back(claimed[i]);
        }
        if (group.empty()) continue;
        Patch patch = buildPatch(group);
        all_alive = patch.alive;
        built.push_back(std::move(patch));
      }
      // The last attempt stands whatever it produced, so a sweep that cannot be
      // cut into sheets still reports why rather than vanishing.
      if (!all_alive && a + 1 < attempts.size()) continue;
      cut_into += built.size();
      for (auto& patch : built) {
        if (patch.alive) {
          for (const std::size_t f : patch.facets) taken[f] = 1;
        }
        patches.push_back(std::move(patch));
      }
      break;
    }
  }

  // What each run borders. Grouping by neighbour has already answered which
  // face it is; what is left is where in that face the run sits, which is what
  // the emitter substitutes into.
  std::map<std::size_t, std::size_t> patch_of_loop;
  for (std::size_t pi = 0; pi < patches.size(); pi++) {
    if (!patches[pi].alive) continue;
    for (const std::size_t f : patches[pi].facets) patch_of_loop[f] = pi;
  }
  for (std::size_t pi = 0; pi < patches.size(); pi++) {
    if (!patches[pi].alive) continue;
    for (auto& run : patches[pi].runs) {
      if (run.verts.size() < 2) continue;  // stays UNRESOLVED
      const auto it = edge_loops.find(edgeKey(run.verts[0], run.verts[1]));
      if (it == edge_loops.end() || it->second.size() != 2) continue;
      const std::size_t f0 = it->second[0], f1 = it->second[1];
      const auto own = patch_of_loop.find(f0);
      const std::size_t other = (own != patch_of_loop.end() && own->second == pi) ? f1 : f0;
      const auto op = patch_of_loop.find(other);
      if (op != patch_of_loop.end()) {
        if (op->second == pi) continue;  // folded back on itself
        run.kind = Patch::Run::OTHER_PATCH;
        run.patch = op->second;
        continue;
      }
      const std::vector<int>& loop = loops[other];
      const std::size_t m = loop.size();
      run.count = run.verts.size() - 1;
      if (run.count > m) continue;
      for (std::size_t j = 0; j < m; j++) {
        bool fwd = true, rev = true;
        for (std::size_t c = 0; c < run.count; c++) {
          const int a = loop[(j + c) % m], b = loop[(j + c + 1) % m];
          fwd = fwd && a == run.verts[c] && b == run.verts[c + 1];
          rev = rev && a == run.verts[run.count - c] && b == run.verts[run.count - c - 1];
        }
        if (!fwd && !rev) continue;
        run.kind = run.count == m ? Patch::Run::WHOLE_LOOP : Patch::Run::LOOP_RUN;
        run.loop = other;
        run.start = j;
        run.reversed = !fwd;
        break;
      }
    }
  }

  std::size_t live = 0, facets = 0, runs = 0, stuck = 0, longest = 0, bounds = 0;
  for (const auto& p : patches) {
    if (!p.alive) {
      report.push_back(
        format("a declared sweep of %d facets was left faceted: %s", int(p.facets.size()), p.dropped));
      continue;
    }
    live++;
    facets += p.facets.size();
    runs += p.runs.size();
    for (const auto& run : p.runs) {
      if (run.kind == Patch::Run::UNRESOLVED) stuck++;
      longest = std::max(longest, run.verts.size() - 1);
      bounds = std::max(bounds, run.bound + 1);
    }
  }
  if (cut_into > 0) {
    // Said out loud, because it is a face count the model did not ask for and
    // the alternative to it is a seam.
    report.push_back(
      format("a sweep closing around its profile was cut into %d faces, so that "
             "no face crosses the surface's seam",
             int(cut_into)));
  }
  if (corners_only > 0) {
    // Worth a line of its own: it is the difference between the facets a
    // declaration claims and the facets that are actually on it, and the
    // biggest single contributor is a face closing the profile of a grid
    // declared open, every corner of which the generator emitted.
    report.push_back(
      format("%d facets have every corner on the sweep and their middle off it, "
             "by up to %.4f against an allowance of %.4f",
             int(corners_only), worst_miss, missed_against));
  }
  if (live > 0) {
    report.push_back(
      format("%d declared sweep%s %s %d facets over %d boundary cycle%s, split "
             "into %d runs of up to %d mesh edges, %d unresolved",
             int(live), live == 1 ? "" : "s", live == 1 ? "covers" : "cover", int(facets), int(bounds),
             bounds == 1 ? "" : "s", int(runs), int(longest), int(stuck)));
  }
  return patches;
}

Result recogniseSurfacesOfRevolution(const Mesh& mesh,
                                     const std::vector<std::shared_ptr<Surface>>& surfaces, double tol)
{
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  const std::vector<char>& loop_valid = *mesh.valid;
  const std::vector<char>& loop_is_hole = *mesh.is_hole;
  const std::vector<Vector3d>& loop_normals = *mesh.normals;
  const std::size_t face_cnt = loops.size();
  const double model_tol = tol > 0 ? tol : 1e-5;

  Result result;
  result.band_of_loop.assign(face_cnt, NO_BAND);
  result.consumed.assign(face_cnt, 0);
  if (surfaces.empty()) return result;

  std::vector<Band>& bands = result.bands;
  std::vector<char>& consumed = result.consumed;
  std::vector<std::size_t>& band_of_loop = result.band_of_loop;
  std::vector<std::pair<RimRef, RimRef>>& rims = result.rims;

  std::map<std::pair<int, int>, std::vector<std::size_t>> loop_edges_map;
  for (std::size_t i = 0; i < face_cnt; i++) {
    if (!loop_valid[i]) continue;
    const auto& loop = loops[i];
    for (std::size_t j = 0; j < loop.size(); j++) {
      const int a = loop[j], b = loop[(j + 1) % loop.size()];
      loop_edges_map[{std::min(a, b), std::max(a, b)}].push_back(i);
    }
  }

  auto edge_key = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };

  // Did the model declare a cylinder of this radius about this axis?
  auto declared_cylinder = [&](double radius, const Vector3d& axis, const Vector3d& base) {
    for (const auto& surface : surfaces) {
      const auto *cyl = dynamic_cast<const CylinderSurface *>(surface.get());
      if (cyl == nullptr) continue;
      if (fabs(cyl->r - radius) > 1e-7 * radius) continue;
      if (fabs(fabs(cyl->normdir.normalized().dot(axis)) - 1.0) > 1e-7) continue;
      if (distanceToAxis(cyl->refpt, base, axis) > 1e-7 * radius) continue;
      return true;
    }
    return false;
  };

  // Walk the strip of quads reached by crossing ruling edges.
  //
  // The previous version grew across edges parallel to the axis, which finds
  // a cylinder and never a frustum: a cone's rulings are tilted, each one
  // differently. Entering a quad through one ruling fixes which pair of its
  // edges are rulings, so the walk needs no axis and is unambiguous even on a
  // cylinder, where both pairs are parallel.
  // Does this facet lie on the surface the band started on? Passing nullptr
  // admits every quad, which is only used for the first, exploratory walk.
  using OnSurface = std::function<bool(std::size_t)>;

  auto walk_strip = [&](std::size_t seed, int entry_side, std::vector<std::size_t>& walls,
                        std::map<std::size_t, int>& entry, const OnSurface *on_surface) {
    walls.clear();
    entry.clear();
    std::vector<std::pair<std::size_t, int>> stack{{seed, entry_side}};
    while (!stack.empty()) {
      const auto cur = stack.back();
      stack.pop_back();
      if (entry.count(cur.first)) continue;
      entry.emplace(cur.first, cur.second);
      walls.push_back(cur.first);
      const auto& loop = loops[cur.first];
      for (const int side : {cur.second, (cur.second + 2) % 4}) {
        const int a = loop[side], b = loop[(side + 1) % 4];
        const auto it = loop_edges_map.find(edge_key(a, b));
        if (it == loop_edges_map.end()) continue;
        for (const std::size_t nb : it->second) {
          if (nb == cur.first || entry.count(nb) || consumed[nb]) continue;
          if (!loop_valid[nb] || loop_is_hole[nb] || loops[nb].size() != 4) continue;
          if (on_surface != nullptr && !(*on_surface)(nb)) continue;
          for (int j = 0; j < 4; j++) {
            if (edge_key(loops[nb][j], loops[nb][(j + 1) % 4]) == edge_key(a, b)) {
              stack.emplace_back(nb, j);
              break;
            }
          }
        }
      }
    }
  };

  // The chords of a set of facets: the edges which are not rulings. They all
  // lie in a plane perpendicular to the axis.
  auto chords_of = [&](const std::vector<std::size_t>& walls, const std::map<std::size_t, int>& entry) {
    std::vector<Vector3d> chords;
    for (const std::size_t f : walls) {
      const int r = entry.at(f);
      for (const int c : {(r + 1) % 4, (r + 3) % 4}) {
        const Vector3d dir = vertices[loops[f][(c + 1) % 4]] - vertices[loops[f][c]];
        if (dir.norm() > 1e-12) chords.push_back(dir.normalized());
      }
    }
    return chords;
  };

  // Two chords which are not parallel fix the axis exactly.
  auto axis_from = [](const std::vector<Vector3d>& chords) {
    for (std::size_t c = 1; c < chords.size(); c++) {
      const Vector3d n = chords[0].cross(chords[c]);
      if (n.norm() < 1e-9) continue;
      Vector3d axis = n.normalized();
      if (axis[2] < 0 || (axis[2] == 0 && axis[0] < 0)) axis = -axis;
      return axis;
    }
    return Vector3d(0, 0, 0);
  };

  auto perpendicular_to = [](const std::vector<Vector3d>& chords, const Vector3d& axis) {
    for (const Vector3d& c : chords) {
      if (fabs(c.dot(axis)) >= 1e-9) return false;
    }
    return true;
  };

  for (std::size_t seed = 0; seed < face_cnt; seed++) {
    if (!loop_valid[seed] || consumed[seed] || loop_is_hole[seed]) continue;
    if (loops[seed].size() != 4) continue;

    for (int side = 0; side < 4; side++) {
      std::vector<std::size_t> walls;
      std::map<std::size_t, int> entry;
      // First walk freely, only to pin down which surface the seed sits on.
      // Left unconstrained this runs off the wall wherever something flat is
      // attached to it - a rib welded to a tube has quads for side faces, so
      // the strip crosses through the rib and back into the next arc, and the
      // whole ring then fails the fit as one band that was never a band.
      walk_strip(seed, side, walls, entry, nullptr);
      if (walls.size() < 3) continue;

      // Take the axis from the seed and the facets joined to it across a
      // ruling, and from nothing else.
      //
      // Taking it from the whole free walk looks more robust and is not: where
      // that walk runs off the surface it brings foreign chords back with it,
      // the perpendicularity test below rejects them, and the candidate is
      // thrown away before the constrained walk ever gets the chance to clean
      // it up. That is how four lug chamfers of the bayonet container came to
      // be silently unrecognisable - the walk crossed the end of a five quad
      // strip into the lug's side face, turned through ninety degrees there
      // because entering a quad by a different edge redefines which pair of
      // its edges are rulings, and came back with four of fourteen chords
      // perpendicular to nothing. Two chords is all the axis needs, and the
      // seed's own neighbours are the two it can trust.
      std::vector<std::size_t> near{seed};
      for (const std::size_t f : walls) {
        if (f == seed) continue;
        bool joined = false;
        for (int j = 0; j < 4 && !joined; j++) {
          const auto key = edge_key(loops[f][j], loops[f][(j + 1) % 4]);
          for (const int s : {side, (side + 2) % 4}) {
            if (key == edge_key(loops[seed][s], loops[seed][(s + 1) % 4])) joined = true;
          }
        }
        if (joined) near.push_back(f);
      }
      const Vector3d axis = axis_from(chords_of(near, entry));
      if (axis.norm() < 0.5) continue;

      // Fit the surface from the seed and its first two neighbours - four
      // vertices on each rim, which is enough - and walk again, this time
      // admitting only facets which sit on it.
      {
        std::map<int, double> probe_along;
        for (std::size_t f = 0; f < 3 && f < walls.size(); f++) {
          for (const int v : loops[walls[f]]) probe_along[v] = axis.dot(vertices[v]);
        }
        double probe_lo = probe_along.begin()->second, probe_hi = probe_lo;
        for (const auto& kv : probe_along) {
          probe_lo = std::min(probe_lo, kv.second);
          probe_hi = std::max(probe_hi, kv.second);
        }
        std::vector<int> probe_bottom, probe_top;
        for (const auto& kv : probe_along) {
          if (fabs(kv.second - probe_lo) < model_tol) probe_bottom.push_back(kv.first);
          else if (fabs(kv.second - probe_hi) < model_tol) probe_top.push_back(kv.first);
        }
        Vector3d probe_base, probe_top_centre;
        if (!fitCircleCentre(vertices, probe_bottom, axis, probe_lo, probe_base)) continue;
        if (!fitCircleCentre(vertices, probe_top, axis, probe_hi, probe_top_centre)) continue;
        double probe_r0 = 0, probe_r1 = 0;
        for (const int v : probe_bottom) probe_r0 += distanceToAxis(vertices[v], probe_base, axis);
        for (const int v : probe_top) probe_r1 += distanceToAxis(vertices[v], probe_base, axis);
        probe_r0 /= double(probe_bottom.size());
        probe_r1 /= double(probe_top.size());
        const double probe_scale = std::max(probe_r0, probe_r1);
        if (probe_scale < model_tol) continue;

        const OnSurface on_surface = [&](std::size_t f) {
          for (const int v : loops[f]) {
            const double t = axis.dot(vertices[v]);
            const double want = fabs(t - probe_lo) < model_tol
                                  ? probe_r0
                                  : (fabs(t - probe_hi) < model_tol ? probe_r1 : -1.0);
            if (want < 0) return false;
            if (fabs(distanceToAxis(vertices[v], probe_base, axis) - want) > 1e-7 * probe_scale) {
              return false;
            }
          }
          return true;
        };
        walk_strip(seed, side, walls, entry, &on_surface);
        if (walls.size() < 3) continue;
      }

      // Now that the walk is confined to one surface, the whole band has to
      // agree with the axis the seed's neighbourhood gave. This is the test
      // that used to run against the free walk, moved to the only set of
      // facets it can be asked of meaningfully - a seed whose neighbourhood
      // happens to give a wrong axis is still rejected here, just later.
      if (!perpendicular_to(chords_of(walls, entry), axis)) continue;

      // every wall vertex has to sit on one of the two rims
      std::map<int, double> along;
      for (const std::size_t f : walls) {
        for (const int v : loops[f]) along[v] = axis.dot(vertices[v]);
      }
      double lo = along.begin()->second, hi = lo;
      for (const auto& kv : along) {
        lo = std::min(lo, kv.second);
        hi = std::max(hi, kv.second);
      }
      if (hi - lo < model_tol) continue;

      std::vector<int> bottom_set, top_set;
      bool split_ok = true;
      for (const auto& kv : along) {
        if (fabs(kv.second - lo) < model_tol) bottom_set.push_back(kv.first);
        else if (fabs(kv.second - hi) < model_tol) top_set.push_back(kv.first);
        else split_ok = false;
      }
      if (!split_ok) continue;

      // A band which closes on itself has one rim vertex per facet; one which
      // stops short of a full turn has one more, the far end of the last
      // facet. Anything else is not a band around a common axis.
      const bool full_turn = bottom_set.size() == walls.size() && top_set.size() == walls.size();
      const bool part_turn = bottom_set.size() == walls.size() + 1 && top_set.size() == walls.size() + 1;
      if (!full_turn && !part_turn) continue;

      // Fit each rim on its own: the centroid of a full rim lies on the axis
      // but the centroid of an arc sits inside its chord, and the two rims of
      // a frustum have different radii anyway.
      Vector3d base, top_centre;
      if (!fitCircleCentre(vertices, bottom_set, axis, lo, base)) continue;
      if (!fitCircleCentre(vertices, top_set, axis, hi, top_centre)) continue;
      if (distanceToAxis(top_centre, base, axis) > 1e-6) continue;  // coaxial

      double r_bottom = 0, r_top = 0;
      for (const int v : bottom_set) r_bottom += distanceToAxis(vertices[v], base, axis);
      for (const int v : top_set) r_top += distanceToAxis(vertices[v], base, axis);
      r_bottom /= double(bottom_set.size());
      r_top /= double(top_set.size());
      const double scale = std::max(r_bottom, r_top);
      if (scale < model_tol) continue;
      double dev = 0;
      for (const int v : bottom_set) {
        dev = std::max(dev, fabs(distanceToAxis(vertices[v], base, axis) - r_bottom));
      }
      for (const int v : top_set) {
        dev = std::max(dev, fabs(distanceToAxis(vertices[v], base, axis) - r_top));
      }
      if (dev > 1e-7 * scale) continue;

      // Intent. A cylinder needs its own record; a frustum has none, because
      // the shape that produces one - hull() of two coaxial cylinders, the
      // standard chamfer - declares the two cylinders rather than the cone
      // between them. Both of its rims matching a declared cylinder is the
      // same statement of intent, made by two primitives instead of one.
      const bool is_cone = fabs(r_bottom - r_top) > 1e-9 * scale;
      if (is_cone) {
        if (!declared_cylinder(r_bottom, axis, base)) continue;
        if (!declared_cylinder(r_top, axis, base)) continue;
      } else if (!declared_cylinder(r_bottom, axis, base)) {
        continue;
      }

      Band info;
      info.walls = walls;
      info.axis = axis;
      info.base = base;
      info.r_bottom = r_bottom;
      info.r_top = r_top;
      info.height = hi - lo;
      info.closed = full_turn;
      info.bottom_set = bottom_set;
      info.top_set = top_set;

      const Vector3d probe = vertices[loops[walls[0]][0]];
      const Vector3d radial = (probe - base) - axis * axis.dot(probe - base);
      info.outward = radial.normalized().dot(loop_normals[walls[0]]) > 0;

      for (const std::size_t f : walls) {
        consumed[f] = 1;
        band_of_loop[f] = bands.size();
      }
      bands.push_back(info);
      break;
    }
  }

  // ---- what each rim borders -------------------------------------------
  //
  // A band whose rim cannot be resolved is dropped, which can leave a
  // neighbour's shared rim unresolvable in turn, so this runs to a fixed
  // point. Dropping is monotone, so it terminates.
  rims.assign(bands.size(), {RimRef(), RimRef()});

  auto rim_edges = [&](std::size_t bi, bool bottom) {
    const Band& band = bands[bi];
    const std::vector<int>& level_v = bottom ? band.bottom_set : band.top_set;
    const std::set<int> level(level_v.begin(), level_v.end());
    std::set<std::pair<int, int>> out;
    for (const std::size_t f : band.walls) {
      const auto& loop = loops[f];
      for (std::size_t j = 0; j < loop.size(); j++) {
        const int a = loop[j], b = loop[(j + 1) % loop.size()];
        if (level.count(a) && level.count(b)) out.insert(edge_key(a, b));
      }
    }
    return out;
  };

  // The two ends of a rim that stops short of a full turn, counter clockwise
  // about the axis.
  //
  // They come out of the rim's own edges - the two vertices used by one of them
  // rather than two - and not out of whatever lies on the far side. That is
  // what lets a rim shared between two bands be handled at all, since there is
  // no neighbouring loop there to index into, and it keeps one code path for
  // both cases rather than two that can drift apart.
  //
  // Ordering them by angle would put the branch cut of atan2 in the way; the
  // sign of one cross product of two *adjacent* rim vertices does not, because
  // adjacent rim vertices are a whole facet apart.
  auto rim_ends = [&](std::size_t bi, bool bottom, int& ccw_start, int& ccw_end) {
    const Band& band = bands[bi];
    const auto edges = rim_edges(bi, bottom);
    std::map<int, std::vector<int>> adjacent;
    for (const auto& edge : edges) {
      adjacent[edge.first].push_back(edge.second);
      adjacent[edge.second].push_back(edge.first);
    }
    std::vector<int> ends;
    for (const auto& kv : adjacent) {
      if (kv.second.size() == 1) ends.push_back(kv.first);
    }
    if (ends.size() != 2) return false;

    const Vector3d centre = bottom ? band.base : band.base + band.axis * band.height;
    const Vector3d va = vertices[ends[0]] - centre;
    const Vector3d vb = vertices[adjacent[ends[0]].front()] - centre;
    const bool first_is_start = band.axis.dot(va.cross(vb)) > 0;
    ccw_start = ends[first_is_start ? 0 : 1];
    ccw_end = ends[first_is_start ? 1 : 0];
    return true;
  };

  // The direction the wall facets traverse a rim edge is the direction the
  // collapsed face has to traverse the whole rim: the face replaces those
  // facets, so its boundary is theirs.
  auto wall_runs_ccw = [&](std::size_t bi, const std::pair<int, int>& edge) {
    const Band& band = bands[bi];
    for (const std::size_t f : band.walls) {
      const auto& loop = loops[f];
      for (std::size_t j = 0; j < loop.size(); j++) {
        const int a = loop[j], b = loop[(j + 1) % loop.size()];
        if (edge_key(a, b) != edge) continue;
        const Vector3d va = vertices[a] - band.base;
        const Vector3d vb = vertices[b] - band.base;
        return band.axis.dot(va.cross(vb)) > 0;
      }
    }
    return true;
  };

  auto resolve_rim = [&](std::size_t bi, bool bottom, RimRef& out, const char **why) {
    const Band& band = bands[bi];
    const auto edges = rim_edges(bi, bottom);
    if (edges.empty()) {
      *why = "no rim edges";
      return false;
    }
    const std::set<std::size_t> in_band(band.walls.begin(), band.walls.end());

    std::set<std::size_t> others;
    for (const auto& edge : edges) {
      const auto it = loop_edges_map.find(edge);
      if (it == loop_edges_map.end()) {
        *why = "a rim edge belongs to no loop";
        return false;
      }
      std::size_t outside = face_cnt;
      int count = 0;
      for (const std::size_t user : it->second) {
        if (in_band.count(user)) continue;
        count++;
        outside = user;
      }
      if (count != 1) {
        *why = "a rim edge is used by more than two faces";
        return false;
      }
      others.insert(outside);
    }

    out.wall_ccw = wall_runs_ccw(bi, *edges.begin());

    if (others.size() == 1) {
      const std::size_t nb = *others.begin();
      if (band_of_loop[nb] != NO_BAND) {
        *why = "the rim borders a single facet of another band";
        return false;
      }  // a one facet band
      if (!loop_valid[nb] || consumed[nb]) {
        *why = "the neighbouring face was dropped";
        return false;
      }
      const std::vector<int>& nb_loop = loops[nb];
      const std::set<int> key(nb_loop.begin(), nb_loop.end());
      if (key.size() == nb_loop.size() && edges.size() == nb_loop.size()) {
        out.kind = RimRef::WHOLE_LOOP;
        out.loop = nb;
        return true;
      }
      // a run inside the loop, which an arc can replace only when its edges
      // are consecutive there
      const std::size_t n = nb_loop.size();
      std::vector<char> on_rim(n, 0);
      std::size_t cnt = 0;
      for (std::size_t j = 0; j < n; j++) {
        if (edges.count(edge_key(nb_loop[j], nb_loop[(j + 1) % n])) == 0) continue;
        on_rim[j] = 1;
        cnt++;
      }
      if (cnt != edges.size() || cnt >= n) {
        *why = "the rim is not a run of its neighbour's edges";
        return false;
      }
      std::size_t start = n;
      for (std::size_t j = 0; j < n; j++) {
        if (on_rim[j] == 0 || on_rim[(j + n - 1) % n] != 0) continue;
        if (start != n) {
          *why = "the rim is split across its neighbour's loop";
          return false;
        }
        start = j;
      }
      if (start == n) {
        *why = "the rim covers its neighbour's whole loop twice";
        return false;
      }
      out.kind = RimRef::LOOP_RUN;
      out.loop = nb;
      out.start = start;
      out.count = cnt;
      return true;
    }

    // shared with another band, which has to be collapsed too
    std::set<std::size_t> nb_bands;
    for (const std::size_t f : others) nb_bands.insert(band_of_loop[f]);
    if (nb_bands.size() != 1 || *nb_bands.begin() == NO_BAND) {
      *why = "the rim borders one face per facet";
      return false;
    }
    const std::size_t other = *nb_bands.begin();
    if (!bands[other].alive) {
      *why = "the band sharing this rim was dropped";
      return false;
    }
    if (band.closed != bands[other].closed) {
      *why = "a shared rim needs both bands to be the same shape";
      return false;
    }

    if (!band.closed) {
      // Two partial bands, so the shared rim is an arc rather than a circle -
      // a bayonet lug is a wall on a chamfer on a wall and none of the three
      // goes all the way round, so every joint in one is this case.
      //
      // It is the same substitution the closed case makes, and safe under the
      // same condition strengthened: the two bands have to meet along the
      // *whole* of the rim. If either had rim edges the other lacked, the arc
      // would have to be split on one side and not the other, and the two
      // faces could no longer share one edge.
      for (const bool other_bottom : {true, false}) {
        if (rim_edges(other, other_bottom) != edges) continue;
        out.kind = RimRef::OTHER_BAND_ARC;
        out.band = other;
        return true;
      }
      *why = "the two partial bands share only part of the rim";
      return false;
    }

    if (others.size() != bands[other].walls.size()) {
      *why = "the shared rim does not cover the whole neighbouring band";
      return false;
    }
    out.kind = RimRef::OTHER_BAND;
    out.band = other;
    return true;
  };

  // The end edges of a partial band have to be edges the mesh already has.
  //
  // The face closes by running along the bottom rim, up one end, back along
  // the top rim and down the other, so the two ends it needs are "where the
  // bottom traversal finishes to where the top traversal starts" and the
  // reverse. If those are not edges of the mesh the face would be closed with
  // a diagonal that is not an edge at all, which opens the shell against every
  // face that shares the real one.
  auto ends_line_up = [&](const RimRef& bottom, const RimRef& top) {
    return loop_edges_map.count(edge_key(bottom.traversalEnd(), top.traversalStart())) != 0 &&
           loop_edges_map.count(edge_key(top.traversalEnd(), bottom.traversalStart())) != 0;
  };

  // Two bands must not rewrite the same planar loop, or the same run of it.
  for (bool changed = true; changed;) {
    changed = false;
    std::set<std::size_t> whole_taken;
    std::map<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>> runs_taken;

    for (std::size_t i = 0; i < bands.size(); i++) {
      if (!bands[i].alive) continue;
      RimRef bottom, top;
      const char *why = "unresolved";
      if (!resolve_rim(i, true, bottom, &why) || !resolve_rim(i, false, top, &why)) {
        bands[i].alive = false;
        bands[i].dropped = why;
        changed = true;
        continue;
      }

      // A full turn collapses each rim into a closed circle, which can only
      // replace a whole loop or the matching rim of another closed band; a
      // partial band collapses each rim into an arc, which either replaces a
      // run of a loop or is shared with another partial band. Anything else
      // would put a closed circle in the middle of a loop, or an arc where a
      // whole bound was wanted.
      auto is_arc = [](const RimRef& rim) {
        return rim.kind == RimRef::LOOP_RUN || rim.kind == RimRef::OTHER_BAND_ARC;
      };
      const bool shapes_ok =
        bands[i].closed ? (!is_arc(bottom) && !is_arc(top)) : (is_arc(bottom) && is_arc(top));
      if (!shapes_ok) {
        bands[i].alive = false;
        bands[i].dropped = "a rim is an arc, but the band covers the full turn";
        changed = true;
        continue;
      }

      // Both ends of a partial band's rims, taken from the rims themselves.
      if (!bands[i].closed && !(rim_ends(i, true, bottom.ccw_start, bottom.ccw_end) &&
                                rim_ends(i, false, top.ccw_start, top.ccw_end))) {
        bands[i].alive = false;
        bands[i].dropped = "a rim of the band has no two ends";
        changed = true;
        continue;
      }

      // The two ends of a partial band are ordinary edges of the mesh. If the
      // runs do not line up - the vertex ending one rim's run sitting on the
      // same ruling as the vertex starting the other's - the face would be
      // closed with a diagonal that is not an edge at all, which opens the
      // shell against every face that shares the real one.
      if (!bands[i].closed && !ends_line_up(bottom, top)) {
        bands[i].alive = false;
        bands[i].dropped = "the two rims of the band do not end on the same rulings";
        changed = true;
        continue;
      }

      bool clash = false;
      for (const RimRef *rim : {&bottom, &top}) {
        if (rim->kind == RimRef::WHOLE_LOOP) {
          if (!whole_taken.insert(rim->loop).second) clash = true;
        } else if (rim->kind == RimRef::LOOP_RUN) {
          const std::size_t n = loops[rim->loop].size();
          for (const auto& taken : runs_taken[rim->loop]) {
            for (std::size_t a = 0; a < rim->count && !clash; a++) {
              for (std::size_t b = 0; b < taken.second; b++) {
                if ((rim->start + a) % n == (taken.first + b) % n) clash = true;
              }
            }
          }
          runs_taken[rim->loop].push_back({rim->start, rim->count});
        }
      }
      if (clash) {
        bands[i].alive = false;
        bands[i].dropped = "another band already rewrites the same loop";
        changed = true;
        continue;
      }
      rims[i] = {bottom, top};
    }
  }

  // A periodic face needs a seam, and the seam has to be a ruling: both of
  // its ends on the same radial direction, or the line would cut through the
  // surface instead of lying on it.
  //
  // Picking each end independently by angle does not work, however obvious it
  // looks. atan2 has its branch cut at pi, a polygon with an even number of
  // facets has a vertex sitting exactly there, and which side of the cut it
  // lands on is decided by the sign of a y coordinate which is zero to
  // fifteen digits. Two rims of one wall disagreed on that sign, their seams
  // came out on different rulings, and a cylinder that was otherwise perfect
  // was dropped.
  //
  // So only one end is chosen, and the other is *derived* from it. Where two
  // bands share a rim they have to use the same vertex - the circle between
  // them is one edge - so a band takes whichever of its rims is already
  // settled and derives the other; a single pass suffices, because a band
  // which finds neither settled settles both.
  std::map<std::set<int>, int> rim_seam;

  auto vertex_on_ruling = [&](int from, const std::vector<int>& level, const Vector3d& axis,
                              const Vector3d& centre) {
    const Vector3d a = vertices[from] - centre;
    const Vector3d ra = (a - axis * axis.dot(a)).normalized();
    for (const int v : level) {
      const Vector3d b = vertices[v] - centre;
      const Vector3d rb = (b - axis * axis.dot(b)).normalized();
      if ((ra - rb).norm() < 1e-6) return v;
    }
    return -1;
  };

  for (std::size_t i = 0; i < bands.size(); i++) {
    Band& band = bands[i];
    if (!band.alive || !band.closed) continue;
    const std::set<int> bottom_key(band.bottom_set.begin(), band.bottom_set.end());
    const std::set<int> top_key(band.top_set.begin(), band.top_set.end());
    const Vector3d top_centre = band.base + band.axis * band.height;

    const auto settled_bottom = rim_seam.find(bottom_key);
    const auto settled_top = rim_seam.find(top_key);
    if (settled_bottom != rim_seam.end()) {
      band.seam_bottom = settled_bottom->second;
      band.seam_top = vertex_on_ruling(band.seam_bottom, band.top_set, band.axis, band.base);
    } else if (settled_top != rim_seam.end()) {
      band.seam_top = settled_top->second;
      band.seam_bottom = vertex_on_ruling(band.seam_top, band.bottom_set, band.axis, top_centre);
    } else {
      band.seam_bottom = band.bottom_set.front();
      band.seam_top = vertex_on_ruling(band.seam_bottom, band.top_set, band.axis, band.base);
    }

    if (band.seam_bottom == -1 || band.seam_top == -1) {
      band.alive = false;
      band.dropped = "the two rims have no ruling in common to run a seam along";
      continue;
    }
    rim_seam[bottom_key] = band.seam_bottom;
    rim_seam[top_key] = band.seam_top;
  }

  // dropping a band puts its facets back
  for (std::size_t i = 0; i < bands.size(); i++) {
    if (bands[i].alive) continue;
    for (const std::size_t f : bands[i].walls) {
      consumed[f] = 0;
      band_of_loop[f] = NO_BAND;
    }
  }

  // ---- merge a run of bands lying on one declared sphere or torus -------
  //
  // A sphere is not a grid to be grown. Every ring of its tessellation is
  // already a frustum whose rims are circles, so the zone is the maximal run of
  // bands joined at shared rims whose vertices all lie on one declared sphere -
  // which means the band pass has done the work and this only has to join up
  // its answer. The merged band keeps the run's outer rims, so the rules that
  // were resolved for the end bands still hold, and the flat cap at either end
  // is untouched.
  //
  // The alternative, flooding across every edge into any face whose vertices
  // are on the sphere, does not work: an OpenSCAD sphere is a closed polyhedron
  // inscribed in the sphere and its caps have every vertex on the surface too,
  // with the same sag as any other facet. There is no local geometric test that
  // separates a cap from a ring quad, because geometrically there is nothing to
  // separate - only the structure says which is which.
  {
    // which live bands meet at each rim, keyed by the rim's vertex set
    std::map<std::set<int>, std::vector<std::size_t>> at_rim;
    for (std::size_t i = 0; i < bands.size(); i++) {
      if (!bands[i].alive || !bands[i].closed) continue;
      for (const bool bottom : {true, false}) {
        const std::vector<int>& level = bottom ? bands[i].bottom_set : bands[i].top_set;
        at_rim[std::set<int>(level.begin(), level.end())].push_back(i);
      }
    }

    // Every vertex of the band on the surface, to a tolerance relative to the
    // surface's own size. A sphere and a torus are the two zone shapes: for the
    // torus the distance measured is to the tube's centre circle, which is what
    // a torus is.
    auto on_zone = [&](const Surface *zone, std::size_t bi) {
      const auto *sph = dynamic_cast<const SphereSurface *>(zone);
      const auto *tor = dynamic_cast<const TorusSurface *>(zone);
      if (sph == nullptr && tor == nullptr) return false;
      for (const std::size_t f : bands[bi].walls) {
        for (const int v : loops[f]) {
          if (sph != nullptr) {
            if (fabs((vertices[v] - sph->refpt).norm() - sph->r) > 1e-7 * sph->r) return false;
          } else {
            const Vector3d rel = vertices[v] - tor->refpt;
            const double along = rel.dot(tor->normdir);
            const double radial = (rel - tor->normdir * along).norm() - tor->r_major;
            if (fabs(sqrt(radial * radial + along * along) - tor->r_minor) > 1e-7 * tor->r_minor) {
              return false;
            }
          }
        }
      }
      return true;
    };

    std::vector<char> absorbed(bands.size(), 0);
    for (const auto& surface : surfaces) {
      const bool is_torus = dynamic_cast<const TorusSurface *>(surface.get()) != nullptr;
      if (dynamic_cast<const SphereSurface *>(surface.get()) == nullptr && !is_torus) continue;

      for (std::size_t seed = 0; seed < bands.size(); seed++) {
        if (!bands[seed].alive || absorbed[seed] || bands[seed].zone != nullptr) continue;
        if (!bands[seed].closed || !on_zone(surface.get(), seed)) continue;

        // walk the run outwards from the seed, one rim at a time
        std::vector<std::size_t> run{seed};
        for (const bool up : {false, true}) {
          std::size_t cur = seed;
          for (;;) {
            const std::vector<int>& level = up ? bands[cur].top_set : bands[cur].bottom_set;
            const auto it = at_rim.find(std::set<int>(level.begin(), level.end()));
            if (it == at_rim.end() || it->second.size() != 2) break;
            const std::size_t next = it->second[0] == cur ? it->second[1] : it->second[0];
            if (absorbed[next] || next == seed) break;
            if (std::find(run.begin(), run.end(), next) != run.end()) break;  // closed on itself
            if (!on_zone(surface.get(), next)) break;
            run.push_back(next);
            absorbed[next] = 1;
            cur = next;
          }
        }
        if (run.size() < 2) continue;

        // the ends of the run are the bands with a rim no other band in it uses
        std::map<std::set<int>, int> uses;
        for (const std::size_t bi : run) {
          for (const bool bottom : {true, false}) {
            const std::vector<int>& level = bottom ? bands[bi].bottom_set : bands[bi].top_set;
            uses[std::set<int>(level.begin(), level.end())]++;
          }
        }
        std::size_t low = bands.size(), high = bands.size();
        for (const std::size_t bi : run) {
          const std::set<int> b(bands[bi].bottom_set.begin(), bands[bi].bottom_set.end());
          const std::set<int> t(bands[bi].top_set.begin(), bands[bi].top_set.end());
          if (uses[b] == 1) low = bi;
          if (uses[t] == 1) high = bi;
        }
        if (low == bands.size() || high == bands.size()) {
          // No free rim at either end: the run closes on itself, which is a
          // torus, and a *complete* one - the pass below writes it as a face
          // bounded by two seams and nothing else.
          for (const std::size_t bi : run) absorbed[bi] = 0;
          continue;
        }

        // For a torus, an end of the run has to be an end of the *surface* and
        // not merely of the walk. A torus's profile turns around at its widest
        // and narrowest points, and the two bands meeting there meet top to top,
        // so a walk that always follows the top rim turns back at the turnaround
        // and stops - leaving a run whose ends look free because the band beyond
        // each of them is not in the run. Merging that would break one complete
        // torus into fragments.
        //
        // What separates the two cases is not whether the end rim is shared -
        // the end rim of a rounded corner is shared with the wall it runs into,
        // and that is the ordinary case - but *what it is shared with*. A band
        // across the rim which lies on this same torus is a turnaround; anything
        // else is a real end.
        if (is_torus) {
          bool turnaround = false;
          for (const auto& end : {std::make_pair(low, true), std::make_pair(high, false)}) {
            const std::vector<int>& level =
              end.second ? bands[end.first].bottom_set : bands[end.first].top_set;
            const auto it = at_rim.find(std::set<int>(level.begin(), level.end()));
            if (it == at_rim.end()) continue;
            for (const std::size_t other : it->second) {
              if (other != end.first && on_zone(surface.get(), other)) turnaround = true;
            }
          }
          if (turnaround) {
            for (const std::size_t bi : run) absorbed[bi] = 0;
            continue;
          }
        }

        std::vector<std::size_t> walls;
        for (const std::size_t bi : run) {
          walls.insert(walls.end(), bands[bi].walls.begin(), bands[bi].walls.end());
        }

        // Read both ends *before* writing anything. The merged band is one of
        // the run - it has to be, or its facets would be put back - so the
        // reference below may alias bands[low] or bands[high], and assigning
        // through it would change what the other end still has to be read from.
        // Taken the other way round the top rim's centre came out at the second
        // ring rather than the last, its reference direction tilted 87 degrees
        // out of the rim's plane, and the flat cap bounded by it stopped being
        // flat.
        const Vector3d base = bands[low].base;
        const Vector3d top_centre = bands[high].base + bands[seed].axis * bands[high].height;
        const std::vector<int> bottom_set = bands[low].bottom_set;
        const std::vector<int> top_set = bands[high].top_set;
        const double r_bottom = bands[low].r_bottom;
        const double r_top = bands[high].r_top;
        const int seam_bottom = bands[low].seam_bottom;
        const int seam_top = bands[high].seam_top;
        const bool outward = bands[low].outward;
        // keep the run's outer rims, and with them the rules already resolved
        const std::pair<RimRef, RimRef> ends{rims[low].first, rims[high].second};

        Band& merged = bands[seed];
        merged.walls = walls;
        merged.bottom_set = bottom_set;
        merged.top_set = top_set;
        merged.base = base;
        // stated as the two centres rather than as a sum of heights, so that it
        // stays right however the run was ordered
        merged.height = merged.axis.dot(top_centre - base);
        merged.r_bottom = r_bottom;
        merged.r_top = r_top;
        merged.seam_bottom = seam_bottom;
        merged.seam_top = seam_top;
        merged.outward = outward;
        merged.zone = surface;
        rims[seed] = ends;
        for (const std::size_t bi : run) {
          if (bi == seed) continue;
          bands[bi].alive = false;
          bands[bi].dropped = nullptr;  // absorbed, not rejected: keep its facets
          absorbed[bi] = 1;
        }
        for (const std::size_t f : walls) band_of_loop[f] = seed;
        absorbed[seed] = 0;
      }
    }
  }

  // ---- merge a run of bands which closes on itself, on a declared torus ----
  //
  // The same observation as for a sphere: a torus is a stack of bands, one per
  // profile edge, and the zone is the run of them joined at shared rims. The
  // difference is only that the run has no ends - every rim is shared - so
  // there is nothing to keep, and the face is bounded by its own two seams
  // instead. That is why it needs a declaration of its own rather than falling
  // out of the ring circles: those already collapse a torus into a stack of
  // exact cones, and only a TorusSurface says the stack was one surface.
  {
    std::vector<char> absorbed(bands.size(), 0);
    for (const auto& surface : surfaces) {
      const auto *tor = dynamic_cast<const TorusSurface *>(surface.get());
      if (tor == nullptr) continue;

      auto on_torus = [&](std::size_t bi) {
        for (const std::size_t f : bands[bi].walls) {
          for (const int v : loops[f]) {
            const Vector3d rel = vertices[v] - tor->refpt;
            const double along = rel.dot(tor->normdir);
            const double radial = (rel - tor->normdir * along).norm();
            const double d = radial - tor->r_major;
            if (fabs(sqrt(d * d + along * along) - tor->r_minor) > 1e-7 * tor->r_minor) return false;
          }
        }
        return true;
      };

      std::map<std::set<int>, std::vector<std::size_t>> at_rim;
      for (std::size_t i = 0; i < bands.size(); i++) {
        if (!bands[i].alive || !bands[i].closed || bands[i].zone != nullptr) continue;
        if (!on_torus(i)) continue;
        for (const bool bottom : {true, false}) {
          const std::vector<int>& level = bottom ? bands[i].bottom_set : bands[i].top_set;
          at_rim[std::set<int>(level.begin(), level.end())].push_back(i);
        }
      }

      for (std::size_t seed = 0; seed < bands.size(); seed++) {
        if (!bands[seed].alive || absorbed[seed] || bands[seed].zone != nullptr) continue;
        if (!bands[seed].closed || !on_torus(seed)) continue;

        // Walk the ring until it comes back to the seed, leaving each band by
        // the rim it was not entered by.
        //
        // Not by its top rim, which is what the sphere pass does. A sphere's
        // bands stack monotonically along the axis, so every rim is one band's
        // top and the next one's bottom. A torus's profile turns around at its
        // widest and narrowest points, and the two bands meeting there meet top
        // to top: a walk which always follows the top rim comes straight back
        // to where it started and the run stops two bands long.
        std::vector<std::size_t> run{seed};
        std::size_t cur = seed;
        std::set<int> came_by(bands[seed].bottom_set.begin(), bands[seed].bottom_set.end());
        bool cyclic = false;
        for (;;) {
          const std::set<int> bottom(bands[cur].bottom_set.begin(), bands[cur].bottom_set.end());
          const std::set<int> top(bands[cur].top_set.begin(), bands[cur].top_set.end());
          const std::set<int>& level = bottom == came_by ? top : bottom;
          const auto it = at_rim.find(level);
          if (it == at_rim.end() || it->second.size() != 2) break;
          const std::size_t next = it->second[0] == cur ? it->second[1] : it->second[0];
          if (next == seed) {
            cyclic = true;
            break;
          }
          if (absorbed[next] || std::find(run.begin(), run.end(), next) != run.end()) break;
          run.push_back(next);
          came_by = level;
          cur = next;
        }
        if (!cyclic || run.size() < 3) continue;

        std::vector<std::size_t> walls;
        for (const std::size_t bi : run) {
          walls.insert(walls.end(), bands[bi].walls.begin(), bands[bi].walls.end());
        }

        // A torus face is bounded by nothing but its own two seams, so the only
        // thing the emitter needs from the mesh is one vertex where they cross.
        // Everything else - both circles, their centres, their radii - comes
        // out of the record.
        const int corner = bands[seed].bottom_set.front();

        // Which way the face looks: away from the tube's centre circle, which
        // for the inner half of a torus is the opposite of away from the axis.
        const Vector3d probe = vertices[loops[bands[seed].walls[0]][0]];
        const Vector3d rel = probe - tor->refpt;
        const double along = rel.dot(tor->normdir);
        const Vector3d radial = rel - tor->normdir * along;
        const Vector3d tube = tor->refpt + radial.normalized() * tor->r_major;
        const bool outward = (probe - tube).normalized().dot(loop_normals[bands[seed].walls[0]]) > 0;

        Band& merged = bands[seed];
        merged.walls = walls;
        merged.seam_bottom = corner;
        merged.seam_top = corner;
        merged.outward = outward;
        merged.zone = surface;
        for (const std::size_t bi : run) {
          if (bi == seed) continue;
          bands[bi].alive = false;
          bands[bi].dropped = nullptr;  // absorbed, not rejected: keep its facets
          absorbed[bi] = 1;
        }
        for (const std::size_t f : walls) band_of_loop[f] = seed;
      }
    }
  }

  std::size_t collapsed = 0, alive = 0, cones = 0, partial = 0, spheres = 0, tori = 0;
  for (const auto& band : bands) {
    if (!band.alive) continue;
    alive++;
    collapsed += band.walls.size();
    if (dynamic_cast<const TorusSurface *>(band.zone.get()) != nullptr) tori++;
    else if (band.zone != nullptr) spheres++;
    else if (band.isCone()) cones++;
    if (!band.closed) partial++;
  }
  if (alive > 0) {
    result.report.push_back(
      format("%d surface%s recognised (%d toroidal, %d spherical, %d conical, %d partial), "
             "%d facets replaced",
             int(alive), alive == 1 ? "" : "s", int(tori), int(spheres), int(cones), int(partial),
             int(collapsed)));
  }
  // Every band here fits its axis exactly and was declared by the model, so a
  // drop is always the topology around it rather than the surface itself.
  // Naming the rule that rejected it is the only way to tell a wall which
  // cannot be written from one which should have been.
  for (const auto& band : bands) {
    if (band.alive || band.dropped == nullptr) continue;
    result.report.push_back(format("r=%g band of %d facets left faceted: %s", band.r_bottom,
                                   int(band.walls.size()), band.dropped));
  }

  return result;
}

}  // namespace AnalyticFeatures
