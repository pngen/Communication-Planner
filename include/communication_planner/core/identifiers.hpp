#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <functional>
#include <atomic>
#include "communication_planner/core/generation.hpp"

namespace communication_planner {

template <typename Tag>
class Id {
 public:
  using value_type = std::uint64_t;

  constexpr Id() noexcept = default;
  explicit constexpr Id(value_type v) noexcept : value_(v) {}

  constexpr value_type value() const noexcept { return value_; }
  constexpr bool isNull() const noexcept { return value_ == 0; }
  explicit constexpr operator bool() const noexcept { return value_ != 0; }

  constexpr bool operator==(const Id& o) const noexcept { return value_ == o.value_; }
  constexpr bool operator!=(const Id& o) const noexcept { return value_ != o.value_; }
  constexpr bool operator<(const Id& o) const noexcept { return value_ < o.value_; }

  std::string str() const { return std::to_string(value_); }

  static std::optional<Id> fromString(std::string_view s) {
    if (s.empty()) return std::nullopt;
    value_type v = 0;
    for (char c : s) {
      if (c < '0' || c > '9') return std::nullopt;
      value_type d = static_cast<value_type>(c - '0');
      if (v > (UINT64_MAX - d) / 10) return std::nullopt;
      v = v * 10 + d;
    }
    return Id(v);
  }

  static Id next() {
    static std::atomic<value_type> counter{1};
    return Id(counter.fetch_add(1, std::memory_order_relaxed));
  }

