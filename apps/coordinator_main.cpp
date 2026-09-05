#include "communication_planner/coordinator/coordinator.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/ranking/weights.hpp"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("usage: cp_coordinator <port-or-0> <storePath>\n"); return 2; }
  unsigned short port = (unsigned short)std::atoi(argv[1]);
  std::string store = argv[2];
  communication_planner::RankingWeights w;
  communication_planner::Bounds b;
  b.maxPathDepth = 8; b.maxCandidates = 512; b.maxFallbacks = 8;
  communication_planner::CoordinatorServer server(w, b, store);
  std::string err;
  if (!server.start(port, err)) { std::printf("coordinator start failed: %s\n", err.c_str()); return 1; }
  unsigned short actual = server.boundPort();
  std::printf("COORDINATOR_READY %u\n", (unsigned)actual);
  std::fflush(stdout);
  while (!server.isStopping()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  server.stop();
  std::printf("COORDINATOR_STOPPED\n");
  return 0;
}
