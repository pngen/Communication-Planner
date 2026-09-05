#include "communication_planner/coordinator/coordinator.hpp"
#include "communication_planner/protocol/protocol.hpp"
#include "communication_planner/planner/planner.hpp"
#include "communication_planner/revalidation/revalidation.hpp"
#include "communication_planner/lifecycle/lifecycle.hpp"
#include "communication_planner/persistence/persistence.hpp"
#include "communication_planner/adapters/broker.hpp"
#include "transport/tcp.hpp"

// winsock2.h/windows.h define these macros; our enums use the same names.
#undef ERROR
#undef FAILED

#include <mutex>
#include <thread>
#include <atomic>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

namespace communication_planner {

struct WorkerInfo {
  WorkerId id;
  WorkerBootId boot;
  std::string name;
  SOCKET sock{INVALID_SOCKET};
  std::vector<EndpointId> endpoints;
  std::vector<LinkId> links;
  bool alive{true};
};

struct PlanRecord {
  CommunicationPlan plan;
  PlanState state{PlanState::REQUESTED};
  WorkerId worker;          // owner of the primary source endpoint (executor)
  WorkerBootId boot;        // worker's boot bound at plan time (executor authority)
};

struct CoordinatorServer::Impl {
  RankingWeights weights;
  Bounds bounds;
  std::string storePath;

  mutable std::mutex mtx;
  EvidenceSnapshot snap;
  CoordinatorEpoch epoch{1};
  MemoryResourceBroker broker;

  std::map<CommunicationPlanId, PlanRecord> plans;
  std::map<CommunicationRequestId, CommunicationPlanId> currentByRequest;
  std::set<CommunicationRequestId> cancelled;
  std::vector<SupersessionRecord> supersessions;
  std::vector<CommunicationRequest> requests;

  transport::Listener listener;
  std::thread acceptThread;
  std::atomic<bool> stopping{false};

  std::map<SOCKET, WorkerId> socketToWorker;
  std::map<WorkerId, WorkerInfo> workers;
  std::set<std::pair<WorkerId, WorkerBootId>> fenced;  // dead worker+boot authorities (stale replay rejected)
  std::set<SOCKET> activeSockets;
  std::vector<std::thread> connThreads;

  Impl(const RankingWeights& w, const Bounds& b, std::string sp)
    : weights(w), bounds(b), storePath(std::move(sp)), epoch(1) {
    snap.epoch = epoch;
    snap.authorityGeneration = currentAuthority();
    snap.topologyGeneration = TopologyGeneration(1);
    snap.capacityGeneration = CapacityGeneration(1);
    snap.reservationGeneration = ReservationGeneration(1);
    snap.congestionGeneration = CongestionGeneration(1);
    snap.capabilityGeneration = CapabilityGeneration(1);
    snap.healthGeneration = HealthGeneration(1);
    snap.placementGeneration = PlacementGeneration(1);
    snap.policyGeneration = PolicyGeneration(1);
  }

  void sendFrame(SOCKET s, MessageType t, const Message& m) {
    Frame f; f.type = t;
    std::vector<std::byte> pl;
    encodeMessage(m, pl);
    f.payload = pl;
    std::string e; transport::sendFrame(s, f, e); (void)e;
  }

  void sendError(SOCKET s, const std::string& e) {
    Message m; m.type = MessageType::ERROR; m.error = e;
    sendFrame(s, MessageType::ERROR, m);
  }
  void sendOk(SOCKET s, MessageType /*ignored*/, std::uint64_t id) {
    Message m; m.type = MessageType::COMMIT_RESULT; m.ok = true; m.planId = CommunicationPlanId(id);
    sendFrame(s, MessageType::COMMIT_RESULT, m);
  }

  void publishEndpoint(const Endpoint& e) {
    bool replaced = false;
    for (auto& ex : snap.endpoints) if (ex.id == e.id) { ex = e; replaced = true; break; }
    if (!replaced) snap.endpoints.push_back(e);
  }
  void publishLink(const Link& l) {
    bool replaced = false;
    for (auto& lx : snap.links) if (lx.id == l.id) { lx = l; replaced = true; break; }
    if (!replaced) snap.links.push_back(l);
    snap.topologyGeneration = snap.topologyGeneration.next();
  }

