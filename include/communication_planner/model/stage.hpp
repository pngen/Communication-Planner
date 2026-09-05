#pragma once

#include <string>
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/provenance.hpp"
#include "communication_planner/core/units.hpp"

namespace communication_planner {

// One ordered movement stage. Staged plans do not claim end-to-end completion
// until every required stage completes.
struct Stage {
  StageId id;
  StageGeneration generation;
  EndpointId source;
  EndpointId destination;
  LinkId link;
  Transport transport{Transport::UNKNOWN};
  Bytes payload{0};
  BytesPerSecond expectedBandwidth{0};
  DurationNs expectedLatency{0};
  ResourceId stagingResource;
  bool requiresAuthority{false};   // execution/authority generation required
  Provenance provenance{Provenance::UNKNOWN};
  std::string description;
};

}  // namespace communication_planner
