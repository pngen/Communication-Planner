#pragma once

#include <string>
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/provenance.hpp"
#include "communication_planner/model/evidence.hpp"
#include "communication_planner/model/location.hpp"

namespace communication_planner {

// A first-class communication endpoint. UNKNOWN kind with no explicit readiness
// must NOT silently satisfy hard constraints.
struct Endpoint {
  EndpointId id;
  EndpointGeneration generation;
  EndpointKind kind{EndpointKind::UNKNOWN};
  NodeId node;
  DeviceId device;
  TopologyLocation location;

  std::string address;    // runtime registry address (bounded)
  std::string protocol;
  std::string version;
  std::string name;

  Capability capability;
  Health health;
  bool reachable{false};   // explicit reachability evidence
  bool ready{false};       // explicit readiness evidence
  WorkerId worker;
  WorkerBootId workerBoot; // process incarnation where applicable
  SourceId source;
  SourceBootId sourceBoot;
  std::string failureDomain;
  Provenance provenance{Provenance::UNKNOWN};

  void setArch(std::string_view a) {
    std::size_t n = a.size();
    if (n > 7) n = 7;
    std::size_t i = 0;
    for (; i < n; ++i) capability.computeArch[i] = a[i];
    capability.computeArch[i] = '\0';
  }
};

}  // namespace communication_planner
