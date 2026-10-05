/*! The corner placement - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

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
