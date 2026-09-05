#pragma once
#include "communication_planner/planner/planner.hpp"
#include "communication_planner/planner/snapshot.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/model/request.hpp"

namespace communication_planner {
// Multicast-style plan construction: one source, many destinations.
PlanOutcome planMulticast(const CommunicationRequest& req,
                          const EvidenceSnapshot& snap,
                          const RankingWeights& weights,
                          const Bounds& bounds);
}
