/*! Every face, in one list - see BodyBuild.h. */
#include "io/step/BodyBuild.h"

#include <vector>
#include "utils/printutils.h"

void StepKernel::BodyBuild::assembleFaces()
{
  // Each planar face: its outer loop, then the loops of its holes.
  for (auto& [i, face] : planar_faces) {
    face.loops.push_back(std::move(planar_loops[i]));
    for (std::size_t j = 0; j < face_cnt; j++) {
      if (parents[j] != int(i) || !boundWritten(j)) continue;
      face.loops.push_back(std::move(planar_loops[j]));
    }
    brep_body.faces.push_back(std::move(face));
  }
  planar_faces.clear();

  if (planes_off_plane > 0) {
    LOG(message_group::Export_Error,
        "STEP export: %1$d planar face%2$s assert a plane one of their own bounds is not in, at "
        "machine precision - by up to %3$.4f; a bound bent by something other than the merge",
        int(planes_off_plane), planes_off_plane == 1 ? "" : "s", worst_plane_off);
  }
  // The analytic faces are faces of the same shell, in the order their
  // emitters made them: read off the entities an emitter made, or as an
  // emitter built them on the B-rep; then the fans.
  for (std::size_t k = 0; k < sfaces_extra.size(); k++) {
    const Face *face = sfaces_extra[k];
    if (face == nullptr) {
      brep_body.faces.push_back(std::move(built_faces.at(k)));
      continue;
    }
    const auto source = face_source.find(face);
    reader.face(face, source != face_source.end() ? &source->second : nullptr);
  }
  built_faces.clear();
  for (auto& face : fan_faces) brep_body.faces.push_back(std::move(face));
  fan_faces.clear();

  brep_body.shells = brep::connectedShells(brep_body);
}
