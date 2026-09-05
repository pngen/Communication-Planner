#pragma once

#include <optional>
#include <vector>
#include <string>
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/model/request.hpp"
#include "communication_planner/model/plan.hpp"
#include "communication_planner/path/hop.hpp"
#include "communication_planner/model/component.hpp"
#include "communication_planner/feasibility/feasibility.hpp"
#include "communication_planner/planner/snapshot.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/ranking/weights.hpp"

namespace communication_planner {

// Report of a single candidate path after filtering (and costing when feasible).
struct CandidateReport {
  CandidatePath path;
  FeasibilityResult feasibility;   // full result; feasible() is the gate
  CostSummary cost;                // valid only when feasible
  std::string detail;
  Provenance provenance{Provenance::UNKNOWN};
};

// Outcome of a planning request.
struct PlanOutcome {
  bool success{false};
  std::optional<CommunicationPlan> plan;
  std::vector<CandidateReport> candidates;   // in enumeration order
  std::vector<std::size_t> rankedIndices;     // best-first, feasible only
  std::vector<std::string> explanations;
};

// Bind the authority/dynamic generations that a plan is valid for.
AuthorityGeneration currentAuthority();

// The planner: request + snapshot + weights + bounds -> PlanOutcome.
PlanOutcome planCommunication(const CommunicationRequest& request,
                              const EvidenceSnapshot& snapshot,
                              const RankingWeights& weights,
                              const Bounds& bounds);

}  // namespace communication_planner
