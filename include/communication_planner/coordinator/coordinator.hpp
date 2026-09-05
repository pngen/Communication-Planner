#pragma once
#include <string>
#include <memory>
#include <map>
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/planner/planner.hpp"
#include "communication_planner/model/request.hpp"
#include "communication_planner/model/plan.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include "communication_planner/planner/snapshot.hpp"

namespace communication_planner {

// A communication-plan coordinator: authoritative evidence snapshot + plan
// lifecycle + authority/supersession/cancellation. Runs as an independent
// process and speaks the framed TCP protocol.
class CoordinatorServer {
 public:
  CoordinatorServer(const RankingWeights& weights, const Bounds& bounds,
                    const std::string& storePath);
  ~CoordinatorServer();

  CoordinatorServer(const CoordinatorServer&) = delete;
  CoordinatorServer& operator=(const CoordinatorServer&) = delete;

  // Bind/listen and start the accept loop on a background thread.
  bool start(unsigned short port, std::string& err);
  unsigned short boundPort() const;
  void stop();
  CoordinatorEpoch epoch() const;
  std::size_t currentPlanCount() const;
  // True when at least one plan is in REVALIDATION_REQUIRED after recovery.
  bool hasRevalidatedPlans() const;
  PlanState planState(CommunicationPlanId id) const;
  void saveStore() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Blocking client used by controllers / proof / workers to talk to a coordinator.
class CoordinatorClient {
 public:
  ~CoordinatorClient();
  bool connect(const std::string& host, unsigned short port, std::string& err);
  void disconnect();

  bool registerWorker(WorkerId worker, WorkerBootId boot, const std::string& name, std::string& err);
  bool publishEndpoint(WorkerId worker, WorkerBootId boot, const Endpoint& e, std::string& err);
  bool publishLink(WorkerId worker, WorkerBootId boot, const Link& l, std::string& err);
  bool publishCapacity(LinkId link, const Capacity& cap, std::string& err);
  bool publishCongestion(LinkId link, const Congestion& cong, std::string& err);

  bool submitRequest(const CommunicationRequest& req, CommunicationPlan& out, bool& feasible, std::string& err);
  bool commitPlan(CommunicationPlanId id, bool& ok, std::string& err);
  bool revalidate(CommunicationPlanId id, bool& fresh, std::string& err);
  bool cancel(CommunicationRequestId reqId, CommunicationPlanId planId, std::string& err);
  bool supersede(CommunicationRequestId reqId, CommunicationPlanId planId, std::string& err);
  bool executionHandoff(CommunicationPlanId id, bool& ok, std::string& err);
  bool executionResult(CommunicationPlanId id, bool ok, std::string& err);
  bool save(std::string& err);
  bool shutdown(std::string& err);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace communication_planner
