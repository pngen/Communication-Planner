#pragma once

#include <string>
#include "communication_planner/core/enums.hpp"
#include "communication_planner/model/request.hpp"
#include "communication_planner/model/evidence.hpp"
#include "communication_planner/planner/snapshot.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/path/hop.hpp"

namespace communication_planner {

// Hard feasibility result. FEASIBLE only when every hard constraint is proven.
struct FeasibilityResult {
  Feasibility outcome{Feasibility::UNKNOWN};
  std::string detail;

  bool feasible() const noexcept { return outcome == Feasibility::FEASIBLE; }

  static FeasibilityResult ok() { return FeasibilityResult{Feasibility::FEASIBLE, ""}; }
  static FeasibilityResult reject(Feasibility f, std::string d) {
    return FeasibilityResult{f, std::move(d)};
  }
};

// Apply hard feasibility filtering to a candidate path. Runs BEFORE ranking;
// a hard-invalid path must never survive because of a good score.
FeasibilityResult evaluateCandidateFeasibility(const CommunicationRequest& req,
                                               const EvidenceSnapshot& snap,
                                               const CandidatePath& cand,
                                               const Bounds& bounds);

// Helper: is an endpoint current/fresh (not stale incarnation)?
bool endpointFresh(const Endpoint& e, const EvidenceSnapshot& snap);

// Transport compatibility check with allowed/forbidden sets.
FeasibilityResult transportAllowed(const CommunicationRequest& req, Transport t);

}  // namespace communication_planner
