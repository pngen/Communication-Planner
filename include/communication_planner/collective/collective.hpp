#pragma once
#include "communication_planner/planner/planner.hpp"
#include "communication_planner/planner/snapshot.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/model/request.hpp"

namespace communication_planner {
// Collective communication-shape plan construction (not collective scheduling).
PlanOutcome planCollective(const CommunicationRequest& req,
                           const EvidenceSnapshot& snap,
                           const RankingWeights& weights,
                           const Bounds& bounds);
}
