/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "FilletNode.h"
#include "module.h"
#include "ModuleInstantiation.h"
#include "Children.h"
#include "Parameters.h"
#include "src/utils/printutils.h"
#include "io/fileutils.h"
#include "Builtins.h"
#include "handle_dep.h"
#include "src/geometry/PolySetBuilder.h"
#include "src/geometry/Surface.h"

#include <algorithm>  // std::clamp
#include <cmath>
#include <map>
#include <sstream>
#include <tuple>

#include <src/geometry/PolySetUtils.h>
#include <src/core/Tree.h>
#include <src/geometry/GeometryEvaluator.h>
#include <boost/functional/hash.hpp>
#include <src/utils/hash.h>
#include <src/geometry/PolySetUtils.h>

struct SearchReplace {
  int pol;
  int search;
  IndexedFace replace;
};

typedef std::vector<int> intList;

bool list_included(const std::vector<int>& list, int needle)
{
  for (size_t i = 0; i < list.size(); i++) {
    if (list[i] == needle) return true;
  }
  return false;
}
std::shared_ptr<const PolySet> childToPolySet(std::shared_ptr<AbstractNode> child)
{
  Tree tree(child, "");
  GeometryEvaluator geomevaluator(tree);
  std::shared_ptr<const Geometry> geom = geomevaluator.evaluateGeometry(*tree.root(), true);
  std::shared_ptr<const PolySet> ps;
  return PolySetUtils::getGeometryAsPolySet(geom);
}

namespace {

/*! One vertex of a fillet gets computed twice, and the two answers differ.
 *
 * The rail where an edge strip meets a corner patch is generated once by the
 * strip, as `p + e_fa - 2f*e_fa + f^2*(e_fa + e_fb)`, and once by the corner, as
 * `center + mat * Bezier(...)`. Same point, different arithmetic, so they land
 * one unit in the last place apart - and PolySetBuilder's vertex lookup is
 * exact, so the mesh ended up with two vertices where it needed one and a crack
 * between them. A filleted cube exported with 48 quadrilateral holes, one
 * wherever a strip end meets a corner, and SolidWorks imported it as loose
 * surfaces rather than a solid.
 *
 * Snapping to a grid fixes it, but the grid has to be fine. GRID_FINE is about
 * 1e-6, coarser than the 1e-7 the exporter uses to decide whether a vertex lies
 * on a declared surface, so aligning to that would push vertices off the very
 * patches this file declares. 1e-9 is far above one ulp at any plausible model
 * size and far below anything that is measured. */
class VertexSnapper
{
public:
  int index(PolySetBuilder& builder, const Vector3d& pt)
  {
    const Key key = keyOf(pt);
    for (int64_t dx = -1; dx <= 1; dx++) {
      for (int64_t dy = -1; dy <= 1; dy++) {
        for (int64_t dz = -1; dz <= 1; dz++) {
          const auto it =
            known.find(Key{std::get<0>(key) + dx, std::get<1>(key) + dy, std::get<2>(key) + dz});
          if (it != known.end()) return it->second;
        }
      }
    }
    return add(builder, pt);
  }

  /*! Allocate unconditionally, and record the position so later points snap
   * onto it. The original mesh's vertices go through here rather than through
   * index(): the fillet relies on vertex i of the input keeping index i, and
   * merging two of them - however close together - would renumber the rest. */
  int add(PolySetBuilder& builder, const Vector3d& pt)
  {
    const int id = builder.vertexIndex(pt);
    known.emplace(keyOf(pt), id);
    return id;
  }

private:
  using Key = std::tuple<int64_t, int64_t, int64_t>;
  static Key keyOf(const Vector3d& pt)
  {
    const double res = 1e-9;
    return Key{(int64_t)llround(pt[0] / res), (int64_t)llround(pt[1] / res),
               (int64_t)llround(pt[2] / res)};
  }
  std::map<Key, int> known;
};

}  // namespace

// Credit: inphase Ryan Colyer
/*! The weight that makes a quadratic Bezier through these three points a
 * circular arc.
 *
 * A *polynomial* quadratic through (a, b, c) is a parabola, which is why
 * fillet(1) did not draw a 1 mm fillet: measured against the axis a true fillet
 * turns about, it was out by 6% of the radius where the two faces meet at a
 * right angle, 25% at a 60 degree dihedral, and the corner patch was 9.5% off
 * the sphere.
 *
 * The same three control points with the middle weight at cos(theta/2), theta
 * being the turn between the two end tangents, is exactly a circular arc - and
 * the weight comes out of the control points themselves, so nothing upstream has
 * to compute an axis or solve for tangency. That is what keeps this construction
 * usable where a circular fillet has no clean definition: faces that are not
 * perpendicular, edges that are not straight, a radius that changes sharply. In
 * those cases the legs are no longer equal and the result is an exact conic
 * rather than a circle, which is the same graceful degradation the polynomial
 * form had, only now correct wherever a circle was meant.
 *
 * cos(theta/2) is sqrt((1 + cos theta) / 2), so this needs no trig call. */
double BezierWeight(const Vector3d& a, const Vector3d& b, const Vector3d& c)
{
  const Vector3d t0 = b - a, t1 = c - b;
  const double n0 = t0.norm(), n1 = t1.norm();
  if (n0 < 1e-12 || n1 < 1e-12) return 1.0;  // no tangent to turn between
  const double cos_theta = std::clamp(t0.dot(t1) / (n0 * n1), -1.0, 1.0);
  return sqrt((1.0 + cos_theta) / 2.0);
}

