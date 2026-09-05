#include "framework.hpp"
#include "testutil.hpp"
#include "communication_planner/planner/planner.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/feasibility/feasibility.hpp"
#include "communication_planner/revalidation/revalidation.hpp"
#include "communication_planner/persistence/persistence.hpp"
#include "communication_planner/lifecycle/lifecycle.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include <random>
#include <vector>
#include <cmath>

using namespace communication_planner;
using cputil::mkEndpoint, cputil::mkLink, cputil::mkRequest;

static EvidenceSnapshot baseSnap() {
  EvidenceSnapshot s;
  s.topologyGeneration=TopologyGeneration(1); s.congestionGeneration=CongestionGeneration(1);
  s.capacityGeneration=CapacityGeneration(1); s.capabilityGeneration=CapabilityGeneration(1);
  s.healthGeneration=HealthGeneration(1); s.placementGeneration=PlacementGeneration(1);
  s.policyGeneration=PolicyGeneration(1); s.authorityGeneration=currentAuthority();
  s.endpoints.push_back(mkEndpoint(EndpointId(1),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(1),true,true,{Transport::HOST_MEMORY,Transport::PCIE}));
  s.endpoints.push_back(mkEndpoint(EndpointId(2),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(2),true,true,{Transport::HOST_MEMORY,Transport::PCIE}));
  s.links.push_back(mkLink(LinkId(1),LinkGeneration(1),EndpointId(1),EndpointId(2),Transport::HOST_MEMORY,1000000));
  return s;
}

CP_TEST(adversarial_stale_endpoint_generation) {
  Bounds b; b.maxPathDepth=4; b.maxCandidates=32; RankingWeights w;
  EvidenceSnapshot s = baseSnap();
  CommunicationRequest req = mkRequest(CommunicationRequestId(1),EndpointId(1),EndpointId(2),4096);
  PlanOutcome po = planCommunication(req, s, w, b);
  REQUIRE(po.success);
  // Advance endpoint generation -> plan must be invalidated.
  s.endpoints[0].generation = EndpointGeneration(2);
  RevalidationResult rr = revalidatePlan(*po.plan, s);
  CHECK(!rr.ok);
  CHECK(rr.feasibility == Feasibility::REVALIDATION_REQUIRED);
}

CP_TEST(adversarial_stale_worker_boot_rejected) {
  // An endpoint published by a worker whose boot is no longer current must be stale.
  Bounds b; b.maxPathDepth=4; b.maxCandidates=32; RankingWeights w;
  EvidenceSnapshot s = baseSnap();
  WorkerId wid(1); WorkerBootId bootA(100);
  s.endpoints[0].worker = wid; s.endpoints[0].workerBoot = bootA;
  s.endpoints[1].worker = wid; s.endpoints[1].workerBoot = bootA;
  s.workerBoots[wid] = bootA;
  CommunicationRequest req = mkRequest(CommunicationRequestId(2),EndpointId(1),EndpointId(2),4096);
  PlanOutcome po = planCommunication(req, s, w, b);
  REQUIRE(po.success);
  // Worker re-incarnates with a new boot -> old boot fenced.
  s.workerBoots[wid] = WorkerBootId(200);
  RevalidationResult rr = revalidatePlan(*po.plan, s);
  CHECK(!rr.ok);
  // A stale-publisher replay is rejected by endpoint freshness.
  FeasibilityResult fr = evaluateCandidateFeasibility(req, s, CandidatePath{EndpointId(1),EndpointId(2),{Hop{LinkId(1),EndpointId(1),EndpointId(2)}}}, b);
  CHECK(!fr.feasible());
}

CP_TEST(adversarial_payload_mismatch_rejected_by_persistence) {
  StoreState st; st.epoch=CoordinatorEpoch(1);
  CommunicationPlan plan;
  plan.id=CommunicationPlanId(1); plan.generation=CommunicationPlanGeneration(1);
  plan.requestId=CommunicationRequestId(1); plan.requestGeneration=CommunicationRequestGeneration(1);
  plan.primaryPath=PathId(1); plan.feasibility=Feasibility::FEASIBLE;
  Path p; p.id=PathId(1); p.source=EndpointId(1); p.destination=EndpointId(2); p.totalPayload=Bytes(1000);
  Stage stg; stg.link=LinkId(1); stg.source=EndpointId(1); stg.destination=EndpointId(2);
  stg.payload=Bytes(999);  // mismatch
  p.stages.push_back(stg);
  plan.candidatePaths.push_back(p);
  plan.orderedStages.push_back(stg);
  StoreRecord rec; rec.plan=plan; rec.state=PlanState::PLAN_READY;
  st.plans.push_back(rec);
  std::vector<std::byte> bytes = serializeStore(st);
  StoreState out; PersistError e = parseStore(bytes.data(), bytes.size(), out);
  CHECK(!e.ok());
  CHECK(e.code == PersistErrorCode::PAYLOAD_MISMATCH);
}

