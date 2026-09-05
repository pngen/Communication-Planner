#include "communication_planner/revalidation/revalidation.hpp"
#include "communication_planner/feasibility/feasibility.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"

namespace communication_planner {

static bool genOk(std::uint64_t current, std::uint64_t bound, std::uint64_t& delta) {
  if (current < bound) { delta = bound - current; return false; }
  return true;
}

RevalidationResult revalidatePath(const Path& path, const EvidenceSnapshot& snap) {
  RevalidationResult r;
  r.ok = true;
  r.feasibility = Feasibility::FEASIBLE;
  const auto emap = snap.endpointMap();
  const auto lmap = snap.linkMap();
  for (const Stage& st : path.stages) {
    auto li = lmap.find(st.link);
    if (li == lmap.end()) {
      r.ok = false; r.feasibility = Feasibility::REVALIDATION_REQUIRED;
      r.detail = "link missing: " + st.link.str(); r.changes.push_back(r.detail);
      return r;
    }
    const Link& link = li->second;
    if (!link.health.healthy) {
      r.ok = false; r.detail = "link unhealthy"; r.changes.push_back(r.detail);
      return r;
    }
    if (link.congestion.hardLimitExceeded) {
      r.ok = false; r.detail = "link congestion hard limit"; r.changes.push_back(r.detail);
      return r;
    }
    auto si = emap.find(st.source);
    auto di = emap.find(st.destination);
    if (si == emap.end() || di == emap.end()) {
      r.ok = false; r.detail = "endpoint missing"; r.changes.push_back(r.detail);
      return r;
    }
    if (!endpointFresh(si->second, snap) || !endpointFresh(di->second, snap)) {
      r.ok = false; r.feasibility = Feasibility::REJECT_STALE_ENDPOINT;
      r.detail = "endpoint incarnation stale"; r.changes.push_back(r.detail);
      return r;
    }
    if (!si->second.reachable || !si->second.ready || !di->second.reachable || !di->second.ready) {
      r.ok = false; r.detail = "endpoint not ready"; r.changes.push_back(r.detail);
      return r;
    }
  }
  return r;
}

RevalidationResult revalidatePlan(const CommunicationPlan& plan, const EvidenceSnapshot& snap) {
  RevalidationResult r;
  r.ok = true;
  r.feasibility = Feasibility::REVALIDATION_REQUIRED;  // default; FEASIBLE only when all facts are current
  bool allOk = true;
  const auto emap = snap.endpointMap();
  const auto lmap = snap.linkMap();

  // Generation monotonicity: never regression.
  {
    std::uint64_t d = 0;
    if (!genOk(snap.topologyGeneration.value(), plan.topologyGeneration.value(), d)) {
      r.ok = false; r.detail = "topology generation regressed"; r.changes.push_back(r.detail);
      return r;
    }
    if (!genOk(snap.capacityGeneration.value(), plan.capacityGeneration.value(), d)) {
      r.ok = false; r.detail = "capacity generation regressed"; r.changes.push_back(r.detail);
      return r;
    }
    if (!genOk(snap.congestionGeneration.value(), plan.congestionGeneration.value(), d)) {
      r.ok = false; r.detail = "congestion generation regressed"; r.changes.push_back(r.detail);
      return r;
    }
  }

  for (const auto& pr : plan.endpointGenerations) {
    auto it = emap.find(pr.first);
    if (it == emap.end()) {
      r.ok = false; r.detail = "endpoint " + pr.first.str() + " missing";
      r.changes.push_back(r.detail); return r;
    }
    if (it->second.generation != pr.second) {
      r.ok = false; r.detail = "endpoint " + pr.first.str() + " generation changed";
      r.changes.push_back(r.detail); return r;
    }
    if (!endpointFresh(it->second, snap) || !it->second.reachable || !it->second.ready) {
      r.ok = false; r.feasibility = Feasibility::REJECT_STALE_ENDPOINT;
      r.detail = "endpoint " + pr.first.str() + " no longer fresh/ready";
      r.changes.push_back(r.detail); return r;
    }
  }

  for (const auto& pr : plan.linkGenerations) {
    auto it = lmap.find(pr.first);
    if (it == lmap.end()) {
      r.ok = false; r.detail = "link " + pr.first.str() + " missing";
      r.changes.push_back(r.detail); return r;
    }
    if (it->second.generation != pr.second) {
      r.ok = false; r.detail = "link " + pr.first.str() + " generation changed";
      r.changes.push_back(r.detail); return r;
    }
    if (!it->second.health.healthy || it->second.congestion.hardLimitExceeded) {
      r.ok = false; r.detail = "link " + pr.first.str() + " unhealthy/congested";
      r.changes.push_back(r.detail); return r;
    }
  }

  // Revalidate every candidate path stages.
  for (const Path& p : plan.candidatePaths) {
    const RevalidationResult pr = revalidatePath(p, snap);
    if (!pr.ok) { r = pr; return r; }
    if (pr.feasibility != Feasibility::FEASIBLE) allOk = false;
  }

  // Resource availability for required staging stays above zero.
  for (const ResourceId rid : plan.requiredStagingResources) {
    bool found = false;
    for (const auto& rc : snap.resourceCapacities) {
      if (rc.first == rid) { found = true; if (rc.second.usable.count() < 1) {
        r.ok = false; r.detail = "staging resource " + rid.str() + " unavailable";
        r.changes.push_back(r.detail); return r; } }
    }
    if (!found) {
      r.ok = false; r.detail = "staging resource " + rid.str() + " not committed";
      r.changes.push_back(r.detail); return r;
    }
  }

  if (allOk) r.feasibility = Feasibility::FEASIBLE;
  return r;
}

}  // namespace communication_planner
