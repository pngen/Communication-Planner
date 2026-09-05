#include "communication_planner/feasibility/feasibility.hpp"
#include "communication_planner/planner/snapshot.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"

#include <map>

namespace communication_planner {

std::map<EndpointId, Endpoint> EvidenceSnapshot::endpointMap() const {
  std::map<EndpointId, Endpoint> m;
  for (const auto& e : endpoints) m[e.id] = e;
  return m;
}
std::map<LinkId, Link> EvidenceSnapshot::linkMap() const {
  std::map<LinkId, Link> m;
  for (const auto& l : links) m[l.id] = l;
  return m;
}

bool endpointFresh(const Endpoint& e, const EvidenceSnapshot& snap) {
  if (!e.workerBoot.isNull()) {
    if (e.worker.isNull()) return false;
    auto it = snap.workerBoots.find(e.worker);
    if (it == snap.workerBoots.end()) return false;
    if (it->second != e.workerBoot) return false;
  }
  if (!e.sourceBoot.isNull()) {
    if (e.source.isNull()) return false;
    auto it = snap.sourceBoots.find(e.source);
    if (it == snap.sourceBoots.end()) return false;
    if (it->second != e.sourceBoot) return false;
  }
  return true;
}

FeasibilityResult transportAllowed(const CommunicationRequest& req, Transport t) {
  if (req.forbidden.count(t) != 0) {
    return FeasibilityResult::reject(Feasibility::REJECT_TRANSPORT,
                                     "transport is forbidden by request");
  }
  if (!req.allowed.empty() && req.allowed.count(t) == 0) {
    return FeasibilityResult::reject(Feasibility::REJECT_TRANSPORT,
                                     "transport is not in allowed set");
  }
  return FeasibilityResult::ok();
}

static ResourceId linkResource(LinkId id) { return ResourceId(id.value()); }

FeasibilityResult evaluateCandidateFeasibility(const CommunicationRequest& req,
                                               const EvidenceSnapshot& snap,
                                               const CandidatePath& cand,
                                               const Bounds& bounds) {
  const auto emap = snap.endpointMap();
  const auto lmap = snap.linkMap();

  if (cand.hops.empty()) {
    return FeasibilityResult::reject(Feasibility::INSUFFICIENT_EVIDENCE, "empty candidate path");
  }
  if (cand.hops.size() > bounds.maxPathDepth) {
    return FeasibilityResult::reject(Feasibility::REJECT_STAGE_LIMIT, "depth exceeds bound");
  }
  if (cand.hops.size() > req.maxStages) {
    return FeasibilityResult::reject(Feasibility::REJECT_STAGE_LIMIT, "exceeds request stage limit");
  }
  if (cand.hops.size() > req.maxHops) {
    return FeasibilityResult::reject(Feasibility::REJECT_STAGE_LIMIT, "exceeds request hop limit");
  }

  // If more than one hop, an interior relay endpoint is used => staging.
  if (cand.hops.size() > 1) {
    if (!req.allowStaging && !req.allowHostStaging) {
      return FeasibilityResult::reject(Feasibility::REJECT_ENDPOINT,
                                       "multi-hop path requires staging but request forbids it");
    }
    if (!req.allowRelay) {
      return FeasibilityResult::reject(Feasibility::REJECT_ENDPOINT, "relay not allowed");
    }
    // Interior relay endpoints and their staging kind.
    const std::size_t n = cand.hops.size();
    for (std::size_t i = 0; i + 1 < n; ++i) {
      const EndpointId relayId = cand.hops[i].to;
      auto it = emap.find(relayId);
      if (it == emap.end()) {
        return FeasibilityResult::reject(Feasibility::REJECT_ENDPOINT, "missing relay endpoint");
      }
      const EndpointKind kind = it->second.kind;
      // A staging relay is any interior memory/storage/state/residency endpoint
      // (not a pure transport relay like a NIC or peer GPU hop).
      const bool stagingRelay =
          (kind == EndpointKind::CPU_MEMORY || kind == EndpointKind::PINNED_HOST_MEMORY ||
           kind == EndpointKind::NUMA_MEMORY || kind == EndpointKind::STORAGE ||
           kind == EndpointKind::PROCESS || kind == EndpointKind::SERVICE ||
           kind == EndpointKind::CACHE || kind == EndpointKind::CHECKPOINT ||
           kind == EndpointKind::MODEL_RESIDENCY || kind == EndpointKind::STATE_LOCATION);
      if (stagingRelay) {
        if (kind == EndpointKind::STORAGE) {
          if (!req.allowStorageStaging) {
            return FeasibilityResult::reject(Feasibility::REJECT_ENDPOINT,
                                             "storage staging not permitted by request");
          }
        } else {
          if (!req.allowHostStaging) {
            return FeasibilityResult::reject(Feasibility::REJECT_ENDPOINT,
                                             "host staging not permitted by request");
          }
        }
      }
    }
  }

  for (std::size_t i = 0; i < cand.hops.size(); ++i) {
    const Hop& hop = cand.hops[i];
    auto li = lmap.find(hop.link);
    if (li == lmap.end()) {
      return FeasibilityResult::reject(Feasibility::REJECT_TOPOLOGY, "link not present");
    }
    const Link& link = li->second;

    // directionality
    if (hop.from != link.source || hop.to != link.destination) {
      return FeasibilityResult::reject(Feasibility::REJECT_DIRECTION, "hop direction mismatch");
    }
    if (!link.directed) {
      return FeasibilityResult::reject(Feasibility::REJECT_DIRECTION, "link not directed");
    }

    // transport
    FeasibilityResult tr = transportAllowed(req, link.transport);
    if (!tr.feasible()) return tr;

    // payload size support (capacity must be nonzero, non-UNKNOWN transport).
    if (link.capacity.count() == 0) {
      return FeasibilityResult::reject(Feasibility::REJECT_CAPACITY, "link has zero capacity");
    }
    if (link.transport == Transport::UNKNOWN) {
      return FeasibilityResult::reject(Feasibility::REJECT_TRANSPORT, "link transport UNKNOWN");
    }

    // health
    if (!link.health.healthy) {
      return FeasibilityResult::reject(Feasibility::REJECT_HEALTH, "link not healthy");
    }
    // congestion hard limit
    if (link.congestion.hardLimitExceeded) {
      return FeasibilityResult::reject(Feasibility::REJECT_CONGESTION_HARD_LIMIT,
                                       "link congestion hard limit exceeded");
    }
    // failure domain restriction
    if (!req.requiredFailureDomain.empty() &&
        link.failureDomain != req.requiredFailureDomain) {
      return FeasibilityResult::reject(Feasibility::REJECT_FAILURE_DOMAIN,
                                       "link failure domain does not match required");
    }

    // reservation conflict on the link's bandwidth resource
    std::uint64_t reserved = 0;
    for (const auto& r : snap.reservations) {
      if (r.resource == linkResource(link.id)) {
        reserved += r.reserved.count();
      }
    }
    if (reserved >= link.capacity.count()) {
      return FeasibilityResult::reject(Feasibility::REJECT_RESERVATION,
                                       "reservation consumes available link capacity");
    }

    // endpoints
    auto si = emap.find(hop.from);
    auto di = emap.find(hop.to);
    if (si == emap.end() || di == emap.end()) {
      return FeasibilityResult::reject(Feasibility::REJECT_ENDPOINT, "endpoint not present");
    }
    const Endpoint& se = si->second;
    const Endpoint& de = di->second;

    for (std::size_t k = 0; k < 2; ++k) {
      const Endpoint& e = (k == 0) ? se : de;
      if (!endpointFresh(e, snap)) {
        return FeasibilityResult::reject(Feasibility::REJECT_STALE_ENDPOINT,
                                         "endpoint published by stale worker/source incarnation");
      }
      if (!e.reachable || !e.ready) {
        return FeasibilityResult::reject(Feasibility::REJECT_ENDPOINT,
                                         "endpoint not reachable/ready");
      }
      if (e.kind == EndpointKind::UNKNOWN) {
        return FeasibilityResult::reject(Feasibility::REJECT_ENDPOINT,
                                         "UNKNOWN endpoint kind cannot satisfy hard readiness");
      }
      if (!e.capability.supports(link.transport)) {
        return FeasibilityResult::reject(Feasibility::REJECT_CAPABILITY,
                                         "endpoint does not support link transport");
      }
    }
  }

  // policy generation freshness
  if (!req.policyGeneration.isNull() &&
      snap.policyGeneration < req.policyGeneration) {
    return FeasibilityResult::reject(Feasibility::REJECT_POLICY,
                                     "policy generation is not current");
  }

  return FeasibilityResult::ok();
}

}  // namespace communication_planner
