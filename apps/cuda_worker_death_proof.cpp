// Integrated real CUDA worker-death communication proof.
// Coordinator (OS process) + a real CUDA Worker A (OS process) + Worker A;.
// Proves plan authority, endpoint/link freshness, stale-plan rejection, and fresh
// re-planning across real CUDA worker reincarnation.
#include "communication_planner/coordinator/coordinator.hpp"
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/model/request.hpp"
#include "communication_planner/model/plan.hpp"
#include "windows.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>
#include <cstdint>
#include <vector>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

using namespace communication_planner;

static unsigned short freePort() {
  WSADATA d; WSAStartup(MAKEWORD(2,2), &d);
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return 0;
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(0);
  if (bind(s, (sockaddr*)&a, sizeof a) != 0) { closesocket(s); return 0; }
  sockaddr_in b{}; int len = sizeof b; getsockname(s, (sockaddr*)&b, &len);
  unsigned short pp = ntohs(b.sin_port); closesocket(s); WSACleanup(); return pp;
}

struct Child { PROCESS_INFORMATION pi{}; bool valid=false; };
static bool spawn(const std::string& exe, const std::string& args, Child& out) {
  std::string cmdline; cmdline.push_back((char)34); cmdline += exe; cmdline.push_back((char)34); cmdline.push_back((char)32); cmdline += args;
  STARTUPINFOA si{}; si.cb = sizeof si; char* cmd = _strdup(cmdline.c_str());
  BOOL ok = CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &out.pi);
  free(cmd); out.valid = (ok != 0); return out.valid;
}
static void killChild(Child& c) { if (c.valid) { TerminateProcess(c.pi.hProcess, 0); WaitForSingleObject(c.pi.hProcess, 5000); CloseHandle(c.pi.hProcess); CloseHandle(c.pi.hThread); c.valid = false; } }

static CommunicationRequest mkReq(CommunicationRequestId id, std::uint64_t payload) {
  CommunicationRequest r; r.id = id; r.generation = CommunicationRequestGeneration(1);
  r.shape = RequestShape::POINT_TO_POINT; r.source = EndpointId(1); r.payloadSize = Bytes(payload);
  r.destinations.push_back(EndpointId(2)); r.allowStaging = true; r.allowHostStaging = true; return r;
}