Vector3d Bezier(double t, Vector3d a, Vector3d b, Vector3d c)
{
  const double w = BezierWeight(a, b, c);
  const double b0 = (1 - t) * (1 - t), b1 = 2 * t * (1 - t) * w, b2 = t * t;
  return (a * b0 + b * b1 + c * b2) / (b0 + b1 + b2);
}

void bezier_patch(PolySetBuilder& builder, VertexSnapper& snap, Vector3d center, Vector3d dir[3],
                  int concave_1, int concave_2, int concave_3, int N)
{
  if ((dir[1].cross(dir[0])).dot(dir[2]) < 0) {
    Vector3d tmp = dir[0];
    dir[0] = dir[1];
    dir[1] = tmp;
  }
  Vector3d xdir = dir[0].normalized();
  Vector3d ydir = dir[1].normalized();
  Vector3d zdir = dir[2].normalized();

  // zdir shall look upwards

  Matrix3d mat;
  mat << xdir[0], ydir[0], zdir[0], xdir[1], ydir[1], zdir[1], xdir[2], ydir[2], zdir[2];

  xdir = Vector3d(1, 0, 0) * dir[0].norm();
  ydir = Vector3d(0, 1, 0) * dir[1].norm();
  zdir = Vector3d(0, 0, 1) * dir[2].norm();

  // now use matrices to transform the vectors into std orientation
  //
  //  N = floor(N/2)*2 + 1;
  Vector3d pt;
  std::vector<Vector3d> points_xz;
  std::vector<Vector3d> points_yz;
  for (int i = 0; i < N; i++) {
    double t = (double)i / (double)(N - 1);
    points_xz.push_back(
      Bezier(t, xdir, xdir + zdir, zdir + 2 * (concave_1 + concave_2) * (xdir + ydir)));
    points_yz.push_back(
      Bezier(t, ydir, ydir + zdir, zdir + 2 * (concave_1 + concave_2) * (xdir + ydir)));
  }

  // The same statement for the corner. Every row of this patch is a quadratic
  // Bezier between the two rails through a control point mixing their
  // coordinates, and each of those three is itself quadratic in the row
  // parameter, which makes the whole thing a tensor product of degree (2,2)
  // rather than the triangular patch it looks like. Its last row is the apex
  // three times - a singular point, and the usual way a rounded corner is
  // written.
  {
    const Vector3d apex = zdir + 2 * (concave_1 + concave_2) * (xdir + ydir);
    std::vector<Vector3d> net{xdir,        xdir + ydir, ydir, xdir + zdir, xdir + ydir + zdir,
                              ydir + zdir, apex,        apex, apex};
    // Rational in both directions, with the weight of each read off that
    // direction's own boundary - the column for u, the row for v - and the net
    // weight their product. The rows Bezier() draws between the two rails turn
    // through the same angle as the row of the net does, which is why one scalar
    // per direction describes the whole patch. On a cube corner both come out at
    // cos 45 degrees and the patch is then exactly an octant of a sphere: this
    // net, degenerate apex row and all, is the classical exact one.
    const double wu = BezierWeight(net[0], net[3], net[6]);
    const double wv = BezierWeight(net[0], net[1], net[2]);
    std::vector<double> wnet{1.0, wv, 1.0, wu, wu * wv, wu, 1.0, wv, 1.0};
    for (auto& p : net) p = center + mat * p;
    builder.addSurface(std::make_shared<BezierPatchSurface>(2, 2, std::move(net), std::move(wnet)));
  }

  std::vector<int> points;
  for (int i = 0; i < N; i++) {
    if (i == N - 1) {
      pt = zdir + 2 * (concave_1 + concave_2) * (xdir + ydir);
      pt = mat * pt;
      points.push_back(snap.index(builder, pt + center));
    } else {
      int M = N - i;
      for (int j = 0; j < M; j++) {
        int k;
        if (concave_1 == 1 || concave_3 == 1) k = j;
        else k = M - 1 - j;
        double t2 = (double)k / (double)(M - 1);
        pt = Bezier(t2, points_xz[i], Vector3d(points_xz[i][0], points_yz[i][1], points_xz[i][2]),
                    points_yz[i]);
        pt = mat * pt;
        points.push_back(snap.index(builder, center + pt));
      }
    }
  }
  // total points = N*(N-1)/2
  int off = 0;
  for (int i = 0; i < N - 1; i++) {  // Zeile i, i-1
    int off_new = off + (N - i);
    for (int j = 0; j < N - i - 1; j++) {
      builder.appendPolygon({points[off + j], points[off + j + 1], points[off_new + j]});
      if (j < N - i - 2) {
        builder.appendPolygon({points[off + j + 1], points[off_new + j + 1], points[off_new + j]});
      }
    }
    off = off_new;
  }
}

void debug_pt(const char *msg, Vector3d pt)
{
  printf("%s %g/%g/%g\n", msg, pt[0], pt[1], pt[2]);
}

