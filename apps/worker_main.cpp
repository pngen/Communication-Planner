#include "communication_planner/coordinator/coordinator.hpp"
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>

using namespace communication_planner;

static Endpoint mkEp(EndpointId id, EndpointKind kind, NodeId node,
                     std::initializer_list<Transport> caps) {
  Endpoint e; e.id = id; e.generation = EndpointGeneration(1); e.kind = kind; e.node = node;
  e.ready = true; e.reachable = true;
  e.health.healthy = true; e.health.generation = HealthGeneration(1);
  e.capability.generation = CapabilityGeneration(1);
  for (Transport t : caps) e.capability.transports.insert(t);
  e.capability.provenance = Provenance::MEASURED;
  e.provenance = Provenance::MEASURED;
  return e;
}
static Link mkLk(LinkId id, EndpointId src, EndpointId dst, Transport t, std::uint64_t cap) {
  Link l; l.id = id; l.generation = LinkGeneration(1); l.source = src; l.destination = dst;
  l.transport = t; l.directed = true; l.kind = LinkKind::COMPOSITE;
  l.capacity = BytesPerSecond(cap); l.effectiveBandwidth = BytesPerSecond(cap);
  l.latency = DurationNs(1000);
  l.health.healthy = true; l.health.generation = HealthGeneration(1);
  l.congestion.generation = CongestionGeneration(1);
  l.congestion.provenance = Provenance::REPORTED;
  l.capacityEvidence.generation = CapacityGeneration(1);
  l.capacityEvidence.usable = BytesPerSecond(cap);
  l.capacityEvidence.headroom = BytesPerSecond(cap);
  l.capacityEvidence.provenance = Provenance::REPORTED;
  l.topologyGeneration = TopologyGeneration(1);
  l.capabilityGeneration = CapabilityGeneration(1);
  l.provenance = Provenance::MEASURED;
  return l;
}

int main(int argc, char** argv) {
  if (argc < 4) { std::printf("usage: cp_worker <coordinatorPort> <A|B> <bootBase>\n"); return 2; }
  unsigned short port = (unsigned short)std::atoi(argv[1]);
  std::string role = argv[2];
  std::uint64_t bootBase = std::strtoull(argv[3], nullptr, 10);
  CoordinatorClient c; std::string err;
  if (!c.connect("127.0.0.1", port, err)) { std::printf("worker connect failed: %s\n", err.c_str()); return 1; }
  WorkerId wid = (role == "A") ? WorkerId(1) : WorkerId(2);
  WorkerBootId boot(bootBase);
  if (!c.registerWorker(wid, boot, role, err)) { std::printf("worker register failed: %s\n", err.c_str()); return 1; }
  if (role == "A") {
    Endpoint e = mkEp(EndpointId(100), EndpointKind::GPU_DEVICE, NodeId(1), {Transport::CUDA, Transport::HOST_MEMORY, Transport::PCIE});
    e.worker = wid; e.workerBoot = boot;
    if (!c.publishEndpoint(wid, boot, e, err)) { std::printf("worker A publish ep failed: %s\n", err.c_str()); return 1; }
    Link l = mkLk(LinkId(10), EndpointId(100), EndpointId(200), Transport::HOST_MEMORY, 3000000);
    l.workerBoot = boot;
    if (!c.publishLink(wid, boot, l, err)) { std::printf("worker A publish link failed: %s\n", err.c_str()); return 1; }
  } else {
    Endpoint eB = mkEp(EndpointId(200), EndpointKind::GPU_DEVICE, NodeId(2), {Transport::CUDA, Transport::HOST_MEMORY, Transport::PCIE});
    eB.worker = wid; eB.workerBoot = boot;
    if (!c.publishEndpoint(wid, boot, eB, err)) { std::printf("worker B publish epB failed: %s\n", err.c_str()); return 1; }
    Endpoint h = mkEp(EndpointId(300), EndpointKind::PINNED_HOST_MEMORY, NodeId(1), {Transport::HOST_MEMORY, Transport::PCIE});
    h.worker = wid; h.workerBoot = boot;
    if (!c.publishEndpoint(wid, boot, h, err)) { std::printf("worker B publish host failed: %s\n", err.c_str()); return 1; }
    Link l1 = mkLk(LinkId(11), EndpointId(100), EndpointId(300), Transport::PCIE, 2000000);
    l1.workerBoot = boot;
    if (!c.publishLink(wid, boot, l1, err)) { std::printf("worker B publish l1 failed: %s\n", err.c_str()); return 1; }
    Link l2 = mkLk(LinkId(12), EndpointId(300), EndpointId(200), Transport::PCIE, 2000000);
    l2.workerBoot = boot;
    if (!c.publishLink(wid, boot, l2, err)) { std::printf("worker B publish l2 failed: %s\n", err.c_str()); return 1; }
  }
  std::printf("WORKER_READY %s\n", role.c_str());
  std::fflush(stdout);
  for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
