// The declaration census's classes - see Reports.h. Each expectation is read
// off what a claim names: a declaration, a fit past them, or nothing at all.
#include "geometry/recognition/Reports.h"

#include <catch2/catch_all.hpp>
#include <cstddef>

using recognition::heldClass;

TEST_CASE("a held facet is as good as the surface its claim names", "[reports]")
{
  // Three declarations: indices 0 to 2 are the model's, 3 and on are fits.
  CHECK(heldClass(0, 3) == recognition::kDeclaredSurface);
  CHECK(heldClass(2, 3) == recognition::kDeclaredSurface);
  CHECK(heldClass(3, 3) == recognition::kFitted);
  CHECK(heldClass(7, 3) == recognition::kFitted);
}

TEST_CASE("a claim that names no surface is malformed, not a fit", "[reports]")
{
  // std::size_t(-1) is the ledger's "cannot name one" (Ledger.h). Counted as a
  // fit, the census's EXPORT-ERROR for it could never fire, and did not from
  // the day it was written (open item 39, R28).
  CHECK(heldClass(std::size_t(-1), 3) == recognition::kUnnamed);
  CHECK(heldClass(std::size_t(-1), 0) == recognition::kUnnamed);
}