namespace {

struct FilletEdgePair {
  int facea = -1;
  int faceb = -1;
};

bool validateFilletEdgePairs(const std::vector<IndexedFace>& indices, EdgeKey& failedEdge)
{
  std::unordered_map<EdgeKey, FilletEdgePair, boost::hash<EdgeKey>> edge_db;

  for (size_t faceIndex = 0; faceIndex < indices.size(); faceIndex++) {
    const auto& face = indices[faceIndex];
    int n = face.size();
    for (int pos = 0; pos < n; pos++) {
      int ind1 = face[pos];
      int ind2 = face[(pos + 1) % n];
      if (ind1 == ind2) continue;

      EdgeKey edge(ind1, ind2);
      auto edgeIt = edge_db.emplace(edge, FilletEdgePair{}).first;
      FilletEdgePair& value = edgeIt->second;
      if (ind2 > ind1) {
        if (value.facea != -1) {
          failedEdge = edge;
          return false;
        }
        value.facea = faceIndex;
      } else {
        if (value.faceb != -1) {
          failedEdge = edge;
          return false;
        }
        value.faceb = faceIndex;
      }
    }
  }

  for (const auto& edge : edge_db) {
    if (edge.second.facea == -1 || edge.second.faceb == -1) {
      failedEdge = edge.first;
      return false;
    }
  }
  return true;
}

}  // namespace