  void loadStoreIfAny() {
    std::ifstream ifs(storePath, std::ios::binary | std::ios::ate);
    if (!ifs) return;
    std::streamsize sz = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    if (sz <= 0) return;
    std::vector<char> buf((std::size_t)sz);
    ifs.read(buf.data(), sz);
    StoreState st;
    PersistError pe = parseStore(reinterpret_cast<const std::byte*>(buf.data()), (std::size_t)sz, st);
    if (!pe.ok()) return;
    // A new coordinator epoch makes the old epoch stale.
    epoch = st.epoch.next();
    snap.epoch = epoch;
    conservativeRecovery(st);
    for (const StoreRecord& rec : st.plans) {
      PlanRecord pr; pr.plan = rec.plan; pr.state = rec.state;
      plans[rec.plan.id] = pr;
      currentByRequest[rec.plan.requestId] = rec.plan.id;
    }
    supersessions = st.supersessions;
    requests = st.requests;
  }

  void fenceWorker(WorkerId id) {
    snap.workerBoots.erase(id);
    snap.sourceBoots.erase(SourceId(id.value()));
    auto it = workers.find(id);
    if (it == workers.end()) return;
    WorkerInfo& w = it->second;
    fenced.insert({id, w.boot});  // old boot is permanently stale
    w.alive = false;
    for (EndpointId eid : w.endpoints) {
      for (auto& e : snap.endpoints) if (e.id == eid) { e.reachable = false; e.ready = false; }
    }
    std::set<LinkId> owned(w.links.begin(), w.links.end());
    snap.links.erase(std::remove_if(snap.links.begin(), snap.links.end(),
                      [&owned](const Link& l){ return owned.count(l.id) != 0; }),
                     snap.links.end());
    snap.topologyGeneration = snap.topologyGeneration.next();
  }

  void acceptLoop() {
    while (!stopping.load()) {
      std::string err;
      SOCKET s = listener.accept(err);
      if (s == INVALID_SOCKET) {
        if (stopping.load()) break;
        continue;
      }
      {
        std::lock_guard<std::mutex> lk(mtx);
        activeSockets.insert(s);
      }
      connThreads.emplace_back([this, s](){ handleConnection(s); });
    }
  }

  void handleConnection(SOCKET s) {
    {
      Message hm; hm.type = MessageType::HELLO; hm.epoch = epoch;
      std::lock_guard<std::mutex> lk(mtx);
      sendFrame(s, MessageType::HELLO, hm);
    }
    while (!stopping.load()) {
      Frame f; std::string err;
      if (!transport::recvFrame(s, f, err, -1)) {
        std::lock_guard<std::mutex> lk(mtx);
        auto si = socketToWorker.find(s);
        if (si != socketToWorker.end()) {
          // Worker disconnected: fence it.
          fenceWorker(si->second);
          socketToWorker.erase(si);
        }
        break;
      }
      Message m;
      ProtocolError pe = decodeMessage(f.type, f.payload.data(), f.payload.size(), m);
      if (!pe.ok()) { sendError(s, pe.detail); continue; }
      handleMessage(s, m);
    }
    transport::closeSock(s);
    std::lock_guard<std::mutex> lk(mtx);
    activeSockets.erase(s);
  }

  void handleMessage(SOCKET s, const Message& m);

