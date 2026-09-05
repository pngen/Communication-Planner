#pragma once
// Test builders for endpoints / links / requests / snapshots.
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/provenance.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include "communication_planner/model/request.hpp"
#include "communication_planner/planner/snapshot.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/ranking/weights.hpp"

namespace cputil {

inline communication_planner::Endpoint mkEndpoint(communication_planner::EndpointId id,
                                                  communication_planner::EndpointGeneration gen,
                                                  communication_planner::EndpointKind kind,
                                                  communication_planner::NodeId node,
                                                  bool ready, bool reachable,
                                                  std::initializer_list<communication_planner::Transport> transports,
                                                  communication_planner::Provenance prov =
                                                    communication_planner::Provenance::MEASURED,
                                                  communication_planner::WorkerBootId boot = {}) {
  communication_planner::Endpoint e;
  e.id = id; e.generation = gen; e.kind = kind; e.node = node;
  e.ready = ready; e.reachable = reachable;
  e.health.healthy = true; e.health.generation = communication_planner::HealthGeneration(1);
  for (auto t : transports) e.capability.transports.insert(t);
  e.capability.provenance = prov;
  e.capability.generation = communication_planner::CapabilityGeneration(1);
  e.provenance = communication_planner::Provenance::MEASURED;
  if (!boot.isNull()) { e.workerBoot = boot; e.worker = communication_planner::WorkerId(id.value()); }
  e.location.provenance = prov;
  return e;
}

inline communication_planner::Link mkLink(communication_planner::LinkId id,
                                          communication_planner::LinkGeneration gen,
                                          communication_planner::EndpointId src,
                                          communication_planner::EndpointId dst,
                                          communication_planner::Transport transport,
                                          std::uint64_t capacity,
                                          bool healthy = true,
                                          communication_planner::Provenance prov = communication_planner::Provenance::MEASURED,
                                          communication_planner::TopologyGeneration topo = communication_planner::TopologyGeneration(1)) {
  communication_planner::Link l;
  l.id = id; l.generation = gen; l.source = src; l.destination = dst;
  l.transport = transport; l.directed = true; l.kind = communication_planner::LinkKind::COMPOSITE;
  l.capacity = communication_planner::BytesPerSecond(capacity);
  l.effectiveBandwidth = communication_planner::BytesPerSecond(capacity);
  l.latency = communication_planner::DurationNs(1000);
  l.health.healthy = healthy; l.health.generation = communication_planner::HealthGeneration(1);
  l.congestion.generation = communication_planner::CongestionGeneration(1);
  l.congestion.load = 0.0; l.congestion.penalty = 0.0;
  l.congestion.provenance = communication_planner::Provenance::REPORTED;
  l.capacityEvidence.generation = communication_planner::CapacityGeneration(1);
  l.capacityEvidence.usable = communication_planner::BytesPerSecond(capacity);
  l.capacityEvidence.headroom = communication_planner::BytesPerSecond(capacity);
  l.capacityEvidence.provenance = communication_planner::Provenance::REPORTED;
  l.topologyGeneration = topo;
  l.capabilityGeneration = communication_planner::CapabilityGeneration(1);
  l.provenance = prov;
  return l;
}

inline communication_planner::CommunicationRequest mkRequest(communication_planner::CommunicationRequestId id,
                                                             communication_planner::EndpointId src,
                                                             communication_planner::EndpointId dst,
                                                             std::uint64_t payload,
                                                             communication_planner::RequestShape shape =
                                                               communication_planner::RequestShape::POINT_TO_POINT) {
  communication_planner::CommunicationRequest r;
  r.id = id; r.generation = communication_planner::CommunicationRequestGeneration(1);
  r.shape = shape; r.source = src; r.payloadSize = communication_planner::Bytes(payload);
  r.destinations.push_back(dst);
  r.allowStaging = true; r.allowHostStaging = true; r.allowStorageStaging = false;
  r.allowRelay = true;
  return r;
}

}  // namespace cputil
