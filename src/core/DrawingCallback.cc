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
#include "core/DrawingCallback.h"

#include <cmath>
#include <memory>
#include <vector>

#include "geometry/Polygon2d.h"
#include "src/core/ColorUtil.h"

DrawingCallback::DrawingCallback(unsigned long fn, double size)
  : pen(Vector2d(0, 0)), offset(Vector2d(0, 0)), advance(Vector2d(0, 0)), fn(fn), size(size)
{
}

DrawingCallback::~DrawingCallback()
{
}

void DrawingCallback::start_glyph()
{
  this->polygon = std::make_shared<Polygon2d>();
  // FIXME: Why do we think that a glyph is sanitized?
  // This is technically not true, since we don't maintain correct values for the 'positive' flag.
  this->polygon->setSanitized(true);
}

void DrawingCallback::finish_glyph()
{
  close_outline();
  if (this->polygon->outlines().size() == 0) {
    this->polygon = nullptr;
  }
  if (this->polygon) {
    Color4f col = *OpenSCAD::parse_color("#f9d72c");
    polygon->setColor(col);
    this->polygons.push_back(this->polygon);
    this->polygon = nullptr;
  }
}

std::vector<std::shared_ptr<const Polygon2d>> DrawingCallback::get_result()
{
  return this->polygons;
}

void DrawingCallback::set_glyph_offset(double offset_x, double offset_y)
{
  offset = Vector2d(offset_x, offset_y);
}

void DrawingCallback::add_glyph_advance(double advance_x, double advance_y)
{
  advance += Vector2d(advance_x, advance_y);
}

void DrawingCallback::add_vertex(const Vector2d& v, EdgeDecl edge)
{
  this->outline.vertices.push_back(size * (v + offset + advance));
  this->arriving.push_back(edge);
}

/*! Hand the contour to the glyph, each edge named as what drew it: line_to()
 * a straight segment, curve_to() a chord of the Bezier it recorded. Measuring
 * that back afterwards - both ends inside some segment's control box - called
 * a glyph's straight edges curved wherever they ran inside a neighbouring
 * curve's box, and a curve's chords straight wherever Clipper's grid moved an
 * end outside it. See EdgeIdentity.h. */
void DrawingCallback::close_outline()
{
  const std::size_t n = this->outline.vertices.size();
  if (n == 0) return;
  if (this->polygon) {
    const EdgeId first = reserveEdgeIds(n);
    this->outline.edge_ids.resize(n);
    for (std::size_t i = 0; i < n; i++) {
      // edge i runs from vertex i to vertex i + 1, so it is what arrived there;
      // the last one closes the contour, straight back to the first vertex
      this->outline.edge_ids[i] = first + EdgeId(i);
      this->polygon->edge_decls[first + EdgeId(i)] =
        i + 1 < n ? this->arriving[i + 1] : EdgeDecl{EdgeKind::Line, -1};
    }
    this->polygon->addOutline(this->outline);
  }
  this->outline.vertices.clear();
  this->outline.edge_ids.clear();
  this->arriving.clear();
}

/*! Keep the curve, not only the points it was sampled at.
 *
 * The control points are in glyph space and add_vertex() is what puts a point
 * into outline space, so the record has to make the same journey - the same
 * size, offset and advance, applied to the control points instead of to the
 * samples. Then the record and the vertices describe one curve rather than two
 * that happen to be near each other.
 *
 * A hint like every record on this channel: an extruder fits it to the mesh
 * before believing it, and a glyph that a boolean has since cut through simply
 * stops matching. */
void DrawingCallback::add_bezier(const Vector2d *ctrl, int degree)
{
  if (!this->polygon) return;
  Bezier2d bez;
  bez.degree = degree;
  for (int i = 0; i <= degree; i++) {
    bez.ctrl[i] = Vector2d(size * (ctrl[i] + offset + advance));
  }
  this->polygon->beziers.push_back(bez);
}

void DrawingCallback::move_to(const Vector2d& to)
{
  close_outline();
  add_vertex(to);
  pen = to;
}

void DrawingCallback::line_to(const Vector2d& to)
{
  add_vertex(to);
  pen = to;
}

// Quadric Bezier curve
void DrawingCallback::curve_to(const Vector2d& c1, const Vector2d& to)
{
  const Vector2d ctrl[3] = {pen, c1, to};
  add_bezier(ctrl, 2);
  const EdgeDecl chord{EdgeKind::Bezier, this->polygon ? int(this->polygon->beziers.size()) - 1 : -1};
  // NOTE - this could be done better using a chord length iteration (uniform in space) to implement $fa
  // (lot of work, little gain)
  for (unsigned long idx = 1; idx <= fn; ++idx) {
    const double a = idx * (1.0 / (double)fn);
    add_vertex(pen * pow(1 - a, 2) + c1 * 2 * pow(1 - a, 1) * a + to * pow(a, 2), chord);
  }
  pen = to;
}

// Cubic Bezier curve
void DrawingCallback::curve_to(const Vector2d& c1, const Vector2d& c2, const Vector2d& to)
{
  const Vector2d ctrl[4] = {pen, c1, c2, to};
  add_bezier(ctrl, 3);
  const EdgeDecl chord{EdgeKind::Bezier, this->polygon ? int(this->polygon->beziers.size()) - 1 : -1};
  // NOTE - this could be done better using a chord length iteration (uniform in space) to implement $fa
  // (lot of work, little gain)
  for (unsigned long idx = 1; idx <= fn; ++idx) {
    const double a = idx * (1.0 / (double)fn);
    add_vertex(pen * pow(1 - a, 3) + c1 * 3 * pow(1 - a, 2) * a + c2 * 3 * pow(1 - a, 1) * pow(a, 2) +
                 to * pow(a, 3),
               chord);
  }
  pen = to;
}
