#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>

namespace communication_planner {

enum class EndpointKind : std::uint8_t {
  GPU_DEVICE,
  CPU_MEMORY,
  PINNED_HOST_MEMORY,
  NUMA_MEMORY,
  NIC,
  STORAGE,
  PROCESS,
  SERVICE,
  CACHE,
  CHECKPOINT,
  MODEL_RESIDENCY,
  STATE_LOCATION,
  UNKNOWN,
};

inline std::string_view toString(EndpointKind k) {
  switch (k) {
    case EndpointKind::GPU_DEVICE: return "GPU_DEVICE";
    case EndpointKind::CPU_MEMORY: return "CPU_MEMORY";
    case EndpointKind::PINNED_HOST_MEMORY: return "PINNED_HOST_MEMORY";
    case EndpointKind::NUMA_MEMORY: return "NUMA_MEMORY";
    case EndpointKind::NIC: return "NIC";
    case EndpointKind::STORAGE: return "STORAGE";
    case EndpointKind::PROCESS: return "PROCESS";
    case EndpointKind::SERVICE: return "SERVICE";
    case EndpointKind::CACHE: return "CACHE";
    case EndpointKind::CHECKPOINT: return "CHECKPOINT";
    case EndpointKind::MODEL_RESIDENCY: return "MODEL_RESIDENCY";
    case EndpointKind::STATE_LOCATION: return "STATE_LOCATION";
    case EndpointKind::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

inline std::optional<EndpointKind> endpointKindFromString(std::string_view s);

enum class LinkKind : std::uint8_t {
  HOST_MEMORY_PATH,
  NUMA_PATH,
  PCIE_PATH,
  GPU_HOST_PATH,
  GPU_PEER_PATH,
  NVLINK_CLASS_PATH,
  NIC_PATH,
  LOOPBACK_TCP,
  NETWORK_PATH,
  STORAGE_PATH,
  SHARED_MEMORY_PATH,
  SOFTWARE_RELAY,
  COMPOSITE,
  UNKNOWN,
};

inline std::string_view toString(LinkKind k) {
  switch (k) {
    case LinkKind::HOST_MEMORY_PATH: return "HOST_MEMORY_PATH";
    case LinkKind::NUMA_PATH: return "NUMA_PATH";
    case LinkKind::PCIE_PATH: return "PCIE_PATH";
    case LinkKind::GPU_HOST_PATH: return "GPU_HOST_PATH";
    case LinkKind::GPU_PEER_PATH: return "GPU_PEER_PATH";
    case LinkKind::NVLINK_CLASS_PATH: return "NVLINK_CLASS_PATH";
    case LinkKind::NIC_PATH: return "NIC_PATH";
    case LinkKind::LOOPBACK_TCP: return "LOOPBACK_TCP";
    case LinkKind::NETWORK_PATH: return "NETWORK_PATH";
    case LinkKind::STORAGE_PATH: return "STORAGE_PATH";
    case LinkKind::SHARED_MEMORY_PATH: return "SHARED_MEMORY_PATH";
    case LinkKind::SOFTWARE_RELAY: return "SOFTWARE_RELAY";
    case LinkKind::COMPOSITE: return "COMPOSITE";
    case LinkKind::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

inline std::optional<LinkKind> linkKindFromString(std::string_view s);

enum class Transport : std::uint8_t {
  LOOPBACK_TCP,
  TCP,
  UNIX_SOCKET,
  SHARED_MEMORY,
  PCIE,
  NVLINK,
  GPU_PEER,
  HOST_MEMORY,
  NETWORK,
  STORAGE,
  CUDA,
  RDMA,
  UNKNOWN,
};

inline std::string_view toString(Transport t) {
  switch (t) {
    case Transport::LOOPBACK_TCP: return "LOOPBACK_TCP";
    case Transport::TCP: return "TCP";
    case Transport::UNIX_SOCKET: return "UNIX_SOCKET";
    case Transport::SHARED_MEMORY: return "SHARED_MEMORY";
    case Transport::PCIE: return "PCIE";
    case Transport::NVLINK: return "NVLINK";
    case Transport::GPU_PEER: return "GPU_PEER";
    case Transport::HOST_MEMORY: return "HOST_MEMORY";
    case Transport::NETWORK: return "NETWORK";
    case Transport::STORAGE: return "STORAGE";
    case Transport::CUDA: return "CUDA";
    case Transport::RDMA: return "RDMA";
    case Transport::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

inline std::optional<Transport> transportFromString(std::string_view s);

enum class RequestShape : std::uint8_t {
  POINT_TO_POINT,
  STAGED,
  MULTICAST,
  COLLECTIVE,
  UNKNOWN,
};

inline std::string_view toString(RequestShape s) {
  switch (s) {
    case RequestShape::POINT_TO_POINT: return "POINT_TO_POINT";
    case RequestShape::STAGED: return "STAGED";
    case RequestShape::MULTICAST: return "MULTICAST";
    case RequestShape::COLLECTIVE: return "COLLECTIVE";
    case RequestShape::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

inline std::optional<RequestShape> requestShapeFromString(std::string_view s);

enum class Direction : std::uint8_t {
  FORWARD,
  REVERSE,
  BIDIRECTIONAL,
};

enum class CollectiveShape : std::uint8_t {
  ALL_REDUCE,
  ALL_GATHER,
  REDUCE_SCATTER,
  BROADCAST,
  GATHER,
  SCATTER,
  ALL_TO_ALL,
  HIERARCHICAL,
  UNKNOWN,
};

inline std::string_view toString(CollectiveShape s) {
  switch (s) {
    case CollectiveShape::ALL_REDUCE: return "ALL_REDUCE";
    case CollectiveShape::ALL_GATHER: return "ALL_GATHER";
    case CollectiveShape::REDUCE_SCATTER: return "REDUCE_SCATTER";
    case CollectiveShape::BROADCAST: return "BROADCAST";
    case CollectiveShape::GATHER: return "GATHER";
    case CollectiveShape::SCATTER: return "SCATTER";
    case CollectiveShape::ALL_TO_ALL: return "ALL_TO_ALL";
    case CollectiveShape::HIERARCHICAL: return "HIERARCHICAL";
    case CollectiveShape::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

inline std::optional<CollectiveShape> collectiveShapeFromString(std::string_view s);

// Plan lifecycle. This is the cleanest final taxonomy.
enum class PlanState : std::uint8_t {
  REQUESTED,
  DISCOVERING,
  CONSTRUCTING,
  FILTERING,
  RANKING,
  PLAN_READY,
  AWAITING_RESOURCE_COMMIT,
  COMMITTED,
  AWAITING_EXECUTION,
  ACTIVE,
  REVALIDATION_REQUIRED,
  COMPLETED,
  CANCELLED,
  FAILED,
  SUPERSEDED,
  RETIRED,
};

inline std::string_view toString(PlanState s) {
  switch (s) {
    case PlanState::REQUESTED: return "REQUESTED";
    case PlanState::DISCOVERING: return "DISCOVERING";
    case PlanState::CONSTRUCTING: return "CONSTRUCTING";
    case PlanState::FILTERING: return "FILTERING";
    case PlanState::RANKING: return "RANKING";
    case PlanState::PLAN_READY: return "PLAN_READY";
    case PlanState::AWAITING_RESOURCE_COMMIT: return "AWAITING_RESOURCE_COMMIT";
    case PlanState::COMMITTED: return "COMMITTED";
    case PlanState::AWAITING_EXECUTION: return "AWAITING_EXECUTION";
    case PlanState::ACTIVE: return "ACTIVE";
    case PlanState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case PlanState::COMPLETED: return "COMPLETED";
    case PlanState::CANCELLED: return "CANCELLED";
    case PlanState::FAILED: return "FAILED";
    case PlanState::SUPERSEDED: return "SUPERSEDED";
    case PlanState::RETIRED: return "RETIRED";
  }
  return "RETIRED";
}

inline std::optional<PlanState> planStateFromString(std::string_view s) {
  if (s == "REQUESTED") return PlanState::REQUESTED;
  if (s == "DISCOVERING") return PlanState::DISCOVERING;
  if (s == "CONSTRUCTING") return PlanState::CONSTRUCTING;
  if (s == "FILTERING") return PlanState::FILTERING;
  if (s == "RANKING") return PlanState::RANKING;
  if (s == "PLAN_READY") return PlanState::PLAN_READY;
  if (s == "AWAITING_RESOURCE_COMMIT") return PlanState::AWAITING_RESOURCE_COMMIT;
  if (s == "COMMITTED") return PlanState::COMMITTED;
  if (s == "AWAITING_EXECUTION") return PlanState::AWAITING_EXECUTION;
  if (s == "ACTIVE") return PlanState::ACTIVE;
  if (s == "REVALIDATION_REQUIRED") return PlanState::REVALIDATION_REQUIRED;
  if (s == "COMPLETED") return PlanState::COMPLETED;
  if (s == "CANCELLED") return PlanState::CANCELLED;
  if (s == "FAILED") return PlanState::FAILED;
  if (s == "SUPERSEDED") return PlanState::SUPERSEDED;
  if (s == "RETIRED") return PlanState::RETIRED;
  return std::nullopt;
}

// Hard feasibility outcome. UNKNOWN must never become FEASIBLE.
enum class Feasibility : std::uint8_t {
  FEASIBLE,
  REJECT_ENDPOINT,
  REJECT_STALE_ENDPOINT,
  REJECT_CAPABILITY,
  REJECT_TRANSPORT,
  REJECT_TOPOLOGY,
  REJECT_DIRECTION,
  REJECT_CAPACITY,
  REJECT_RESERVATION,
  REJECT_CONGESTION_HARD_LIMIT,
  REJECT_HEALTH,
  REJECT_STAGE_LIMIT,
  REJECT_FAILURE_DOMAIN,
  REJECT_POLICY,
  REVALIDATION_REQUIRED,
  INSUFFICIENT_EVIDENCE,
  UNKNOWN,
};

inline std::string_view toString(Feasibility f) {
  switch (f) {
    case Feasibility::FEASIBLE: return "FEASIBLE";
    case Feasibility::REJECT_ENDPOINT: return "REJECT_ENDPOINT";
    case Feasibility::REJECT_STALE_ENDPOINT: return "REJECT_STALE_ENDPOINT";
    case Feasibility::REJECT_CAPABILITY: return "REJECT_CAPABILITY";
    case Feasibility::REJECT_TRANSPORT: return "REJECT_TRANSPORT";
    case Feasibility::REJECT_TOPOLOGY: return "REJECT_TOPOLOGY";
    case Feasibility::REJECT_DIRECTION: return "REJECT_DIRECTION";
    case Feasibility::REJECT_CAPACITY: return "REJECT_CAPACITY";
    case Feasibility::REJECT_RESERVATION: return "REJECT_RESERVATION";
    case Feasibility::REJECT_CONGESTION_HARD_LIMIT: return "REJECT_CONGESTION_HARD_LIMIT";
    case Feasibility::REJECT_HEALTH: return "REJECT_HEALTH";
    case Feasibility::REJECT_STAGE_LIMIT: return "REJECT_STAGE_LIMIT";
    case Feasibility::REJECT_FAILURE_DOMAIN: return "REJECT_FAILURE_DOMAIN";
    case Feasibility::REJECT_POLICY: return "REJECT_POLICY";
    case Feasibility::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case Feasibility::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
    case Feasibility::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

inline std::optional<Feasibility> feasibilityFromString(std::string_view s) {
  if (s == "FEASIBLE") return Feasibility::FEASIBLE;
  if (s == "REJECT_ENDPOINT") return Feasibility::REJECT_ENDPOINT;
  if (s == "REJECT_STALE_ENDPOINT") return Feasibility::REJECT_STALE_ENDPOINT;
  if (s == "REJECT_CAPABILITY") return Feasibility::REJECT_CAPABILITY;
  if (s == "REJECT_TRANSPORT") return Feasibility::REJECT_TRANSPORT;
  if (s == "REJECT_TOPOLOGY") return Feasibility::REJECT_TOPOLOGY;
  if (s == "REJECT_DIRECTION") return Feasibility::REJECT_DIRECTION;
  if (s == "REJECT_CAPACITY") return Feasibility::REJECT_CAPACITY;
  if (s == "REJECT_RESERVATION") return Feasibility::REJECT_RESERVATION;
  if (s == "REJECT_CONGESTION_HARD_LIMIT") return Feasibility::REJECT_CONGESTION_HARD_LIMIT;
  if (s == "REJECT_HEALTH") return Feasibility::REJECT_HEALTH;
  if (s == "REJECT_STAGE_LIMIT") return Feasibility::REJECT_STAGE_LIMIT;
  if (s == "REJECT_FAILURE_DOMAIN") return Feasibility::REJECT_FAILURE_DOMAIN;
  if (s == "REJECT_POLICY") return Feasibility::REJECT_POLICY;
  if (s == "REVALIDATION_REQUIRED") return Feasibility::REVALIDATION_REQUIRED;
  if (s == "INSUFFICIENT_EVIDENCE") return Feasibility::INSUFFICIENT_EVIDENCE;
  if (s == "UNKNOWN") return Feasibility::UNKNOWN;
  return std::nullopt;
}

// Named cost factors for ranking. Policy never hides in one opaque score.
enum class CostFactor : std::uint8_t {
  TOTAL_EXPECTED_COMPLETION_COST,
  PATH_LATENCY,
  EFFECTIVE_BANDWIDTH,
  RESIDUAL_BANDWIDTH,
  CONGESTION_PENALTY,
  QUEUEING_ESTIMATE,
  HOP_COUNT,
  STAGE_COUNT,
  STAGING_OVERHEAD,
  HOST_STAGING_COST,
  NUMA_DISTANCE,
  PCIE_LOCALITY,
  NIC_LOCALITY,
  STORAGE_LOCALITY,
  ENDPOINT_HEALTH,
  RESERVATION_FIT,
  CAPACITY_HEADROOM,
  FAILURE_DOMAIN_RISK,
  RELIABILITY,
  MOVEMENT_COST,
  POLICY_PREFERENCE,
  UNKNOWN,
};

inline std::string_view toString(CostFactor f) {
  switch (f) {
    case CostFactor::TOTAL_EXPECTED_COMPLETION_COST: return "TOTAL_EXPECTED_COMPLETION_COST";
    case CostFactor::PATH_LATENCY: return "PATH_LATENCY";
    case CostFactor::EFFECTIVE_BANDWIDTH: return "EFFECTIVE_BANDWIDTH";
    case CostFactor::RESIDUAL_BANDWIDTH: return "RESIDUAL_BANDWIDTH";
    case CostFactor::CONGESTION_PENALTY: return "CONGESTION_PENALTY";
    case CostFactor::QUEUEING_ESTIMATE: return "QUEUEING_ESTIMATE";
    case CostFactor::HOP_COUNT: return "HOP_COUNT";
    case CostFactor::STAGE_COUNT: return "STAGE_COUNT";
    case CostFactor::STAGING_OVERHEAD: return "STAGING_OVERHEAD";
    case CostFactor::HOST_STAGING_COST: return "HOST_STAGING_COST";
    case CostFactor::NUMA_DISTANCE: return "NUMA_DISTANCE";
    case CostFactor::PCIE_LOCALITY: return "PCIE_LOCALITY";
    case CostFactor::NIC_LOCALITY: return "NIC_LOCALITY";
    case CostFactor::STORAGE_LOCALITY: return "STORAGE_LOCALITY";
    case CostFactor::ENDPOINT_HEALTH: return "ENDPOINT_HEALTH";
    case CostFactor::RESERVATION_FIT: return "RESERVATION_FIT";
    case CostFactor::CAPACITY_HEADROOM: return "CAPACITY_HEADROOM";
    case CostFactor::FAILURE_DOMAIN_RISK: return "FAILURE_DOMAIN_RISK";
    case CostFactor::RELIABILITY: return "RELIABILITY";
    case CostFactor::MOVEMENT_COST: return "MOVEMENT_COST";
    case CostFactor::POLICY_PREFERENCE: return "POLICY_PREFERENCE";
    case CostFactor::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

inline std::optional<CostFactor> costFactorFromString(std::string_view s);

}  // namespace communication_planner
