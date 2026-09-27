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

#include <memory>
#include <vector>

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
std::shared_ptr<const DeclaredSolid> solidOfHullOfRevolution(
  const std::vector<std::vector<std::shared_ptr<Surface>>>& operands);
