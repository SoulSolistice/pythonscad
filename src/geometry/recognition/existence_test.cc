// Whether the model's solid has a face where a facet was made - see
// Existence.h. The facets are built by hand at places whose answer is read off
// the model: a body r = 20, h = 20, bored through at r = 15, and a slot's inner
// cutter wall at r = 14.9 - inside the bore, so the model never shows it
// (examples/step_test/phantom-sliver.scad).
#include "geometry/recognition/Existence.h"

#include <catch2/catch_all.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "geometry/SolidTree.h"
#include "geometry/Surface.h"
#include "geometry/recognition/FacetGraph.h"
#include "geometry/recognition/Provenance.h"

using namespace recognition;
using Op = SolidTree::Op;

namespace {

std::shared_ptr<const SolidTree> cylinder(double r, double z0, double h)
{
  auto s = std::make_shared<DeclaredSolid>();
  const Vector3d z(0, 0, 1);
  s->bounds.push_back(std::make_shared<CylinderSurface>(Vector3d(0, 0, z0), z, r));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, z0), z));
  s->bounds.push_back(std::make_shared<PlaneSurface>(Vector3d(0, 0, z0 + h), z));
  s->interior = Vector3d(0, 0, z0 + h / 2);
  return SolidTree::declared(s);
}

/*! Facets as a recogniser receives them: loops over shared vertices. */
struct Facets {
  std::vector<Vector3d> vertices;
  std::vector<std::vector<int>> loops;
  std::vector<char> valid, is_hole;
  std::vector<Vector3d> normals;
  std::vector<int32_t> made_on;

  /*! A quad on the cylinder of radius r about z, between two angles (degrees)
   * and two heights, made on the record at `surface`. */
  void wall(double r, double a0, double a1, double z0, double z1, int32_t surface)
  {
    const int base = int(vertices.size());
    for (const auto& [a, z] :
         {std::pair{a0, z0}, std::pair{a1, z0}, std::pair{a1, z1}, std::pair{a0, z1}}) {
      vertices.emplace_back(r * std::cos(a * M_PI / 180), r * std::sin(a * M_PI / 180), z);
    }
    add({base, base + 1, base + 2, base + 3}, surface);
  }
  /*! A triangle on the plane z, at three (r, degrees) corners. */
  void cap(double z, std::initializer_list<std::pair<double, double>> corners, int32_t surface)
  {
    std::vector<int> loop;
    for (const auto& [r, a] : corners) {
      loop.push_back(int(vertices.size()));
      vertices.emplace_back(r * std::cos(a * M_PI / 180), r * std::sin(a * M_PI / 180), z);
    }
    add(loop, surface);
  }
  void add(const std::vector<int>& loop, int32_t surface)
  {
    loops.push_back(loop);
    valid.push_back(1);
    is_hole.push_back(0);
    normals.emplace_back(0, 0, 1);
    made_on.push_back(surface);
  }
  [[nodiscard]] Mesh mesh() const { return Mesh{&vertices, &loops, &valid, &is_hole, &normals}; }
};

}  // namespace

TEST_CASE("a facet the model rules out at every point is a phantom", "[existence]")
{
  // The records the facets were made on, as the exporter lists them.
  const std::vector<std::shared_ptr<Surface>> surfaces = {
    std::make_shared<CylinderSurface>(Vector3d(0, 0, 9), Vector3d(0, 0, 1), 14.9),  // the cutter's wall
    std::make_shared<CylinderSurface>(Vector3d(0, 0, -1), Vector3d(0, 0, 1), 15),   // the bore
    std::make_shared<PlaneSurface>(Vector3d(0, 0, 20), Vector3d(0, 0, 1)),          // the body's top
  };
  Facets m;
  m.wall(14.9, 0, 5, 12, 14, 0);                      // the sliver's outer wall: inside the bore
  m.wall(15, 0, 5, 2, 4, 1);                          // the bore's wall below the slot: a face
  m.cap(20, {{16, 0}, {17, 0}, {17, 5}}, 2);          // the top, between bore and body: a face
  m.cap(20, {{14.9, 0}, {14.95, 0}, {14.95, 2}}, 2);  // the top inside the bore: a phantom
  m.wall(15, 10, 15, 2, 4, Provenance::kNone);        // made on no declared surface: not asked

  const FacetGraph graph(m.mesh());
  Provenance provenance;
  provenance.face_surface = &m.made_on;
  const auto part = SolidTree::combine(Op::Difference, cylinder(20, 0, 20), cylinder(15, -1, 22));

  std::vector<const SolidTree *> by;
  std::vector<Open> open;
  const std::vector<Exists> e = existence(graph, surfaces, provenance, part.get(), &by, &open);
  REQUIRE(e.size() == 5);
  // the phantoms were ruled out by the bore that holds them
  CHECK(by[0] == part->children()[1].get());
  CHECK(by[3] == part->children()[1].get());
  CHECK(e[0] == Exists::Phantom);
  CHECK(e[1] == Exists::Face);
  CHECK(e[2] == Exists::Face);
  CHECK(e[3] == Exists::Phantom);
  CHECK(e[4] == Exists::NotAsked);

  // A union with an operand that states nothing rules nothing out: the same
  // facets, all undecided - never guessed.
  const auto blind = SolidTree::combine(Op::Union, part, SolidTree::opaque());
  std::vector<const SolidTree *> ubY;
  std::vector<Open> uopen;
  const std::vector<Exists> u = existence(graph, surfaces, provenance, blind.get(), &ubY, &uopen);
  // and each names the operand that stated nothing
  CHECK(uopen[0] == Open::Stated);
  CHECK(ubY[0] == blind->children()[1].get());
  CHECK(u[0] == Exists::Undecided);
  CHECK(u[1] == Exists::Undecided);
  CHECK(u[3] == Exists::Undecided);

  // And with no boolean carried, nothing is asked at all.
  CHECK(existence(graph, surfaces, provenance, nullptr).empty());
}

TEST_CASE("a facet crossing the model's boundary is undecided, not a phantom", "[existence]")
{
  // A facet of the bore that straddles where a subtracted slot begins: its
  // corners below are on the bore's face, its corners above are in the slot's
  // air. Every point is not ruled out, so it is not a phantom.
  const std::vector<std::shared_ptr<Surface>> surfaces = {
    std::make_shared<CylinderSurface>(Vector3d(0, 0, -1), Vector3d(0, 0, 1), 15)};
  Facets m;
  m.wall(15, 30, 35, 8, 12, 0);
  const FacetGraph graph(m.mesh());
  Provenance provenance;
  provenance.face_surface = &m.made_on;
  // body less bore less a slot r in [14.9, 17] from z = 10 - here a full ring,
  // the part of it that matters at 30 to 35 degrees
  const auto slot = SolidTree::combine(Op::Difference, cylinder(17, 10, 11), cylinder(14.9, 9, 13));
  const auto part = SolidTree::combine(
    Op::Difference, SolidTree::combine(Op::Difference, cylinder(20, 0, 20), cylinder(15, -1, 22)), slot);
  const std::vector<Exists> e = existence(graph, surfaces, provenance, part.get());
  REQUIRE(e.size() == 1);
  CHECK(e[0] != Exists::Phantom);
}
