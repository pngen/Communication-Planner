#pragma once

#include <vector>
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/provenance.hpp"
#include "communication_planner/core/units.hpp"
#include "communication_planner/model/stage.hpp"
#include "communication_planner/model/component.hpp"
#include "communication_planner/core/error.hpp"

namespace communication_planner {

// An explicit ordered, generation-bound path composed of stages.
struct Path {
  PathId id;
  PathGeneration generation;
  EndpointId source;
  EndpointId destination;
  std::vector<Stage> stages;
  Bytes totalPayload{0};       // must equal request payload (exact accounting)
  CostSummary cost;
  Provenance provenance{Provenance::UNKNOWN};
  std::string failureDomain;
  std::string reason;          // rejection detail when not feasible

  bool isEmpty() const noexcept { return stages.empty(); }
  bool isDirect() const noexcept { return stages.size() == 1; }
};

// Enumeration of candidate paths derived from the graph (bounded).
struct PathCandidate {
  Path path;
  Feasibility feasibility{Feasibility::UNKNOWN};
  std::string detail;
};

}  // namespace communication_planner
