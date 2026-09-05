#pragma once

#include <vector>
#include <set>
#include <optional>
#include <string>
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/units.hpp"
#include "communication_planner/core/error.hpp"

namespace communication_planner {

// Explicit, validated communication request model.
struct CommunicationRequest {
  CommunicationRequestId id;
  CommunicationRequestGeneration generation;

  RequestShape shape{RequestShape::POINT_TO_POINT};
  EndpointId source;                      // single source
  std::vector<EndpointId> destinations;   // point-to-point: size 1; multicast: fan-out
  Bytes payloadSize{0};
  Direction direction{Direction::FORWARD};

  bool latencySensitive{false};
  BytesPerSecond throughputTarget{0};

  bool requireOrdering{false};
  bool requireReliability{false};
  bool allowRetry{false};

  // staging permissions
  bool allowStaging{true};
  bool allowHostStaging{true};
  bool allowStorageStaging{false};
  bool allowRelay{true};
  bool allowMulticast{false};

  RequestShape effectiveShape() const {
    if (shape == RequestShape::MULTICAST || allowMulticast) return RequestShape::MULTICAST;
    return shape;
  }

  // transports
  std::set<Transport> allowed;     // empty => any
  std::set<Transport> forbidden;   // never allowed

  // topology / locality
  std::string requiredFailureDomain;
  std::string preferredLocality;

  // limits
  unsigned maxHops{16};
  unsigned maxStages{16};
  std::optional<Bytes> maxMovementCost;

  // reservation / priority / policy
  std::optional<ResourceId> reservationBinding;
  unsigned externalPriority{0};
  PolicyGeneration policyGeneration;

  // collective
  CollectiveShape collectiveShape{CollectiveShape::UNKNOWN};
  std::vector<EndpointId> participants;
  unsigned collectiveCount{0};

  // provenance
  std::string provenance;   // caller description

  // ---- Validation ----
  bool validate(std::string* why) const {
    if (id.isNull()) { if (why) *why = "null request id"; return false; }
    if (generation.isNull()) { if (why) *why = "null request generation"; return false; }
    if (payloadSize.count() == 0) { if (why) *why = "zero payload size"; return false; }
    if (maxHops == 0 || maxHops > 255) { if (why) *why = "invalid maxHops"; return false; }
    if (maxStages == 0 || maxStages > 255) { if (why) *why = "invalid maxStages"; return false; }
    if (direction == Direction::BIDIRECTIONAL) { /* allowed */ }
    if (shape == RequestShape::COLLECTIVE) {
      if (collectiveCount == 0) { if (why) *why = "collective zero participant count"; return false; }
      if (collectiveCount > 1u << 20) { if (why) *why = "collective participant count overflow"; return false; }
      if (collectiveShape == CollectiveShape::UNKNOWN) { if (why) *why = "collective shape unknown"; return false; }
      if (participants.empty()) { if (why) *why = "collective empty participants"; return false; }
    }
    if (shape == RequestShape::MULTICAST || allowMulticast) {
      if (destinations.size() < 2) { if (why) *why = "multicast needs >=2 destinations"; return false; }
      if (destinations.size() > 1u << 16) { if (why) *why = "multicast fan-out overflow"; return false; }
    } else if (shape == RequestShape::POINT_TO_POINT || shape == RequestShape::STAGED) {
      if (source.isNull()) { if (why) *why = "point-to-point missing source"; return false; }
      if (destinations.size() != 1) { if (why) *why = "point-to-point needs exactly one destination"; return false; }
      if (destinations.empty()) { if (why) *why = "point-to-point missing destination"; return false; }
    }
    // transport conflict
    for (const Transport t : allowed) {
      if (forbidden.count(t)) { if (why) *why = "transport in both allowed and forbidden"; return false; }
    }
    if (forbidden.count(Transport::UNKNOWN)) { if (why) *why = "cannot forbid UNKNOWN transport"; return false; }
    return true;
  }

  void requireValid() const {
    std::string why;
    if (!validate(&why)) throw ComError("malformed communication request: " + why);
  }
};

}  // namespace communication_planner
