#pragma once

#include "communication_planner/model/component.hpp"
#include "communication_planner/model/request.hpp"
#include "communication_planner/planner/snapshot.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/path/hop.hpp"
#include "communication_planner/ranking/weights.hpp"

namespace communication_planner {

// Lower is better. Every value is in nanos-equivalent units unless otherwise
// stated; provenance is attached per factor. Never emits NaN/Inf.
CostSummary evaluateCandidateCost(const CommunicationRequest& req,
                                  const EvidenceSnapshot& snap,
                                  const CandidatePath& cand,
                                  const RankingWeights& weights,
                                  const Bounds& bounds);

// Finite clamp to prevent overflow from entering ranking.
constexpr double kMaxCost = 1e18;

// Worst (least trustworthy) provenance in a cost summary.
Provenance worstProvenance(const CostSummary& c);

}  // namespace communication_planner
