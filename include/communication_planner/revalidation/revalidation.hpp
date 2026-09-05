#pragma once
#include <vector>
#include <string>
#include "communication_planner/core/enums.hpp"
#include "communication_planner/model/plan.hpp"
#include "communication_planner/planner/snapshot.hpp"

namespace communication_planner {

struct RevalidationResult {
  bool ok{false};
  Feasibility feasibility{Feasibility::REVALIDATION_REQUIRED};
  std::string detail;
  std::vector<std::string> changes;
};

// Mandatory revalidation before execution handoff and fallback activation.
// If any hard fact changed, the plan must be rejected or a fresh plan generation
// produced; an old plan is never silently mutated in place.
RevalidationResult revalidatePlan(const CommunicationPlan& plan,
                                  const EvidenceSnapshot& snap);
RevalidationResult revalidatePath(const Path& path,
                                  const EvidenceSnapshot& snap);

}  // namespace communication_planner
