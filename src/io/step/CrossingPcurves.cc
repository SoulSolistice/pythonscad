/*! The crossing curves' pcurves - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <cmath>
#include <vector>
#include <Eigen/Dense>
#include "utils/printutils.h"

void StepKernel::BodyBuild::writeCrossingPcurves()
{
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
}
