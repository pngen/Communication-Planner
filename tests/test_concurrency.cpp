#include "framework.hpp"
#include "testutil.hpp"
#include "communication_planner/coordinator/coordinator.hpp"
#include "communication_planner/planner/planner.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"

#include <thread>
#include <vector>
#include <atomic>
#include <string>
#include <chrono>

using namespace communication_planner;
using cputil::mkEndpoint, cputil::mkLink, cputil::mkRequest;

CP_TEST(concurrency_planning_and_cancel_race) {
  RankingWeights w; Bounds b;
  b.maxPathDepth = 6; b.maxCandidates = 256; b.maxFallbacks = 4;
  CoordinatorServer server(w, b, "concurrency_store.bin");
  std::string err;
  REQUIRE(server.start(0, err));
  const unsigned short port = server.boundPort();
  REQUIRE(port != 0);

  // Publisher worker stays connected for the whole test so its evidence is not fenced.
  CoordinatorClient setup;
  REQUIRE(setup.connect("127.0.0.1", port, err));
  {
    WorkerId wid(100); WorkerBootId boot(55);
    REQUIRE(setup.registerWorker(wid, boot, "setup", err));
    EndpointId a(1), h(2), gpu(3);
    Endpoint ea = mkEndpoint(a, EndpointGeneration(1), EndpointKind::GPU_DEVICE, NodeId(1), true, true, {Transport::CUDA, Transport::HOST_MEMORY, Transport::PCIE});
    ea.worker = wid; ea.workerBoot = boot;
    Endpoint eh = mkEndpoint(h, EndpointGeneration(1), EndpointKind::PINNED_HOST_MEMORY, NodeId(1), true, true, {Transport::HOST_MEMORY, Transport::PCIE});
    eh.worker = wid; eh.workerBoot = boot;
    Endpoint eg = mkEndpoint(gpu, EndpointGeneration(1), EndpointKind::GPU_DEVICE, NodeId(2), true, true, {Transport::CUDA, Transport::HOST_MEMORY, Transport::PCIE});
    eg.worker = wid; eg.workerBoot = boot;
    REQUIRE(setup.publishEndpoint(wid, boot, ea, err));
    REQUIRE(setup.publishEndpoint(wid, boot, eh, err));
    REQUIRE(setup.publishEndpoint(wid, boot, eg, err));
    Link l1 = mkLink(LinkId(1), LinkGeneration(1), a, h, Transport::PCIE, 2000000);
    Link l2 = mkLink(LinkId(2), LinkGeneration(1), h, gpu, Transport::PCIE, 2000000);
    Link l3 = mkLink(LinkId(3), LinkGeneration(1), a, gpu, Transport::HOST_MEMORY, 3000000);
    REQUIRE(setup.publishLink(wid, boot, l1, err));
    REQUIRE(setup.publishLink(wid, boot, l2, err));
    REQUIRE(setup.publishLink(wid, boot, l3, err));
  }

  std::atomic<int> okCount{0};
  std::atomic<int> cancelTerminal{0};
  std::atomic<bool> startFlag{false};
  std::vector<std::thread> threads;
  const int NTHREADS = 6;
  for (int t = 0; t < NTHREADS; ++t) {
    threads.emplace_back([&, t]() {
      CoordinatorClient c; std::string e;
      if (!c.connect("127.0.0.1", port, e)) return;
      while (!startFlag.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      for (int k = 0; k < 12; ++k) {
        CommunicationRequest req = mkRequest(CommunicationRequestId((std::uint64_t)(t * 1000 + k + 1)), EndpointId(1), EndpointId(3), 4096);
        req.allowStaging = true; req.allowHostStaging = true; req.allowRelay = true;
        CommunicationPlan plan; bool feasible = false;
        if (!c.submitRequest(req, plan, feasible, e)) continue;
        if (!feasible) continue;
        bool commitOk = false;
        if (k % 2 == 0) {
          c.cancel(req.id, plan.id, e);
          c.commitPlan(plan.id, commitOk, e);
          cancelTerminal.fetch_add(1);
        } else {
          c.commitPlan(plan.id, commitOk, e);
          bool fresh = false;
          c.revalidate(plan.id, fresh, e);
          if (fresh) {
            bool hok = false;
            c.executionHandoff(plan.id, hok, e);
            if (hok) c.executionResult(plan.id, true, e);
          }
        }
        okCount.fetch_add(1);
      }
    });
  }
  startFlag.store(true);
  for (auto& th : threads) th.join();
  CHECK(okCount.load() >= 1);
  CHECK(cancelTerminal.load() >= 1);
  CHECK(server.currentPlanCount() >= 1);
  server.stop();
}

CP_TEST(concurrency_concurrent_planning_requests) {
  RankingWeights w; Bounds b;
  b.maxPathDepth = 6; b.maxCandidates = 256;
  CoordinatorServer server(w, b, "concurrency2_store.bin");
  std::string err;
  REQUIRE(server.start(0, err));
  unsigned short port = server.boundPort();

  CoordinatorClient setup;
  REQUIRE(setup.connect("127.0.0.1", port, err));
  {
    WorkerId wid(2); WorkerBootId boot(2);
    REQUIRE(setup.registerWorker(wid, boot, "setup", err));
    EndpointId a(10), g(20);
    Endpoint ea = mkEndpoint(a, EndpointGeneration(1), EndpointKind::GPU_DEVICE, NodeId(1), true, true, {Transport::CUDA, Transport::HOST_MEMORY});
    ea.worker=wid; ea.workerBoot=boot;
    Endpoint eg = mkEndpoint(g, EndpointGeneration(1), EndpointKind::GPU_DEVICE, NodeId(2), true, true, {Transport::CUDA, Transport::HOST_MEMORY});
    eg.worker=wid; eg.workerBoot=boot;
    REQUIRE(setup.publishEndpoint(wid, boot, ea, err));
    REQUIRE(setup.publishEndpoint(wid, boot, eg, err));
    Link l = mkLink(LinkId(7), LinkGeneration(1), a, g, Transport::HOST_MEMORY, 4000000);
    REQUIRE(setup.publishLink(wid, boot, l, err));
  }
  std::atomic<int> done{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t]() {
      CoordinatorClient c; std::string e;
      if (!c.connect("127.0.0.1", port, e)) return;
      for (int k = 0; k < 8; ++k) {
        CommunicationRequest req = mkRequest(CommunicationRequestId((std::uint64_t)(t * 50 + k + 1000)), EndpointId(10), EndpointId(20), 2048);
        CommunicationPlan plan; bool feasible=false;
        if (c.submitRequest(req, plan, feasible, e) && feasible) done.fetch_add(1);
      }
    });
  }
  for (auto& th : threads) th.join();
  CHECK(done.load() >= 1);
  server.stop();
}

CP_MAIN();
