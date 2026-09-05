#include "communication_planner/ranking/cost.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"

#include <cmath>
#include <limits>

namespace communication_planner {

static double clampCost(double v) {
  if (std::isnan(v)) return kMaxCost;
  if (v < 0.0) return 0.0;
  if (std::isinf(v) || v > kMaxCost) return kMaxCost;
  return v;
}

static double toTransferNs(std::uint64_t payload, std::uint64_t bwPerSec) {
  if (payload == 0) return 0.0;
  if (bwPerSec == 0) return kMaxCost;
  double ns = (static_cast<double>(payload) * 1e9) / static_cast<double>(bwPerSec);
  return clampCost(ns);
}

CostSummary evaluateCandidateCost(const CommunicationRequest& req,
                                  const EvidenceSnapshot& snap,
                                  const CandidatePath& cand,
                                  const RankingWeights& weights,
                                  const Bounds& bounds) {
  (void)req; (void)snap; (void)weights; (void)bounds;
  const auto lmap = snap.linkMap();
  const auto emap = snap.endpointMap();

  const std::uint64_t payload = req.payloadSize.count();

  double latNs = 0.0, transferNs = 0.0, queueNs = 0.0, congPenNs = 0.0;
  double hostStageNs = 0.0, stageNs = 0.0;
  double numaLowNs = 0.0, pcieNs = 0.0, nicNs = 0.0, storageNs = 0.0;
  double healthRiskNs = 0.0, reservationNs = 0.0, headroomNs = 0.0, fdRiskNs = 0.0;
  double reliabilityNs = 0.0, movementNs = 0.0;
  Provenance worstProv = Provenance::MEASURED;

  bool hasStaging = cand.hops.size() > 1;
  bool hasHostStaging = false;
  bool hasStorageStaging = false;

  for (std::size_t i = 0; i + 1 < cand.hops.size(); ++i) {
    auto it = emap.find(cand.hops[i].to);
    if (it != emap.end()) {
      const EndpointKind kind = it->second.kind;
      if (kind == EndpointKind::STORAGE) hasStorageStaging = true;
      else if (kind == EndpointKind::CPU_MEMORY || kind == EndpointKind::PINNED_HOST_MEMORY ||
               kind == EndpointKind::NUMA_MEMORY || kind == EndpointKind::CACHE ||
               kind == EndpointKind::CHECKPOINT || kind == EndpointKind::MODEL_RESIDENCY ||
               kind == EndpointKind::STATE_LOCATION || kind == EndpointKind::PROCESS ||
               kind == EndpointKind::SERVICE)
        hasHostStaging = true;
    }
  }

  for (const Hop& hop : cand.hops) {
    auto li = lmap.find(hop.link);
    if (li == lmap.end()) continue;
    const Link& link = li->second;

    std::uint64_t bw = link.effectiveBandwidth.count();
    if (bw == 0) bw = link.capacity.count();
    const double t = toTransferNs(payload, bw);
    transferNs += clampCost(t);
    latNs += clampCost(static_cast<double>(link.latency.count()));
    queueNs += clampCost(link.congestion.penalty);
    congPenNs += clampCost(link.congestion.penalty);

    // headroom: less headroom => more contention cost
    const std::uint64_t headroom = link.capacityEvidence.headroom.count();
    if (headroom > 0) {
      const double ratio = static_cast<double>(bw) / static_cast<double>(headroom + 1);
      headroomNs += clampCost(t * std::min(ratio, 100.0));
    } else if (link.capacityEvidence.provenance != Provenance::UNKNOWN) {
      headroomNs += clampCost(t * 2.0); // no declared headroom => contention risk
    }

    // locality / topology distance penalties
    auto si = emap.find(hop.from);
    auto di = emap.find(hop.to);
    if (si != emap.end() && di != emap.end()) {
      const int nd = std::abs(di->second.location.numaDistance - si->second.location.numaDistance);
      numaLowNs += clampCost(static_cast<double>(nd) * 200.0);
      if (si->second.location.pcieRoot != di->second.location.pcieRoot)
        pcieNs += clampCost(static_cast<double>(payload) * 1e-6);
      if (si->second.location.nic != di->second.location.nic)
        nicNs += clampCost(static_cast<double>(payload) * 2e-6);
      if (si->second.kind == EndpointKind::STORAGE || di->second.kind == EndpointKind::STORAGE)
        storageNs += clampCost(static_cast<double>(payload) * 1e-6);
    }

    if (link.provenance == Provenance::UNKNOWN || link.provenance == Provenance::ESTIMATED ||
        link.provenance == Provenance::FORECAST || link.provenance == Provenance::SYNTHETIC) {
      healthRiskNs += clampCost(static_cast<double>(transferNs) * 0.1);
    }
    if (link.provenance == Provenance::SYNTHETIC) worstProv = Provenance::SYNTHETIC;
    if (link.provenance == Provenance::UNKNOWN) worstProv = Provenance::UNKNOWN;
  }

  if (hasStaging) {
    stageNs += clampCost(static_cast<double>(payload) * 4e-6); // staging buffer + copy overhead
  }
  if (hasHostStaging) {
    hostStageNs += clampCost(static_cast<double>(payload) * 3e-6);
  }
  if (hasStorageStaging) {
    storageNs += clampCost(static_cast<double>(payload) * 6e-6);
  }

  // failure-domain risk and reliability
  if (!req.requiredFailureDomain.empty()) fdRiskNs = 0.0; // already satisfied
  reliabilityNs = clampCost(static_cast<double>(cand.hops.size()) * 1e-3);

  movementNs = clampCost(transferNs + stageNs + hostStageNs);

  const auto comp = [&](CostFactor f, double value, Provenance prov) -> CostComponent {
    return CostComponent{f, weights.weightOf(f), clampCost(value), prov};
  };

  std::vector<CostComponent> comps;
  comps.push_back(comp(CostFactor::PATH_LATENCY, latNs, worstProv));
  comps.push_back(comp(CostFactor::EFFECTIVE_BANDWIDTH, transferNs, worstProv));
  comps.push_back(comp(CostFactor::RESIDUAL_BANDWIDTH, headroomNs, worstProv));
  comps.push_back(comp(CostFactor::CONGESTION_PENALTY, congPenNs, worstProv));
  comps.push_back(comp(CostFactor::QUEUEING_ESTIMATE, queueNs, worstProv));
  comps.push_back(comp(CostFactor::HOP_COUNT, static_cast<double>(cand.hops.size()), Provenance::DERIVED));
  comps.push_back(comp(CostFactor::STAGE_COUNT, static_cast<double>(cand.hops.size()), Provenance::DERIVED));
  comps.push_back(comp(CostFactor::STAGING_OVERHEAD, stageNs, hasStaging ? worstProv : Provenance::DERIVED));
  comps.push_back(comp(CostFactor::HOST_STAGING_COST, hostStageNs, hasHostStaging ? worstProv : Provenance::DERIVED));
  comps.push_back(comp(CostFactor::NUMA_DISTANCE, numaLowNs, Provenance::DERIVED));
  comps.push_back(comp(CostFactor::PCIE_LOCALITY, pcieNs, Provenance::DERIVED));
  comps.push_back(comp(CostFactor::NIC_LOCALITY, nicNs, Provenance::DERIVED));
  comps.push_back(comp(CostFactor::STORAGE_LOCALITY, storageNs, Provenance::DERIVED));
  comps.push_back(comp(CostFactor::ENDPOINT_HEALTH, healthRiskNs, worstProv));
  comps.push_back(comp(CostFactor::RESERVATION_FIT, reservationNs, Provenance::DERIVED));
  comps.push_back(comp(CostFactor::CAPACITY_HEADROOM, headroomNs, worstProv));
  comps.push_back(comp(CostFactor::FAILURE_DOMAIN_RISK, fdRiskNs, Provenance::DERIVED));
  comps.push_back(comp(CostFactor::RELIABILITY, reliabilityNs, Provenance::DERIVED));
  comps.push_back(comp(CostFactor::MOVEMENT_COST, movementNs, worstProv));

  return CostSummary::of(comps);
}

}  // namespace communication_planner
