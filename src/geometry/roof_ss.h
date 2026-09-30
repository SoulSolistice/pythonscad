// This file is a part of openscad. Everything implied is implied.
// Author: Alexey Korepanov <kaikaikai@yandex.ru>

#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "geometry/PolySet.h"
#include "geometry/Polygon2d.h"
#include "geometry/SweptSurfaces.h"

namespace roof_ss {
/*! The roof over `poly` by its straight skeleton. Each face is named, where
 * `name` is given, by the tag of the record over the profile edge it stands
 * over (checklist E8) - the skeleton face's own contour edge, whose name its
 * two ends carry through Clipper - and the floor by the floor's. */
std::unique_ptr<PolySet> straight_skeleton_roof(
  const Polygon2d& poly, const std::function<uint32_t(const RoofSite&)>& name = {});
}  // namespace roof_ss
