#pragma once

/*! The surfaces a hull of coaxial solids of revolution makes, stated from its
 * operands' own declarations.
 *
 * hull() declares its operands' records and, until 2026-09-25, nothing of its
 * own - so the commonest chamfer there is, hull(cylinder(r = R),
 * cylinder(r = R - c)), made a cone nobody declared, and every recogniser that
 * met it named it after a rim it is not on (doc/step-export-wip.md, open item
 * 36). But what such a hull makes is fixed in closed form by what its operands
 * declare: each operand that is a solid of revolution states its profile - one
 * lateral surface (a cylinder, or a cone) and the two cap planes across its
 * axis - and the hull of solids of revolution about one axis is the solid of
 * revolution of the convex hull of their profiles. That hull is a profile of
 * straight edges, and what it makes turned is the sweep table's answer
 * (SweptSurfaces.h) - the same rows rotate_extrude answers from: a cone for
 * every sloped edge, and the planes and cylinders the operands declared.
 *
 * Nothing is read from the mesh: radii come from the records, heights from the
 * cap planes, and every comparison is at machine precision
 * (recognition/Exact.h). Where the operands are not all such solids about one
 * axis line, it states nothing - intent it cannot read is not guessed.
 */

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "geometry/linalg.h"

class Surface;
struct DeclaredSolid;

/*! The surfaces the hull of `operands` makes, the hull of their profiles
 * turned about their common axis - or none, where the operands' declarations do not
 * state solids of revolution about one axis. `operands` holds each operand's
 * own declarations, face and evidence records alike; only faces are read. */
std::vector<std::shared_ptr<Surface>> declareHullOfRevolution(
  const std::vector<std::vector<std::shared_ptr<Surface>>>& operands);

/*! The solid a hull of coaxial solids of revolution is, where the operands'
 * records say they are such solids: the intersection of the insides of the
 * surfaces its profile turns - convex, so exactly - with a point inside and
 * its extent. Null where they are not, or where the turned surfaces do not
 * bound every edge of the profile. A caller asks this only of operands that are
 * each one declared primitive: records alone would take a cylinder with an
 * undeclared cut for the whole cylinder. */
/*! Which of the hull's own records (declareHullOfRevolution, returned in
 * `records`) each triangle of the hull's mesh lies on, derived in closed form,
 * or -1. A vertex is identified with the ring it is on - a point on a declared
 * circle, exactly, which a point, having no interior, can be; coaxial rings
 * share no point. A triangle with every vertex on one ring is in that ring's
 * plane, the cap at that level where the hull has one; one whose vertices lie
 * on one edge of the hull's profile, on two or more of its rings, is on that
 * edge's conic - the hull of coaxial circles joins adjacent ones and nothing
 * else. Anything else is none. A derivation, not a maker's word: its names are
 * D1 (mintDerivedTag). Empty where the operands are not coaxial solids of
 * revolution. */
std::vector<int> hullFacetRecords(const std::vector<std::vector<std::shared_ptr<Surface>>>& operands,
                                  const std::vector<Vector3d>& vertices,
                                  const std::vector<std::array<std::size_t, 3>>& triangles,
                                  std::vector<std::shared_ptr<Surface>>& records);

std::shared_ptr<const DeclaredSolid> solidOfHullOfRevolution(
  const std::vector<std::vector<std::shared_ptr<Surface>>>& operands);
