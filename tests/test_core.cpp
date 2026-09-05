#include "framework.hpp"
#include "testutil.hpp"
#include "communication_planner/planner/planner.hpp"
#include "communication_planner/feasibility/feasibility.hpp"
#include "communication_planner/ranking/ranking.hpp"
#include "communication_planner/ranking/cost.hpp"
#include "communication_planner/lifecycle/lifecycle.hpp"
#include "communication_planner/revalidation/revalidation.hpp"
#include "communication_planner/persistence/persistence.hpp"
#include "communication_planner/protocol/protocol.hpp"
#include "communication_planner/multicast/multicast.hpp"
#include "communication_planner/collective/collective.hpp"
#include "communication_planner/adapters/broker.hpp"

#include <algorithm>
#include <random>
#include <vector>
#include <cstring>

using namespace communication_planner;
using cputil::mkEndpoint, cputil::mkLink, cputil::mkRequest;

static EvidenceSnapshot buildBasic(Bounds* outBounds = nullptr, RankingWeights* outW = nullptr) {
  Bounds b;
  b.maxPathDepth = 6; b.maxCandidates = 256; b.maxFallbacks = 4;
  RankingWeights w;
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

  NodeId n1(1), n2(2);
  EndpointId gpuA(10), gpuB(11), host(12);
  WorkerBootId wkBoot(7);
  s.workerBoots[WorkerId(10)] = wkBoot;

  // endpoints
  s.endpoints.push_back(mkEndpoint(gpuA, EndpointGeneration(1), EndpointKind::GPU_DEVICE, n1, true, true,
    {Transport::CUDA, Transport::HOST_MEMORY, Transport::PCIE}));
  s.endpoints.push_back(mkEndpoint(gpuB, EndpointGeneration(1), EndpointKind::GPU_DEVICE, n2, true, true,
    {Transport::CUDA, Transport::HOST_MEMORY, Transport::PCIE}));
  s.endpoints.push_back(mkEndpoint(host, EndpointGeneration(1), EndpointKind::PINNED_HOST_MEMORY, n1, true, true,
    {Transport::HOST_MEMORY, Transport::PCIE}));
  (void)wkBoot;

  // links: direct gpuA->gpuB (host memory), staged via host
  s.links.push_back(mkLink(LinkId(1), LinkGeneration(1), gpuA, gpuB, Transport::HOST_MEMORY, 1000000));
  s.links.push_back(mkLink(LinkId(2), LinkGeneration(1), gpuA, host, Transport::PCIE, 2000000));
  s.links.push_back(mkLink(LinkId(3), LinkGeneration(1), host, gpuB, Transport::PCIE, 2000000));

  if (outBounds) *outBounds = b;
  if (outW) *outW = w;
  return s;
}

CP_TEST(request_validation) {
  CommunicationRequest r;
  r.id = CommunicationRequestId(1); r.generation = CommunicationRequestGeneration(1);
  r.source = EndpointId(1); r.destinations.push_back(EndpointId(2));
  r.payloadSize = Bytes(0);
  REQUIRE(!r.validate(nullptr));           // zero payload rejected
  r.payloadSize = Bytes(100);
  REQUIRE(r.validate(nullptr));
  r.maxHops = 0; REQUIRE(!r.validate(nullptr));
  r.maxHops = 16;
  r.allowed.insert(Transport::PCIE); r.forbidden.insert(Transport::PCIE);
  REQUIRE(!r.validate(nullptr));           // conflict
  r.forbidden.clear();
  r.shape = RequestShape::COLLECTIVE; r.collectiveShape = CollectiveShape::UNKNOWN;
  REQUIRE(!r.validate(nullptr));
}

