/*Copyright(c) 2018, slugdev
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met :
1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
3. All advertising materials mentioning features or use of this software
must display the following acknowledgement :
This product includes software developed by slugdev.
4. Neither the name of the slugdev nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY SLUGDEV ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.IN NO EVENT SHALL SLUGDEV BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.*/

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <utility>
#include <algorithm>
#include <sstream>
#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <math.h>
#include "src/geometry/GeometryUtils.h"
#include <src/geometry/Curve.h>
#include <src/geometry/Surface.h>

// Format a double as an ISO 10303-21 REAL literal.
//
// The default ostream formatting is unusable for STEP: it only emits 6
// significant digits (which quietly shifts vertices and opens gaps between
// neighbouring faces) and it produces literals such as "0" or "1e-07" which
// are not valid REALs - the standard requires an explicit decimal point and
// an upper case exponent marker.
inline std::string step_real(double v)
{
  if (!std::isfinite(v)) v = 0.0;
  if (v == 0.0) return "0.";

  // The formatting has to be independent of the active locale: openscad.cc
  // calls setlocale(LC_ALL, ""), so on a locale with a comma radix (de, fr,
  // ...) a coordinate would come out as "-5,394" and a STEP reader would parse
  // it as two separate numbers, shifting every following argument.
  char buf[64];
  std::string s;

#ifdef __cpp_lib_to_chars
  // std::to_chars is locale independent by definition and already yields the
  // shortest representation which round-trips exactly.
  const auto res = std::to_chars(buf, buf + sizeof(buf), v);
  if (res.ec == std::errc{}) s.assign(buf, res.ptr);
#endif

  if (s.empty()) {
    // Fallback: snprintf honours LC_NUMERIC, so the radix has to be corrected
    // afterwards.
    for (int prec = 15; prec <= 17; prec++) {
      snprintf(buf, sizeof(buf), "%.*g", prec, v);
      if (std::strtod(buf, nullptr) == v) break;
    }
    s = buf;
    const char *radix = std::localeconv()->decimal_point;
    if (radix != nullptr && radix[0] != '\0' && radix[0] != '.') {
      std::replace(s.begin(), s.end(), radix[0], '.');
    }
  }

  auto epos = s.find_first_of("eE");
  if (epos == std::string::npos) {
    if (s.find('.') == std::string::npos) s += '.';
  } else {
    s[epos] = 'E';
    if (s.find('.') == std::string::npos) s.insert(epos, 1, '.');
  }
  return s;
}

// Escape a string for use inside an ISO 10303-21 string literal. An apostrophe
// is written twice, a backslash (which starts a control directive) as well.
inline std::string step_string(const std::string& str)
{
  std::string out;
  out.reserve(str.size());
  for (const char c : str) {
    if (c == '\'') out += "''";
    else if (c == '\\') out += "\\\\";
    else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
    else out += c;
  }
  return out;
}

class StepKernel
{
public:
  // Entities are arena owned: every one registers itself here in its base
  // constructor, and ~StepKernel deletes the arena. Nothing else owns an
  // Entity, and nothing should delete one.
  //
  // Registering in the *base* constructor is what makes that safe. It runs
  // before any derived member is initialised, so an entity built as an argument
  // to another entity's constructor is already in the arena by the time the
  // outer one is being built, and is freed even if the outer never completes.
  // Entities therefore do not leak on a throw.
  //
  // The one hole, recorded rather than fixed: if a *derived* constructor throws
  // after this one has run, the runtime frees the storage and the arena is left
  // holding a dangling pointer to it, which the destructor then deletes again.
  // Closing it properly means taking self-registration out of the constructor
  // across 126 construction sites, which is not worth doing for a path only
  // reachable on allocation failure - but it is worth knowing about before
  // anyone adds a constructor here that can throw for an ordinary reason.
  class Entity
  {
  public:
    Entity(std::vector<Entity *>& ent_list)
    {
      ent_list.push_back(this);
      id = int(ent_list.size());
    }
    virtual ~Entity() {}

    std::vector<std::string> tokenize(const std::string& str, const std::string& delimiters = ",")
    {
      std::vector<std::string> tokens;
      // Skip delimiters at beginning.
      std::string::size_type lastPos = str.find_first_not_of(delimiters, 0);

      // Find first non-delimiter.
      std::string::size_type pos = str.find_first_of(delimiters, lastPos);

      while (std::string::npos != pos || std::string::npos != lastPos) {
        // Found a token, add it to the vector.
        tokens.push_back(str.substr(lastPos, pos - lastPos));

        // Skip delimiters.
        lastPos = str.find_first_not_of(delimiters, pos);

        // Find next non-delimiter.
        pos = str.find_first_of(delimiters, lastPos);
      }
      return tokens;
    }