  // Commit processing with broker acquire performed OUTSIDE the state mutex.
  void handleCommit(SOCKET s, const Message& m) {
    CommunicationPlanId id = m.planId;
    std::vector<ResourceId> resources;
    std::vector<Bytes> sizes;
    {
      std::lock_guard<std::mutex> lk(mtx);
      auto it = plans.find(id);
      if (it == plans.end()) { sendError(s, "plan not found"); return; }
      PlanRecord& rec = it->second;
      if (rec.state != PlanState::PLAN_READY && rec.state != PlanState::AWAITING_RESOURCE_COMMIT) {
        sendError(s, "plan not committable"); return;
      }
      if (cancelled.count(rec.plan.requestId)) { sendError(s, "request cancelled"); return; }
      rec.state = PlanState::AWAITING_RESOURCE_COMMIT;
      resources = rec.plan.requiredResourceClaims;
      std::uint64_t payload = rec.plan.orderedStages.empty() ? 1024 : rec.plan.orderedStages.front().payload.count();
      for (std::size_t i = 0; i < resources.size(); ++i) sizes.push_back(Bytes(payload));
    }
    // Broker acquire OUTSIDE the lock.
    std::string berr;
    const bool acquired = broker.acquire(resources, sizes, berr);
    {
      std::lock_guard<std::mutex> lk(mtx);
      auto it = plans.find(id);
      if (it == plans.end()) {
        if (acquired) broker.release(resources);
        sendError(s, "plan missing after commit"); return;
      }
      PlanRecord& rec = it->second;
      if (cancelled.count(rec.plan.requestId) || rec.state == PlanState::CANCELLED || rec.state == PlanState::SUPERSEDED || rec.state == PlanState::RETIRED) {
        if (acquired) broker.release(resources);
        rec.state = PlanState::CANCELLED;
        sendError(s, "cancelled during commit"); return;
      }
      if (!acquired) {
        rec.state = PlanState::FAILED;
        sendError(s, "resource commit failed: " + berr);
        return;
      }
      rec.state = PlanState::COMMITTED;
      sendOk(s, MessageType::COMMIT_RESULT, id.value());
    }
  }
};

void CoordinatorServer::Impl::handleMessage(SOCKET s, const Message& m) {
  switch (m.type) {
    case MessageType::REGISTER: {
      std::lock_guard<std::mutex> lk(mtx);
      if (m.worker.isNull() || m.boot.isNull()) { sendError(s, "null worker/boot"); return; }
      if (fenced.count({m.worker, m.boot})) { sendError(s, "fenced boot authority"); return; }
      WorkerInfo w; w.id = m.worker; w.boot = m.boot; w.name = m.name; w.sock = s; w.alive = true;
      workers[m.worker] = w;
      socketToWorker[s] = m.worker;
      snap.workerBoots[m.worker] = m.boot;
      snap.sourceBoots[SourceId(m.worker.value())] = SourceBootId(m.boot.value());
      sendOk(s, MessageType::REGISTER, m.worker.value());
      return;
    }
    case MessageType::PUBLISH_ENDPOINT: {
      std::lock_guard<std::mutex> lk(mtx);
      auto wk = workers.find(m.worker);
      if (wk == workers.end() || !wk->second.alive || wk->second.boot != m.boot) { sendError(s, "stale publisher authority"); return; }
      Endpoint e = m.endpoint; e.worker = m.worker; e.workerBoot = m.boot;
      publishEndpoint(e);
      wk->second.endpoints.push_back(e.id);
      sendOk(s, MessageType::PUBLISH_ENDPOINT, e.id.value());
      return;
    }
    case MessageType::PUBLISH_LINK: {
      std::lock_guard<std::mutex> lk(mtx);
      auto wk = workers.find(m.worker);
      if (wk == workers.end() || !wk->second.alive || wk->second.boot != m.boot) { sendError(s, "stale publisher authority"); return; }
      Link l = m.link; l.workerBoot = m.boot;
      publishLink(l);
      wk->second.links.push_back(l.id);
      sendOk(s, MessageType::PUBLISH_LINK, l.id.value());
      return;
    }
    case MessageType::PUBLISH_CAPACITY: {
      std::lock_guard<std::mutex> lk(mtx);
      if (!m.worker.isNull()) {
        auto wk = workers.find(m.worker);
        if (wk == workers.end() || !wk->second.alive || wk->second.boot != m.boot) { sendError(s, "stale publisher authority"); return; }
      }
      snap.capacityGeneration = snap.capacityGeneration.next();
      for (auto& l : snap.links) if (l.id == m.linkId) l.capacityEvidence = m.capacity;
      sendOk(s, MessageType::PUBLISH_CAPACITY, m.linkId.value());
      return;
    }
    case MessageType::PUBLISH_CONGESTION: {
      std::lock_guard<std::mutex> lk(mtx);
      if (!m.worker.isNull()) {
        auto wk = workers.find(m.worker);
        if (wk == workers.end() || !wk->second.alive || wk->second.boot != m.boot) { sendError(s, "stale publisher authority"); return; }
      }
      snap.congestionGeneration = snap.congestionGeneration.next();
      for (auto& l : snap.links) if (l.id == m.linkId) l.congestion = m.congestion;
      sendOk(s, MessageType::PUBLISH_CONGESTION, m.linkId.value());
      return;
    }
    case MessageType::QUERY_PLAN: {
      std::lock_guard<std::mutex> lk(mtx);
      auto it = plans.find(m.planId);
      Message qm; qm.type = MessageType::COMMIT_RESULT;
      if (it != plans.end()) { qm.ok = true; qm.planId = CommunicationPlanId((std::uint64_t)it->second.state); }
      sendFrame(s, MessageType::COMMIT_RESULT, qm);
      return;
    }
    case MessageType::SUBMIT_REQUEST: {
      CommunicationPlan planOut; bool feasible = false;
      {
        std::lock_guard<std::mutex> lk(mtx);
        if (m.request.id.isNull()) { sendError(s, "null request"); return; }
        if (cancelled.count(m.request.id)) { sendError(s, "request already cancelled"); return; }
        requests.push_back(m.request);
        PlanOutcome po = planCommunication(m.request, snap, weights, bounds);
        feasible = po.success;
        if (feasible && po.plan) {
          CommunicationPlan plan = std::move(*po.plan);
          PlanRecord rec; rec.plan = plan; rec.state = PlanState::PLAN_READY;
          if (!plan.orderedStages.empty()) {
            const EndpointId srcId = plan.orderedStages.front().source;
            for (const Endpoint& e : snap.endpoints) if (e.id == srcId) { rec.worker = e.worker; rec.boot = e.workerBoot; break; }
          }
          plans[plan.id] = rec;
          currentByRequest[m.request.id] = plan.id;
          planOut = plan;
        }
      }
      Message pm; pm.type = MessageType::PLAN_RESULT; pm.ok = feasible;
      if (feasible) { pm.plan = planOut; }
      sendFrame(s, MessageType::PLAN_RESULT, pm);
      return;
    }
    case MessageType::COMMIT_REQUEST: { handleCommit(s, m); return; }
    case MessageType::REVALIDATE: {
      bool fresh = false;
      {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = plans.find(m.planId);
        if (it != plans.end()) {
          RevalidationResult rr = revalidatePlan(it->second.plan, snap);
          if (rr.ok) { it->second.state = PlanState::AWAITING_EXECUTION; fresh = true; }
          else { it->second.state = PlanState::REVALIDATION_REQUIRED; fresh = false; }
        }
      }
      Message rm; rm.type = MessageType::COMMIT_RESULT; rm.ok = fresh; rm.planId = m.planId;
      sendFrame(s, MessageType::COMMIT_RESULT, rm);
      return;
    }
    case MessageType::EXECUTION_HANDOFF: {
      bool ok = false;
      SOCKET workerSock = INVALID_SOCKET;
      Frame executeFrame;
      {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = plans.find(m.planId);
        if (it != plans.end()) {
          RevalidationResult rr = revalidatePlan(it->second.plan, snap);
          if (rr.ok) {
            it->second.state = PlanState::ACTIVE; ok = true;
            if (!it->second.worker.isNull()) {
              auto wk = workers.find(it->second.worker);
              if (wk != workers.end() && wk->second.alive) {
                workerSock = wk->second.sock;
                Message em; em.type = MessageType::EXECUTE; em.planId = it->second.plan.id;
                em.bytes = it->second.plan.orderedStages.empty() ? 0 : it->second.plan.orderedStages.front().payload.count();
                em.worker = it->second.worker; em.boot = it->second.boot;
                executeFrame.type = MessageType::EXECUTE;
                std::vector<std::byte> plv; encodeMessage(em, plv); executeFrame.payload = plv;
              }
            }
          } else { it->second.state = PlanState::REVALIDATION_REQUIRED; ok = false; }
        }
      }
      if (ok && workerSock != INVALID_SOCKET) { std::string e2; transport::sendFrame(workerSock, executeFrame, e2); (void)e2; }
      Message rm; rm.type = MessageType::COMMIT_RESULT; rm.ok = ok; rm.planId = m.planId;
      sendFrame(s, MessageType::COMMIT_RESULT, rm);
      return;
    }
    case MessageType::EXECUTION_RESULT: {
      std::lock_guard<std::mutex> lk(mtx);
      auto it = plans.find(m.planId);
      if (it != plans.end() && it->second.state == PlanState::ACTIVE) {
        if (!m.worker.isNull()) {
          if (it->second.worker != m.worker || it->second.boot != m.boot) { sendError(s, "stale result authority"); return; }
        }
        it->second.state = m.ok ? PlanState::COMPLETED : PlanState::FAILED;
      } else {
        // stale result for a non-active plan is ignored (no mutation of current state)
        Message rm; rm.type = MessageType::COMMIT_RESULT; rm.ok = false; rm.planId = m.planId; rm.error = "stale result";
        sendFrame(s, MessageType::COMMIT_RESULT, rm);
        return;
      }
      Message rm; rm.type = MessageType::COMMIT_RESULT; rm.ok = true; rm.planId = m.planId;
      sendFrame(s, MessageType::COMMIT_RESULT, rm);
      return;
    }
    case MessageType::CANCEL: {
      std::lock_guard<std::mutex> lk(mtx);
      cancelled.insert(m.requestId);
      auto it = plans.find(m.planId);
      if (it != plans.end()) {
        if (it->second.state == PlanState::COMMITTED || it->second.state == PlanState::AWAITING_EXECUTION ||
            it->second.state == PlanState::ACTIVE || it->second.state == PlanState::AWAITING_RESOURCE_COMMIT) {
          // Release provisional claims through owning adapter.
          broker.release(it->second.plan.requiredResourceClaims);
        }
        if (it->second.state != PlanState::COMPLETED && it->second.state != PlanState::CANCELLED) {
          it->second.state = PlanState::CANCELLED;
        }
      }
      Message rm; rm.type = MessageType::COMMIT_RESULT; rm.ok = true; rm.planId = m.planId;
      sendFrame(s, MessageType::COMMIT_RESULT, rm);
      return;
    }
    case MessageType::SUPERSEDE: {
      std::lock_guard<std::mutex> lk(mtx);
      auto it = plans.find(m.planId);
      if (it != plans.end() && it->second.state != PlanState::COMPLETED && it->second.state != PlanState::CANCELLED) {
        it->second.state = PlanState::SUPERSEDED;
        broker.release(it->second.plan.requiredResourceClaims);
      }
      // find older plan for same request
      auto cur = currentByRequest.find(m.requestId);
      if (cur != currentByRequest.end() && cur->second != m.planId) {
        auto old = plans.find(cur->second);
        if (old != plans.end()) { old->second.state = PlanState::SUPERSEDED; supersessions.push_back({cur->second, m.planId}); }
      }
      Message rm; rm.type = MessageType::COMMIT_RESULT; rm.ok = true; rm.planId = m.planId;
      sendFrame(s, MessageType::COMMIT_RESULT, rm);
      return;
    }
    case MessageType::SAVE: {
      StoreState st; st.epoch = epoch;
      {
        std::lock_guard<std::mutex> lk(mtx);
        for (const auto& [id, rec] : plans) { StoreRecord sr; sr.plan = rec.plan; sr.state = rec.state; st.plans.push_back(sr); }
        st.supersessions = supersessions;
        st.requests = requests;
      }
      std::vector<std::byte> bytes = serializeStore(st);
      std::ofstream ofs(storePath, std::ios::binary);
      ofs.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
      ofs.close();
      Message rm; rm.type = MessageType::COMMIT_RESULT; rm.ok = true;
      sendFrame(s, MessageType::COMMIT_RESULT, rm);
      return;
    }
    case MessageType::SHUTDOWN: {
      stopping.store(true);
      Message rm; rm.type = MessageType::COMMIT_RESULT; rm.ok = true;
      sendFrame(s, MessageType::COMMIT_RESULT, rm);
      return;
    }
    default: sendError(s, "unsupported message"); return;
  }
}

CoordinatorServer::CoordinatorServer(const RankingWeights& weights, const Bounds& bounds,
                                     const std::string& storePath)
  : impl_(std::make_unique<Impl>(weights, bounds, storePath)) {}
CoordinatorServer::~CoordinatorServer() { stop(); }

bool CoordinatorServer::start(unsigned short port, std::string& err) {
  transport::libraryInit();
  std::lock_guard<std::mutex> lk(impl_->mtx);
  impl_->loadStoreIfAny();
  if (!impl_->listener.listenOn(port, err)) return false;
  impl_->acceptThread = std::thread([this](){ impl_->acceptLoop(); });
  return true;
}
bool CoordinatorServer::isStopping() const { return impl_->stopping.load(); }
unsigned short CoordinatorServer::boundPort() const { return impl_->listener.port(); }
CoordinatorEpoch CoordinatorServer::epoch() const { return impl_->epoch; }

void CoordinatorServer::stop() {
  impl_->stopping.store(true);
  // Close sockets to unblock recv.
  {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->listener.close();
    for (SOCKET s : impl_->activeSockets) transport::closeSock(s);
  }
  if (impl_->acceptThread.joinable()) impl_->acceptThread.join();
  for (auto& t : impl_->connThreads) if (t.joinable()) t.join();
  transport::libraryCleanup();
}

std::size_t CoordinatorServer::currentPlanCount() const { std::lock_guard<std::mutex> lk(impl_->mtx); return impl_->plans.size(); }
bool CoordinatorServer::hasRevalidatedPlans() const {
  std::lock_guard<std::mutex> lk(impl_->mtx);
  for (const auto& [id, rec] : impl_->plans) if (rec.state == PlanState::REVALIDATION_REQUIRED) return true;
  return false;
}
PlanState CoordinatorServer::planState(CommunicationPlanId id) const {
  std::lock_guard<std::mutex> lk(impl_->mtx);
  auto it = impl_->plans.find(id);
  return it != impl_->plans.end() ? it->second.state : PlanState::RETIRED;
}
void CoordinatorServer::saveStore() const {
  StoreState st; st.epoch = impl_->epoch;
  {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    for (const auto& [id, rec] : impl_->plans) { StoreRecord sr; sr.plan = rec.plan; sr.state = rec.state; st.plans.push_back(sr); }
    st.supersessions = impl_->supersessions;
    st.requests = impl_->requests;
  }
  std::vector<std::byte> bytes = serializeStore(st);
  std::ofstream ofs(impl_->storePath, std::ios::binary);
  ofs.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
  ofs.close();
}

// -------------------------------- Client -------------------------------------
struct CoordinatorClient::Impl {
  SOCKET sock{INVALID_SOCKET};
  bool connected{false};