CP_TEST(equality_deterministic_ranking_direct_first) {
  Bounds b; RankingWeights w; EvidenceSnapshot s = buildBasic(&b, &w);
  CommunicationRequest req = mkRequest(CommunicationRequestId(5), EndpointId(10), EndpointId(11), 4096);
  req.requireValid();
  PlanOutcome po = planCommunication(req, s, w, b);
  REQUIRE(po.success);
  REQUIRE(po.plan.has_value());
  const CommunicationPlan& plan = *po.plan;
  // direct (1 hop) should have lowest cost
  REQUIRE(plan.orderedStages.size() == 1);
  const auto lm = s.linkMap();
  const Link& l = lm.at(plan.orderedStages.front().link);
  REQUIRE(l.source == EndpointId(10) && l.destination == EndpointId(11));
  // winner satisfies hard constraints
  CHECK(plan.feasibility == Feasibility::FEASIBLE);
}

CP_TEST(hard_filter_forces_staged) {
  Bounds b; RankingWeights w; EvidenceSnapshot s = buildBasic(&b, &w);
  // Make the direct link forbidden -> only staged path (via host) feasible.
  CommunicationRequest req = mkRequest(CommunicationRequestId(6), EndpointId(10), EndpointId(11), 4096);
  req.forbidden.insert(Transport::HOST_MEMORY);
  req.allowStaging = true; req.allowHostStaging = true;
  PlanOutcome po = planCommunication(req, s, w, b);
  REQUIRE(po.success);
  REQUIRE(po.plan.has_value());
  CHECK(po.plan->orderedStages.size() == 2);  // gpuA->host->gpuB staged
}

CP_TEST(hard_invalid_never_wins) {
  Bounds b; RankingWeights w; EvidenceSnapshot s = buildBasic(&b, &w);
  // Direct path unhealthy; staged path present. Direct must not win despite low hop.
  s.links[0].health.healthy = false;
  CommunicationRequest req = mkRequest(CommunicationRequestId(7), EndpointId(10), EndpointId(11), 4096);
  PlanOutcome po = planCommunication(req, s, w, b);
  REQUIRE(po.success);
  // staged path selected
  CHECK(po.plan->orderedStages.size() == 2);
}

CP_TEST(unknown_endpoint_not_feasible) {
  Bounds b; RankingWeights w; EvidenceSnapshot s = buildBasic(&b, &w);
  s.endpoints[0].kind = EndpointKind::UNKNOWN;
  CommunicationRequest req = mkRequest(CommunicationRequestId(8), EndpointId(10), EndpointId(11), 4096);
  PlanOutcome po = planCommunication(req, s, w, b);
  CHECK(!po.success);
}

CP_TEST(insertion_order_does_not_change_result) {
  Bounds b; RankingWeights w;
  std::vector<CommunicationPlan> results;
  std::mt19937 rng(1234u);
  for (int trial = 0; trial < 20; ++trial) {
    EvidenceSnapshot s = buildBasic(&b, &w);
    // shuffle link/endpoint insertion order deterministically
    std::shuffle(s.links.begin(), s.links.end(), rng);
    std::shuffle(s.endpoints.begin(), s.endpoints.end(), rng);
    CommunicationRequest req = mkRequest(CommunicationRequestId((uint64_t)trial + 1), EndpointId(10), EndpointId(11), 4096);
    PlanOutcome po = planCommunication(req, s, w, b);
    REQUIRE(po.success);
    results.push_back(*po.plan);
  }
  // All plans must have identical primary path link sequence.
  std::vector<uint64_t> firstSeq;
  for (size_t i = 0; i < results.size(); ++i) {
    std::vector<uint64_t> seq;
    for (const Stage& st : results[i].orderedStages) seq.push_back(st.link.value());
    if (i == 0) firstSeq = seq;
    else CHECK(seq == firstSeq);
  }
}

CP_TEST(cost_never_nan_inf) {
  Bounds b; RankingWeights w; EvidenceSnapshot s = buildBasic(&b, &w);
  CommunicationRequest req = mkRequest(CommunicationRequestId(9), EndpointId(10), EndpointId(11), 1ull << 30);
  PlanOutcome po = planCommunication(req, s, w, b);
  REQUIRE(po.success);
  for (const CandidateReport& cr : po.candidates) {
    if (cr.feasibility.feasible()) {
      CHECK(cr.cost.totalValid());
      for (const auto& c : cr.cost.components) CHECK(c.isValid());
    }
  }
}

