/*! The corner placement - see BodyBuild.h. */
#include "geometry/brep/BodyBuild.h"

using namespace step;

void step::BodyBuild::placeCorners()
{
  for (const auto& m : cornerMoves) move_rung[m.first] = CornerRung::Provenance;
  placeOnMeshPlanes();
  reportRecognition();
  placeOnFileFaces();
  holdWeldedVertices();
  checkMovesStayOnFaces();
  applyCornerMoves();
}
