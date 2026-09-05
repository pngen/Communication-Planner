#include "communication_planner/planner/planner.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include <cstdio>

using namespace communication_planner;

int main() {
  EvidenceSnapshot s;
  s.topologyGeneration=TopologyGeneration(1); s.congestionGeneration=CongestionGeneration(1);
  s.capacityGeneration=CapacityGeneration(1); s.capabilityGeneration=CapabilityGeneration(1);
  s.healthGeneration=HealthGeneration(1); s.placementGeneration=PlacementGeneration(1);
  s.policyGeneration=PolicyGeneration(1); s.authorityGeneration=currentAuthority();

  Endpoint a, b;
  a.id=EndpointId(1); a.generation=EndpointGeneration(1); a.kind=EndpointKind::GPU_DEVICE;
  a.node=NodeId(1); a.ready=true; a.reachable=true; a.health.healthy=true;
  a.capability.transports.insert(Transport::HOST_MEMORY); a.capability.transports.insert(Transport::CUDA);
  a.capability.provenance=Provenance::MEASURED; a.capability.generation=CapabilityGeneration(1);
  b.id=EndpointId(2); b.generation=EndpointGeneration(1); b.kind=EndpointKind::GPU_DEVICE;
  b.node=NodeId(2); b.ready=true; b.reachable=true; b.health.healthy=true;
  b.capability.transports.insert(Transport::HOST_MEMORY); b.capability.transports.insert(Transport::CUDA);
  b.capability.provenance=Provenance::MEASURED; b.capability.generation=CapabilityGeneration(1);
  s.endpoints.push_back(a); s.endpoints.push_back(b);

  Link l; l.id=LinkId(1); l.generation=LinkGeneration(1); l.source=EndpointId(1);
  l.destination=EndpointId(2); l.transport=Transport::HOST_MEMORY; l.directed=true;
  l.capacity=BytesPerSecond(1000000); l.effectiveBandwidth=BytesPerSecond(1000000);
  l.latency=DurationNs(1000); l.health.healthy=true; l.provenance=Provenance::MEASURED;
  l.topologyGeneration=TopologyGeneration(1); l.capabilityGeneration=CapabilityGeneration(1);
  s.links.push_back(l);

  RankingWeights w; Bounds bnd; bnd.maxPathDepth=4; bnd.maxCandidates=64; bnd.maxFallbacks=2;
  CommunicationRequest req;
  req.id=CommunicationRequestId(1); req.generation=CommunicationRequestGeneration(1);
  req.shape=RequestShape::POINT_TO_POINT; req.source=EndpointId(1);
  req.destinations.push_back(EndpointId(2)); req.payloadSize=Bytes(4096);
  req.allowStaging=true; req.allowHostStaging=true;

  PlanOutcome po = planCommunication(req, s, w, bnd);
  if (!po.success) { std::printf("downstream: plan failed\n"); return 1; }
  std::printf("downstream: deterministic plan success, stages=%zu\n", po.plan->orderedStages.size());
  std::printf("downstream: primary path id=%llu cost=%.3f provenance=%s\n",
    (unsigned long long)po.plan->primaryPath.value(), po.plan->cost.total, po.plan->provenance.c_str());
  return 0;
}
