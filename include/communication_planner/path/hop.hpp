#pragma once

#include <vector>
#include "communication_planner/core/identifiers.hpp"

namespace communication_planner {

// One hop in a candidate path: a directed link between two endpoints.
struct Hop {
  LinkId link;
  EndpointId from;
  EndpointId to;
};

// An enumerated candidate path (sequence of hops), before cost/ranking.
struct CandidatePath {
  EndpointId source;
  EndpointId destination;
  std::vector<Hop> hops;
};

}  // namespace communication_planner
