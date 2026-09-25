/*! The corner placement - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <cmath>

#include "geometry/AnalyticFeatures.h"

using namespace step;

void StepKernel::BodyBuild::placeCorners()
{
  for (const auto& m : cornerMoves) move_rung[m.first] = CornerRung::Provenance;
  placeOnMeshPlanes();
  reportRecognition();
  placeOnFileFaces();
  placeOnOwnQuadric();
  holdWeldedVertices();
  keepExactSurfaces();
  checkMovesStayOnFaces();
  applyCornerMoves();
}

bool StepKernel::BodyBuild::deserts_exact(int v, const Surface *own, const Vector3d& target)
{
  for (const auto& other : surfaces) {
    if (other.get() == own || !AnalyticFeatures::isQuadric(other.get())) continue;
    Vector3d on_now, on_then;
    if (!AnalyticFeatures::closestOnSurface(other.get(), vertices[v], on_now) ||
        (on_now - vertices[v]).norm() > 1e-9) {
      continue;
    }
    if (!AnalyticFeatures::closestOnSurface(other.get(), target, on_then) ||
        (on_then - target).norm() > 1e-9) {
      return true;
    }
  }
  return false;
}
