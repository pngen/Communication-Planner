#pragma once

#include <vector>
#include <string>
#include <utility>
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/provenance.hpp"
#include "communication_planner/core/units.hpp"
#include "communication_planner/model/path.hpp"
#include "communication_planner/model/component.hpp"
#include "communication_planner/model/evidence.hpp"

namespace communication_planner {

// A bounded execution-intent plan. A plan is NOT resource ownership; it records
// the commits it requires and the evidence generations it is bound to.
struct CommunicationPlan {
  CommunicationPlanId id;
  CommunicationPlanGeneration generation;
  CommunicationRequestId requestId;
  CommunicationRequestGeneration requestGeneration;
  RequestShape shape{RequestShape::POINT_TO_POINT};

  PathId primaryPath;
  std::vector<PathId> fallbackPaths;   // deterministic order
  std::vector<Path> candidatePaths;    // all feasible paths, indexed by PathId
  std::vector<Stage> orderedStages;    // primary path stages
  Feasibility feasibility{Feasibility::UNKNOWN};

  // Authority-sensitive generations bound at construction.
  TopologyGeneration topologyGeneration;
  CapacityGeneration capacityGeneration;
  ReservationGeneration reservationGeneration;
  CongestionGeneration congestionGeneration;
  CapabilityGeneration capabilityGeneration;
  HealthGeneration healthGeneration;
  PlacementGeneration placementGeneration;
  AuthorityGeneration authorityGeneration;

  // Endpoint/link generations by identity.
  std::vector<std::pair<EndpointId, EndpointGeneration>> endpointGenerations;
  std::vector<std::pair<LinkId, LinkGeneration>> linkGenerations;

  // Required staging / resource claims (must be committed before ACTIVE).
  std::vector<ResourceId> requiredStagingResources;
  std::vector<ResourceId> requiredResourceClaims;

  CostSummary cost;
  std::vector<std::string> explanations;

  std::string provenance;
  RevalidationGeneration revalidationGeneration;
  bool revalidateBeforeExecute{true};
  std::string handoffRequirement;

  // History / lifecycle metadata.
  std::string createdProvenance;

  // Route-specific metadata.
  std::vector<EndpointId> multicastDestinations;
  CollectiveShape collectiveShape{CollectiveShape::UNKNOWN};
  std::vector<EndpointId> collectiveParticipants;
  std::string description;
};

}  // namespace communication_planner
