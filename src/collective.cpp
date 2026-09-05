#include "communication_planner/collective/collective.hpp"
#include "communication_planner/feasibility/feasibility.hpp"
#include "communication_planner/ranking/ranking.hpp"
#include "communication_planner/ranking/cost.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "planner_internal.hpp"

#include <map>
#include <set>

namespace communication_planner {

namespace internal {

PlanOutcome buildCollectivePlan(const CommunicationRequest& req,
                                const EvidenceSnapshot& snap,
                                const RankingWeights& weights,
                                const Bounds& bounds) {
  PlanOutcome out;
  CommunicationPlan plan;
  plan.id = CommunicationPlanId::next();
  plan.generation = nextPlanGeneration();
  plan.requestId = req.id;
  plan.requestGeneration = req.generation;
  plan.shape = RequestShape::COLLECTIVE;
  plan.collectiveShape = req.collectiveShape;
  plan.collectiveParticipants = req.participants;

  const auto emap = snap.endpointMap();

  // Participant completeness is a hard constraint.
  if (req.collectiveCount != req.participants.size()) {
    plan.feasibility = Feasibility::REJECT_ENDPOINT;
    plan.explanations.push_back("Collective participant count does not match participant set.");
    out.success = false;
    out.plan = std::move(plan);
    return out;
  }
  if (req.collectiveCount > bounds.maxCollectiveParticipants) {
    plan.feasibility = Feasibility::REJECT_ENDPOINT;
    plan.explanations.push_back("Collective participant count exceeds bound.");
    out.success = false;
    out.plan = std::move(plan);
    return out;
  }

  for (const EndpointId pid : req.participants) {
    auto it = emap.find(pid);
    if (it == emap.end()) {
      plan.feasibility = Feasibility::REJECT_ENDPOINT;
      plan.explanations.push_back("Missing collective participant " + pid.str() + ".");
      out.success = false;
      out.plan = std::move(plan);
      return out;
    }
    const Endpoint& e = it->second;
    if (!endpointFresh(e, snap) || !e.reachable || !e.ready || e.kind == EndpointKind::UNKNOWN) {
      plan.feasibility = Feasibility::REJECT_ENDPOINT;
      plan.explanations.push_back("Collective participant " + pid.str() + " not current/readable.");
      out.success = false;
      out.plan = std::move(plan);
      return out;
    }
  }

  // Build a spanning (fan-out) shape from the first participant as the root to
  // every other participant for broadcast/gather phases.
  const EndpointId root = req.participants.front();
  std::vector<CandidateReport> reports;
  std::vector<std::size_t> ranked;
  std::vector<CostComponent> comps;
  std::set<ResourceId> claims;
  std::set<std::pair<EndpointId, EndpointGeneration>> egs;
  std::set<std::pair<LinkId, LinkGeneration>> lgs;
  const auto lmap = snap.linkMap();

  for (const EndpointId member : req.participants) {
    if (member == root) continue;
    std::vector<CandidatePath> cands = enumerateCandidatePaths(snap, root, member, bounds);
    std::vector<Rankable> rankables;
    std::vector<std::size_t> foe;
    for (std::size_t j = 0; j < cands.size(); ++j) {
      const CandidatePath& c = cands[j];
      FeasibilityResult f = evaluateCandidateFeasibility(req, snap, c, bounds);
      if (f.feasible()) {
        CostSummary cst = evaluateCandidateCost(req, snap, c, weights, bounds);
        rankables.push_back(Rankable{c, f, cst});
        foe.push_back(j);
      }
    }
    std::vector<std::size_t> order = rankFeasible(rankables);
    if (order.empty()) {
      plan.feasibility = Feasibility::REJECT_ENDPOINT;
      plan.explanations.push_back("No feasible path between collective participants " + root.str() +
                                  " and " + member.str() + ".");
      out.success = false;
      out.plan = std::move(plan);
      return out;
    }
    const std::size_t bi = foe[order[0]];
    const CandidatePath& best = cands[bi];
    CostSummary cst = rankables[order[0]].cost;
    Path pt = internal::makePath(PathId(plan.candidatePaths.size() + 1), best, snap, req, cst);
    plan.candidatePaths.push_back(pt);
    ranked.push_back(plan.candidatePaths.size() - 1);
    for (const auto& cc : cst.components) comps.push_back(cc);
    for (const Stage& st : pt.stages) {
      auto li = lmap.find(st.link);
      if (li != lmap.end()) lgs.insert({li->second.id, li->second.generation});
      auto si = emap.find(st.source);
      auto di = emap.find(st.destination);
      if (si != emap.end()) egs.insert({si->second.id, si->second.generation});
      if (di != emap.end()) egs.insert({di->second.id, di->second.generation});
      if (!st.stagingResource.isNull()) claims.insert(st.stagingResource);
    }
  }

  plan.orderedStages = plan.candidatePaths.empty() ? std::vector<Stage>{} : plan.candidatePaths.front().stages;
  plan.cost = CostSummary::of(comps);
  if (!plan.candidatePaths.empty()) plan.primaryPath = plan.candidatePaths.front().id;
  plan.endpointGenerations.assign(egs.begin(), egs.end());
  plan.linkGenerations.assign(lgs.begin(), lgs.end());
  plan.requiredStagingResources.assign(claims.begin(), claims.end());
  plan.requiredResourceClaims.assign(claims.begin(), claims.end());
  plan.topologyGeneration = snap.topologyGeneration;
  plan.capacityGeneration = snap.capacityGeneration;
  plan.reservationGeneration = snap.reservationGeneration;
  plan.congestionGeneration = snap.congestionGeneration;
  plan.capabilityGeneration = snap.capabilityGeneration;
  plan.healthGeneration = snap.healthGeneration;
  plan.placementGeneration = snap.placementGeneration;
  plan.authorityGeneration = snap.authorityGeneration;
  plan.provenance = plan.cost.totalValid() ? internal::provName(worstProvenance(plan.cost)) : "UNKNOWN";
  plan.revalidationGeneration = RevalidationGeneration(1);
  plan.revalidateBeforeExecute = true;
  plan.feasibility = Feasibility::FEASIBLE;

  plan.explanations.push_back("Collective shape: " + std::string(toString(req.collectiveShape)));
  plan.explanations.push_back("Participants: " + std::to_string(req.participants.size()));
  plan.explanations.push_back("Feasible root-to-member paths: " + std::to_string(plan.candidatePaths.size()));
  plan.explanations.push_back("Collective scheduling is the responsibility of the Collective Scheduler.");

  out.candidates = std::move(reports);
  out.rankedIndices = ranked;
  out.success = true;
  out.plan = std::move(plan);
  return out;
}

}  // namespace internal

PlanOutcome planCollective(const CommunicationRequest& req,
                           const EvidenceSnapshot& snap,
                           const RankingWeights& weights,
                           const Bounds& bounds) {
  return internal::buildCollectivePlan(req, snap, weights, bounds);
}

}  // namespace communication_planner
