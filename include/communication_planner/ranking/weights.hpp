#pragma once

#include <map>
#include "communication_planner/core/enums.hpp"

namespace communication_planner {

// Explicit, inspectable ranking weights. Policy is never hidden in one opaque
// score: each named factor carries an explicit weight and provenance.
struct RankingWeights {
  std::map<CostFactor, double> weights;

  void set(CostFactor f, double w) { weights[f] = w; }

  double weightOf(CostFactor f) const {
    auto it = weights.find(f);
    // Sensible default weights so an empty config still ranks deterministically.
    if (it != weights.end()) return it->second;
    switch (f) {
      case CostFactor::TOTAL_EXPECTED_COMPLETION_COST: return 1.0;
      case CostFactor::PATH_LATENCY: return 1.0;
      case CostFactor::EFFECTIVE_BANDWIDTH: return 1.0;
      case CostFactor::RESIDUAL_BANDWIDTH: return 1.0;
      case CostFactor::CONGESTION_PENALTY: return 1.0;
      case CostFactor::QUEUEING_ESTIMATE: return 1.0;
      case CostFactor::HOP_COUNT: return 0.5;
      case CostFactor::STAGE_COUNT: return 1.0;
      case CostFactor::STAGING_OVERHEAD: return 1.0;
      case CostFactor::HOST_STAGING_COST: return 1.0;
      case CostFactor::NUMA_DISTANCE: return 1.0;
      case CostFactor::PCIE_LOCALITY: return 1.0;
      case CostFactor::NIC_LOCALITY: return 1.0;
      case CostFactor::STORAGE_LOCALITY: return 1.0;
      case CostFactor::ENDPOINT_HEALTH: return 1.0;
      case CostFactor::RESERVATION_FIT: return 1.0;
      case CostFactor::CAPACITY_HEADROOM: return 1.0;
      case CostFactor::FAILURE_DOMAIN_RISK: return 1.0;
      case CostFactor::RELIABILITY: return 1.0;
      case CostFactor::MOVEMENT_COST: return 1.0;
      case CostFactor::POLICY_PREFERENCE: return 1.0;
      case CostFactor::UNKNOWN: return 0.0;
    }
    return 1.0;
  }

  bool allWeightsFinite() const {
    for (const auto& [k, w] : weights) {
      if (w < 0.0 || w != w || w > 1e308) return false;
    }
    return true;
  }
};

}  // namespace communication_planner
