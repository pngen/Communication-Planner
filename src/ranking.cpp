#include "communication_planner/ranking/ranking.hpp"
#include "communication_planner/model/component.hpp"
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/ranking/cost.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace communication_planner {

Provenance worstProvenance(const CostSummary& c) {
  Provenance worst = Provenance::MEASURED;
  for (const auto& comp : c.components) {
    if (provenanceRank(comp.provenance) > provenanceRank(worst)) worst = comp.provenance;
  }
  return worst;
}

int provenanceRank(Provenance p) {
  switch (p) {
    case Provenance::MEASURED: return 0;
    case Provenance::REPORTED: return 1;
    case Provenance::DERIVED: return 2;
    case Provenance::ESTIMATED: return 3;
    case Provenance::FORECAST: return 4;
    case Provenance::SYNTHETIC: return 5;
    case Provenance::UNKNOWN: return 6;
  }
  return 6;
}

static double componentValue(const CostSummary& c, CostFactor f, double def) {
  for (const auto& comp : c.components) {
    if (comp.factor == f) return comp.value;
  }
  return def;
}

namespace {
struct Key {
  double total;
  double uncertainty;
  int    prov;
  double hops;
  double congestion;
  double residual;
  std::vector<std::uint64_t> idseq;
};
}  // namespace

static bool keyLess(const Key& a, const Key& b) {
  if (a.total != b.total) return a.total < b.total;
  if (a.uncertainty != b.uncertainty) return a.uncertainty < b.uncertainty;
  if (a.prov != b.prov) return a.prov < b.prov;
  if (a.hops != b.hops) return a.hops < b.hops;
  if (a.congestion != b.congestion) return a.congestion < b.congestion;
  if (a.residual != b.residual) return a.residual > b.residual;
  if (a.idseq.size() != b.idseq.size()) return a.idseq.size() < b.idseq.size();
  return std::lexicographical_compare(a.idseq.begin(), a.idseq.end(),
                                      b.idseq.begin(), b.idseq.end());
}

std::vector<std::size_t> rankFeasible(const std::vector<Rankable>& input) {
  struct Item { std::size_t idx; Key key; };
  std::vector<Item> items;
  items.reserve(input.size());

  for (std::size_t i = 0; i < input.size(); ++i) {
    const Rankable& r = input[i];
    if (!r.feasibility.feasible()) continue;
    Key k;
    k.total = r.cost.totalValid() ? r.cost.total : kMaxCost;
    k.uncertainty = r.cost.uncertaintyCount();
    k.prov = provenanceRank(worstProvenance(r.cost));
    k.hops = componentValue(r.cost, CostFactor::HOP_COUNT, static_cast<double>(r.path.hops.size()));
    k.congestion = componentValue(r.cost, CostFactor::CONGESTION_PENALTY, 0.0);
    k.residual = componentValue(r.cost, CostFactor::RESIDUAL_BANDWIDTH, 0.0);
    k.idseq.reserve(r.path.hops.size());
    for (const Hop& h : r.path.hops) k.idseq.push_back(h.link.value());
    items.push_back(Item{i, std::move(k)});
  }

  std::stable_sort(items.begin(), items.end(),
                   [](const Item& a, const Item& b) { return keyLess(a.key, b.key); });

  std::vector<std::size_t> out;
  out.reserve(items.size());
  for (const Item& it : items) out.push_back(it.idx);
  return out;
}

}  // namespace communication_planner
