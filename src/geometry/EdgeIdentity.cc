/*! Which maker's edge each edge of a 2D outline is - see EdgeIdentity.h. */
#include "geometry/EdgeIdentity.h"

#include <atomic>

namespace {

constexpr std::uint64_t kIdMask = 0x7fffffffu;

EdgeId firstOf(std::int64_t z)
{
  return EdgeId((std::uint64_t(z) >> 32) & kIdMask);
}
EdgeId secondOf(std::int64_t z)
{
  return EdgeId(std::uint64_t(z) & kIdMask);
}

}  // namespace

EdgeId reserveEdgeIds(std::size_t n)
{
  // Ids wrap after 2^31 - 1 edges. Two edges would then share an id only if
  // one made that long ago were in the same operation as one made now.
  static std::atomic<std::uint32_t> next{1};
  std::uint32_t first = next.fetch_add(std::uint32_t(n)) & std::uint32_t(kIdMask);
  if (first == 0) first = next.fetch_add(std::uint32_t(n)) & std::uint32_t(kIdMask);
  return first;
}

std::int64_t packEdges(EdgeId a, EdgeId b)
{
  return std::int64_t(((std::uint64_t(a) & kIdMask) << 32) | (std::uint64_t(b) & kIdMask));
}

EdgeId commonEdge(std::int64_t za, std::int64_t zb)
{
  const EdgeId a[2] = {firstOf(za), secondOf(za)};
  const EdgeId b[2] = {firstOf(zb), secondOf(zb)};
  EdgeId found = 0;
  for (const EdgeId x : a) {
    if (x == 0) continue;
    if (x != b[0] && x != b[1]) continue;
    if (found != 0 && found != x) return 0;  // two edges in common: no one edge between them
    found = x;
  }
  return found;
}
