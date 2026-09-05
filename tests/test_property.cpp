#include "framework.hpp"
#include "testutil.hpp"
#include "communication_planner/planner/planner.hpp"
#include "communication_planner/feasibility/feasibility.hpp"
#include "communication_planner/ranking/ranking.hpp"
#include "communication_planner/revalidation/revalidation.hpp"
#include "communication_planner/model/component.hpp"
#include "communication_planner/adapters/broker.hpp"

#include <random>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace communication_planner;
using cputil::mkEndpoint, cputil::mkLink, cputil::mkRequest;

static std::mt19937_64 rngFor(std::uint64_t seed) { return std::mt19937_64(seed); }

// Build a random sparse topology of n endpoints in a ring with random extra links.
static EvidenceSnapshot buildRandom(std::uint64_t seed, int n, std::uint64_t payload) {
  (void)payload;
  auto rng = rngFor(seed);
  EvidenceSnapshot s;
  s.topologyGeneration = TopologyGeneration(1);
  s.capacityGeneration = CapacityGeneration(1);
  s.reservationGeneration = ReservationGeneration(1);
  s.congestionGeneration = CongestionGeneration(1);
  s.capabilityGeneration = CapabilityGeneration(1);
  s.healthGeneration = HealthGeneration(1);
  s.placementGeneration = PlacementGeneration(1);
  s.policyGeneration = PolicyGeneration(1);
  s.authorityGeneration = currentAuthority();

  std::vector<EndpointId> ids;
  for (int i = 0; i < n; ++i) {
    EndpointId eid((std::uint64_t)(i + 1));
    ids.push_back(eid);
    EndpointKind kind = (i % 3 == 0) ? EndpointKind::GPU_DEVICE : (i % 3 == 1) ? EndpointKind::CPU_MEMORY : EndpointKind::NIC;
    std::vector<Transport> caps = {Transport::CUDA, Transport::HOST_MEMORY, Transport::PCIE, Transport::NETWORK};
    s.endpoints.push_back(mkEndpoint(eid, EndpointGeneration(1), kind, NodeId((std::uint64_t)(i % 4) + 1), true, true, {caps[i % 4]}));
  }
  // Ring links + random chords.
  std::uint64_t linkId = 1;
  auto addLink = [&](EndpointId a, EndpointId b, Provenance prov) {
    Link l = mkLink(LinkId(linkId), LinkGeneration(1), a, b,
                    (std::uint64_t)(rng() % 2) ? Transport::NETWORK : Transport::HOST_MEMORY,
                    (std::uint64_t)(1000000 + rng() % 10000000));
    l.provenance = prov;
    if ((rng() % 10) == 0) l.health.healthy = false;
    if ((rng() % 10) == 0) l.congestion.hardLimitExceeded = true;
    s.links.push_back(l);
    ++linkId;
  };
  for (int i = 0; i < n; ++i) {
    addLink(ids[i], ids[(i + 1) % n], Provenance::MEASURED);
  }
  for (int i = 0; i < n / 2; ++i) {
    EndpointId a = ids[(std::size_t)(rng() % n)];
    EndpointId b = ids[(std::size_t)(rng() % n)];
    if (a != b) addLink(a, b, Provenance::ESTIMATED);
  }
  return s;
}

CP_TEST(property_winner_satisfies_hard_constraints) {
  std::vector<std::uint64_t> seeds = {1, 2, 3, 42, 12345, 999, 777, 0xbeef, 0xdead, 0xfeed};
  for (std::uint64_t seed : seeds) {
    EvidenceSnapshot s = buildRandom(seed, 12, 8192);
    Bounds b; b.maxPathDepth = 6; b.maxCandidates = 512; b.maxFallbacks = 4;
    RankingWeights w;
    EndpointId src = EndpointId(1), dst = EndpointId(12);
    CommunicationRequest req = mkRequest(CommunicationRequestId((std::uint64_t)(seed + 1000)), src, dst, 8192);
    req.allowStaging = true; req.allowHostStaging = true; req.allowRelay = true;
    PlanOutcome po = planCommunication(req, s, w, b);
    if (po.success) {
      const CommunicationPlan& plan = *po.plan;
      // winner satisfies all hard constraints: re-run feasibility on the primary path.
      for (const Stage& st : plan.orderedStages) {
        // stage.payload == request payload (exact accounting)
        CHECK(st.payload.count() == req.payloadSize.count());
      }
      // candidatePaths each have consistent payload
      for (const Path& p : plan.candidatePaths) {
        CHECK(p.totalPayload.count() == req.payloadSize.count());
        for (const Stage& st : p.stages) CHECK(st.payload.count() == p.totalPayload.count());
      }
      // hard feasibility of primary: build CandidatePath from orderedStages
      CandidatePath cp;
      cp.source = plan.orderedStages.empty() ? EndpointId(0) : plan.orderedStages.front().source;
      cp.destination = plan.orderedStages.empty() ? EndpointId(0) : plan.orderedStages.back().destination;
      for (const Stage& st : plan.orderedStages) cp.hops.push_back(Hop{st.link, st.source, st.destination});
      FeasibilityResult fr = evaluateCandidateFeasibility(req, s, cp, b);
      CHECK(fr.feasible());  // hard-invalid must never be selected
    }
  }
}

