/*! The rims of a set of facets on a declared surface of revolution - see Rims.h. */
#include "geometry/recognition/Rims.h"

#include <algorithm>

#include "geometry/Surface.h"
#include "geometry/recognition/Exact.h"

namespace recognition {

namespace {

/*! A vertex's place on a surface of revolution: its level along the declared
 * axis and its radius about it, each with the forward error of computing it. */
struct AxialPlace {
  double t = 0, rho = 0;
  double t_err = 0, rho_err = 0;
};

AxialPlace placeOf(const Surface *s, const Vector3d& p)
{
  AxialPlace out;
  // axis: 9 roundings; p - refpt: 3; the dot: 5 - 17 for the level. The radius
  // adds axis * t (3), the difference (3) and a norm (6) - 29.
  const Vector3d axis = s->normdir.normalized();
  const Vector3d rel = p - s->refpt;
  out.t = rel.dot(axis);
  out.rho = (rel - axis * out.t).norm();
  const double scale = p.norm() + s->refpt.norm();
  out.t_err = forwardError(17, scale);
  out.rho_err = forwardError(29, scale);
  return out;
}

/*! Group values that are equal at machine precision: sorted, a value within the
 * two values' combined error bounds of the group's first joins it. Returns, per
 * input, its group's index; groups ascend with the values. */
std::vector<std::size_t> groupEqual(const std::vector<double>& value, const std::vector<double>& err)
{
  std::vector<std::size_t> order(value.size());
  for (std::size_t i = 0; i < order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(),
            [&](std::size_t a, std::size_t b) { return value[a] < value[b]; });
  std::vector<std::size_t> group(value.size(), 0);
  std::size_t g = 0, first = 0;
  for (std::size_t k = 0; k < order.size(); k++) {
    const std::size_t i = order[k];
    if (k == 0) {
      first = i;
    } else if (value[i] - value[first] > err[i] + err[first]) {
      g++;
      first = i;
    }
    group[i] = g;
  }
  return group;
}

}  // namespace

Rims rimsOf(const Mesh& mesh, const Surface *s, const std::vector<std::size_t>& facets)
{
  const std::vector<Vector3d>& vertices = *mesh.vertices;
  const std::vector<std::vector<int>>& loops = *mesh.loops;
  Rims out;
  for (const std::size_t f : facets)
    out.vertices.insert(out.vertices.end(), loops[f].begin(), loops[f].end());
  std::sort(out.vertices.begin(), out.vertices.end());
  out.vertices.erase(std::unique(out.vertices.begin(), out.vertices.end()), out.vertices.end());

  std::vector<double> t, t_err, rho, rho_err;
  for (const int v : out.vertices) {
    const AxialPlace place = placeOf(s, vertices[v]);
    t.push_back(place.t);
    t_err.push_back(place.t_err);
    rho.push_back(place.rho);
    rho_err.push_back(place.rho_err);
  }
  out.level = groupEqual(t, t_err);
  for (const std::size_t l : out.level) out.levels = std::max(out.levels, l + 1);

  // Within each level, by radius.
  std::map<std::size_t, std::vector<std::size_t>> at_level;
  for (std::size_t i = 0; i < out.vertices.size(); i++) at_level[out.level[i]].push_back(i);
  std::size_t next = 0;
  for (const auto& kv : at_level) {
    std::vector<double> rr, re;
    for (const std::size_t i : kv.second) {
      rr.push_back(rho[i]);
      re.push_back(rho_err[i]);
    }
    const std::vector<std::size_t> g = groupEqual(rr, re);
    std::size_t most = 0;
    for (std::size_t k = 0; k < kv.second.size(); k++) {
      out.rim[out.vertices[kv.second[k]]] = next + g[k];
      most = std::max(most, g[k]);
    }
    next += most + 1;
  }
  return out;
}

}  // namespace recognition