  bool send(const Message& m, Message& reply) {
    if (!connected) return false;
    Frame f; f.type = m.type;
    std::vector<std::byte> pl; encodeMessage(m, pl); f.payload = pl;
    std::string err;
    if (!transport::sendFrame(sock, f, err)) return false;
    Frame rf; if (!transport::recvFrame(sock, rf, err, -1)) return false;
    ProtocolError pe = decodeMessage(rf.type, rf.payload.data(), rf.payload.size(), reply);
    return pe.ok();
  }
};

CoordinatorClient::~CoordinatorClient() { if (impl_) { disconnect(); delete impl_; impl_ = nullptr; } }
void CoordinatorClient::disconnect() { if (impl_ && impl_->connected) { transport::closeSock(impl_->sock); impl_->connected = false; } }
bool CoordinatorClient::connect(const std::string& host, unsigned short port, std::string& err) {
  if (impl_) { delete impl_; impl_ = nullptr; }
  impl_ = new Impl;
  transport::libraryInit();
  impl_->sock = transport::connectTo(host, port, err);
  if (impl_->sock == INVALID_SOCKET) return false;
  impl_->connected = true;
  // read HELLO
  Frame hf; if (!transport::recvFrame(impl_->sock, hf, err, -1)) { disconnect(); return false; }
  return true;
}

#define CP_CLIENT_SEND(body) do { if (!impl_) { err="not connected"; return false; } Message reply; if (!impl_->send(m, reply)) { err="transport error"; return false; } if (reply.type==MessageType::ERROR) { err=reply.error; return false; } body } while (0)

bool CoordinatorClient::registerWorker(WorkerId worker, WorkerBootId boot, const std::string& name, std::string& err) {
  Message m; m.type=MessageType::REGISTER; m.worker=worker; m.boot=boot; m.name=name;
  CP_CLIENT_SEND({ return reply.ok; });
}
bool CoordinatorClient::publishEndpoint(WorkerId worker, WorkerBootId boot, const Endpoint& e, std::string& err) {
  Message m; m.type=MessageType::PUBLISH_ENDPOINT; m.worker=worker; m.boot=boot; m.endpoint=e;
  CP_CLIENT_SEND({ return reply.ok; });
}
bool CoordinatorClient::publishLink(WorkerId worker, WorkerBootId boot, const Link& l, std::string& err) {
  Message m; m.type=MessageType::PUBLISH_LINK; m.worker=worker; m.boot=boot; m.link=l;
  CP_CLIENT_SEND({ return reply.ok; });
}
bool CoordinatorClient::publishCapacity(LinkId link, const Capacity& cap, std::string& err) {
  Message m; m.type=MessageType::PUBLISH_CAPACITY; m.linkId=link; m.capacity=cap;
  CP_CLIENT_SEND({ return reply.ok; });
}
bool CoordinatorClient::publishCongestion(LinkId link, const Congestion& cong, std::string& err) {
  Message m; m.type=MessageType::PUBLISH_CONGESTION; m.linkId=link; m.congestion=cong;
  CP_CLIENT_SEND({ return reply.ok; });
}
bool CoordinatorClient::submitRequest(const CommunicationRequest& req, CommunicationPlan& out, bool& feasible, std::string& err) {
  if (!impl_) { err="not connected"; return false; }
  Message m; m.type=MessageType::SUBMIT_REQUEST; m.request=req;
  Message reply; if (!impl_->send(m, reply)) { err="transport error"; return false; }
  if (reply.type==MessageType::ERROR) { err=reply.error; return false; }
  feasible = reply.ok;
  if (feasible) out = reply.plan;
  return true;
}
bool CoordinatorClient::commitPlan(CommunicationPlanId id, bool& ok, std::string& err) {
  Message m; m.type=MessageType::COMMIT_REQUEST; m.planId=id;
  CP_CLIENT_SEND({ ok = reply.ok; return true; });
}
bool CoordinatorClient::revalidate(CommunicationPlanId id, bool& fresh, std::string& err) {
  Message m; m.type=MessageType::REVALIDATE; m.planId=id;
  CP_CLIENT_SEND({ fresh = reply.ok; return true; });
}
bool CoordinatorClient::executionHandoff(CommunicationPlanId id, bool& ok, std::string& err) {
  Message m; m.type=MessageType::EXECUTION_HANDOFF; m.planId=id;
  CP_CLIENT_SEND({ ok = reply.ok; return true; });
}
bool CoordinatorClient::executionResult(CommunicationPlanId id, bool okv, std::string& err) {
  Message m; m.type=MessageType::EXECUTION_RESULT; m.planId=id; m.ok=okv;
  CP_CLIENT_SEND({ return reply.ok; });
}
bool CoordinatorClient::queryPlanState(CommunicationPlanId id, int& state, std::string& err) {
  if (!impl_) { err="not connected"; return false; }
  Message m; m.type=MessageType::QUERY_PLAN; m.planId=id;
  Message reply; if (!impl_->send(m, reply)) { err="transport error"; return false; }
  if (reply.type==MessageType::ERROR) { err=reply.error; return false; }
  state = (int)reply.planId.value();
  return reply.ok;
}
bool CoordinatorClient::cancel(CommunicationRequestId reqId, CommunicationPlanId planId, std::string& err) {
  Message m; m.type=MessageType::CANCEL; m.requestId=reqId; m.planId=planId;
  CP_CLIENT_SEND({ return reply.ok; });
}
bool CoordinatorClient::supersede(CommunicationRequestId reqId, CommunicationPlanId planId, std::string& err) {
  Message m; m.type=MessageType::SUPERSEDE; m.requestId=reqId; m.planId=planId;
  CP_CLIENT_SEND({ return reply.ok; });
}
bool CoordinatorClient::save(std::string& err) {
  Message m; m.type=MessageType::SAVE;
  CP_CLIENT_SEND({ return reply.ok; });
}
bool CoordinatorClient::shutdown(std::string& err) {
  Message m; m.type=MessageType::SHUTDOWN;
  CP_CLIENT_SEND({ return reply.ok; });
}

}  // namespace communication_planner