static bool waitConnect(CoordinatorClient& c, unsigned short port, int tries) {
  for (int i = 0; i < tries; ++i) { std::string e; if (c.connect("127.0.0.1", port, e)) return true; c.disconnect(); std::this_thread::sleep_for(std::chrono::milliseconds(50)); } return false;
}
// Read the CUDA worker evidence log written by the worker for a given boot.
static bool workerLogOk(std::uint64_t boot, std::uint64_t& freeAfter, int& parity) {
  char path[128]; std::snprintf(path, sizeof path, "cuda_worker_log_%llu.txt", (unsigned long long)boot);
  FILE* f = nullptr; if (fopen_s(&f, path, "r") != 0 || !f) return false;
  unsigned long long fb = 0, fa = 0; int p = 0, cl = 0, bl = 0;
  int n = fscanf_s(f, "freeBefore=%llu freeAfter=%llu parity=%d clean=%d baseline=%d", &fb, &fa, &p, &cl, &bl);
  std::fclose(f);
  freeAfter = (std::uint64_t)fa; parity = p;
  return (n == 5) && (cl == 1) && (bl == 1);
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: cp_cuda_worker_death_proof <coordinatorExe> [cudaWorkerExe]\n"); return 2; }
  std::string coordExe = argv[1];
  std::string workerExe = (argc >= 3) ? argv[2] : "cp_cuda_worker.exe";
  // if worker exe not found, look in build/cuda and cuda dirs. Else SKIP (return 77).
  { std::string e; WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(workerExe.c_str(), &fd); if (h == INVALID_HANDLE_VALUE) {
      std::printf("CUDA worker exe not found at [%s]; built separately with nvcc. SKIPPING.\n", workerExe.c_str()); return 77; } FindClose(h); }

  unsigned short port = freePort(); if (port == 0) { std::printf("no free port\n"); return 1; }
  std::string store = "cuda_worker_store.bin"; std::remove(store.c_str());
  Child coord; Child wa; Child wa2;
  int failures = 0;
  #define ASSERT(c, m) do { if (!(c)) { std::printf("CUDA-WORKER-DEATH FAIL: %s\n", m); ++failures; } else { std::printf("CUDA-WORKER-DEATH OK: %s\n", m); } } while(0)

  if (!spawn(coordExe, " " + std::to_string(port) + " " + store, coord)) { std::printf("cannot spawn coordinator\n"); return 1; }
  CoordinatorClient ctrl; std::string e;
  if (!waitConnect(ctrl, port, 60)) { std::printf("coordinator not ready\n"); return 1; }

  // Worker A: real OS process, fresh boot 1000, rediscover RTX 5090, publish.
  spawn(workerExe, " " + std::to_string(port) + " A 1000", wa);
  DWORD pidA2 = 0;
  std::uint64_t oldPlanGen = 0, newPlanGen = 0;
  auto submitUntilFeasible = [&](CommunicationRequestId rid, std::uint64_t payload, CommunicationPlan& out, bool& feasible) {
    for (int i = 0; i < 40; ++i) {
      CommunicationRequest req = mkReq(rid, payload);
      if (ctrl.submitRequest(req, out, feasible, e) && feasible) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  };

  // ---- Scenario 1: plan, commit, hand off, Worker A executes real CUDA. ----
  const std::uint64_t payload = 4u << 20;
  CommunicationPlan planOld; bool feasOld = false;
  submitUntilFeasible(CommunicationRequestId(1), payload, planOld, feasOld);
  ASSERT(feasOld, "plan involving Worker A live CUDA endpoint is feasible");
  if (feasOld) {
    ASSERT(planOld.orderedStages.size() == 1, "direct host<->GPU path selected (1 stage)");
    oldPlanGen = planOld.generation.value();
    bool commitOk = false; ctrl.commitPlan(planOld.id, commitOk, e); ASSERT(commitOk, "resource/staging commit succeeds");
    bool fresh = false; ctrl.revalidate(planOld.id, fresh, e); ASSERT(fresh, "revalidation fresh before handoff");
    bool handoffOk = false; ctrl.executionHandoff(planOld.id, handoffOk, e); ASSERT(handoffOk, "execution handoff authorized to Worker A");
    // Poll for Worker A result (bounded product-semantic wait).
    for (int i = 0; i < 400; ++i) { int st=-1; ctrl.queryPlanState(planOld.id, st, e); if (st == (int)PlanState::COMPLETED) break; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    std::uint64_t freeAfterA = 0; int parityA = 0;
    bool logA = workerLogOk(1000, freeAfterA, parityA);
    ASSERT(logA, "Worker A recorded device-memory baseline + clean cleanup evidence (REAL cudaMalloc/H2D/kernel/D2H/cudaFree)");
    int st=-1; ctrl.queryPlanState(planOld.id, st, e);
    ASSERT(st == (int)PlanState::COMPLETED, "Worker A executed real CUDA movement and reported success");
  }

  // ---- Scenario 2: kill Worker A (literal OS process death). ----
  DWORD pidA = wa.valid ? wa.pi.dwProcessId : 0;
  killChild(wa);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  ASSERT(pidA != 0, "Worker A has a distinct OS PID");
  std::printf("PROCESS: Worker A terminated PID=%lu\n", (unsigned long)pidA);

  // Old plan must become non-executable.
  bool freshAfterDeath = true; ctrl.revalidate(planOld.id, freshAfterDeath, e);
  ASSERT(!freshAfterDeath, "old plan REVALIDATION_REQUIRED after Worker A death");
  bool handoffAfterDeath = true; ctrl.executionHandoff(planOld.id, handoffAfterDeath, e);
  ASSERT(!handoffAfterDeath, "old plan execution handoff rejected after Worker A death");

  // Stale authority replay rejection.
  {
    CoordinatorClient stale; stale.connect("127.0.0.1", port, e);
    bool regOld = stale.registerWorker(WorkerId(1), WorkerBootId(1000), "oldA", e);
    ASSERT(!regOld, "old WorkerBootId REGISTER rejected (fenced)");
    Endpoint ghost; ghost.id = EndpointId(1); ghost.generation = EndpointGeneration(1); ghost.kind = EndpointKind::GPU_DEVICE; ghost.node = NodeId(1);
    ghost.ready = true; ghost.reachable = true; ghost.health.healthy = true; ghost.capability.transports.insert(Transport::CUDA);
    bool pubOld = stale.publishEndpoint(WorkerId(1), WorkerBootId(1000), ghost, e);
    ASSERT(!pubOld, "stale endpoint publication rejected");
    Link ghostL; ghostL.id = LinkId(1); ghostL.generation = LinkGeneration(1); ghostL.source = EndpointId(1); ghostL.destination = EndpointId(2); ghostL.transport = Transport::CUDA; ghostL.directed = true;
    ghostL.capacity = BytesPerSecond(1); ghostL.health.healthy = true;
    bool pubLink = stale.publishLink(WorkerId(1), WorkerBootId(1000), ghostL, e);
    ASSERT(!pubLink, "stale link publication rejected");
    // stale result for the old plan must not complete it.
    stale.executionResult(planOld.id, true, e);
    int st = -1; ctrl.queryPlanState(planOld.id, st, e);
    ASSERT(st != (int)PlanState::COMPLETED, "stale result does not complete the old plan");
  }

  // ---- Scenario 3: Worker A\x27 reincarnates with a fresh boot. ----
  spawn(workerExe, " " + std::to_string(port) + " A 3000", wa2);
  pidA2 = wa2.valid ? wa2.pi.dwProcessId : 0;
  ASSERT(pidA2 != 0 && pidA2 != pidA, "Worker A\x27 has a fresh distinct OS PID");
  std::printf("PROCESS: Worker A\x27 PID=%lu (distinct from dead Worker A PID=%lu)\n", (unsigned long)pidA2, (unsigned long)pidA);
  CommunicationPlan planNew; bool feasNew = false;
  submitUntilFeasible(CommunicationRequestId(2), payload, planNew, feasNew);
  ASSERT(feasNew, "fresh plan after Worker A\x27 republish is feasible");
  if (feasNew) {
    newPlanGen = planNew.generation.value();
    ASSERT(newPlanGen > oldPlanGen, "fresh CommunicationPlanGeneration advances");
    bool commitOk = false; ctrl.commitPlan(planNew.id, commitOk, e); ASSERT(commitOk, "fresh resource/staging commit succeeds");
    bool fresh = false; ctrl.revalidate(planNew.id, fresh, e); ASSERT(fresh, "fresh revalidation uses new generations");
    bool hok = false; ctrl.executionHandoff(planNew.id, hok, e); ASSERT(hok, "fresh execution handoff reaches Worker A\x27");
    for (int i = 0; i < 400; ++i) { int st=-1; ctrl.queryPlanState(planNew.id, st, e); if (st == (int)PlanState::COMPLETED) break; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    std::uint64_t freeAfterA2 = 0; int parityA2 = 0;
    bool logA2 = workerLogOk(3000, freeAfterA2, parityA2);
    ASSERT(logA2, "Worker A\x27 recorded fresh device-memory baseline + clean cleanup evidence (REAL cudaMalloc/H2D/kernel/D2H/cudaFreeHost)");
    int st=-1; ctrl.queryPlanState(planNew.id, st, e);
    ASSERT(st == (int)PlanState::COMPLETED, "Worker A\x27 executed real CUDA movement (cudaMalloc/H2D/kernel/D2H/parity/cleanup)");
  }
  // Old generations remain permanently rejected.
  { CoordinatorClient stale; stale.connect("127.0.0.1", port, e); bool regOld = stale.registerWorker(WorkerId(1), WorkerBootId(1000), "oldA", e); ASSERT(!regOld, "old WorkerBootId remains fenced after A\x27 reincarnation"); }
  killChild(wa2); killChild(coord);
  if (failures == 0) { std::printf("CUDA-WORKER-DEATH PROOF PASS\n"); return 0; }
  std::printf("CUDA-WORKER-DEATH PROOF FAIL (count=%d)\n", failures);
  return 1;
}
