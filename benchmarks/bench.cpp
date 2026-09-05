#include "communication_planner/planner/planner.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/persistence/persistence.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <vector>

using namespace communication_planner;

static double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

static void runScale(std::uint64_t nEndpoints, std::uint64_t nLinks) {
  Bounds b; b.maxPathDepth=4; b.maxCandidates=64; b.maxFallbacks=4;
  RankingWeights w;
  EvidenceSnapshot s;
  s.topologyGeneration=TopologyGeneration(1); s.congestionGeneration=CongestionGeneration(1);
  s.capacityGeneration=CapacityGeneration(1); s.capabilityGeneration=CapabilityGeneration(1);
  s.healthGeneration=HealthGeneration(1); s.placementGeneration=PlacementGeneration(1);
  s.policyGeneration=PolicyGeneration(1); s.authorityGeneration=currentAuthority();

  auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i=0;i<nEndpoints;++i) {
    Endpoint e; e.id=EndpointId(i+1); e.generation=EndpointGeneration(1);
    e.kind=EndpointKind::GPU_DEVICE; e.node=NodeId((i%4)+1); e.ready=true; e.reachable=true;
    e.health.healthy=true;
    e.capability.transports.insert(Transport::HOST_MEMORY);
    e.capability.transports.insert(Transport::PCIE);
    e.capability.provenance=Provenance::MEASURED; e.capability.generation=CapabilityGeneration(1);
    s.endpoints.push_back(e);
  }
  // ring + chords to reach nLinks.
  std::uint64_t linkId=1;
  for (std::uint64_t i=0;i<nEndpoints && linkId<=nLinks;++i,++linkId) {
    Link l; l.id=LinkId(linkId); l.generation=LinkGeneration(1);
    l.source=EndpointId(i+1); l.destination=EndpointId((i+1)%nEndpoints+1);
    l.transport=Transport::HOST_MEMORY; l.directed=true; l.capacity=BytesPerSecond(1000000);
    l.effectiveBandwidth=BytesPerSecond(1000000); l.latency=DurationNs(100);
    l.health.healthy=true; l.provenance=Provenance::MEASURED;
    l.capacityEvidence.generation=CapacityGeneration(1); l.capacityEvidence.usable=BytesPerSecond(1000000);
    l.topologyGeneration=TopologyGeneration(1); l.capabilityGeneration=CapabilityGeneration(1);
    l.capacityEvidence.provenance=Provenance::REPORTED;
    s.links.push_back(l);
  }
  for (std::uint64_t i=0; linkId<=nLinks; ++i,++linkId) {
    std::uint64_t a = (i*7)%nEndpoints + 1;
    std::uint64_t bb = (i*3)%nEndpoints + 1;
    if (a==bb) { bb = bb%nEndpoints+1; }
    Link l; l.id=LinkId(linkId); l.generation=LinkGeneration(1);
    l.source=EndpointId(a); l.destination=EndpointId(bb);
    l.transport=Transport::PCIE; l.directed=true; l.capacity=BytesPerSecond(2000000);
    l.effectiveBandwidth=BytesPerSecond(2000000); l.latency=DurationNs(200);
    l.health.healthy=true; l.provenance=Provenance::MEASURED;
    l.topologyGeneration=TopologyGeneration(1); l.capabilityGeneration=CapabilityGeneration(1);
    l.capacityEvidence.generation=CapacityGeneration(1); l.capacityEvidence.usable=BytesPerSecond(2000000);
    l.capacityEvidence.provenance=Provenance::REPORTED;
    s.links.push_back(l);
  }
  auto tIngest = std::chrono::steady_clock::now();

  CommunicationRequest req; req.id=CommunicationRequestId(1);
  req.generation=CommunicationRequestGeneration(1); req.shape=RequestShape::POINT_TO_POINT;
  req.source=EndpointId(1); req.destinations.push_back(EndpointId(2));
  req.payloadSize=Bytes(4096); req.allowStaging=true; req.allowHostStaging=true;
  PlanOutcome po = planCommunication(req, s, w, b);
  auto tPlan = std::chrono::steady_clock::now();

  // persistence
  StoreState st; st.epoch=CoordinatorEpoch(1);
  if (po.success) { StoreRecord rec; rec.plan=*po.plan; rec.state=PlanState::PLAN_READY; st.plans.push_back(rec); }
  std::vector<std::byte> bytes = serializeStore(st);
  StoreState out; PersistError pe = parseStore(bytes.data(), bytes.size(), out);
  auto tPersist = std::chrono::steady_clock::now();

  std::printf("scale: endpoints=%llu links=%llu ingest=%.2fms plan=%.2fms success=%d candidates=%zu save/load=%.2fms err=%d\n",
    (unsigned long long)nEndpoints, (unsigned long long)nLinks,
    ms(t0,tIngest), ms(tIngest,tPlan), (int)po.success, po.candidates.size(), ms(tPlan,tPersist), (int)pe.ok());
}

int main(int argc, char** argv) {
  std::printf("Communication Planner benchmark (Release)\n");
  runScale(100, 1000);
  runScale(1000, 10000);
  bool big = (argc > 1 && std::strcmp(argv[1], "big") == 0);
  if (big) runScale(10000, 100000);
  return 0;
}
