#pragma once

#include <vector>
#include <map>
#include <string>
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include "communication_planner/model/evidence.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/units.hpp"

namespace communication_planner {

// An immutable evidence snapshot consumed by the planner. It binds all current
// authoritative generations. The planner reads current facts; it never measures.
struct EvidenceSnapshot {
  TopologyGeneration topologyGeneration;
  CapacityGeneration capacityGeneration;
  ReservationGeneration reservationGeneration;
  CongestionGeneration congestionGeneration;
  CapabilityGeneration capabilityGeneration;
  HealthGeneration healthGeneration;
  PlacementGeneration placementGeneration;
  PolicyGeneration policyGeneration;

  std::vector<Endpoint> endpoints;
  std::vector<Link> links;
  std::vector<Reservation> reservations;
  std::vector<std::pair<ResourceId, Capacity>> resourceCapacities;

  // Current worker/source incarnations as observed by coordinators. An endpoint published by a worker whose WorkerBootId is not in this map is stale.
  std::map<WorkerId, WorkerBootId> workerBoots;
  std::map<SourceId, SourceBootId> sourceBoots;
  CoordinatorEpoch epoch;
  AuthorityGeneration authorityGeneration;

  // Build a lookup map of endpoint/link by identity for fast access.
  std::map<EndpointId, Endpoint> endpointMap() const;
  std::map<LinkId, Link> linkMap() const;
};

}  // namespace communication_planner
