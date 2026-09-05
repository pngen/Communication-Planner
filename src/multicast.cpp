#include "communication_planner/multicast/multicast.hpp"
#include "communication_planner/feasibility/feasibility.hpp"
#include "communication_planner/ranking/ranking.hpp"
#include "communication_planner/ranking/cost.hpp"
#include "planner_internal.hpp"

#include <map>
#include <set>

namespace communication_planner {

static void bindGenerations(const std::vector<Path>& paths, const EvidenceSnapshot& snap,
                            std::vector<std::pair<EndpointId, EndpointGeneration>>& egs,
                            std::vector<std::pair<LinkId, LinkGeneration>>& lgs,
                            std::set<ResourceId>& claims) {
  const auto lmap = snap.linkMap();
  const auto emap = snap.endpointMap();
  std::set<std::pair<EndpointId, EndpointGeneration>> es;
  std::set<std::pair<LinkId, LinkGeneration>> ls;
  for (const Path& p : paths) {
    for (const Stage& st : p.stages) {
      auto li = lmap.find(st.link);
      if (li != lmap.end()) ls.insert({li->second.id, li->second.generation});
      auto si = emap.find(st.source);
      auto di = emap.find(st.destination);
      if (si != emap.end()) es.insert({si->second.id, si->second.generation});
      if (di != emap.end()) es.insert({di->second.id, di->second.generation});
      if (!st.stagingResource.isNull()) claims.insert(st.stagingResource);
    }
  }
  egs.assign(es.begin(), es.end());
  lgs.assign(ls.begin(), ls.end());
}

namespace internal {

PlanOutcome buildMulticastPlan(const CommunicationRequest& req,
                               const EvidenceSnapshot& snap,
                               const RankingWeights& weights,
                               const Bounds& bounds) {
  PlanOutcome out;
  CommunicationPlan plan;
  plan.id = CommunicationPlanId::next();
  plan.generation = CommunicationPlanGeneration(1);
  plan.requestId = req.id;
  plan.requestGeneration = req.generation;
  plan.shape = RequestShape::MULTICAST;
  plan.multicastDestinations = req.destinations;
  plan.feasibility = Feasibility::FEASIBLE;

  std::vector<CandidateReport> reports;
  std::vector<std::size_t> ranked;   // indices into reports (feasible retained order)
  bool anyFail = false;

  CostSummary combined;
  std::vector<CostComponent> comps;

  for (const EndpointId dest : req.destinations) {
    std::vector<CandidatePath> cands = enumerateCandidatePaths(snap, req.source, dest, bounds);
    std::vector<Rankable> rankables;
    std::vector<std::size_t> foe;
    std::vector<std::size_t> localReports;
    for (std::size_t j = 0; j < cands.size(); ++j) {
      const CandidatePath& c = cands[j];
      CandidateReport rep;
      rep.path = c;
      rep.feasibility = evaluateCandidateFeasibility(req, snap, c, bounds);
      rep.detail = rep.feasibility.detail;
      if (rep.feasibility.feasible()) {
        rep.cost = evaluateCandidateCost(req, snap, c, weights, bounds);
        rep.provenance = worstProvenance(rep.cost);
        rankables.push_back(Rankable{c, rep.feasibility, rep.cost});
        foe.push_back(j);
      } else {
        rep.provenance = Provenance::UNKNOWN;
      }
      localReports.push_back(reports.size());
      reports.push_back(std::move(rep));
    }
    std::vector<std::size_t> order = rankFeasible(rankables);
    if (order.empty()) {
      anyFail = true;
      plan.explanations.push_back("Destination " + dest.str() +
                                  " has no feasible path; multicast cannot be satisfied.");
      continue;
    }
    const std::size_t bestLocal = foe[order[0]];
    const std::size_t bestReportIdx = localReports[bestLocal];
    ranked.push_back(bestReportIdx);
    const CandidatePath& best = cands[bestLocal];
    Path pt = internal::makePath(PathId(plan.candidatePaths.size() + 1), best, snap, req,
                                 reports[bestReportIdx].cost);
    plan.candidatePaths.push_back(pt);
    for (const auto& c : reports[bestReportIdx].cost.components) comps.push_back(c);
    if (plan.primaryPath.isNull()) plan.primaryPath = pt.id;
  }

  plan.orderedStages = plan.candidatePaths.empty() ? std::vector<Stage>{} : plan.candidatePaths.front().stages;
  combined = CostSummary::of(comps);
  plan.cost = combined;

  std::set<ResourceId> claims;
  std::vector<std::pair<EndpointId, EndpointGeneration>> egs;
  std::vector<std::pair<LinkId, LinkGeneration>> lgs;
  bindGenerations(plan.candidatePaths, snap, egs, lgs, claims);
  plan.endpointGenerations = egs;
  plan.linkGenerations = lgs;
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
  plan.provenance = combined.totalValid() ? internal::provName(worstProvenance(combined)) : "UNKNOWN";
  plan.revalidationGeneration = RevalidationGeneration(1);
  plan.revalidateBeforeExecute = true;

  out.candidates = std::move(reports);
  out.rankedIndices = ranked;
  out.success = !anyFail;
  if (anyFail) plan.feasibility = Feasibility::REJECT_ENDPOINT;
  out.plan = std::move(plan);
  if (!out.success) out.explanations.push_back("Multicast plan rejected: one or more destinations infeasible.");
  return out;
}

}  // namespace internal

PlanOutcome planMulticast(const CommunicationRequest& req,
                          const EvidenceSnapshot& snap,
                          const RankingWeights& weights,
                          const Bounds& bounds) {
  return internal::buildMulticastPlan(req, snap, weights, bounds);
}

}  // namespace communication_planner