CP_TEST(property_cost_finite_and_no_overflow) {
  for (std::uint64_t seed = 1; seed <= 8; ++seed) {
    EvidenceSnapshot s = buildRandom(seed * 13, 10, 1ull << 33);
    Bounds b; b.maxPathDepth = 6; b.maxCandidates = 256;
    RankingWeights w;
    CommunicationRequest req = mkRequest(CommunicationRequestId(seed + 5000), EndpointId(1), EndpointId(10), 1ull << 33);
    PlanOutcome po = planCommunication(req, s, w, b);
    for (const CandidateReport& cr : po.candidates) {
      if (cr.feasibility.feasible()) {
        CHECK(cr.cost.totalValid());
        for (const auto& c : cr.cost.components) { CHECK(c.isValid()); CHECK(c.value >= 0.0); }
        CHECK(!std::isnan(cr.cost.total) && !std::isinf(cr.cost.total));
      }
    }
  }
}

CP_TEST(property_determinism_and_insertion_permutation) {
  for (std::uint64_t seed : {10u, 20u, 30u}) {
    std::vector<uint64_t> firstSeq;
    for (int perm = 0; perm < 8; ++perm) {
      EvidenceSnapshot s = buildRandom(seed, 10, 4096);
      auto rng = rngFor(seed + perm);
      std::shuffle(s.links.begin(), s.links.end(), rng);
      std::shuffle(s.endpoints.begin(), s.endpoints.end(), rng);
      Bounds b; b.maxPathDepth = 6; b.maxCandidates = 256;
      RankingWeights w;
      CommunicationRequest req = mkRequest(CommunicationRequestId((std::uint64_t)perm + 100), EndpointId(1), EndpointId(10), 4096);
      PlanOutcome po = planCommunication(req, s, w, b);
      if (po.success) {
        std::vector<uint64_t> seq;
        for (const Stage& st : po.plan->orderedStages) seq.push_back(st.link.value());
        if (firstSeq.empty()) firstSeq = seq;
        else CHECK(seq == firstSeq);
      }
    }
  }
}

CP_TEST(property_ranking_matches_reference_minimum) {
  // A slow reference: the best candidate must be the feasible one with min total cost.
  for (std::uint64_t seed : {5u, 6u, 7u}) {
    EvidenceSnapshot s = buildRandom(seed, 8, 4096);
    Bounds b; b.maxPathDepth = 6; b.maxCandidates = 256;
    RankingWeights w;
    CommunicationRequest req = mkRequest(CommunicationRequestId(seed + 300), EndpointId(1), EndpointId(8), 4096);
    PlanOutcome po = planCommunication(req, s, w, b);
    if (po.success && po.rankedIndices.size() >= 1) {
      const std::size_t best = po.rankedIndices[0];
      const CandidateReport& bestRep = po.candidates[best];
      CHECK(bestRep.feasibility.feasible());
      // every other feasible candidate must have total >= best total (or equal with tie-break)
      for (std::size_t idx : po.rankedIndices) {
        const CandidateReport& rep = po.candidates[idx];
        if (rep.feasibility.feasible()) CHECK(rep.cost.total >= bestRep.cost.total - 1e-9);
      }
    }
  }
}

CP_TEST(property_generation_never_moves_backward) {
  Generation<TopologyGenerationTag> g(5);
  Generation<TopologyGenerationTag> g2 = g.next();
  CHECK(g2.value() > g.value());
  CHECK(!(g2 < g));
  // distinct generation types never compare
  Generation<LinkGenerationTag> lg(5);
  (void)lg;
  CHECK(g.value() == 5);
}

CP_MAIN();
