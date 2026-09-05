#include "communication_planner/planner/planner.hpp"
#include "communication_planner/feasibility/feasibility.hpp"
#include "communication_planner/ranking/ranking.hpp"
#include "communication_planner/ranking/cost.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include "planner_internal.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace communication_planner {

AuthorityGeneration currentAuthority() {
  static AuthorityGeneration a(1);
  return a;
}

namespace internal {

ResourceId stagingResourceFor(EndpointId relay) {
  return ResourceId(relay.value() * 2 + 1);
}

CandidatePath directCandidate(EndpointId src, EndpointId dst, LinkId link) {
  CandidatePath c;
  c.source = src;
  c.destination = dst;
  c.hops.push_back(Hop{link, src, dst});
  return c;
}

std::vector<CandidatePath> enumerateCandidatePaths(const EvidenceSnapshot& snap,
                                                   EndpointId src, EndpointId dst,
                                                   const Bounds& bounds) {
  struct Adj { EndpointId to; LinkId link; };
  std::map<EndpointId, std::vector<Adj>> adj;
  for (const Link& l : snap.links) {
    adj[l.source].push_back(Adj{l.destination, l.id});
  }
  for (auto& pr : adj) {
    std::vector<Adj>& vec = pr.second;
    std::sort(vec.begin(), vec.end(), [](const Adj& a, const Adj& b) {
      if (a.to.value() != b.to.value()) return a.to.value() < b.to.value();
      return a.link.value() < b.link.value();
    });
  }

  std::vector<CandidatePath> out;
  std::set<EndpointId> visited;
  std::vector<Hop> hops;

  std::function<void(EndpointId)> dfs = [&](EndpointId cur) {
    if (out.size() >= bounds.maxCandidates) return;
    if (hops.size() >= bounds.maxPathDepth) return;
    auto it = adj.find(cur);
    if (it == adj.end()) return;
    for (const Adj& a : it->second) {
      if (out.size() >= bounds.maxCandidates) return;
      if (visited.count(a.to)) continue;
      hops.push_back(Hop{a.link, cur, a.to});
      visited.insert(a.to);
      if (a.to == dst) {
        const std::size_t relays = hops.size() - 1;
        if (relays <= bounds.maxRelayCount) {
          CandidatePath p;
          p.source = src;
          p.destination = dst;
          p.hops = hops;
          out.push_back(std::move(p));
        }
      } else {
        dfs(a.to);
      }
      visited.erase(a.to);
      hops.pop_back();
    }
  };

  visited.insert(src);
  dfs(src);

  std::sort(out.begin(), out.end(), [](const CandidatePath& a, const CandidatePath& b) {
    if (a.hops.size() != b.hops.size()) return a.hops.size() < b.hops.size();
    for (std::size_t i = 0; i < a.hops.size(); ++i) {
      if (a.hops[i].link.value() != b.hops[i].link.value())
        return a.hops[i].link.value() < b.hops[i].link.value();
    }
    return false;
  });
  return out;
}

}  // namespace internal

namespace internal {

std::vector<Stage> buildStages(const CandidatePath& cand,
                               const EvidenceSnapshot& snap,
                               const CommunicationRequest& req) {
  const auto lmap = snap.linkMap();
  std::vector<Stage> stages;
  stages.reserve(cand.hops.size());
  for (std::size_t i = 0; i < cand.hops.size(); ++i) {
    const Hop& h = cand.hops[i];
    const Link& link = lmap.at(h.link);
    Stage st;
    st.id = StageId::next();
    st.generation = StageGeneration(1);
    st.source = h.from;
    st.destination = h.to;
    st.link = h.link;
    st.transport = link.transport;
    st.payload = req.payloadSize;
    st.expectedBandwidth = (link.effectiveBandwidth.count() != 0) ? link.effectiveBandwidth : link.capacity;
    st.expectedLatency = link.latency;
    st.requiresAuthority = true;
    st.provenance = link.provenance;
    if (i + 1 < cand.hops.size()) st.stagingResource = internal::stagingResourceFor(h.to);
    std::ostringstream os;
    os << toString(link.transport) << " " << h.from.str() << "->" << h.to.str();
    st.description = os.str();
    stages.push_back(std::move(st));
  }
  return stages;
}

Path makePath(PathId id, const CandidatePath& cand, const EvidenceSnapshot& snap,
              const CommunicationRequest& req, const CostSummary& cost) {
  Path p;
  p.id = id;
  p.generation = PathGeneration(1);
  p.source = cand.source;
  p.destination = cand.destination;
  p.stages = buildStages(cand, snap, req);
  p.totalPayload = req.payloadSize;
  p.cost = cost;
  p.provenance = worstProvenance(cost);
  return p;
}

std::string provName(Provenance p) { return std::string(toString(p)); }

}  // namespace internal