 private:
  value_type value_{0};
};

struct CommunicationRequestIdTag {};
struct CommunicationPlanIdTag {};
struct PathIdTag {};
struct StageIdTag {};
struct EndpointIdTag {};
struct SourceEndpointIdTag {};
struct DestinationEndpointIdTag {};
struct RelayEndpointIdTag {};
struct MulticastGroupIdTag {};
struct CollectiveIdTag {};
struct FlowIdTag {};
struct ResourceIdTag {};
struct DeviceIdTag {};
struct NodeIdTag {};
struct CpuDomainIdTag {};
struct MemoryDomainIdTag {};
struct NicIdTag {};
struct StorageEndpointIdTag {};
struct LinkIdTag {};
struct WorkloadIdTag {};
struct ExecutionIdTag {};
struct WorkerIdTag {};
struct SourceIdTag {};

using CommunicationRequestId = Id<CommunicationRequestIdTag>;
using CommunicationPlanId = Id<CommunicationPlanIdTag>;
using PathId = Id<PathIdTag>;
using StageId = Id<StageIdTag>;
using EndpointId = Id<EndpointIdTag>;
using SourceEndpointId = Id<SourceEndpointIdTag>;
using DestinationEndpointId = Id<DestinationEndpointIdTag>;
using RelayEndpointId = Id<RelayEndpointIdTag>;
using MulticastGroupId = Id<MulticastGroupIdTag>;
using CollectiveId = Id<CollectiveIdTag>;
using FlowId = Id<FlowIdTag>;
using ResourceId = Id<ResourceIdTag>;
using DeviceId = Id<DeviceIdTag>;
using NodeId = Id<NodeIdTag>;
using CpuDomainId = Id<CpuDomainIdTag>;
using MemoryDomainId = Id<MemoryDomainIdTag>;
using NicId = Id<NicIdTag>;
using StorageEndpointId = Id<StorageEndpointIdTag>;
using LinkId = Id<LinkIdTag>;
using WorkloadId = Id<WorkloadIdTag>;
using ExecutionId = Id<ExecutionIdTag>;
using WorkerId = Id<WorkerIdTag>;
using SourceId = Id<SourceIdTag>;

struct CommunicationRequestGenerationTag {};
struct CommunicationPlanGenerationTag {};
struct PathGenerationTag {};
struct StageGenerationTag {};
struct EndpointGenerationTag {};
struct MulticastGroupGenerationTag {};
struct CollectiveGenerationTag {};
struct FlowGenerationTag {};
struct ResourceGenerationTag {};
struct DeviceGenerationTag {};
struct NodeGenerationTag {};
struct CpuDomainGenerationTag {};
struct MemoryDomainGenerationTag {};
struct NicGenerationTag {};
struct StorageEndpointGenerationTag {};
struct LinkGenerationTag {};
struct TopologyGenerationTag {};
struct CapabilityGenerationTag {};
struct HealthGenerationTag {};
struct CapacityGenerationTag {};
struct ReservationGenerationTag {};
struct CongestionGenerationTag {};
struct PlacementGenerationTag {};
struct WorkloadGenerationTag {};
struct ExecutionGenerationTag {};
struct PolicyGenerationTag {};
struct PriorityGenerationTag {};
struct SloGenerationTag {};
struct WorkerBootIdTag {};
struct SourceBootIdTag {};
struct CoordinatorEpochTag {};
struct AuthorityGenerationTag {};
struct RevalidationGenerationTag {};

using CommunicationRequestGeneration = Generation<CommunicationRequestGenerationTag>;
using CommunicationPlanGeneration = Generation<CommunicationPlanGenerationTag>;
using PathGeneration = Generation<PathGenerationTag>;
using StageGeneration = Generation<StageGenerationTag>;
using EndpointGeneration = Generation<EndpointGenerationTag>;
using MulticastGroupGeneration = Generation<MulticastGroupGenerationTag>;
using CollectiveGeneration = Generation<CollectiveGenerationTag>;
using FlowGeneration = Generation<FlowGenerationTag>;
using ResourceGeneration = Generation<ResourceGenerationTag>;
using DeviceGeneration = Generation<DeviceGenerationTag>;
using NodeGeneration = Generation<NodeGenerationTag>;
using CpuDomainGeneration = Generation<CpuDomainGenerationTag>;
using MemoryDomainGeneration = Generation<MemoryDomainGenerationTag>;
using NicGeneration = Generation<NicGenerationTag>;
using StorageEndpointGeneration = Generation<StorageEndpointGenerationTag>;
using LinkGeneration = Generation<LinkGenerationTag>;
using TopologyGeneration = Generation<TopologyGenerationTag>;
using CapabilityGeneration = Generation<CapabilityGenerationTag>;
using HealthGeneration = Generation<HealthGenerationTag>;
using CapacityGeneration = Generation<CapacityGenerationTag>;
using ReservationGeneration = Generation<ReservationGenerationTag>;
using CongestionGeneration = Generation<CongestionGenerationTag>;
using PlacementGeneration = Generation<PlacementGenerationTag>;
using WorkloadGeneration = Generation<WorkloadGenerationTag>;
using ExecutionGeneration = Generation<ExecutionGenerationTag>;
using PolicyGeneration = Generation<PolicyGenerationTag>;
using PriorityGeneration = Generation<PriorityGenerationTag>;
using SloGeneration = Generation<SloGenerationTag>;
using WorkerBootId = Generation<WorkerBootIdTag>;
using SourceBootId = Generation<SourceBootIdTag>;
using CoordinatorEpoch = Generation<CoordinatorEpochTag>;
using AuthorityGeneration = Generation<AuthorityGenerationTag>;
using RevalidationGeneration = Generation<RevalidationGenerationTag>;

}  // namespace communication_planner

namespace std {
template <typename Tag>
struct hash<::communication_planner::Id<Tag>> {
  std::size_t operator()(const ::communication_planner::Id<Tag>& id) const noexcept {
    return std::hash<std::uint64_t>{}(id.value());
  }
};
template <typename Tag>
struct hash<::communication_planner::Generation<Tag>> {
  std::size_t operator()(const ::communication_planner::Generation<Tag>& g) const noexcept {
    return std::hash<std::uint64_t>{}(g.value());
  }
};
}  // namespace std