CP_TEST(lifecycle_illegal_transitions) {
  CHECK(!PlanLifecycle::canTransition(PlanState::REQUESTED, PlanState::ACTIVE));
  CHECK(!PlanLifecycle::canTransition(PlanState::COMPLETED, PlanState::ACTIVE));
  CHECK(!PlanLifecycle::canTransition(PlanState::CANCELLED, PlanState::ACTIVE));
  CHECK(PlanLifecycle::canTransition(PlanState::REQUESTED, PlanState::DISCOVERING));
  CHECK(PlanLifecycle::canTransition(PlanState::PLAN_READY, PlanState::AWAITING_RESOURCE_COMMIT));
  CHECK(PlanLifecycle::canTransition(PlanState::AWAITING_RESOURCE_COMMIT, PlanState::COMMITTED));
  CHECK(PlanLifecycle::canTransition(PlanState::COMMITTED, PlanState::AWAITING_EXECUTION));
  CHECK(PlanLifecycle::canTransition(PlanState::AWAITING_EXECUTION, PlanState::ACTIVE));
  CHECK(PlanLifecycle::canTransition(PlanState::ACTIVE, PlanState::COMPLETED));
}

CP_TEST(revalidation_generation_change) {
  Bounds b; RankingWeights w; EvidenceSnapshot s = buildBasic(&b, &w);
  CommunicationRequest req = mkRequest(CommunicationRequestId(10), EndpointId(10), EndpointId(11), 4096);
  PlanOutcome po = planCommunication(req, s, w, b);
  REQUIRE(po.success);
  RevalidationResult ok = revalidatePlan(*po.plan, s);
  CHECK(ok.ok);
  // bump link generation -> revalidation must fail
  EvidenceSnapshot s2 = s;
  s2.links[0].generation = LinkGeneration(2);
  RevalidationResult changed = revalidatePlan(*po.plan, s2);
  CHECK(!changed.ok);
  CHECK(changed.feasibility == Feasibility::REVALIDATION_REQUIRED);
  // topology regression
  EvidenceSnapshot s3 = s;
  s3.topologyGeneration = TopologyGeneration(0);
  RevalidationResult reg = revalidatePlan(*po.plan, s3);
  CHECK(!reg.ok);
}

CP_TEST(persistence_roundtrip_and_corruption) {
  Bounds b; RankingWeights w; EvidenceSnapshot s = buildBasic(&b, &w);
  CommunicationRequest req = mkRequest(CommunicationRequestId(11), EndpointId(10), EndpointId(11), 4096);
  PlanOutcome po = planCommunication(req, s, w, b);
  REQUIRE(po.success);
  StoreState st;
  st.epoch = CoordinatorEpoch(3);
  StoreRecord rec; rec.plan = *po.plan; rec.state = PlanState::PLAN_READY;
  st.plans.push_back(rec);
  st.requests.push_back(req);
  std::vector<std::byte> bytes = serializeStore(st);
  StoreState out;
  PersistError e = parseStore(bytes.data(), bytes.size(), out);
  CHECK(e.ok());
  CHECK(out.plans.size() == 1);
  CHECK(out.plans[0].plan.id == st.plans[0].plan.id);
  CHECK(out.plans[0].plan.orderedStages.size() == st.plans[0].plan.orderedStages.size());

  // corruption: flip a byte in the payload
  std::vector<std::byte> bad = bytes;
  bad[bad.size() / 2] = static_cast<std::byte>(static_cast<std::uint8_t>(bad[bad.size()/2]) ^ 0x40);
  StoreState out2;
  PersistError e2 = parseStore(bad.data(), bad.size(), out2);
  CHECK(!e2.ok() && e2.code == PersistErrorCode::CHECKSUM_MISMATCH);

  // truncation
  StoreState out3;
  PersistError e3 = parseStore(bytes.data(), bytes.size() - 3, out3);
  CHECK(!e3.ok());

  // bad magic
  std::vector<std::byte> bm = bytes; bm[0] = std::byte{0x00};
  StoreState out4;
  PersistError e4 = parseStore(bm.data(), bm.size(), out4);
  CHECK(!e4.ok() && e4.code == PersistErrorCode::BAD_MAGIC);

  // trailing garbage
  std::vector<std::byte> tg = bytes; tg.push_back(std::byte{0x7f});
  StoreState out5;
  PersistError e5 = parseStore(tg.data(), tg.size(), out5);
  CHECK(!e5.ok() && e5.code == PersistErrorCode::TRAILING_GARBAGE);

  // conservative recovery
  StoreState sr = st;
  sr.plans[0].state = PlanState::COMMITTED;
  conservativeRecovery(sr);
  CHECK(sr.plans[0].state == PlanState::REVALIDATION_REQUIRED);
}

