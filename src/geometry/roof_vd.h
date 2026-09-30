// This file is a part of openscad. Everything implied is implied.
// Author: Alexey Korepanov <kaikaikai@yandex.ru>

#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "core/CurveDiscretizer.h"
#include "geometry/PolySet.h"
#include "geometry/Polygon2d.h"
#include "geometry/SweptSurfaces.h"

namespace roof_vd {
/*! The roof over `poly` by its Voronoi diagram. Each face is named, where
 * `name` is given, by the tag of the record over the site whose cell it is
 * (checklist E8): a segment's cell by its edge's, a corner's by the corner's -
 * the names the profile's points carry through Clipper - and the floor by the
 * floor's. */
std::unique_ptr<PolySet> voronoi_diagram_roof(const Polygon2d& poly, const CurveDiscretizer& discretizer,
                                              const std::function<uint32_t(const RoofSite&)>& name = {});
}  // namespace roof_vd
