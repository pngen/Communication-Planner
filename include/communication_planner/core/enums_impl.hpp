#pragma once

#include <string>
#include <string_view>
#include <optional>
#include "communication_planner/core/enums.hpp"

namespace communication_planner {

inline std::optional<EndpointKind> endpointKindFromString(std::string_view s) {
  if (s == "GPU_DEVICE") return EndpointKind::GPU_DEVICE;
  if (s == "CPU_MEMORY") return EndpointKind::CPU_MEMORY;
  if (s == "PINNED_HOST_MEMORY") return EndpointKind::PINNED_HOST_MEMORY;
  if (s == "NUMA_MEMORY") return EndpointKind::NUMA_MEMORY;
  if (s == "NIC") return EndpointKind::NIC;
  if (s == "STORAGE") return EndpointKind::STORAGE;
  if (s == "PROCESS") return EndpointKind::PROCESS;
  if (s == "SERVICE") return EndpointKind::SERVICE;
  if (s == "CACHE") return EndpointKind::CACHE;
  if (s == "CHECKPOINT") return EndpointKind::CHECKPOINT;
  if (s == "MODEL_RESIDENCY") return EndpointKind::MODEL_RESIDENCY;
  if (s == "STATE_LOCATION") return EndpointKind::STATE_LOCATION;
  if (s == "UNKNOWN") return EndpointKind::UNKNOWN;
  return std::nullopt;
}

inline std::optional<LinkKind> linkKindFromString(std::string_view s) {
  if (s == "HOST_MEMORY_PATH") return LinkKind::HOST_MEMORY_PATH;
  if (s == "NUMA_PATH") return LinkKind::NUMA_PATH;
  if (s == "PCIE_PATH") return LinkKind::PCIE_PATH;
  if (s == "GPU_HOST_PATH") return LinkKind::GPU_HOST_PATH;
  if (s == "GPU_PEER_PATH") return LinkKind::GPU_PEER_PATH;
  if (s == "NVLINK_CLASS_PATH") return LinkKind::NVLINK_CLASS_PATH;
  if (s == "NIC_PATH") return LinkKind::NIC_PATH;
  if (s == "LOOPBACK_TCP") return LinkKind::LOOPBACK_TCP;
  if (s == "NETWORK_PATH") return LinkKind::NETWORK_PATH;
  if (s == "STORAGE_PATH") return LinkKind::STORAGE_PATH;
  if (s == "SHARED_MEMORY_PATH") return LinkKind::SHARED_MEMORY_PATH;
  if (s == "SOFTWARE_RELAY") return LinkKind::SOFTWARE_RELAY;
  if (s == "COMPOSITE") return LinkKind::COMPOSITE;
  if (s == "UNKNOWN") return LinkKind::UNKNOWN;
  return std::nullopt;
}

inline std::optional<Transport> transportFromString(std::string_view s) {
  if (s == "LOOPBACK_TCP") return Transport::LOOPBACK_TCP;
  if (s == "TCP") return Transport::TCP;
  if (s == "UNIX_SOCKET") return Transport::UNIX_SOCKET;
  if (s == "SHARED_MEMORY") return Transport::SHARED_MEMORY;
  if (s == "PCIE") return Transport::PCIE;
  if (s == "NVLINK") return Transport::NVLINK;
  if (s == "GPU_PEER") return Transport::GPU_PEER;
  if (s == "HOST_MEMORY") return Transport::HOST_MEMORY;
  if (s == "NETWORK") return Transport::NETWORK;
  if (s == "STORAGE") return Transport::STORAGE;
  if (s == "CUDA") return Transport::CUDA;
  if (s == "RDMA") return Transport::RDMA;
  if (s == "UNKNOWN") return Transport::UNKNOWN;
  return std::nullopt;
}

inline std::optional<RequestShape> requestShapeFromString(std::string_view s) {
  if (s == "POINT_TO_POINT") return RequestShape::POINT_TO_POINT;
  if (s == "STAGED") return RequestShape::STAGED;
  if (s == "MULTICAST") return RequestShape::MULTICAST;
  if (s == "COLLECTIVE") return RequestShape::COLLECTIVE;
  if (s == "UNKNOWN") return RequestShape::UNKNOWN;
  return std::nullopt;
}

inline std::optional<CollectiveShape> collectiveShapeFromString(std::string_view s) {
  if (s == "ALL_REDUCE") return CollectiveShape::ALL_REDUCE;
  if (s == "ALL_GATHER") return CollectiveShape::ALL_GATHER;
  if (s == "REDUCE_SCATTER") return CollectiveShape::REDUCE_SCATTER;
  if (s == "BROADCAST") return CollectiveShape::BROADCAST;
  if (s == "GATHER") return CollectiveShape::GATHER;
  if (s == "SCATTER") return CollectiveShape::SCATTER;
  if (s == "ALL_TO_ALL") return CollectiveShape::ALL_TO_ALL;
  if (s == "HIERARCHICAL") return CollectiveShape::HIERARCHICAL;
  if (s == "UNKNOWN") return CollectiveShape::UNKNOWN;
  return std::nullopt;
}

inline std::optional<CostFactor> costFactorFromString(std::string_view s) {
  if (s == "TOTAL_EXPECTED_COMPLETION_COST") return CostFactor::TOTAL_EXPECTED_COMPLETION_COST;
  if (s == "PATH_LATENCY") return CostFactor::PATH_LATENCY;
  if (s == "EFFECTIVE_BANDWIDTH") return CostFactor::EFFECTIVE_BANDWIDTH;
  if (s == "RESIDUAL_BANDWIDTH") return CostFactor::RESIDUAL_BANDWIDTH;
  if (s == "CONGESTION_PENALTY") return CostFactor::CONGESTION_PENALTY;
  if (s == "QUEUEING_ESTIMATE") return CostFactor::QUEUEING_ESTIMATE;
  if (s == "HOP_COUNT") return CostFactor::HOP_COUNT;
  if (s == "STAGE_COUNT") return CostFactor::STAGE_COUNT;
  if (s == "STAGING_OVERHEAD") return CostFactor::STAGING_OVERHEAD;
  if (s == "HOST_STAGING_COST") return CostFactor::HOST_STAGING_COST;
  if (s == "NUMA_DISTANCE") return CostFactor::NUMA_DISTANCE;
  if (s == "PCIE_LOCALITY") return CostFactor::PCIE_LOCALITY;
  if (s == "NIC_LOCALITY") return CostFactor::NIC_LOCALITY;
  if (s == "STORAGE_LOCALITY") return CostFactor::STORAGE_LOCALITY;
  if (s == "ENDPOINT_HEALTH") return CostFactor::ENDPOINT_HEALTH;
  if (s == "RESERVATION_FIT") return CostFactor::RESERVATION_FIT;
  if (s == "CAPACITY_HEADROOM") return CostFactor::CAPACITY_HEADROOM;
  if (s == "FAILURE_DOMAIN_RISK") return CostFactor::FAILURE_DOMAIN_RISK;
  if (s == "RELIABILITY") return CostFactor::RELIABILITY;
  if (s == "MOVEMENT_COST") return CostFactor::MOVEMENT_COST;
  if (s == "POLICY_PREFERENCE") return CostFactor::POLICY_PREFERENCE;
  if (s == "UNKNOWN") return CostFactor::UNKNOWN;
  return std::nullopt;
}

}  // namespace communication_planner