CP_TEST(protocol_frame_roundtrip) {
  Message m; m.type = MessageType::HELLO; m.epoch = CoordinatorEpoch(9);
  std::vector<std::byte> payload; CHECK(encodeMessage(m, payload).ok());
  Frame f; f.type = m.type; f.payload = payload;
  std::vector<std::byte> frame; CHECK(encodeFrame(f, frame).ok());
  Frame f2; ProtocolError e = decodeFrame(frame.data(), frame.size(), f2);
  CHECK(e.ok());
  CHECK(f2.type == MessageType::HELLO);
  Message m2; ProtocolError e2 = decodeMessage(f2.type, f2.payload.data(), f2.payload.size(), m2);
  CHECK(e2.ok());
  CHECK(m2.epoch == CoordinatorEpoch(9));

  // checksum mismatch
  std::vector<std::byte> c = frame; c[c.size()-1] = static_cast<std::byte>((std::uint8_t)c[c.size()-1] ^ 0x01);
  Frame f3; ProtocolError e3 = decodeFrame(c.data(), c.size(), f3);
  CHECK(!e3.ok());

  // truncation
  Frame f4; ProtocolError e4 = decodeFrame(frame.data(), frame.size()-2, f4);
  CHECK(!e4.ok());

  // invalid type
  std::vector<std::byte> it = frame; it[8] = std::byte{0xFF};
  Frame f5; ProtocolError e5 = decodeFrame(it.data(), it.size(), f5);
  CHECK(!e5.ok());
}

CP_TEST(rankable_tiebreak_and_provenance) {
  // Two candidates with identical cost -> tie broken by fewer unknowns / stable id order.
  CandidatePath p1; p1.source=EndpointId(1); p1.destination=EndpointId(3);
  p1.hops.push_back(Hop{LinkId(1), EndpointId(1), EndpointId(3)});
  CandidatePath p2; p2.source=EndpointId(1); p2.destination=EndpointId(3);
  p2.hops.push_back(Hop{LinkId(2), EndpointId(1), EndpointId(3)});
  CostSummary c1; c1.components = {CostComponent{CostFactor::HOP_COUNT, 1.0, 1.0, Provenance::MEASURED}};
  c1.total = 1.0;
  CostSummary c2 = c1;
  std::vector<Rankable> in = { Rankable{p1, FeasibilityResult::ok(), c1}, Rankable{p2, FeasibilityResult::ok(), c2} };
  std::vector<std::size_t> order = rankFeasible(in);
  REQUIRE(order.size() == 2);
  CHECK(order[0] == 0);  // p1 has smaller link id
}

CP_TEST(broker_all_or_nothing) {
  MemoryResourceBroker brk(1024);
  std::vector<ResourceId> res = {ResourceId(1)}; std::vector<Bytes> sz = {Bytes(600)};
  std::string err;
  CHECK(brk.acquire(res, sz, err));
  CHECK(!brk.acquire(res, std::vector<Bytes>{Bytes(600)}, err));  // would exceed 1024
  brk.release(res);
  CHECK(brk.acquire(res, sz, err));
}

CP_MAIN();
