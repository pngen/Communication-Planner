#pragma once
#include "communication_planner/core/enums.hpp"

namespace communication_planner {

// Guarded, explicit plan lifecycle. Illegal transitions are rejected.
class PlanLifecycle {
 public:
  static bool canTransition(PlanState from, PlanState to) noexcept;
};

}  // namespace communication_planner