CP_TEST(adversarial_nan_cost_rejected) {
  StoreState st; st.epoch=CoordinatorEpoch(1);
  CommunicationPlan plan;
  plan.id=CommunicationPlanId(2); plan.generation=CommunicationPlanGeneration(1);
  plan.requestId=CommunicationRequestId(2); plan.requestGeneration=CommunicationRequestGeneration(1);
  plan.primaryPath=PathId(1); plan.feasibility=Feasibility::FEASIBLE;
  Path p; p.id=PathId(1); p.source=EndpointId(1); p.destination=EndpointId(2); p.totalPayload=Bytes(1000);
  Stage stg; stg.link=LinkId(1); stg.source=EndpointId(1); stg.destination=EndpointId(2); stg.payload=Bytes(1000);
  p.stages.push_back(stg); plan.candidatePaths.push_back(p); plan.orderedStages.push_back(stg);
  plan.cost.total = std::numeric_limits<double>::infinity();  // Inf cost
  StoreRecord rec; rec.plan=plan; rec.state=PlanState::PLAN_READY;
  st.plans.push_back(rec);
  std::vector<std::byte> bytes = serializeStore(st);
  StoreState out; PersistError e = parseStore(bytes.data(), bytes.size(), out);
  CHECK(!e.ok());
  CHECK(e.code == PersistErrorCode::NAN_INF_COST);
}

CP_TEST(adversarial_stage_cycle_rejected) {
  StoreState st; st.epoch=CoordinatorEpoch(1);
  CommunicationPlan plan;
  plan.id=CommunicationPlanId(3); plan.generation=CommunicationPlanGeneration(1);
  plan.requestId=CommunicationRequestId(3); plan.requestGeneration=CommunicationRequestGeneration(1);
  plan.primaryPath=PathId(1); plan.feasibility=Feasibility::FEASIBLE;
  Path p; p.id=PathId(1); p.source=EndpointId(1); p.destination=EndpointId(2); p.totalPayload=Bytes(1000);
  Stage s1; s1.link=LinkId(1); s1.source=EndpointId(1); s1.destination=EndpointId(2); s1.payload=Bytes(1000);
  Stage s2; s2.link=LinkId(2); s2.source=EndpointId(2); s2.destination=EndpointId(1); s2.payload=Bytes(1000);
  Stage s3; s3.link=LinkId(3); s3.source=EndpointId(1); s3.destination=EndpointId(2); s3.payload=Bytes(1000); // revisits endpoint 2 -> cycle
  p.stages.push_back(s1); p.stages.push_back(s2); p.stages.push_back(s3);
  plan.candidatePaths.push_back(p); plan.orderedStages.push_back(s1);
  StoreRecord rec; rec.plan=plan; rec.state=PlanState::PLAN_READY;
  st.plans.push_back(rec);
  std::vector<std::byte> bytes = serializeStore(st);
  StoreState out; PersistError e = parseStore(bytes.data(), bytes.size(), out);
  CHECK(!e.ok());
  // Either cycle or invalid stage order (source != prev destination) is rejected.
  CHECK(e.code == PersistErrorCode::CYCLE_FORBIDDEN || e.code == PersistErrorCode::INVALID_STAGE_ORDER);
}

CP_TEST(adversarial_committed_plan_without_evidence_rejected) {
  StoreState st; st.epoch=CoordinatorEpoch(1);
  CommunicationPlan plan;
  plan.id=CommunicationPlanId(4); plan.generation=CommunicationPlanGeneration(1);
  plan.requestId=CommunicationRequestId(4); plan.requestGeneration=CommunicationRequestGeneration(1);
  plan.primaryPath=PathId(1); plan.feasibility=Feasibility::FEASIBLE;
  Path p; p.id=PathId(1); p.source=EndpointId(1); p.destination=EndpointId(2); p.totalPayload=Bytes(1000);
  Stage stg; stg.link=LinkId(1); stg.source=EndpointId(1); stg.destination=EndpointId(2); stg.payload=Bytes(1000);
  p.stages.push_back(stg); plan.candidatePaths.push_back(p); plan.orderedStages.push_back(stg);
  plan.endpointGenerations = {{EndpointId(1), EndpointGeneration(1)}, {EndpointId(2), EndpointGeneration(1)}};
  StoreRecord rec; rec.plan=plan; rec.state=PlanState::COMMITTED;  // committed but no resource claims
  st.plans.push_back(rec);
  std::vector<std::byte> bytes = serializeStore(st);
  StoreState out; PersistError e = parseStore(bytes.data(), bytes.size(), out);
  CHECK(!e.ok());
  CHECK(e.code == PersistErrorCode::COMMITTED_PLAN_NO_EVIDENCE);
}

CP_TEST(adversarial_hard_invalid_never_wins_due_to_score) {
  // A link that is unhealthy (hard invalid) must not win even if it is the only/fastest path.
  Bounds b; b.maxPathDepth=4; b.maxCandidates=32; RankingWeights w;
  EvidenceSnapshot s = baseSnap();
  s.links[0].health.healthy = false;
  CommunicationRequest req = mkRequest(CommunicationRequestId(5),EndpointId(1),EndpointId(2),4096);
  PlanOutcome po = planCommunication(req, s, w, b);
  CHECK(!po.success);  // no feasible path; hard invalid excluded
}

CP_TEST(adversarial_generation_regression_rejected) {
  Bounds b; b.maxPathDepth=4; b.maxCandidates=32; RankingWeights w;
  EvidenceSnapshot s = baseSnap();
  CommunicationRequest req = mkRequest(CommunicationRequestId(6),EndpointId(1),EndpointId(2),4096);
  PlanOutcome po = planCommunication(req, s, w, b);
  REQUIRE(po.success);
  EvidenceSnapshot s2 = s;
  s2.topologyGeneration = TopologyGeneration(0);  // regressed below plan's bound
  RevalidationResult rr = revalidatePlan(*po.plan, s2);
  CHECK(!rr.ok);
}

CP_MAIN();
