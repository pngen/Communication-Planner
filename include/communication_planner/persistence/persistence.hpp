#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include "communication_planner/model/plan.hpp"
#include "communication_planner/model/request.hpp"
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"

namespace communication_planner {

enum class PersistErrorCode : std::uint8_t {
  OK, BAD_MAGIC, UNSUPPORTED_VERSION, TRUNCATION, CORRUPTION, CHECKSUM_MISMATCH,
  MALFORMED_LENGTH, OVERSIZED_COUNT, INVALID_ENUM, DUPLICATE_ID, DUPLICATE_CURRENT_AUTHORITY,
  GENERATION_REGRESSION, IMPOSSIBLE_LIFECYCLE, PATH_MISSING_ENDPOINT, INVALID_STAGE_ORDER,
  CYCLE_FORBIDDEN, PAYLOAD_MISMATCH, SELECTED_PLAN_NOT_IN_CANDIDATE_SET,
  COMMITTED_PLAN_NO_EVIDENCE, NAN_INF_COST, CP_OVERFLOW, TRAILING_GARBAGE
};
std::string toString(PersistErrorCode c);

struct PersistError {
  PersistErrorCode code{PersistErrorCode::OK};
  std::string detail;
  bool ok() const { return code == PersistErrorCode::OK; }
};

struct SupersessionRecord {
  CommunicationPlanId superseded;
  CommunicationPlanId newer;
};

struct StoreRecord {
  CommunicationPlan plan;
  PlanState state{PlanState::REQUESTED};
};

struct StoreState {
  CoordinatorEpoch epoch;
  std::vector<StoreRecord> plans;
  std::vector<SupersessionRecord> supersessions;
  std::vector<CommunicationRequest> requests;
};

// Deterministic, integrity-checked, versioned binary persistence.
std::vector<std::byte> serializeStore(const StoreState& state);
PersistError parseStore(const std::byte* data, std::size_t n, StoreState& out);
PersistError validateStore(const StoreState& state);

// Conservative recovery: every executable plan becomes REVALIDATION_REQUIRED
// because dynamic evidence is not persisted and cannot be assumed fresh.
void conservativeRecovery(StoreState& state);

}  // namespace communication_planner