std::unique_ptr<const Geometry> createFilletInt(std::shared_ptr<const PolySet> ps,
                                                std::vector<bool> corner_selected, double r_, int bn,
                                                double minang)
{
  double cos_minang = cos(minang * 3.1415 / 180.0);
  std::vector<Vector4d> normals, newnormals;
  std::vector<int> faceParents;
  normals = calcTriangleNormals(ps->vertices, ps->indices);
  std::vector<IndexedFace> merged =
    mergeTriangles(ps->indices, normals, newnormals, faceParents, ps->vertices);

  EdgeKey failedEdge;
  if (!validateFilletEdgePairs(merged, failedEdge)) {
    LOG(message_group::Error,
        "fillet() cannot process the selected object: edge %1$d-%2$d is not shared by exactly two "
        "oppositely-oriented faces. This can happen when the fillet radius collides with nearby "
        "geometry; try reducing the fillet radius or applying fillet() before unioning adjacent solids.",
        failedEdge.ind1, failedEdge.ind2);
    return PolySet::createEmpty();
  }

  if (bn < 2) bn = 2;
  // Create vertex2face db
  auto vertices_copy = ps->vertices;

  bool improved = false;
  std::unordered_map<EdgeKey, EdgeVal, boost::hash<EdgeKey>> edge_db;
  std::vector<intList> polinds, polposs;

  std::vector<std::vector<int>> corner_rounds;
  do {
    improved = false;  // fix short edges until happy

    polinds.clear();
    polposs.clear();
    intList empty;
    for (size_t i = 0; i < vertices_copy.size(); i++) {
      polinds.push_back(empty);
      polposs.push_back(empty);
    }
    for (size_t i = 0; i < merged.size(); i++) {
      for (size_t j = 0; j < merged[i].size(); j++) {
        int ind = merged[i][j];
        polinds[ind].push_back(i);
        polposs[ind].push_back(j);
      }
    }

    // create Edge DB
    int error;
    edge_db = createEdgeDb(merged, error);
    if (error)
      LOG(message_group::Warning,
          "Resulting fillet is not manifold anymore, further processing might be inaccurate");

    // which rounded edges in a corner coner_rounds[vert]=[other_verts]
    corner_rounds.clear();
    for (size_t i = 0; i < vertices_copy.size(); i++) corner_rounds.push_back(empty);

    for (auto& e : edge_db) {
      if (corner_selected[e.first.ind1] && corner_selected[e.first.ind2]) {
        assert(e.second.facea >= 0);
        assert(e.second.faceb >= 0);
        auto& facea = merged[e.second.facea];
        auto& faceb = merged[e.second.faceb];
        Vector3d fan = calcTriangleNormal(vertices_copy, facea).head<3>();
        Vector3d fbn = calcTriangleNormal(vertices_copy, faceb).head<3>();
        double d = fan.dot(fbn);
        e.second.sel = 0;
        if (d >= cos_minang) continue;  // dont create facets when the angle conner is too small
        if (polinds[e.first.ind1].size() != 3) continue;  // start must be 3edge corner
        if (polinds[e.first.ind2].size() != 3) continue;  // start must be 3edge corner

        e.second.sel = 1;
      }
    }
    // Where an edge is shorter than 2r the arc cannot close along it, so the two
    // corners are merged into one and the faces around them extended to meet at
    // that point. An arc of radius r genuinely cannot be drawn along a shorter
    // edge - a parabola through the same control points quietly produced
    // something, which is one reason this pass was never needed before.
    //
    // The collapse is over-determined, and that is the whole difficulty. Both
    // endpoints are 3-edge corners (the selection above requires it), so *four*
    // planes surround the pair: the two sharing the edge, plus the third face at
    // either end. The merged vertex has to lie on all four, and four planes
    // through one point is one equation too many unless they are concurrent.
    //
    // Cutting three of them and ignoring the fourth - which is what this did
    // while it was disabled - puts the vertex off that face by an amount linear
    // in the edge length, with a factor the angles set and nothing bounds: 2.13
    // times the length on one ordinary configuration, so a 0.5 mm edge moved the
    // vertex 1 mm off a face it should have been on, and with two of the planes
    // nearly parallel a 0.1 mm edge landed 12 units away with no error reported,
    // because the determinant was not zero. An edge length is therefore the wrong
    // gate. All four planes go in, in least squares, and the residual - which is
    // exactly their non-concurrency - decides.
    //
    // Which bounds what this pass can do, and the bound is algebraic rather than
    // a matter of tuning the tolerance. Corner one is facea & faceb & third1 and
    // corner two is facea & faceb & third2, so if all four planes share a point
    // then *both* corners are that point and the edge has zero length. A short
    // edge of non-zero length therefore always has a non-zero residual - measured
    // at 0.2887 times the length on a cube with a cut corner, at every size from
    // 0.4 down to 1e-9 - and a collapse to one point is only available where the
    // edge is already degenerate. That is worth having: a boolean leaves slivers
    // and they are cleaned up here, verified to leave the mesh manifold. But it
    // is not what removes a real short edge.
    //
    // What does, for a real one, is collapsing the small *face* rather than one
    // of its edges: drop it and intersect its neighbours, which for a three sided
    // one is three planes and exactly determined. On the same cut corner that is
    // exact - residual 0, every face still planar, the sharp corner restored -
    // where every edge collapse of it is refused. That is the shape of the next
    // step here, and it is a different operation from this one.
    //
    // Until then the value of this pass for a real short edge is the refusal
    // below: the edge is left unrounded and says why, rather than being filleted
    // with two arcs that cannot both fit along it.
    for (auto& e : edge_db) {
      if (!e.second.sel) continue;
      const int ind1 = e.first.ind1, ind2 = e.first.ind2;
      if ((vertices_copy[ind1] - vertices_copy[ind2]).norm() >= 2 * r_) continue;

      const int facea_ind = e.second.facea, faceb_ind = e.second.faceb;
      const int posa = e.second.posa;
      if (facea_ind < 0 || faceb_ind < 0 || posa < 0) continue;
      const IndexedFace& facea = merged[facea_ind];
      const int na = int(facea.size());
      // facea traverses the edge ind1 -> ind2 and faceb the other way round, so
      // the two faces to extend are the ones across facea's neighbouring edges:
      // the one before ind1 and the one after ind2. posa says where the edge sits
      // without searching for it again.
      if (posa >= na || na < 4) continue;
      if (facea[posa] != ind1 || facea[(posa + 1) % na] != ind2) continue;
      const int a_prev = facea[(posa + na - 1) % na];
      const int a_next = facea[(posa + 2) % na];
      if (a_prev == ind2 || a_next == ind1 || a_prev == a_next) continue;

      // The third face at either end is the one on the other side of that
      // neighbouring edge. At a 3-edge corner it can only be the third face, but
      // check rather than assume: everything below dereferences it.
      auto other_face = [&](const EdgeKey& key) {
        const auto it = edge_db.find(key);
        if (it == edge_db.end()) return -1;
        if (it->second.facea == facea_ind) return it->second.faceb;
        if (it->second.faceb == facea_ind) return it->second.facea;
        return -1;
      };
      const int third1 = other_face(EdgeKey(a_prev, ind1));
      const int third2 = other_face(EdgeKey(ind2, a_next));
      const int around[4] = {facea_ind, faceb_ind, third1, third2};
      bool usable = true;
      Eigen::Matrix<double, 4, 3> planes;
      Eigen::Matrix<double, 4, 1> offsets;
      for (int i = 0; i < 4; i++) {
        if (around[i] < 0 || around[i] >= int(merged.size()) || merged[around[i]].size() < 3) {
          usable = false;
          break;
        }
        const Vector4d plane = calcTriangleNormal(vertices_copy, merged[around[i]]);
        planes.row(i) = plane.head<3>().transpose();
        offsets[i] = plane[3];  // the plane is norm . x == offset
      }
      if (!usable || third1 == faceb_ind || third2 == faceb_ind || third1 == third2) continue;

      const Vector3d ptcut = planes.colPivHouseholderQr().solve(offsets);
      double residual = 0;
      for (int i = 0; i < 4; i++) {
        residual = std::max(residual, fabs(planes.row(i).dot(ptcut) - offsets[i]));
      }
      // One thousandth of the radius: the merged vertex is then off each face by
      // less than a micron on a 1 mm fillet, and the arcs drawn tangent to those
      // faces inherit that error and no more. Beyond it the faces genuinely do
      // not meet at a point and there is nothing to collapse to, so the edge is
      // left unrounded rather than moved somewhere invented - and said out loud,
      // because a silently dropped feature looks exactly like one that was never
      // asked for.
      if (!ptcut.allFinite() || residual > 1e-3 * r_) {
        LOG(message_group::Warning,
            "fillet() left edge %1$d-%2$d unrounded: it is shorter than the diameter of the fillet, "
            "and the four faces around it miss a common point by %3$s, so there is no vertex to "
            "collapse it to. Reduce the radius, or remove the short edge from the model.",
            ind1, ind2, residual);
        e.second.sel = 0;
        continue;
      }

      // Merge ind2 into ind1 at the point they both become.
      vertices_copy[ind1] = ptcut;
      for (int j = 0; j < int(merged.size()); j++) {
        IndexedFace& tri = merged[j];
        int n = int(tri.size());
        int dupind = -1;
        for (int i = 0; i < n; i++) {
          if (tri[i] != ind2) continue;
          tri[i] = ind1;
          if (tri[(i + 1) % n] == ind1 || tri[(i + n - 1) % n] == ind1) dupind = i;
        }
        if (dupind != -1) {
          tri.erase(tri.begin() + dupind);
          n--;
        }
        if (n < 3) {
          merged.erase(merged.begin() + j);
          j--;
        }
      }

      // One collapse per pass, then start over. edge_db holds facea and faceb as
      // *indices* into merged, and polinds, polposs and corner_rounds index it
      // too; erasing a face here shifts every later index, so carrying on round
      // this loop would read the wrong faces. The enclosing do-while rebuilds all
      // of them, which is also why no lockout list is needed to keep two
      // collapses in one pass from interfering.
      improved = true;
      break;
    }

    // Which rounded edges meet at each corner, built from the selection that
    // *survived* the pass above rather than the one that entered it. An edge
    // refused there is not rounded, and a corner which still lists it draws a
    // patch whose rails were never drawn: cube(10) with fillet(5.1) left every
    // edge sharp and every corner rounded, and the result was a mesh whose own
    // volume, 1532, exceeded the 1000 of the box it sits in.
    for (auto& e : edge_db) {
      if (!e.second.sel) continue;
      corner_rounds[e.first.ind1].push_back(e.first.ind2);
      corner_rounds[e.first.ind2].push_back(e.first.ind1);
    }
  } while (improved == true);

  // start builder with existing vertices to have VertexIndex available
  //
  PolySetBuilder builder;
  VertexSnapper snap;
  for (size_t i = 0; i < vertices_copy.size(); i++) {
    snap.add(builder, vertices_copy[i]);  // allocate all vertices in the right order
  }

  SearchReplace s;
  std::vector<SearchReplace> sp;

  // plan fillets of all edges now
  for (auto& e : edge_db) {
    if (e.second.sel == 1) {
      Vector3d p1 = vertices_copy[e.first.ind1];  // both ends of the selected edge
      Vector3d p2 = vertices_copy[e.first.ind2];
      Vector3d p1org = p1, p2org = p2;
      Vector3d dir = p2 - p1;
      if (corner_rounds[e.first.ind1].size() >= 3) p1 += dir.normalized() * r_;
      if (corner_rounds[e.first.ind2].size() >= 3) p2 -= dir.normalized() * r_;
      dir = dir.normalized();  // TODO
      auto& facea = merged[e.second.facea];
      auto& faceb = merged[e.second.faceb];
      //

      int facean = facea.size();
      int facebn = faceb.size();
      double fanf = (faceParents[e.second.facea] != -1) ? -1 : 1;  // is the edge part of a hole
      double fbnf = (faceParents[e.second.faceb] != -1) ? -1 : 1;
      Vector3d fan = calcTriangleNormal(vertices_copy, facea).head<3>();
      Vector3d fbn = calcTriangleNormal(vertices_copy, faceb).head<3>();

      // A 1st side of the edge
      // B 2nd face of the edge
      int indposao, indposbo, indposai, indposbi;
      Vector3d unit;

      indposao = facea[(e.second.posa + facean - 1) % facean];  // o away from edge
      indposai = facea[(e.second.posa + 1) % facean];           // i on edge

      indposbo = faceb[(e.second.posb + 2) % facebn];
      indposbi = faceb[e.second.posb];

      Vector3d e_fa1 = (vertices_copy[indposao] - vertices_copy[facea[e.second.posa]]).normalized() *
                       fanf;  // Facea neben ind1
      Vector3d e_fa1p =
        (vertices_copy[indposai] - vertices_copy[facea[e.second.posa]]) * fanf;  // Face1 nahe  richtung
                                                                                 //
      Vector3d e_fb1 =
        (vertices_copy[indposbo] - vertices_copy[faceb[(e.second.posb + 1) % facebn]]).normalized() *
        fbnf;  // Faceb neben ind1
      Vector3d e_fb1p =
        (vertices_copy[indposbi] - vertices_copy[faceb[(e.second.posb + 1) % facebn]]) * fbnf;

      if (corner_rounds[e.first.ind1].size() == 2) {
        double a = (e_fb1.cross(e_fa1)).dot(dir);
        double b = (fan.cross(fbn)).dot(e_fa1p) * fanf * fbnf;
        if (list_included(corner_rounds[e.first.ind1], indposao)) {
          double ang = (dir).dot(e_fa1.normalized());
          e_fa1 += dir * fanf;
          if (a * b < 0) e_fa1 = -e_fa1 * fanf;
          e_fa1 /= sqrt(1 - ang * ang);
        }

        if (list_included(corner_rounds[e.first.ind1], indposbo)) {
          double ang = (dir).dot(e_fb1.normalized());
          e_fb1 += dir * fbnf;
          if (a * b < 0) e_fb1 = -e_fb1 * fbnf;
          e_fb1 /= sqrt(1 - ang * ang);
        }
      }

      if (corner_rounds[e.first.ind1].size() == 3) {
        if ((fbn.cross(fan)).dot(e_fa1p) < 0 || (fbn.cross(fan)).dot(e_fb1p) < 0) {
          if ((e_fa1p.cross(e_fa1)).dot(fan) * fanf < 0) {
            e_fa1 = -e_fa1 * fanf - 2 * (p2org - p1org).normalized();
          }
          if ((e_fb1p.cross(e_fb1)).dot(fbn) * fbnf > 0) {
            e_fb1 = -e_fb1 * fbnf - 2 * (p2org - p1org).normalized();
          }
        }
        if ((fbn.cross(fan)).dot(e_fb1p) > 0) {
          if ((e_fa1p.cross(e_fa1)).dot(fan) * fanf > 0 && (e_fa1p.cross(e_fb1)).dot(fbn) * fbnf > 0) {
            e_fb1 = -e_fb1 * fbnf - 2 * dir;
          }
          if ((e_fb1p.cross(e_fb1)).dot(fbn) * fbnf < 0 && (e_fb1p.cross(e_fa1)).dot(fan) * fanf < 0) {
            e_fa1 = -e_fa1 * fanf - 2 * dir;
          }
        }
      }
      e_fa1 *= r_;
      e_fb1 *= r_;

      indposao = facea[(e.second.posa + 2) % facean];
      indposai = facea[e.second.posa];

      indposbo = faceb[(e.second.posb + facebn - 1) % facebn];
      indposbi = faceb[(e.second.posb + 1) % facebn];

      Vector3d e_fa2 =
        (vertices_copy[indposao] - vertices_copy[facea[(e.second.posa + 1) % facean]]).normalized() *
        fanf;  // Face1 entfernte richtung
      Vector3d e_fa2p = (vertices_copy[indposai] - vertices_copy[facea[(e.second.posa + 1) % facean]]) *
                        fanf;  // Face1 entfernte richtung
                               //
      Vector3d e_fb2 =
        (vertices_copy[indposbo] - vertices_copy[faceb[(e.second.posb + 0) % facebn]]).normalized() *
        fbnf;  // Face2 entfernte Rcithung
      Vector3d e_fb2p = (vertices_copy[indposbi] - vertices_copy[faceb[(e.second.posb + 0) % facebn]]) *
                        fbnf;  // Face2 entfernte Rcithung

      //
      if (corner_rounds[e.first.ind2].size() == 2) {
        double a = (e_fb2.cross(e_fa2)).dot(dir);
        double b = (fan.cross(fbn)).dot(e_fa2p) * fanf * fbnf;
        if (list_included(corner_rounds[e.first.ind2], indposao)) {
          double ang = (dir).dot(e_fa2.normalized());
          e_fa2 -= dir * fanf;
          if (a * b > 0) e_fa2 = -e_fa2 * fanf;
          e_fa2 /= sqrt(1 - ang * ang);
        }

        if (list_included(corner_rounds[e.first.ind2], indposbo)) {
          double ang = (dir).dot(e_fb2.normalized());
          e_fb2 -= dir * fbnf;
          if (a * b > 0) e_fb2 = -e_fb2 * fbnf;
          e_fb2 /= sqrt(1 - ang * ang);
        }
      }

      if (corner_rounds[e.first.ind2].size() == 3) {
        if (-(fbn.cross(fan)).dot(e_fa2p) < 0 || -(fbn.cross(fan)).dot(e_fb2p) < 0) {
          if (-(e_fa2p.cross(e_fa2)).dot(fan) * fanf < 0) {
            e_fa2 = -e_fa2 * fanf + 2 * dir;
          }
          if (-(e_fb2p.cross(e_fb2)).dot(fbn) * fbnf > 0) {
            e_fb2 = -e_fb2 * fbnf + 2 * dir;
          }
        }
        if (/* -(fbn.cross(fan)).dot(e_fa2p) > 0 || */ -(fbn.cross(fan)).dot(e_fb2p) > 0) {
          if (-(e_fb2p.cross(e_fb2)).dot(fbn) * fbnf < 0 && (e_fb2p.cross(e_fa2)).dot(fan) * fanf > 0) {
            e_fa2 = -e_fa2 * fanf + 2 * dir;  // laengs links
          }
          if (-(e_fa2p.cross(e_fa2)).dot(fan) * fanf > 0 && (e_fa2p.cross(e_fb2)).dot(fbn) * fbnf < 0) {
            e_fb2 = -e_fb2 * fbnf + 2 * dir;
          }
        }
      }

      e_fa2 *= r_;
      e_fb2 *= r_;

      // Calculate bezier patches
      //
      // Through the shared Bezier() rather than the expanded polynomial this
      // used to spell out. Two reasons: the rail is now rational, and the
      // expansion was the second of two ways to compute the same point - the
      // corner patch computes the shared rail the other way, the two landed one
      // unit in the last place apart, and every filleted body was non-manifold
      // because of it. One arithmetic, one answer.
      for (int i = 0; i < bn; i++) {
        double f = (double)i / (double)(bn - 1);  // from 0 to 1
        e.second.bez1.push_back(snap.index(builder, Bezier(f, p1 + e_fa1, p1, p1 + e_fb1)));
        e.second.bez2.push_back(snap.index(builder, Bezier(f, p2 + e_fa2, p2, p2 + e_fb2)));
      }

      // Say what was just drawn. Expanding the rail above,
      // p + e_fa - 2f*e_fa + f^2*(e_fa + e_fb) is the quadratic Bezier through
      // the control points (p + e_fa, p, p + e_fb): it leaves one of the two
      // faces meeting at this edge, is controlled by the original edge vertex,
      // and arrives on the other. The strip is the ruled surface between the
      // two rails, so it is a tensor product of degree (2,1) and the net is
      // those six points - the same numbers the loop was already evaluating,
      // which is why this needs no fitting and cannot drift from the mesh.
      // The weights are the ones Bezier() just drew with, one per rail: where
      // the edge is straight and the radius constant the two are equal and the
      // strip is exactly a piece of a cylinder.
      builder.addSurface(std::make_shared<BezierPatchSurface>(
        2, 1, std::vector<Vector3d>{p1 + e_fa1, p2 + e_fa2, p1, p2, p1 + e_fb1, p2 + e_fb2},
        std::vector<double>{1.0, 1.0, BezierWeight(p1 + e_fa1, p1, p1 + e_fb1),
                            BezierWeight(p2 + e_fa2, p2, p2 + e_fb2), 1.0, 1.0}));
      s.pol = e.second.facea;  // laengsseite1
      s.search = e.first.ind1;
      s.replace = {e.second.bez1[0]};
      sp.push_back(s);
      s.pol = e.second.facea;  // laengsseite1
      s.search = e.first.ind2;
      s.replace = {e.second.bez2[0]};
      sp.push_back(s);

      s.pol = e.second.faceb;  // laengsseite2
      s.search = e.first.ind2;
      s.replace = {e.second.bez2[bn - 1]};
      sp.push_back(s);
      s.pol = e.second.faceb;  // laengsseite2
      s.search = e.first.ind1;
      s.replace = {e.second.bez1[bn - 1]};
      sp.push_back(s);

      // stirnseite 1
      if (corner_rounds[e.first.ind1].size() == 1) {
        for (size_t i = 0; i < polinds[e.first.ind1].size(); i++) {
          int faceid = polinds[e.first.ind1][i];
          if (faceid == e.second.facea) continue;
          if (faceid == e.second.faceb) continue;
          s.pol = faceid;  // stirnseite1
          s.search = e.first.ind1;
          s.replace = {e.second.bez1};
          std::reverse(s.replace.begin(), s.replace.end());
          sp.push_back(s);
        }
      }

      // stirnseite2
      if (corner_rounds[e.first.ind2].size() == 1) {
        for (size_t i = 0; i < polinds[e.first.ind2].size(); i++) {
          int faceid = polinds[e.first.ind2][i];
          if (faceid == e.second.facea) continue;
          if (faceid == e.second.faceb) continue;
          s.pol = faceid;  // stirnseite2
          s.search = e.first.ind2;
          s.replace = {e.second.bez2};
          sp.push_back(s);
        }
      }
      //     printf("\nNum=%d\n",debug);
      //     printf("P : %g/%g/%g EA: %g/%g/%g EB %g/%g/%g\n",p1[0], p1[1], p1[2], e_fa1[0], e_fa1[1],
      //     e_fa1[2], e_fb1[0], e_fb1[1], e_fb1[2]); printf("P : %g/%g/%g EA: %g/%g/%g EB
      //     %g/%g/%g\n",p2[0], p2[1], p2[2], e_fa2[0], e_fa2[1], e_fa2[2], e_fb2[0], e_fb2[1],
      //     e_fb2[2]);
    }
  }
  // copy modified faces
  std::vector<IndexedFace> newfaces;
  for (size_t i = 0; i < merged.size(); i++) {
    const IndexedFace& face = merged[i];
    IndexedFace newface;
    for (size_t j = 0; j < face.size(); j++) {
      int ind = face[j];
      newface.push_back(ind);
    }
    int fn = newface.size();
    // does newface need any mods ?
    for (size_t j = 0; j < sp.size(); j++) {  // TODO effektiver, sp sortiren und 0 groesser machen
      if ((size_t)sp[j].pol == i) {
        int needle = sp[j].search;
        for (int k = 0; k < fn; k++) {  // all possible shifts
          if (newface[k] == needle) {
            // match bei shift k gefunden
            IndexedFace tmp = sp[j].replace;
            for (int l = 0; l < fn - 1; l++) {
              tmp.push_back(newface[(k + 1 + l) % fn]);
            }
            newface = tmp;
            fn = newface.size();
            break;
          }
        }
      }
    }

    newfaces.push_back(newface);
  }
  std::vector<Vector3d> vertices;
  builder.copyVertices(vertices);
  std::vector<Vector3f> verticesFloat;
  for (const auto& v : vertices) verticesFloat.push_back(v.cast<float>());

  for (size_t i = 0; i < newfaces.size(); i++) {
    // tessellate first with holes // search all holes
    if (faceParents[i] != -1) continue;
    std::vector<IndexedFace> faces;
    faces.push_back(newfaces[i]);
    for (size_t j = 0; j < newfaces.size(); j++)
      if ((size_t)faceParents[j] == i) faces.push_back(newfaces[j]);
    //    if(faces.size() >1 ) continue;
    std::vector<IndexedTriangle> triangles;
    Vector3f norm(newnormals[i][0], newnormals[i][1], newnormals[i][2]);
    GeometryUtils::tessellatePolygonWithHoles(verticesFloat, faces, triangles, &norm);
    for (const auto& t : triangles) {
      builder.appendPolygon({t[0], t[1], t[2]});
    }
  }

  // add Rounded edges
  for (auto& e : edge_db) {
    if (e.second.sel == 1) {
      // now create the faces
      for (int i = 0; i < bn - 1; i++) {
        builder.appendPolygon(
          {e.second.bez1[i], e.second.bez1[i + 1], e.second.bez2[i + 1], e.second.bez2[i]});
      }
    }
  }
  // add missing 3 corner patches
  //
  for (size_t i = 0; i < vertices_copy.size(); i++) {
    if (corner_rounds[i].size() > 3) {
      printf("corner %ld not possible\n", i);
    } else if (corner_rounds[i].size() == 3) {
      // now get the right ordering of corner_rounds[i]
      IndexedFace face[3];
      Vector3d facenorm[3];
      for (int j = 0; j < 3; j++) {
        face[j] = merged[polinds[i][j]];
        facenorm[j] = calcTriangleNormal(vertices_copy, face[j]).head<3>();
        if (faceParents[polinds[i][j]] != -1) facenorm[j] = -facenorm[j];
      }

      int facebeg[3];
      int faceend[3];
      for (int j = 0; j < 3; j++) {
        facebeg[j] = face[j][(polposs[i][j] + face[j].size() - 1) % face[j].size()];
        faceend[j] = face[j][(polposs[i][j] + 1) % face[j].size()];
      }

      std::vector<int> angle;
      std::vector<Vector3d> dir;
      Vector3d x;
      if (faceend[0] == facebeg[1]) {  // 0,1,2
        x = vertices_copy[faceend[1]] - vertices_copy[i];
        dir.push_back(x.normalized() * r_);
        angle.push_back(
          (facenorm[1].cross(facenorm[2])).dot(vertices_copy[faceend[1]] - vertices_copy[i]) > 0 ? 1
                                                                                                 : -1);
        x = vertices_copy[faceend[2]] - vertices_copy[i];
        dir.push_back(x.normalized() * r_);
        angle.push_back(
          (facenorm[2].cross(facenorm[0])).dot(vertices_copy[faceend[2]] - vertices_copy[i]) > 0 ? 1
                                                                                                 : -1);
        x = vertices_copy[faceend[0]] - vertices_copy[i];
        dir.push_back(x.normalized() * r_);
        angle.push_back(
          (facenorm[0].cross(facenorm[1])).dot(vertices_copy[faceend[0]] - vertices_copy[i]) > 0 ? 1
                                                                                                 : -1);
      } else if (faceend[0] == facebeg[2]) {
        x = vertices_copy[faceend[2]] - vertices_copy[i];
        dir.push_back(x.normalized() * r_);
        angle.push_back(
          (facenorm[2].cross(facenorm[1])).dot(vertices_copy[faceend[2]] - vertices_copy[i]) > 0 ? 1
                                                                                                 : -1);
        x = vertices_copy[faceend[0]] - vertices_copy[i];
        dir.push_back(x.normalized() * r_);
        angle.push_back(
          (facenorm[0].cross(facenorm[2])).dot(vertices_copy[faceend[0]] - vertices_copy[i]) > 0 ? 1
                                                                                                 : -1);
        x = vertices_copy[faceend[1]] - vertices_copy[i];
        dir.push_back(x.normalized() * r_);
        angle.push_back(
          (facenorm[1].cross(facenorm[0])).dot(vertices_copy[faceend[1]] - vertices_copy[i]) > 0 ? 1
                                                                                                 : -1);
      } else assert(0);
      int conc1 = -1, conc2 = -1, conc3 = -1;
      int dirshift = -1;
      Vector3d pdir[3];
      if (angle[0] == -1 && angle[1] == -1 && angle[2] == -1) {
        dirshift = 0;
        conc1 = 0;
        conc2 = 0;
        conc3 = 1;
      }
      if (angle[0] == -1 && angle[1] == -1 && angle[2] == 1) {
        dirshift = 0;
        conc1 = 0;
        conc2 = 1;
        conc3 = 0;
      }
      if (angle[0] == -1 && angle[1] == 1 && angle[2] == -1) {
        dirshift = 2;
        conc1 = 0;
        conc2 = 1;
        conc3 = 0;
      }
      if (angle[0] == -1 && angle[1] == 1 && angle[2] == 1) {
        dirshift = 1;
        conc1 = 1;
        conc2 = 0;
        conc3 = 0;
      }
      if (angle[0] == 1 && angle[1] == -1 && angle[2] == -1) {
        dirshift = 1;
        conc1 = 0;
        conc2 = 1;
        conc3 = 0;
      }
      if (angle[0] == 1 && angle[1] == -1 && angle[2] == 1) {
        dirshift = 2;
        conc1 = 1;
        conc2 = 0;
        conc3 = 0;
      }
      if (angle[0] == 1 && angle[1] == 1 && angle[2] == -1) {
        dirshift = 0;
        conc1 = 1;
        conc2 = 0;
        conc3 = 0;
      }
      if (angle[0] == 1 && angle[1] == 1 && angle[2] == 1) {
        dirshift = 0;
        conc1 = 0;
        conc2 = 0;
        conc3 = 0;
      }
      if (dirshift != -1) {
        for (int i = 0; i < 3; i++) {
          pdir[i] = -dir[(i + dirshift) % 3];
        }
        bezier_patch(builder, snap, ps->vertices[i] - pdir[0] - pdir[1] - pdir[2], pdir, conc1, conc2,
                     conc3, bn);
      }
    }
  }
  //
  auto result = builder.build();

  return result;
}