PlanOutcome planCommunication(const CommunicationRequest& request,
                              const EvidenceSnapshot& snapshot,
                              const RankingWeights& weights,
                              const Bounds& bounds) {
  request.requireValid();
  const RequestShape shape = request.effectiveShape();
  if (shape == RequestShape::MULTICAST) return internal::buildMulticastPlan(request, snapshot, weights, bounds);
  if (shape == RequestShape::COLLECTIVE) return internal::buildCollectivePlan(request, snapshot, weights, bounds);

  PlanOutcome out;
  const EndpointId dst = request.destinations.at(0);
  std::vector<CandidatePath> cands = internal::enumerateCandidatePaths(snapshot, request.source, dst, bounds);

  std::vector<Rankable> rankables;
  std::vector<std::size_t> feasibleOrig;
  std::vector<CandidateReport> reports;
  reports.reserve(cands.size());
  rankables.reserve(cands.size());

  for (std::size_t i = 0; i < cands.size(); ++i) {
    const CandidatePath& c = cands[i];
    CandidateReport rep;
    rep.path = c;
    rep.feasibility = evaluateCandidateFeasibility(request, snapshot, c, bounds);
    rep.detail = rep.feasibility.detail;
    if (rep.feasibility.feasible()) {
      rep.cost = evaluateCandidateCost(request, snapshot, c, weights, bounds);
      rep.provenance = worstProvenance(rep.cost);
      rankables.push_back(Rankable{c, rep.feasibility, rep.cost});
      feasibleOrig.push_back(i);
    } else {
      rep.provenance = Provenance::UNKNOWN;
    }
    reports.push_back(std::move(rep));
  }

  std::vector<std::size_t> rankOrder = rankFeasible(rankables);
  std::vector<std::size_t> orderOrig;
  orderOrig.reserve(rankOrder.size());
  for (const std::size_t idx : rankOrder) orderOrig.push_back(feasibleOrig[idx]);

  out.rankedIndices = orderOrig;
  out.candidates = std::move(reports);

  if (orderOrig.empty()) {
    out.success = false;
    out.explanations.push_back("No feasible path meets all hard constraints.");
    return out;
  }

  const std::size_t nFallback = std::min<std::size_t>(bounds.maxFallbacks, orderOrig.size() - 1);
  const std::size_t total = 1 + nFallback;

  CommunicationPlan plan;
  plan.id = CommunicationPlanId::next();
  plan.generation = CommunicationPlanGeneration(1);
  plan.requestId = request.id;
  plan.requestGeneration = request.generation;
  plan.shape = RequestShape::POINT_TO_POINT;
  plan.feasibility = Feasibility::FEASIBLE;

  // Build primary.
  std::vector<CandidatePath> orderedCands;
  orderedCands.reserve(total);
  for (std::size_t k = 0; k < total; ++k) {
    orderedCands.push_back(cands[orderOrig[k]]);
    Path pt = internal::makePath(PathId(k + 1), cands[orderOrig[k]], snapshot, request,
                       rankables[rankOrder[k]].cost);
    plan.candidatePaths.push_back(pt);
    if (k == 0) {
      plan.primaryPath = pt.id;
      plan.orderedStages = pt.stages;
      plan.cost = pt.cost;
    } else {
      plan.fallbackPaths.push_back(pt.id);
    }
  }

  plan.topologyGeneration = snapshot.topologyGeneration;
  plan.capacityGeneration = snapshot.capacityGeneration;
  plan.reservationGeneration = snapshot.reservationGeneration;
  plan.congestionGeneration = snapshot.congestionGeneration;
  plan.capabilityGeneration = snapshot.capabilityGeneration;
  plan.healthGeneration = snapshot.healthGeneration;
  plan.placementGeneration = snapshot.placementGeneration;
  plan.authorityGeneration = snapshot.authorityGeneration;

  std::set<std::pair<EndpointId, EndpointGeneration>> egs;
  std::set<std::pair<LinkId, LinkGeneration>> lgs;
  std::set<ResourceId> claims;
  const auto lmap = snapshot.linkMap();
  const auto emap = snapshot.endpointMap();
  for (std::size_t ci = 0; ci < orderedCands.size(); ++ci) {
    const CandidatePath& c = orderedCands[ci];
    for (const Hop& h : c.hops) {
      auto li = lmap.find(h.link);
      if (li != lmap.end()) lgs.insert({li->second.id, li->second.generation});
      auto si = emap.find(h.from);
      auto di = emap.find(h.to);
      if (si != emap.end()) egs.insert({si->second.id, si->second.generation});
      if (di != emap.end()) egs.insert({di->second.id, di->second.generation});
    }
    if (ci == 0) {  // claims needed to EXECUTE the primary plan
      for (std::size_t i = 0; i + 1 < c.hops.size(); ++i) {
        claims.insert(internal::stagingResourceFor(c.hops[i].to));
      }
    }
  }
  plan.endpointGenerations.assign(egs.begin(), egs.end());
  plan.linkGenerations.assign(lgs.begin(), lgs.end());
  plan.requiredStagingResources.assign(claims.begin(), claims.end());
  plan.requiredResourceClaims.assign(claims.begin(), claims.end());

  plan.provenance = plan.cost.totalValid() ? internal::provName(worstProvenance(plan.cost)) : "UNKNOWN";
  plan.revalidationGeneration = RevalidationGeneration(1);
  plan.revalidateBeforeExecute = true;
  plan.handoffRequirement = "revalidate all bound generations before execution handoff";

  plan.explanations.push_back("Selected the feasible path with the lowest expected cost.");
  plan.explanations.push_back("Primary path stages: " + std::to_string(plan.orderedStages.size()));
  plan.explanations.push_back("Feasible candidate paths retained: " + std::to_string(total));
  plan.explanations.push_back("Total candidates rejected by hard filters: " + std::to_string(cands.size() - total));
  plan.explanations.push_back("Cost provenance: " + plan.provenance);

  out.success = true;
  out.plan = std::move(plan);
  return out;
}

}  // namespace communication_planner
