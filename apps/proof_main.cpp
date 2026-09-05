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
#include <vector>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

using namespace communication_planner;

static unsigned short freePort() {
  WSADATA d; WSAStartup(MAKEWORD(2,2), &d);
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return 0;
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(0);
  if (bind(s, (sockaddr*)&a, sizeof a) != 0) { closesocket(s); return 0; }
  sockaddr_in b{}; int len = sizeof b;
  getsockname(s, (sockaddr*)&b, &len);
  unsigned short p = ntohs(b.sin_port);
  closesocket(s);
  WSACleanup();
  return p;
}

struct Child { PROCESS_INFORMATION pi{}; bool valid=false; };

static bool spawn(const std::string& exe, const std::string& args, Child& out) {
  std::string cmdline;
  cmdline.push_back((char)34);   // double quote
  cmdline += exe;
  cmdline.push_back((char)34);
  cmdline.push_back((char)32);   // space
  cmdline += args;
  STARTUPINFOA si{}; si.cb = sizeof si;
  char* cmd = _strdup(cmdline.c_str());
  BOOL ok = CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &out.pi);
  free(cmd);
  out.valid = (ok != 0);
  return out.valid;
}
static void killChild(Child& c) {
  if (c.valid) { TerminateProcess(c.pi.hProcess, 0); WaitForSingleObject(c.pi.hProcess, 5000); CloseHandle(c.pi.hProcess); CloseHandle(c.pi.hThread); c.valid=false; }
}

static CommunicationRequest mkReq(CommunicationRequestId id, EndpointId src, EndpointId dst, std::uint64_t payload) {
  CommunicationRequest r;
  r.id = id; r.generation = CommunicationRequestGeneration(1);
  r.shape = RequestShape::POINT_TO_POINT; r.source = src; r.payloadSize = Bytes(payload);
  r.destinations.push_back(dst);
  r.allowStaging = true; r.allowHostStaging = true; r.allowStorageStaging = true; r.allowRelay = true;
  return r;
}

static bool waitConnect(CoordinatorClient& c, unsigned short port, int tries) {
  for (int i = 0; i < tries; ++i) {
    std::string e;
    if (c.connect("127.0.0.1", port, e)) return true;
    c.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("usage: cp_multiprocess_proof <coordinatorExe> <workerExe>\n"); return 2; }
  std::string coordExe = argv[1];
  std::string workerExe = argv[2];
  unsigned short port = freePort();
  if (port == 0) { std::printf("no free port\n"); return 2; }
  std::string store = "proof_store.bin";
  std::remove(store.c_str());

  Child coord; Child wa; Child wb; Child wa2;
  int failures = 0;
  #define ASSERT(cond, msg) do { if (!(cond)) { std::printf("PROOF FAIL: %s\n", msg); ++failures; } else { std::printf("PROOF OK: %s\n", msg); } } while(0)

  if (!spawn(coordExe, " " + std::to_string(port) + " " + store, coord)) { std::printf("cannot spawn coordinator\n"); return 1; }
  {
    CoordinatorClient ctrl; std::string e;
    if (!waitConnect(ctrl, port, 60)) { std::printf("coordinator not ready\n"); return 1; }
    spawn(workerExe, " " + std::to_string(port) + " A 1000", wa);
    spawn(workerExe, " " + std::to_string(port) + " B 2000", wb);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    CommunicationPlan plan1; bool feas=false;
    CommunicationRequest req = mkReq(CommunicationRequestId(1), EndpointId(100), EndpointId(200), 4096);
    if (!ctrl.submitRequest(req, plan1, feas, e)) std::printf("submit req1 err: %s\n", e.c_str());
    ASSERT(feas, "initial direct plan feasible");
    if (feas) ASSERT(plan1.orderedStages.size() == 1, "direct path selected (1 stage)");
    ctrl.save(e);

    Congestion cong; cong.generation = CongestionGeneration(5); cong.load = 0.99; cong.penalty = 1e12;
    cong.hardLimitExceeded = false; cong.provenance = Provenance::REPORTED;
    ctrl.publishCongestion(LinkId(10), cong, e);

    bool fresh = true;
    if (feas) { ctrl.revalidate(plan1.id, fresh, e); ASSERT(!fresh, "congestion advances generation -> revalidation required"); }

    CommunicationPlan plan2; bool feas2=false;
    CommunicationRequest req2 = mkReq(CommunicationRequestId(2), EndpointId(100), EndpointId(200), 4096);
    ctrl.submitRequest(req2, plan2, feas2, e);
    ASSERT(feas2, "post-congestion re-plan feasible");
    if (feas2) ASSERT(plan2.orderedStages.size() == 2, "staged path selected after congestion");

    killChild(wa);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    bool fresh3 = true;
    if (feas2) { ctrl.revalidate(plan2.id, fresh3, e); ASSERT(!fresh3, "worker death fences evidence -> revalidation required"); }
    CommunicationPlan plan3; bool feas3=false;
    CommunicationRequest req3 = mkReq(CommunicationRequestId(3), EndpointId(100), EndpointId(200), 4096);
    ctrl.submitRequest(req3, plan3, feas3, e);
    ASSERT(!feas3, "no feasible path after worker A death");

    spawn(workerExe, " " + std::to_string(port) + " A 3000", wa2);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CommunicationPlan plan4; bool feas4=false;
    CommunicationRequest req4 = mkReq(CommunicationRequestId(4), EndpointId(100), EndpointId(200), 4096);
    ctrl.submitRequest(req4, plan4, feas4, e);
    ASSERT(feas4, "fresh plan after worker A restart and republish");
    ctrl.save(e);
  }

  killChild(coord);
  if (!spawn(coordExe, " " + std::to_string(port) + " " + store, coord)) { std::printf("cannot respawn coordinator\n"); return 1; }
  {
    CoordinatorClient ctrl2; std::string e;
    if (!waitConnect(ctrl2, port, 60)) { std::printf("coordinator restart not ready\n"); return 1; }
    int state = -1;
    bool qok = ctrl2.queryPlanState(CommunicationPlanId(1), state, e);
    ASSERT(qok, "recovered plan query works");
    ASSERT(qok && state == (int)PlanState::REVALIDATION_REQUIRED, "recovered executable plan is REVALIDATION_REQUIRED");
    CommunicationPlan planR; bool feasR=false;
    CommunicationRequest reqR = mkReq(CommunicationRequestId(99), EndpointId(100), EndpointId(200), 4096);
    ctrl2.submitRequest(reqR, planR, feasR, e);
    ASSERT(!feasR, "recovered coordinator cannot plan without fresh dynamic evidence");
    ctrl2.shutdown(e);
  }

  killChild(wb); killChild(wa2); killChild(coord);
  if (failures == 0) { std::printf("PROOF PASS\n"); return 0; }
  std::printf("PROOF FAIL (count=%d)\n", failures);
  return 1;
}