    virtual void serialize(std::ostream& stream_in) = 0;
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) = 0;
    int id;
    std::string label;
  };

  class Direction : public Entity
  {
  public:
    Direction(std::vector<Entity *>& ent_list) : Entity(ent_list)
    {
      pt[0] = 0;
      pt[1] = 0;
      pt[2] = 0;
    }
    Direction(std::vector<Entity *>& ent_list, Vector3d pt_in) : Entity(ent_list) { pt = pt_in; }

    virtual ~Direction() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = DIRECTION('" << label << "', (" << step_real(pt[0]) << ", "
                << step_real(pt[1]) << ", " << step_real(pt[2]) << "));\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of('(');
      auto en = args.find_last_of(')');
      auto arg_str = args.substr(st + 1, en - st - 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::stringstream ss(arg_str);
      ss >> pt[0] >> pt[1] >> pt[2];
    }
    Vector3d pt;
  };

  class Point : public Entity
  {
  public:
    Point(std::vector<Entity *>& ent_list) : Entity(ent_list)
    {
      pt[0] = 0;
      pt[1] = 0;
      pt[2] = 0;
    }
    Point(std::vector<Entity *>& ent_list, Vector3d pt_in) : Entity(ent_list) { pt = pt_in; }

    virtual ~Point() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = CARTESIAN_POINT('" << label << "', (" << step_real(pt[0]) << ","
                << step_real(pt[1]) << "," << step_real(pt[2]) << "));\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of('(');
      auto en = args.find_last_of(')');
      auto arg_str = args.substr(st + 1, en - st - 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::stringstream ss(arg_str);
      ss >> pt[0] >> pt[1] >> pt[2];
    }
    Vector3d pt;
  };

  class Axis2Placement : public Entity
  {
  public:
    Axis2Placement(std::vector<Entity *>& ent_list) : Entity(ent_list)
    {
      dir1 = 0;
      dir2 = 0;
      point = 0;
    }

    Axis2Placement(std::vector<Entity *>& ent_list, Direction *dir1_in, Direction *dir2_in,
                   Point *point_in)
      : Entity(ent_list)
    {
      dir1 = dir1_in;
      dir2 = dir2_in;
      point = point_in;
    }

    virtual ~Axis2Placement() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = AXIS2_PLACEMENT_3D('" << label << "',#" << point->id << ",#"
                << dir1->id << ",#" << dir2->id << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int d1_id, d2_id, p_id;
      ss >> p_id >> d1_id >> d2_id;

      dir1 = dynamic_cast<Direction *>(ent_map[d1_id]);
      dir2 = dynamic_cast<Direction *>(ent_map[d2_id]);
      point = dynamic_cast<Point *>(ent_map[p_id]);
    }

    Direction *dir1;
    Direction *dir2;
    Point *point;
  };

  class SurfaceType : public Entity
  {
  public:
    SurfaceType(std::vector<Entity *>& ent_list) : Entity(ent_list) {}
  };

  class Plane : public SurfaceType
  {
  public:
    Plane(std::vector<Entity *>& ent_list) : SurfaceType(ent_list) { axis = 0; }

    Plane(std::vector<Entity *>& ent_list, Axis2Placement *axis_in) : SurfaceType(ent_list)
    {
      axis = axis_in;
    }
    virtual ~Plane() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = PLANE('" << label << "',#" << axis->id << ");\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p_id;
      ss >> p_id;

      axis = dynamic_cast<Axis2Placement *>(ent_map[p_id]);
    }

    Axis2Placement *axis;
  };

  class CylindricalSurface : public SurfaceType
  {
  public:
    CylindricalSurface(std::vector<Entity *>& ent_list) : SurfaceType(ent_list)
    {
      axis = 0;
      r = 0;
    }

    CylindricalSurface(std::vector<Entity *>& ent_list, std::string name_in, Axis2Placement *axis_in,
                       double r_in)
      : SurfaceType(ent_list)
    {
      name = name_in;
      axis = axis_in;
      r = r_in;
    }
    virtual ~CylindricalSurface() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = CYLINDRICAL_SURFACE('" << label << "',#" << axis->id << ","
                << step_real(r) << ");\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p_id;
      ss >> p_id >> r;
      axis = dynamic_cast<Axis2Placement *>(ent_map[p_id]);
    }
    std::string name;
    double r;
    Axis2Placement *axis;
  };

  /*! A torus, given by the radius of the circle the tube's centre traces and
   * the radius of the tube itself. */
  class ToroidalSurface : public SurfaceType
  {
  public:
    ToroidalSurface(std::vector<Entity *>& ent_list) : SurfaceType(ent_list)
    {
      axis = 0;
      r_major = 0;
      r_minor = 0;
    }

    ToroidalSurface(std::vector<Entity *>& ent_list, std::string name_in, Axis2Placement *axis_in,
                    double r_major_in, double r_minor_in)
      : SurfaceType(ent_list)
    {
      name = name_in;
      axis = axis_in;
      r_major = r_major_in;
      r_minor = r_minor_in;
    }
    virtual ~ToroidalSurface() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = TOROIDAL_SURFACE('" << label << "',#" << axis->id << ","
                << step_real(r_major) << "," << step_real(r_minor) << ");\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p_id;
      ss >> p_id >> r_major >> r_minor;
      axis = dynamic_cast<Axis2Placement *>(ent_map[p_id]);
    }
    std::string name;
    double r_major, r_minor;
    Axis2Placement *axis;
  };

  /*! A sphere. The placement's axis is the pole of the surface's own
   * parameterisation and carries no geometry: a sphere looks the same from
   * every direction, but a face on one still has to say which way its seam
   * runs. */
  class SphericalSurface : public SurfaceType
  {
  public:
    SphericalSurface(std::vector<Entity *>& ent_list) : SurfaceType(ent_list)
    {
      axis = 0;
      r = 0;
    }

    SphericalSurface(std::vector<Entity *>& ent_list, std::string name_in, Axis2Placement *axis_in,
                     double r_in)
      : SurfaceType(ent_list)
    {
      name = name_in;
      axis = axis_in;
      r = r_in;
    }
    virtual ~SphericalSurface() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = SPHERICAL_SURFACE('" << label << "',#" << axis->id << ","
                << step_real(r) << ");\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p_id;
      ss >> p_id >> r;
      axis = dynamic_cast<Axis2Placement *>(ent_map[p_id]);
    }
    std::string name;
    double r;
    Axis2Placement *axis;
  };

  /*! A cone, given by the radius in the placement's plane and the half angle it
   * opens by along the placement's axis. ISO 10303 wants the half angle in
   * (0, pi/2), so a cone which narrows along its axis has to be written from
   * its other end rather than with a negative angle. */
  class ConicalSurface : public SurfaceType
  {
  public:
    ConicalSurface(std::vector<Entity *>& ent_list) : SurfaceType(ent_list)
    {
      axis = 0;
      r = 0;
      half_angle = 0;
    }

    ConicalSurface(std::vector<Entity *>& ent_list, std::string name_in, Axis2Placement *axis_in,
                   double r_in, double half_angle_in)
      : SurfaceType(ent_list)
    {
      name = name_in;
      axis = axis_in;
      r = r_in;
      half_angle = half_angle_in;
    }
    virtual ~ConicalSurface() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = CONICAL_SURFACE('" << label << "',#" << axis->id << ","
                << step_real(r) << "," << step_real(half_angle) << ");\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p_id;
      ss >> p_id >> r >> half_angle;
      axis = dynamic_cast<Axis2Placement *>(ent_map[p_id]);
    }
    std::string name;
    double r;
    double half_angle;
    Axis2Placement *axis;
  };

  class RoundType : public Entity
  {
  public:
    RoundType(std::vector<Entity *>& ent_list) : Entity(ent_list) {}
  };

  /*! A tensor-product B-spline surface.
   *
   * Two shapes of net arrive here. A fillet's patch is a Bezier: a Bezier of
   * degree d is the B-spline whose only knots are 0 and 1, each with
   * multiplicity d+1, so no knot vector has to be invented - the control net is
   * written straight out and the parameterisation follows. Both patches a
   * fillet draws are degree 2 in at least one direction, and the corner's apex
   * row makes three of its control points coincide - a singular point, which is
   * how a rounded corner is normally written.
   *
   * A declared grid is the other shape: many more control points than degree+1,
   * with interior knots that come from the interpolation and are not derivable
   * from the degrees. Pass those through `setKnots`, which is the only reason
   * this class is not Bezier-only; leave them unset and the Bezier knots are
   * synthesised as before. */
  class BSplineSurface : public SurfaceType
  {
  public:
    BSplineSurface(std::vector<Entity *>& ent_list) : SurfaceType(ent_list) {}
    BSplineSurface(std::vector<Entity *>& ent_list, std::string name_in, int degree_u_in,
                   int degree_v_in, std::vector<std::vector<Point *>> net_in,
                   std::vector<std::vector<double>> weights_in = {})
      : SurfaceType(ent_list)
    {
      label = std::move(name_in);
      degree_u = degree_u_in;
      degree_v = degree_v_in;
      net = std::move(net_in);
      weights = std::move(weights_in);
    }
    virtual ~BSplineSurface() {}

    virtual void serialize(std::ostream& stream_in)
    {
      std::ostringstream ctrl;
      for (std::size_t i = 0; i < net.size(); i++) {
        ctrl << (i ? ",(" : "(");
        for (std::size_t j = 0; j < net[i].size(); j++) {
          ctrl << (j ? ",#" : "#") << net[i][j]->id;
        }
        ctrl << ")";
      }
      // ISO 10303-42 orders these u_multiplicities, v_multiplicities, u_knots,
      // v_knots - all four multiplicities before any knot, not one direction
      // fully then the other. Writing them interleaved gives a reader (0.,1.)
      // where it expects v_multiplicities, and a face whose surface will not
      // build: OpenCASCADE drops it and takes the shell as loose surfaces. Both
      // branches below share these so the two orders cannot drift apart again -
      // which is exactly what had happened, the polynomial one being right and
      // the rational one wrong.
      const std::string knots = knot_text();

      if (weights.empty()) {
        stream_in << "#" << id << " = B_SPLINE_SURFACE_WITH_KNOTS('" << label << "'," << degree_u << ","
                  << degree_v << ",(" << ctrl.str() << "),.UNSPECIFIED.,.F.,.F.,.F.," << knots
                  << ",.UNSPECIFIED.);\n";
        return;
      }

      // A rational surface has no single entity of its own in ISO 10303: it is a
      // complex instance, the subtypes named in alphabetical order, with the
      // weights carried by RATIONAL_B_SPLINE_SURFACE. Writing the weights away
      // and the rest as a plain B_SPLINE_SURFACE_WITH_KNOTS would describe a
      // different surface - a parabola in place of a circular arc.
      std::ostringstream wts;
      for (std::size_t i = 0; i < weights.size(); i++) {
        wts << (i ? ",(" : "(");
        for (std::size_t j = 0; j < weights[i].size(); j++) {
          wts << (j ? "," : "") << step_real(weights[i][j]);
        }
        wts << ")";
      }
      stream_in << "#" << id << " = ( BOUNDED_SURFACE() B_SPLINE_SURFACE(" << degree_u << "," << degree_v
                << ",(" << ctrl.str() << "),.UNSPECIFIED.,.F.,.F.,.F.)"
                << " B_SPLINE_SURFACE_WITH_KNOTS(" << knots
                << ",.UNSPECIFIED.) GEOMETRIC_REPRESENTATION_ITEM()" << " RATIONAL_B_SPLINE_SURFACE(("
                << wts.str() << "))" << " REPRESENTATION_ITEM('" << label << "') SURFACE() );\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    /*! The knots of a surface that is not a Bezier: distinct values and their
     * multiplicities, per direction. Both directions have to be given together
     * - a surface with general knots in one direction and Bezier knots in the
     * other is still a surface with general knots, and the reader is told all
     * four lists or none. */
    void setKnots(std::vector<double> ku, std::vector<int> mu, std::vector<double> kv,
                  std::vector<int> mv)
    {
      knots_u = std::move(ku);
      mults_u = std::move(mu);
      knots_v = std::move(kv);
      mults_v = std::move(mv);
    }

    int degree_u = 0, degree_v = 0;
    std::vector<std::vector<Point *>> net;     // net[u][v]
    std::vector<std::vector<double>> weights;  // empty for a polynomial patch

  private:
    static std::string int_list(const std::vector<int>& v)
    {
      std::string out = "(";
      for (std::size_t i = 0; i < v.size(); i++) out += (i ? "," : "") + std::to_string(v[i]);
      return out + ")";
    }
    static std::string real_list(const std::vector<double>& v)
    {
      std::string out = "(";
      for (std::size_t i = 0; i < v.size(); i++) out += (i ? "," : "") + step_real(v[i]);
      return out + ")";
    }

    /*! The four lists, in the order ISO 10303-42 gives them. */
    std::string knot_text() const
    {
      if (!knots_u.empty() && !knots_v.empty()) {
        return int_list(mults_u) + "," + int_list(mults_v) + "," + real_list(knots_u) + "," +
               real_list(knots_v);
      }
      const std::string mult_u =
        "(" + std::to_string(degree_u + 1) + "," + std::to_string(degree_u + 1) + ")";
      const std::string mult_v =
        "(" + std::to_string(degree_v + 1) + "," + std::to_string(degree_v + 1) + ")";
      return mult_u + "," + mult_v + ",(0.,1.),(0.,1.)";
    }

    std::vector<double> knots_u, knots_v;  // empty for a Bezier
    std::vector<int> mults_u, mults_v;
  };

  /*! One boundary curve of such a patch: a row or a column of its net. */
  class BSplineCurve : public RoundType
  {
  public:
    BSplineCurve(std::vector<Entity *>& ent_list) : RoundType(ent_list) {}
    BSplineCurve(std::vector<Entity *>& ent_list, std::string name_in, std::vector<Point *> pts_in,
                 std::vector<double> weights_in)
      : RoundType(ent_list)
    {
      label = std::move(name_in);
      pts = std::move(pts_in);
      weights = std::move(weights_in);
    }
    BSplineCurve(std::vector<Entity *>& ent_list, std::string name_in, std::vector<Point *> pts_in)
      : RoundType(ent_list)
    {
      label = std::move(name_in);
      pts = std::move(pts_in);
    }
    virtual ~BSplineCurve() {}

    virtual void serialize(std::ostream& stream_in)
    {
      const int degree = int(pts.size()) - 1;
      std::ostringstream ctrl;
      for (std::size_t i = 0; i < pts.size(); i++) ctrl << (i ? ",#" : "#") << pts[i]->id;

      if (weights.empty()) {
        stream_in << "#" << id << " = B_SPLINE_CURVE_WITH_KNOTS('" << label << "'," << degree << ",("
                  << ctrl.str() << "),.UNSPECIFIED.,.F.,.F.,(" << degree + 1 << "," << degree + 1
                  << "),(0.,1.),.UNSPECIFIED.);\n";
        return;
      }

      // The boundary of a rational patch is a rational curve of the patch's own
      // weights, so it takes the same complex instance treatment. See
      // BSplineSurface::serialize().
      std::ostringstream wts;
      for (std::size_t i = 0; i < weights.size(); i++) {
        wts << (i ? "," : "") << step_real(weights[i]);
      }
      stream_in << "#" << id << " = ( BOUNDED_CURVE() B_SPLINE_CURVE(" << degree << ",(" << ctrl.str()
                << "),.UNSPECIFIED.,.F.,.F.) B_SPLINE_CURVE_WITH_KNOTS((" << degree + 1 << ","
                << degree + 1 << "),(0.,1.),.UNSPECIFIED.) CURVE()"
                << " GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_CURVE((" << wts.str() << "))"
                << " REPRESENTATION_ITEM('" << label << "') );\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    std::vector<Point *> pts;
    std::vector<double> weights;  // empty for a polynomial curve
  };

  class Circle : public RoundType
  {
  public:
    Circle(std::vector<Entity *>& ent_list) : RoundType(ent_list)
    {
      axis = 0;
      r = 0;
    }

    Circle(std::vector<Entity *>& ent_list, std::string name_in, Axis2Placement *axis_in, double r_in)
      : RoundType(ent_list)
    {
      name = name_in;
      axis = axis_in;
      r = r_in;
    }
    virtual ~Circle() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = CIRCLE('" << label << "',#" << axis->id << "," << step_real(r)
                << ");\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p_id;
      ss >> p_id >> r;
      axis = dynamic_cast<Axis2Placement *>(ent_map[p_id]);
    }
    std::string name;
    double r;
    Axis2Placement *axis;
  };

  /*! The section of a cylinder by a plane which is not perpendicular to its
   * axis.
   *
   * semi_axis_1 runs along the placement's reference direction and
   * semi_axis_2 along the other in-plane direction, so a rim of radius r cut
   * at an angle whose plane normal makes cos t with the axis is written with
   * the reference direction along the steepest ascent, semi_axis_1 = r/cos t
   * and semi_axis_2 = r.
   *
   * ISO 10303 also allows the trim to carry a pcurve in the cylinder's own
   * (theta, z) parameterisation, where an ellipse unrolls to a sinusoid and
   * has to be written as a B-spline. That is not emitted here: it is
   * derivable from the 3D curve, and a kernel reading this file recovers it
   * to 2e-6 of the radius, which is inside the mesh's own tessellation band.
   * See doc/step-export-status.md. */
  class Ellipse : public RoundType
  {
  public:
    Ellipse(std::vector<Entity *>& ent_list) : RoundType(ent_list)
    {
      axis = 0;
      semi_1 = 0;
      semi_2 = 0;
    }

    Ellipse(std::vector<Entity *>& ent_list, std::string name_in, Axis2Placement *axis_in,
            double semi_1_in, double semi_2_in)
      : RoundType(ent_list)
    {
      name = name_in;
      axis = axis_in;
      semi_1 = semi_1_in;
      semi_2 = semi_2_in;
    }
    virtual ~Ellipse() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = ELLIPSE('" << label << "',#" << axis->id << "," << step_real(semi_1)
                << "," << step_real(semi_2) << ");\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p_id;
      ss >> p_id >> semi_1 >> semi_2;
      axis = dynamic_cast<Axis2Placement *>(ent_map[p_id]);
    }
    std::string name;
    double semi_1, semi_2;
    Axis2Placement *axis;
  };

  class OrientedEdge;

  class EdgeLoop : public Entity
  {
  public:
    EdgeLoop(std::vector<Entity *>& ent_list) : Entity(ent_list) {}
    EdgeLoop(std::vector<Entity *>& ent_list, std::vector<OrientedEdge *>& edges_in) : Entity(ent_list)
    {
      faces = edges_in;
    }
    virtual ~EdgeLoop() {}

    virtual void serialize(std::ostream& stream_in)
    {
      // #17 = ADVANCED_FACE('', (#18), #32, .T.);
      stream_in << "#" << id << " = EDGE_LOOP('" << label << "', (";
      for (size_t i = 0; i < faces.size(); i++) {
        stream_in << "#" << faces[i]->id;
        if (i != faces.size() - 1) stream_in << ",";
      }
      stream_in << "));\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of('(');
      auto en = args.find_last_of(')');
      auto arg_str = args.substr(st + 1, en - st - 1);
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      auto vals = tokenize(arg_str);
      for (auto v : vals) {
        int id = std::atoi(v.c_str());
        faces.push_back(dynamic_cast<OrientedEdge *>(ent_map[id]));
      }
      // axis = dynamic_cast<Axis2Placement*>(ent_map[p_id]);
    }
    std::vector<OrientedEdge *> faces;
  };

  class FaceBound : public Entity
  {
  public:
    FaceBound(std::vector<Entity *>& ent_list) : Entity(ent_list)
    {
      edgeLoop = 0;
      dir = true;
      outer = false;
    }
    FaceBound(std::vector<Entity *>& ent_list, EdgeLoop *edge_loop_in, bool dir_in,
              bool outer_in = false)
      : Entity(ent_list)
    {
      edgeLoop = edge_loop_in;
      dir = dir_in;
      outer = outer_in;
    }
    virtual ~FaceBound() {}

    virtual void serialize(std::ostream& stream_in)
    {
      // The outer loop of a face has to be tagged as FACE_OUTER_BOUND, otherwise
      // importers have to guess which of the bounds is the perimeter and which
      // ones are the holes.
      stream_in << "#" << id << " = " << (outer ? "FACE_OUTER_BOUND" : "FACE_BOUND") << "('" << label
                << "', #" << edgeLoop->id << "," << (dir ? ".T." : ".F.") << ");\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p_id;
      std::string tf;
      ss >> p_id >> tf;

      edgeLoop = dynamic_cast<EdgeLoop *>(ent_map[p_id]);
      dir = (tf == ".T.");
    }
    EdgeLoop *edgeLoop;
    bool dir;
    bool outer;
  };

  class Face : public Entity
  {
  public:
    Face(std::vector<Entity *>& ent_list) : Entity(ent_list)
    {
      dir = true;
      surface = 0;
    }
    Face(std::vector<Entity *>& ent_list, std::vector<FaceBound *> face_bounds_in,
         SurfaceType *surface_in, bool dir_in)
      : Entity(ent_list)
    {
      faceBounds = face_bounds_in;
      dir = dir_in;
      surface = surface_in;
    }
    virtual ~Face() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = ADVANCED_FACE('" << label << "', (";
      for (size_t i = 0; i < faceBounds.size(); i++) {
        stream_in << "#" << faceBounds[i]->id;
        if (i != faceBounds.size() - 1) stream_in << ",";
      }
      stream_in << "),#" << surface->id << "," << (dir ? ".T." : ".F.") << ");\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of('(');
      auto en = args.find_last_of(')');
      auto arg_str = args.substr(st + 1, en - st - 1);
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      auto vals = tokenize(arg_str);
      for (auto v : vals) {
        int id = std::atoi(v.c_str());
        faceBounds.push_back(dynamic_cast<FaceBound *>(ent_map[id]));
      }
      auto remaining = args.substr(en + 1);
      std::replace(remaining.begin(), remaining.end(), '#', ' ');
      std::replace(remaining.begin(), remaining.end(), ',', ' ');
      std::stringstream ss(remaining);
      int p_id;
      std::string tf;
      ss >> p_id >> tf;

      surface = dynamic_cast<SurfaceType *>(ent_map[p_id]);
      dir = (tf == ".T.");
    }

    std::vector<FaceBound *> faceBounds;
    bool dir;
    SurfaceType *surface;
  };

  class Shell : public Entity
  {
  public:
    Shell(std::vector<Entity *>& ent_list) : Entity(ent_list) { isOpen = true; }
    Shell(std::vector<Entity *>& ent_list, std::vector<Face *>& faces_in) : Entity(ent_list)
    {
      faces = faces_in;
      isOpen = true;
    }
    virtual ~Shell() {}

    virtual void serialize(std::ostream& stream_in)
    {
      if (isOpen) stream_in << "#" << id << " = OPEN_SHELL('" << label << "',(";
      else stream_in << "#" << id << " = CLOSED_SHELL('" << label << "',(";

      for (size_t i = 0; i < faces.size(); i++) {
        stream_in << "#" << faces[i]->id;
        if (i != faces.size() - 1) stream_in << ",";
      }
      stream_in << "));\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of('(');
      auto en = args.find_last_of(')');
      auto arg_str = args.substr(st + 1, en - st - 1);
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      auto vals = tokenize(arg_str);
      for (auto v : vals) {
        int id = std::atoi(v.c_str());
        faces.push_back(dynamic_cast<Face *>(ent_map[id]));
      }
    }

    std::vector<Face *> faces;
    bool isOpen;
  };

  class ShellModel : public Entity
  {
  public:
    ShellModel(std::vector<Entity *>& ent_list) : Entity(ent_list) {}
    ShellModel(std::vector<Entity *>& ent_list, std::vector<Shell *> shells_in) : Entity(ent_list)
    {
      shells = shells_in;
    }
    virtual ~ShellModel() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = SHELL_BASED_SURFACE_MODEL('" << label << "', (";
      for (size_t i = 0; i < shells.size(); i++) {
        stream_in << "#" << shells[i]->id;
        if (i != shells.size() - 1) stream_in << ",";
      }
      stream_in << "));\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of('(');
      auto en = args.find_last_of(')');
      auto arg_str = args.substr(st + 1, en - st - 1);
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      auto vals = tokenize(arg_str);
      for (auto v : vals) {
        int id = std::atoi(v.c_str());
        shells.push_back(dynamic_cast<Shell *>(ent_map[id]));
      }
    }

    std::vector<Shell *> shells;
  };

  class ManifoldShape : public Entity
  {
  public:
    ManifoldShape(std::vector<Entity *>& ent_list) : Entity(ent_list)
    {
      axis = 0;
      shellModel = 0;
    }
    ManifoldShape(std::vector<Entity *>& ent_list, Axis2Placement *axis_in, ShellModel *shell_model_in)
      : Entity(ent_list)
    {
      axis = axis_in;
      shellModel = shell_model_in;
    }
    virtual ~ManifoldShape() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = MANIFOLD_SURFACE_SHAPE_REPRESENTATION('" << label << "', (#"
                << axis->id << ", #" << shellModel->id << "));\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of('(');
      auto en = args.find_last_of(')');
      auto arg_str = args.substr(st + 1, en - st - 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p1_id, p2_id;
      ss >> p1_id >> p2_id;

      axis = dynamic_cast<Axis2Placement *>(ent_map[p1_id]);
      shellModel = dynamic_cast<ShellModel *>(ent_map[p2_id]);

      if (!axis && !shellModel) {
        axis = dynamic_cast<Axis2Placement *>(ent_map[p2_id]);
        shellModel = dynamic_cast<ShellModel *>(ent_map[p1_id]);
      }
    }

    Axis2Placement *axis;
    ShellModel *shellModel;
  };

  class ManifoldSolid : public Entity
  {
  public:
    ManifoldSolid(std::vector<Entity *>& ent_list) : Entity(ent_list)
    {
      axis = 0;
      shell = 0;
    }
    ManifoldSolid(std::vector<Entity *>& ent_list, Axis2Placement *axis_in, Shell *shell_in)
      : Entity(ent_list)
    {
      axis = axis_in;
      shell = shell_in;
    }
    virtual ~ManifoldSolid() {}

    virtual void serialize(std::ostream& stream_in)
    {
      if (axis == 0)
        stream_in << "#" << id << " = MANIFOLD_SOLID_BREP('" << label << "',#" << shell->id << ");\n";
      else
        stream_in << "#" << id << " = MANIFOLD_SURFACE_SHAPE_REPRESENTATION('" << label << "', (#"
                  << axis->id << ", #" << shell->id << "));\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of('(');
      auto en = args.find_last_of(')');
      auto arg_str = args.substr(st + 1, en - st - 1);
      //			std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      auto toks = tokenize(arg_str);
      int p1_id = -1, p2_id = -1;
      if (toks.size() >= 1) {
        std::stringstream ss(toks[0]);
        ss >> p1_id;
      }
      if (toks.size() >= 2) {
        std::stringstream ss(toks[1]);
        ss >> p2_id;
      }

      axis = dynamic_cast<Axis2Placement *>(ent_map[p1_id]);
      shell = dynamic_cast<Shell *>(ent_map[p2_id]);

      if (!axis) axis = dynamic_cast<Axis2Placement *>(ent_map[p1_id]);
      if (!shell) {
        shell = dynamic_cast<Shell *>(ent_map[p2_id]);
        printf("t %p\n", ent_map[p2_id]);
      }
    }

    Axis2Placement *axis;
    Shell *shell;
  };

  class Vertex : public Entity
  {
  public:
    Vertex(std::vector<Entity *>& ent_list) : Entity(ent_list) { point = 0; }
    Vertex(std::vector<Entity *>& ent_list, Point *point_in) : Entity(ent_list) { point = point_in; }
    virtual ~Vertex() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = VERTEX_POINT('" << label << "', #" << point->id << ");\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p1_id;
      ss >> p1_id;

      point = dynamic_cast<Point *>(ent_map[p1_id]);
    }

    Point *point;
  };

  class Line;

  class SurfaceCurve : public RoundType
  {
  public:
    SurfaceCurve(std::vector<Entity *>& ent_list) : RoundType(ent_list) { line = 0; }
    SurfaceCurve(std::vector<Entity *>& ent_list, Line *surface_curve_in) : RoundType(ent_list)
    {
      line = surface_curve_in;
    }
    virtual ~SurfaceCurve() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = SURFACE_CURVE('" << label << "', #" << line->id << ");\n";
    }
    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p1_id;
      ss >> p1_id;

      line = dynamic_cast<Line *>(ent_map[p1_id]);
    }

    Line *line;
  };

  class EdgeCurve : public Entity
  {
  public:
    EdgeCurve(std::vector<Entity *>& ent_list) : Entity(ent_list)
    {
      vert1 = 0;
      vert2 = 0;
      dir = true;
    }
    EdgeCurve(std::vector<Entity *>& ent_list, Vertex *vert1_in, Vertex *vert2_in, RoundType *round_in,
              bool dir_in)
      : Entity(ent_list)
    {
      vert1 = vert1_in;
      vert2 = vert2_in;
      round = round_in;
      dir = dir_in;
    }
    virtual ~EdgeCurve() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = EDGE_CURVE('', #" << vert1->id << ", #" << vert2->id << ",#"
                << round->id << "," << (dir ? ".T." : ".F.") << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p1_id, p2_id, p3_id;
      std::string tf;
      ss >> p1_id >> p2_id >> p3_id >> tf;

      vert1 = dynamic_cast<Vertex *>(ent_map[p1_id]);
      vert2 = dynamic_cast<Vertex *>(ent_map[p2_id]);
      round = dynamic_cast<RoundType *>(ent_map[p3_id]);
      dir = (tf == ".T.");
    }

    Vertex *vert1;
    Vertex *vert2;
    RoundType *round;
    bool dir;
  };

  class OrientedEdge : public Entity
  {
  public:
    OrientedEdge(std::vector<Entity *>& ent_list) : Entity(ent_list)
    {
      edge = 0;
      dir = 0;
    }
    OrientedEdge(std::vector<Entity *>& ent_list, EdgeCurve *edge_curve_in, bool dir_in)
      : Entity(ent_list)
    {
      edge = edge_curve_in;
      dir = dir_in;
    }
    virtual ~OrientedEdge() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = ORIENTED_EDGE('" << label << "',*,*,#" << edge->id << ","
                << (dir ? ".T." : ".F.") << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p1_id;
      std::string s1, s2, tf;
      ss >> s1 >> s2 >> p1_id >> tf;

      edge = dynamic_cast<EdgeCurve *>(ent_map[p1_id]);
      dir = (tf == ".T.");
    }

    bool dir;
    EdgeCurve *edge;
  };

  class Vector : public Entity
  {
  public:
    Vector(std::vector<Entity *>& ent_list) : Entity(ent_list)
    {
      dir = 0;
      length = 0;
    }
    Vector(std::vector<Entity *>& ent_list, Direction *dir_in, double len_in) : Entity(ent_list)
    {
      dir = dir_in;
      length = len_in;
    }
    virtual ~Vector() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = VECTOR('" << label << "',#" << dir->id << "," << step_real(length)
                << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p1_id;
      ss >> p1_id >> length;

      dir = dynamic_cast<Direction *>(ent_map[p1_id]);
    }

    double length;
    Direction *dir;
  };
  // ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) ) and friends.
  class SiUnit : public Entity
  {
  public:
    enum Kind { LENGTH, PLANE_ANGLE, SOLID_ANGLE };
    SiUnit(std::vector<Entity *>& ent_list, Kind kind_in) : Entity(ent_list) { kind = kind_in; }
    virtual ~SiUnit() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = ";
      switch (kind) {
      case LENGTH:      stream_in << "(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT(.MILLI.,.METRE.))"; break;
      case PLANE_ANGLE: stream_in << "(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.))"; break;
      case SOLID_ANGLE: stream_in << "(NAMED_UNIT(*)SI_UNIT($,.STERADIAN.)SOLID_ANGLE_UNIT())"; break;
      }
      stream_in << ";\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    Kind kind;
  };

  // The modelling tolerance. Without it importers fall back to their own
  // default, which is often far tighter than the accuracy of the exported
  // coordinates, and then report gaps between neighbouring faces.
  class UncertaintyMeasure : public Entity
  {
  public:
    UncertaintyMeasure(std::vector<Entity *>& ent_list, SiUnit *unit_in, double value_in)
      : Entity(ent_list)
    {
      unit = unit_in;
      value = value_in;
    }
    virtual ~UncertaintyMeasure() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(" << step_real(value)
                << "),#" << unit->id << ",'distance_accuracy_value','confusion accuracy');\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    SiUnit *unit;
    double value;
  };

  class GeometricContext : public Entity
  {
  public:
    GeometricContext(std::vector<Entity *>& ent_list, UncertaintyMeasure *uncertainty_in,
                     SiUnit *length_in, SiUnit *angle_in, SiUnit *solid_in)
      : Entity(ent_list)
    {
      uncertainty = uncertainty_in;
      length = length_in;
      angle = angle_in;
      solid = solid_in;
    }
    virtual ~GeometricContext() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = (GEOMETRIC_REPRESENTATION_CONTEXT(3)"
                << "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#" << uncertainty->id << "))"
                << "GLOBAL_UNIT_ASSIGNED_CONTEXT((#" << length->id << ",#" << angle->id << ",#"
                << solid->id << "))"
                << "REPRESENTATION_CONTEXT('Context','3D Context with UNIT and UNCERTAINTY'));\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    UncertaintyMeasure *uncertainty;
    SiUnit *length;
    SiUnit *angle;
    SiUnit *solid;
  };

  class ApplicationContext : public Entity
  {
  public:
    ApplicationContext(std::vector<Entity *>& ent_list) : Entity(ent_list) {}
    virtual ~ApplicationContext() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id
                << " = APPLICATION_CONTEXT('configuration controlled 3d designs of mechanical parts "
                   "and assemblies');\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}
  };

  class ApplicationProtocolDefinition : public Entity
  {
  public:
    ApplicationProtocolDefinition(std::vector<Entity *>& ent_list, ApplicationContext *context_in)
      : Entity(ent_list)
    {
      context = context_in;
    }
    virtual ~ApplicationProtocolDefinition() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id
                << " = APPLICATION_PROTOCOL_DEFINITION('international standard',"
                   "'config_control_design',1994,#"
                << context->id << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    ApplicationContext *context;
  };

  class ProductContext : public Entity
  {
  public:
    ProductContext(std::vector<Entity *>& ent_list, ApplicationContext *context_in) : Entity(ent_list)
    {
      context = context_in;
    }
    virtual ~ProductContext() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = PRODUCT_CONTEXT('',#" << context->id << ",'mechanical');\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    ApplicationContext *context;
  };

  class Product : public Entity
  {
  public:
    Product(std::vector<Entity *>& ent_list, const std::string& name_in, ProductContext *context_in)
      : Entity(ent_list)
    {
      name = name_in;
      context = context_in;
    }
    virtual ~Product() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = PRODUCT('" << step_string(name) << "','" << step_string(name)
                << "','',(#" << context->id << "));\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    std::string name;
    ProductContext *context;
  };

  class ProductDefinitionFormation : public Entity
  {
  public:
    ProductDefinitionFormation(std::vector<Entity *>& ent_list, Product *product_in) : Entity(ent_list)
    {
      product = product_in;
    }
    virtual ~ProductDefinitionFormation() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE('','',#"
                << product->id << ",.NOT_KNOWN.);\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    Product *product;
  };

  class ProductDefinitionContext : public Entity
  {
  public:
    ProductDefinitionContext(std::vector<Entity *>& ent_list, ApplicationContext *context_in)
      : Entity(ent_list)
    {
      context = context_in;
    }
    virtual ~ProductDefinitionContext() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = PRODUCT_DEFINITION_CONTEXT('part definition',#" << context->id
                << ",'design');\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    ApplicationContext *context;
  };

  class ProductRelatedProductCategory : public Entity
  {
  public:
    ProductRelatedProductCategory(std::vector<Entity *>& ent_list, Product *product_in)
      : Entity(ent_list)
    {
      product = product_in;
    }
    virtual ~ProductRelatedProductCategory() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = PRODUCT_RELATED_PRODUCT_CATEGORY('part','',(#" << product->id
                << "));\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    Product *product;
  };

  class ProductDefinition : public Entity
  {
  public:
    ProductDefinition(std::vector<Entity *>& ent_list) : Entity(ent_list)
    {
      formation = 0;
      context = 0;
    }
    ProductDefinition(std::vector<Entity *>& ent_list, ProductDefinitionFormation *formation_in,
                      ProductDefinitionContext *context_in)
      : Entity(ent_list)
    {
      formation = formation_in;
      context = context_in;
    }
    virtual ~ProductDefinition() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = PRODUCT_DEFINITION('design','',#" << formation->id << ",#"
                << context->id << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    ProductDefinitionFormation *formation;
    ProductDefinitionContext *context;
  };
  class ProductDefinitionShape : public Entity
  {
  public:
    ProductDefinitionShape(std::vector<Entity *>& ent_list, ProductDefinition *prod_in)
      : Entity(ent_list)
    {
      prod = prod_in;
    }
    virtual ~ProductDefinitionShape() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = PRODUCT_DEFINITION_SHAPE('', '', #" << prod->id << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    ProductDefinition *prod;
  };

  class ShapeRepresentation : public Entity
  {
  public:
    ShapeRepresentation(std::vector<Entity *>& ent_list, const std::string& name_in,
                        Axis2Placement *axis_in, GeometricContext *context_in)
      : Entity(ent_list)
    {
      name = name_in;
      axis = axis_in;
      context = context_in;
    }
    virtual ~ShapeRepresentation() {}

    virtual void serialize(std::ostream& stream_in)
    {
      // SHAPE_REPRESENTATION(name, items, context_of_items) - all three
      // arguments are mandatory.
      stream_in << "#" << id << " = SHAPE_REPRESENTATION('" << step_string(name) << "',(#" << axis->id
                << "),#" << context->id << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}

    std::string name;
    Axis2Placement *axis;
    GeometricContext *context;
  };
  class ShapeDefinition_Representation : public Entity
  {
  public:
    ShapeDefinition_Representation(std::vector<Entity *>& ent_list,
                                   ProductDefinitionShape *prod_shape_in, ShapeRepresentation *repr_in)
      : Entity(ent_list)
    {
      repr = repr_in;
      prod_shape = prod_shape_in;
    }
    virtual ~ShapeDefinition_Representation() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = SHAPE_DEFINITION_REPRESENTATION(#" << prod_shape->id << " ,#"
                << repr->id << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}
    ProductDefinitionShape *prod_shape;
    ShapeRepresentation *repr;

    double length;
    Direction *dir;
  };

  class AdvancesBrepRepresentation : public Entity
  {
  public:
    AdvancesBrepRepresentation(std::vector<Entity *>& ent_list, const std::string& name_in,
                               const std::vector<ManifoldSolid *>& solids_in,
                               GeometricContext *context_in)
      : Entity(ent_list)
    {
      name = name_in;
      solids = solids_in;
      context = context_in;
    }
    virtual ~AdvancesBrepRepresentation() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = ADVANCED_BREP_SHAPE_REPRESENTATION('" << step_string(name) << "',(";
      for (size_t i = 0; i < solids.size(); i++) {
        if (i) stream_in << ",";
        stream_in << "#" << solids[i]->id;
      }
      stream_in << "),#" << context->id << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}
    std::string name;
    std::vector<ManifoldSolid *> solids;
    GeometricContext *context;
  };

  class ShapeRepresentationRelationShip : public Entity
  {
  public:
    ShapeRepresentationRelationShip(std::vector<Entity *>& ent_list, ShapeRepresentation *shape_repr_in,
                                    AdvancesBrepRepresentation *adv_brep_in)
      : Entity(ent_list)
    {
      shape_repr = shape_repr_in;
      adv_brep = adv_brep_in;
    }
    virtual ~ShapeRepresentationRelationShip() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = SHAPE_REPRESENTATION_RELATIONSHIP('', '', #" << shape_repr->id
                << ", #" << adv_brep->id << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args) {}
    ShapeRepresentation *shape_repr;
    AdvancesBrepRepresentation *adv_brep;
  };

  class Line : public RoundType
  {
  public:
    Line(std::vector<Entity *>& ent_list) : RoundType(ent_list)
    {
      vector = 0;
      point = 0;
    }
    Line(std::vector<Entity *>& ent_list, Point *point_in, Vector *vec_in) : RoundType(ent_list)
    {
      vector = vec_in;
      point = point_in;
    }
    virtual ~Line() {}

    virtual void serialize(std::ostream& stream_in)
    {
      stream_in << "#" << id << " = LINE('" << label << "',#" << point->id << ", #" << vector->id
                << ");\n";
    }

    virtual void parse_args(std::map<int, Entity *>& ent_map, std::string args)
    {
      auto st = args.find_first_of(',');
      auto arg_str = args.substr(st + 1);
      std::replace(arg_str.begin(), arg_str.end(), ',', ' ');
      std::replace(arg_str.begin(), arg_str.end(), '#', ' ');
      std::stringstream ss(arg_str);
      int p1_id, p2_id;
      ss >> p1_id >> p2_id;

      point = dynamic_cast<Point *>(ent_map[p1_id]);
      vector = dynamic_cast<Vector *>(ent_map[p2_id]);
    }

    Point *point;
    Vector *vector;
  };

public:
  StepKernel();
  virtual ~StepKernel();

  StepKernel::EdgeCurve *create_line_edge_curve(StepKernel::Vertex *vert1, StepKernel::Vertex *vert2,
                                                bool dir);
  StepKernel::EdgeCurve *create_arc_edge_curve(StepKernel::Vertex *vert1, StepKernel::Vertex *vert2,
                                               bool dir);

  void build_tri_body(const char *name, const std::vector<Vector3d>& vertices,
                      const std::vector<IndexedFace>& faces,
                      const std::vector<std::shared_ptr<Curve>>& curves,
                      const std::vector<std::shared_ptr<Surface>>& surfaces,
                      const std::vector<int>& faceParents, const std::vector<Vector4d>& faceNormals,
                      double tol, bool analytic = false, bool approximate = false);
  EdgeCurve *get_line_from_map(std::map<std::pair<int, int>, StepKernel::EdgeCurve *>& edge_map,
                               int ind1, int ind2, StepKernel::Vertex *vert1, StepKernel::Vertex *vert2,
                               bool& edge_dir, int& merge_cnt);
  std::string read_line(std::ifstream& stp_file, bool skip_all_space);
  void read_step(std::string file_name);
  std::vector<Entity *> entities;
};
