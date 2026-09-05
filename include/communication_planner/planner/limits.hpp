#pragma once

#include <cstdint>

namespace communication_planner {

// Explicit resource bounds so hostile graph sizes cannot produce unbounded work.
struct Bounds {
  std::uint64_t maxEndpoints{100000};
  std::uint64_t maxLinks{100000};
  std::uint64_t maxCandidates{4096};
  unsigned   maxPathDepth{16};
  unsigned   maxRelayCount{16};
  unsigned   maxMulticastFanout{1024};
  unsigned   maxCollectiveParticipants{65536};
  std::uint64_t maxFallbacks{16};
  std::uint64_t maxHistory{4096};
  std::uint64_t maxStagingPerPlan{64};
  std::uint64_t maxExplanationSize{64};
  std::uint64_t maxPayloadBytes{1ull << 34};  // 16 GiB
};

}  // namespace communication_planner
