#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/provenance.hpp"

namespace communication_planner {

// Physical/logical topology facts. Never inferred from names — always from an
// explicit topology source with its own generation.
struct TopologyLocation {
  NodeId node;
  CpuDomainId cpuDomain;
  MemoryDomainId memoryDomain;
  NicId nic;
  DeviceId device;
  std::string pcieRoot;
  int numaDistance{0};
  Provenance provenance{Provenance::UNKNOWN};
};

}  // namespace communication_planner