std::unique_ptr<const Geometry> FilletNode::createGeometry() const
{
  std::shared_ptr<const PolySet> ps;
  std::shared_ptr<PolySet> ps_merged;
  std::vector<bool> corner_selected;
  if (this->children.size() >= 1) {
    ps = childToPolySet(this->children[0]);
    if (ps == nullptr) return std::unique_ptr<PolySet>();

    ps_merged = std::make_shared<PolySet>(*ps);

    std::vector<Vector4d> normals = calcTriangleNormals(ps->vertices, ps->indices);
    std::vector<int> faceParents;
    std::vector<Vector4d> newnormals;
    ps_merged->indices = mergeTriangles(ps->indices, normals, newnormals, faceParents, ps->vertices);

  } else return std::unique_ptr<PolySet>();
  if (this->children.size() >= 2) {
    std::shared_ptr<const PolySet> sel = childToPolySet(this->children[1]);
    if (sel != nullptr) {
      auto sel_tess = PolySetUtils::tessellate_faces(*sel);
      for (size_t i = 0; i < ps_merged->vertices.size(); i++) {
        corner_selected.push_back(sel_tess->point_inside(ps_merged->vertices[i]));
      }
    }

  } else {
    for (size_t i = 0; i < ps_merged->vertices.size(); i++) corner_selected.push_back(true);
  }
  return createFilletInt(ps_merged, corner_selected, this->r, this->fn, this->minang);
}
