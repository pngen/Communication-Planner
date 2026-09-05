#include "communication_planner/lifecycle/lifecycle.hpp"

namespace communication_planner {

bool PlanLifecycle::canTransition(PlanState from, PlanState to) noexcept {
  auto allowed = [&]() -> bool {
    switch (from) {
      case PlanState::REQUESTED:
        return to == PlanState::DISCOVERING || to == PlanState::CONSTRUCTING ||
               to == PlanState::CANCELLED || to == PlanState::FAILED ||
               to == PlanState::RETIRED;
      case PlanState::DISCOVERING:
        return to == PlanState::CONSTRUCTING || to == PlanState::FILTERING ||
               to == PlanState::CANCELLED || to == PlanState::FAILED ||
               to == PlanState::RETIRED;
      case PlanState::CONSTRUCTING:
        return to == PlanState::FILTERING || to == PlanState::RANKING ||
               to == PlanState::CANCELLED || to == PlanState::FAILED ||
               to == PlanState::RETIRED;
      case PlanState::FILTERING:
        return to == PlanState::RANKING || to == PlanState::CANCELLED ||
               to == PlanState::FAILED || to == PlanState::RETIRED;
      case PlanState::RANKING:
        return to == PlanState::PLAN_READY || to == PlanState::CANCELLED ||
               to == PlanState::FAILED || to == PlanState::RETIRED;
      case PlanState::PLAN_READY:
        return to == PlanState::AWAITING_RESOURCE_COMMIT ||
               to == PlanState::REVALIDATION_REQUIRED || to == PlanState::CANCELLED ||
               to == PlanState::FAILED || to == PlanState::SUPERSEDED ||
               to == PlanState::RETIRED;
      case PlanState::AWAITING_RESOURCE_COMMIT:
        return to == PlanState::COMMITTED || to == PlanState::CANCELLED ||
               to == PlanState::FAILED || to == PlanState::REVALIDATION_REQUIRED ||
               to == PlanState::SUPERSEDED || to == PlanState::RETIRED;
      case PlanState::COMMITTED:
        return to == PlanState::AWAITING_EXECUTION || to == PlanState::CANCELLED ||
               to == PlanState::FAILED || to == PlanState::REVALIDATION_REQUIRED ||
               to == PlanState::SUPERSEDED || to == PlanState::RETIRED;
      case PlanState::AWAITING_EXECUTION:
        return to == PlanState::ACTIVE || to == PlanState::CANCELLED ||
               to == PlanState::FAILED || to == PlanState::REVALIDATION_REQUIRED ||
               to == PlanState::SUPERSEDED || to == PlanState::RETIRED;
      case PlanState::ACTIVE:
        return to == PlanState::COMPLETED || to == PlanState::REVALIDATION_REQUIRED ||
               to == PlanState::FAILED || to == PlanState::CANCELLED ||
               to == PlanState::RETIRED;
      case PlanState::REVALIDATION_REQUIRED:
        return to == PlanState::AWAITING_EXECUTION || to == PlanState::PLAN_READY ||
               to == PlanState::CANCELLED || to == PlanState::FAILED ||
               to == PlanState::RETIRED;
      case PlanState::COMPLETED: return to == PlanState::RETIRED;
      case PlanState::CANCELLED: return to == PlanState::RETIRED;
      case PlanState::FAILED:    return to == PlanState::RETIRED;
      case PlanState::SUPERSEDED:return to == PlanState::RETIRED;
      case PlanState::RETIRED:   return false;
    }
    return false;
  };
  return allowed();
}

}  // namespace communication_planner
