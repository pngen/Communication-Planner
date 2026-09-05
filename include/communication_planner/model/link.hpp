#pragma once

#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/provenance.hpp"
#include "communication_planner/core/units.hpp"
#include "communication_planner/model/evidence.hpp"

namespace communication_planner {

// A first-class directed communication link. Never claim a physical transport
// capability that was not actually observed (guarded by provenance).
struct Link {
  LinkId id;
  LinkGeneration generation;
  EndpointId source;
  EndpointId destination;
  LinkKind kind{LinkKind::UNKNOWN};
  Transport transport{Transport::UNKNOWN};
  bool directed{true};
  BytesPerSecond capacity{0};
  BytesPerSecond effectiveBandwidth{0};
  DurationNs latency{0};
  Health health;
  Congestion congestion;
  Capacity capacityEvidence;
  TopologyGeneration topologyGeneration;
  CapabilityGeneration capabilityGeneration;
  std::string failureDomain;
  WorkerBootId workerBoot;
  Provenance provenance{Provenance::UNKNOWN};
};

}  // namespace communication_planner
