#pragma once
// Internal planner helpers shared by planner.cpp / multicast.cpp / collective.cpp.
// NOT part of the installed public API.
#include "communication_planner/path/hop.hpp"
#include "communication_planner/planner/snapshot.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/planner/planner.hpp"
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/model/request.hpp"
#include "communication_planner/model/stage.hpp"
#include "communication_planner/model/path.hpp"
#include "communication_planner/model/component.hpp"

namespace communication_planner::internal {

std::vector<CandidatePath> enumerateCandidatePaths(const EvidenceSnapshot& snap,
                                                   EndpointId src, EndpointId dst,
                                                   const Bounds& bounds);

CandidatePath directCandidate(EndpointId src, EndpointId dst, LinkId link);

ResourceId stagingResourceFor(EndpointId relay);

std::vector<Stage> buildStages(const CandidatePath& cand,
                               const EvidenceSnapshot& snap,
                               const CommunicationRequest& req);

Path makePath(PathId id, const CandidatePath& cand, const EvidenceSnapshot& snap,
              const CommunicationRequest& req, const CostSummary& cost);

std::string provName(Provenance p);

PlanOutcome buildMulticastPlan(const CommunicationRequest& req,
                               const EvidenceSnapshot& snap,
                               const RankingWeights& weights,
                               const Bounds& bounds);

PlanOutcome buildCollectivePlan(const CommunicationRequest& req,
                                const EvidenceSnapshot& snap,
                                const RankingWeights& weights,
                                const Bounds& bounds);

}  // namespace communication_planner::internal
