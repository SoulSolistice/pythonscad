/*! The crossing curves' pcurves - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <cmath>
#include <functional>
#include <vector>
#include <Eigen/Dense>
#include "io/step/CrossingRuns.h"
#include "utils/printutils.h"

using namespace step;

void StepKernel::BodyBuild::writeCrossingPcurves()
{
  int pcurved = 0;
  for (std::size_t index = 0; index < crossing_pieces.size(); index++) {
    const CrossingPiece& piece = crossing_pieces[index];
    const auto sides = crossing_sides.find(index);
    if (piece.edge == nullptr || sides == crossing_sides.end() || sides->second.size() != 2) continue;
    auto *curve = dynamic_cast<BSplineCurve *>(piece.edge->round);
    if (curve == nullptr || curve->pts.size() < 2) continue;

    // The curve at the points it was fitted through, and how the same curve in
    // a surface's parameters is fitted through their images there: then the
    // pcurve and the 3D curve agree at those points exactly. A piece of one edge
    // is one Bezier, collocated at its own parameters; a longer piece is the
    // interpolation of its run, carried over with its parameters and knots.
    std::vector<Vector3d> on_curve;
    std::function<std::vector<Eigen::Vector2d>(const std::vector<Eigen::Vector2d>&)> fit_flat;
    const bool bezier = piece.fit.ctrl.empty();
    Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr;
    if (bezier) {
      const int degree = int(curve->pts.size()) - 1;
      on_curve.resize(degree + 1);
      for (int i = 0; i <= degree; i++) {
        const double t = double(i) / degree;
        Vector3d p = Vector3d::Zero();
        double binom = 1;
        for (int j = 0; j <= degree; j++) {
          p += curve->pts[j]->pt * (binom * pow(t, j) * pow(1 - t, degree - j));
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
      qr.compute(m);
      fit_flat = [&](const std::vector<Eigen::Vector2d>& uv) {
        Eigen::MatrixXd rhs(long(uv.size()), 2);
        for (std::size_t i = 0; i < uv.size(); i++) rhs.row(long(i)) = uv[i].transpose();
        const Eigen::MatrixXd solved = qr.solve(rhs);
        std::vector<Eigen::Vector2d> out(uv.size());
        for (std::size_t j = 0; j < uv.size(); j++) out[j] = solved.row(long(j)).transpose();
        return out;
      };
    } else {
      on_curve = piece.fit.points;
      fit_flat = [&](const std::vector<Eigen::Vector2d>& uv) { return interpolateLike(piece.fit, uv); };
    }

    std::vector<PCurve *> on_both;
    for (const CrossingSide& side : sides->second) {
      const Vector3d ref2 = side.axis.cross(side.ref);
      std::vector<Eigen::Vector2d> uv(on_curve.size());
      double previous = 0;
      for (std::size_t i = 0; i < on_curve.size(); i++) {
        const Vector3d rel = on_curve[i] - side.base;
        double u = atan2(rel.dot(ref2), rel.dot(side.ref));
        // A surface of revolution's parameter wraps, and a curve that steps over
        // the seam has to keep counting rather than jump a turn.
        if (i > 0) {
          while (u - previous > M_PI) u -= 2 * M_PI;
          while (u - previous < -M_PI) u += 2 * M_PI;
        }
        previous = u;
        uv[i] = Eigen::Vector2d(u, rel.dot(side.axis));
      }
      const std::vector<Eigen::Vector2d> solved = fit_flat(uv);
      if (solved.size() != curve->pts.size()) break;
      std::vector<Point2d *> cp;
      for (const auto& c : solved) cp.push_back(new Point2d(entities, c.x(), c.y()));
      auto flat = new BSplineCurve2d(entities, cp);
      if (!bezier) flat->setKnots(CurveFit::degree, piece.fit.knots, piece.fit.mults);
      if (param_context == nullptr) param_context = new ParametricContext(entities);
      auto rep = new DefinitionalRepresentation(entities, flat, param_context);
      on_both.push_back(new PCurve(entities, side.surface, rep));
    }
    if (on_both.size() != 2) continue;
    piece.edge->round = new SurfaceCurve(entities, curve, on_both);
    pcurved++;
  }
  if (pcurved > 0) {
    LOG(
      "STEP export: %1$d of those carr%2$s a pcurve on each of the two surfaces, so the curve "
      "is written as lying on them rather than left to be projected back",
      pcurved, pcurved == 1 ? "ies" : "y");
  }
}
