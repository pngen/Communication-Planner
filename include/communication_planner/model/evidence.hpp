#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/provenance.hpp"
#include "communication_planner/core/units.hpp"

namespace communication_planner {

// Device capability: the transports/features this endpoint actually supports.
// Provenance guards against claiming capabilities never observed.
struct Capability {
  CapabilityGeneration generation;
  std::set<Transport> transports;
  std::set<std::string> features;
  Provenance provenance{Provenance::UNKNOWN};

  char computeArch[8]{};  // e.g. "sm_120"; stable, bounded
  std::string archString() const { return std::string(computeArch); }

  bool supports(Transport t) const { return transports.count(t) != 0; }
};

struct Health {
  HealthGeneration generation;
  bool healthy{false};
  std::string message;
};

struct Capacity {
  CapacityGeneration generation;
  BytesPerSecond usable{0};
  BytesPerSecond headroom{0};  // residable headroom after current commitments
  Provenance provenance{Provenance::UNKNOWN};
};

struct Congestion {
  CongestionGeneration generation;
  double load{0.0};         // normalized 0..1
  double penalty{0.0};      // additive cost penalty (nanos-equivalent)
  bool hardLimitExceeded{false};
  Provenance provenance{Provenance::UNKNOWN};

  bool hasValidLoad() const noexcept { return load >= 0.0 && load <= 1.0 && !isNan(load); }
  static bool isNan(double v) noexcept { return v != v; }
};

struct Reservation {
  ResourceId resource;
  BytesPerSecond reserved{0};
  Provenance provenance{Provenance::UNKNOWN};
};

struct FailureDomain {
  std::string name;
};

}  // namespace communication_planner
