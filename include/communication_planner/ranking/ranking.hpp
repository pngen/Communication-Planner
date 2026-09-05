#pragma once

#include <cstdint>
#include <vector>
#include "communication_planner/core/enums.hpp"
#include "communication_planner/path/hop.hpp"
#include "communication_planner/model/component.hpp"
#include "communication_planner/feasibility/feasibility.hpp"

namespace communication_planner {

struct Rankable {
  CandidatePath path;
  FeasibilityResult feasibility;
  CostSummary cost;
};

std::vector<std::size_t> rankFeasible(const std::vector<Rankable>& input);

int provenanceRank(Provenance p);

}  // namespace communication_planner
