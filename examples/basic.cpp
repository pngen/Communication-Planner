#include "communication_planner/planner/planner.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/multicast/multicast.hpp"
#include "communication_planner/collective/collective.hpp"
#include "communication_planner/persistence/persistence.hpp"
#include "communication_planner/revalidation/revalidation.hpp"
#include "communication_planner/feasibility/feasibility.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include <cstdio>

using namespace communication_planner;

static Endpoint ep(EndpointId id, EndpointKind kind, NodeId node, std::initializer_list<Transport> caps) {
  Endpoint e; e.id=id; e.generation=EndpointGeneration(1); e.kind=kind; e.node=node;
  e.ready=true; e.reachable=true; e.health.healthy=true;
  for (Transport t : caps) e.capability.transports.insert(t);
  e.capability.provenance=Provenance::MEASURED; e.capability.generation=CapabilityGeneration(1);
  return e;
}
static Link lk(LinkId id, EndpointId s, EndpointId d, Transport t, std::uint64_t cap) {
  Link l; l.id=id; l.generation=LinkGeneration(1); l.source=s; l.destination=d; l.transport=t; l.directed=true;
  l.capacity=BytesPerSecond(cap); l.effectiveBandwidth=BytesPerSecond(cap); l.latency=DurationNs(1000);
  l.health.healthy=true; l.provenance=Provenance::MEASURED;
  l.topologyGeneration=TopologyGeneration(1); l.capabilityGeneration=CapabilityGeneration(1);
  l.capacityEvidence.provenance=Provenance::REPORTED; l.capacityEvidence.generation=CapacityGeneration(1);
  l.capacityEvidence.usable=BytesPerSecond(cap); l.capacityEvidence.headroom=BytesPerSecond(cap);
  return l;
}

int main() {
  RankingWeights w; Bounds b; b.maxPathDepth=6; b.maxCandidates=256; b.maxFallbacks=4;
  EvidenceSnapshot s;
  s.topologyGeneration=TopologyGeneration(1); s.congestionGeneration=CongestionGeneration(1);
  s.capacityGeneration=CapacityGeneration(1); s.capabilityGeneration=CapabilityGeneration(1);
  s.healthGeneration=HealthGeneration(1); s.placementGeneration=PlacementGeneration(1);
  s.policyGeneration=PolicyGeneration(1); s.authorityGeneration=currentAuthority();
  s.endpoints.push_back(ep(EndpointId(1),EndpointKind::GPU_DEVICE,NodeId(1),{Transport::HOST_MEMORY,Transport::PCIE,Transport::CUDA}));
  s.endpoints.push_back(ep(EndpointId(2),EndpointKind::GPU_DEVICE,NodeId(2),{Transport::HOST_MEMORY,Transport::PCIE,Transport::CUDA}));
  s.endpoints.push_back(ep(EndpointId(3),EndpointKind::PINNED_HOST_MEMORY,NodeId(1),{Transport::HOST_MEMORY,Transport::PCIE}));
  s.links.push_back(lk(LinkId(1),EndpointId(1),EndpointId(2),Transport::HOST_MEMORY,1000000));
  s.links.push_back(lk(LinkId(2),EndpointId(1),EndpointId(3),Transport::PCIE,2000000));
  s.links.push_back(lk(LinkId(3),EndpointId(3),EndpointId(2),Transport::PCIE,2000000));

  CommunicationRequest req; req.id=CommunicationRequestId(1);
  req.generation=CommunicationRequestGeneration(1); req.shape=RequestShape::POINT_TO_POINT;
  req.source=EndpointId(1); req.destinations.push_back(EndpointId(2));
  req.payloadSize=Bytes(4096); req.allowStaging=true; req.allowHostStaging=true;

  PlanOutcome po = planCommunication(req, s, w, b);
  std::printf("Example: host<->GPU plan success=%d stages=%zu\n", (int)po.success, po.success?po.plan->orderedStages.size():0);
  if (po.success) {
    for (const Stage& st : po.plan->orderedStages)
      std::printf("  stage %s %s->%s\n", std::string(toString(st.transport)).c_str(), st.source.str().c_str(), st.destination.str().c_str());
  }

  // Hard rejection: forbid the direct transport.
  CommunicationRequest req2 = req; req2.id=CommunicationRequestId(2);
  req2.forbidden.insert(Transport::HOST_MEMORY);
  PlanOutcome po2 = planCommunication(req2, s, w, b);
  std::printf("Example: forbidden direct transport -> success=%d stages=%zu\n", (int)po2.success, po2.success?po2.plan->orderedStages.size():0);

  // Persistence round-trip.
  StoreState store; store.epoch=CoordinatorEpoch(1);
  if (po.success) { StoreRecord rec; rec.plan=*po.plan; rec.state=PlanState::PLAN_READY; store.plans.push_back(rec); }
  std::vector<std::byte> bytes = serializeStore(store);
  StoreState loaded; PersistError pe = parseStore(bytes.data(), bytes.size(), loaded);
  std::printf("Example: persistence round-trip ok=%d plans=%zu\n", (int)pe.ok(), loaded.plans.size());
  return 0;
}
