// CUDA Worker: a real OS process that publishes RTX 5090 host<->GPU evidence to a
// Communication Planner coordinator, then executes plan-gated CUDA movement when the
// coordinator hands a plan off (EXECUTE). Built directly with nvcc.
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/provenance.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include "communication_planner/protocol/protocol.hpp"
#include "transport/tcp.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <process.h>
#include <cuda_runtime_api.h>

using namespace communication_planner;

__global__ void cuda_add_one_kernel(int* out, const int* in, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = in[i] + 1;
}

static cudaError_t chk(const char* w) { cudaError_t e = cudaGetLastError(); if (e != cudaSuccess) std::printf("CUDA ERROR at %s: %s\n", w, cudaGetErrorString(e)); return e; }

int main(int argc, char** argv) {
  if (argc < 4) { std::printf("usage: cp_cuda_worker <coordPort> <role> <bootBase>\n"); return 2; }
  unsigned short port = (unsigned short)std::atoi(argv[1]);
  std::string role = argv[2];
  std::uint64_t bootBase = std::strtoull(argv[3], nullptr, 10);
  WorkerId wid(1);
  WorkerBootId boot(bootBase);

  transport::libraryInit();
  std::string err;
  SOCKET s = transport::connectTo("127.0.0.1", port, err);
  if (s == INVALID_SOCKET) { std::printf("worker connect failed: %s\n", err.c_str()); return 1; }
  // read HELLO
  Frame hf; if (!transport::recvFrame(s, hf, err, -1)) { std::printf("worker HELLO recv failed\n"); return 1; }

  // Discover the real RTX 5090.
  int devCount = 0; cudaGetDeviceCount(&devCount);
  if (devCount < 1) { std::printf("no CUDA device\n"); return 1; }
  cudaDeviceProp prop{}; cudaGetDeviceProperties(&prop, 0);
  std::printf("CUDA WORKER device0: %s CC %d.%d\n", prop.name, prop.major, prop.minor);
  if (prop.major != 12) { std::printf("expected sm_120\n"); return 1; }

  size_t freeBefore = 0, totalBefore = 0; cudaMemGetInfo(&freeBefore, &totalBefore);

  // Register with the coordinator (fresh boot authority).
  {
    Message m; m.type = MessageType::REGISTER; m.worker = wid; m.boot = boot; m.name = role;
    std::vector<std::byte> pl; encodeMessage(m, pl); Frame f; f.type = MessageType::REGISTER; f.payload = pl;
    std::string e; if (!transport::sendFrame(s, f, e)) { std::printf("REGISTER send failed: %s\n", e.c_str()); return 1; }
    Frame ack; if (!transport::recvFrame(s, ack, e, -1)) { std::printf("REGISTER ack failed\n"); return 1; }
  }

  // Publish real host + GPU endpoints and the host<->GPU link.
  auto mkEp = [&](EndpointId id, EndpointKind kind, NodeId node) {
    Endpoint e; e.id = id; e.generation = EndpointGeneration(1); e.kind = kind; e.node = node;
    e.ready = true; e.reachable = true; e.health.healthy = true; e.health.generation = HealthGeneration(1);
    e.capability.transports.insert(Transport::CUDA); e.capability.transports.insert(Transport::HOST_MEMORY);
    e.capability.provenance = Provenance::MEASURED; e.capability.generation = CapabilityGeneration(1);
    e.worker = wid; e.workerBoot = boot; e.provenance = Provenance::MEASURED;
    return e;
  };
  Endpoint hostEp = mkEp(EndpointId(1), EndpointKind::CPU_MEMORY, NodeId(1));
  Endpoint gpuEp = mkEp(EndpointId(2), EndpointKind::GPU_DEVICE, NodeId(2));
  gpuEp.setArch("sm_120");
  Link link; link.id = LinkId(1); link.generation = LinkGeneration(1);
  link.source = EndpointId(1); link.destination = EndpointId(2); link.transport = Transport::CUDA; link.directed = true;
  link.kind = LinkKind::GPU_HOST_PATH; link.capacity = BytesPerSecond(30ull << 30); link.effectiveBandwidth = BytesPerSecond(30ull << 30);
  link.latency = DurationNs(5000); link.health.healthy = true;
  link.health.generation = HealthGeneration(1); link.congestion.generation = CongestionGeneration(1);
  link.congestion.provenance = Provenance::REPORTED; link.capacityEvidence.generation = CapacityGeneration(1);
  link.capacityEvidence.usable = BytesPerSecond(30ull << 30); link.capacityEvidence.headroom = BytesPerSecond(30ull << 30);
  link.capacityEvidence.provenance = Provenance::REPORTED; link.topologyGeneration = TopologyGeneration(1);
  link.capabilityGeneration = CapabilityGeneration(1); link.workerBoot = boot; link.provenance = Provenance::MEASURED;

  auto pub = [&](MessageType t, Message& m) {
    std::vector<std::byte> pl; encodeMessage(m, pl); Frame f; f.type = t; f.payload = pl;
    std::string e; transport::sendFrame(s, f, e); Frame ack; transport::recvFrame(s, ack, e, -1);
  };
  { Message m; m.type = MessageType::PUBLISH_ENDPOINT; m.worker = wid; m.boot = boot; m.endpoint = hostEp; pub(MessageType::PUBLISH_ENDPOINT, m); }
  { Message m; m.type = MessageType::PUBLISH_ENDPOINT; m.worker = wid; m.boot = boot; m.endpoint = gpuEp; pub(MessageType::PUBLISH_ENDPOINT, m); }
  { Message m; m.type = MessageType::PUBLISH_LINK; m.worker = wid; m.boot = boot; m.link = link; pub(MessageType::PUBLISH_LINK, m); }
  Capacity cap; cap.generation = CapacityGeneration(1); cap.usable = BytesPerSecond(30ull << 30); cap.headroom = BytesPerSecond(30ull << 30); cap.provenance = Provenance::REPORTED;
  { Message m; m.type = MessageType::PUBLISH_CAPACITY; m.worker = wid; m.boot = boot; m.linkId = LinkId(1); m.capacity = cap; pub(MessageType::PUBLISH_CAPACITY, m); }
  Congestion cong; cong.generation = CongestionGeneration(1); cong.load = 0.0; cong.penalty = 0.0; cong.hardLimitExceeded = false; cong.provenance = Provenance::REPORTED;
  { Message m; m.type = MessageType::PUBLISH_CONGESTION; m.worker = wid; m.boot = boot; m.linkId = LinkId(1); m.congestion = cong; pub(MessageType::PUBLISH_CONGESTION, m); }

  std::printf("CUDA WORKER READY pid=%d boot=%llu freeBefore=%llu\n", (int)GetCurrentProcessId(), (unsigned long long)boot.value(), (unsigned long long)freeBefore);
  std::fflush(stdout);

  // Execute loop: receive EXECUTE, run real CUDA movement, report result.
  for (;;) {
    Frame f; std::string e;
    if (!transport::recvFrame(s, f, e, -1)) { std::printf("CUDA WORKER disconnect: %s\n", e.c_str()); break; }
    Message m; ProtocolError pe = decodeMessage(f.type, f.payload.data(), f.payload.size(), m);
    if (!pe.ok()) continue;
    if (f.type == MessageType::SHUTDOWN) break;
    if (f.type == MessageType::EXECUTE) {
      if (m.boot != boot) { std::printf("CUDA WORKER rejected EXECUTE with wrong boot authority\n"); continue; }
      const std::uint64_t bytes = m.bytes;
      const int N = (int)(bytes / sizeof(int));
      if (N <= 0) { std::printf("CUDA WORKER bad payload\n"); continue; }
      std::vector<int> hostInput((std::size_t)N);
      for (int i = 0; i < N; ++i) hostInput[(std::size_t)i] = (i & 0x7f);
      int* dIn = nullptr; int* dOut = nullptr; int* pinned = nullptr;
      cudaMalloc(&dIn, (std::size_t)N * sizeof(int)); cudaMalloc(&dOut, (std::size_t)N * sizeof(int));
      cudaMallocHost(&pinned, (std::size_t)N * sizeof(int));
      if (chk("alloc") != cudaSuccess) { continue; }
      std::memcpy(pinned, hostInput.data(), (std::size_t)N * sizeof(int));
      bool okc = true;
      okc = okc && (cudaMemcpy(dIn, pinned, (std::size_t)N * sizeof(int), cudaMemcpyHostToDevice) == cudaSuccess);
      cuda_add_one_kernel<<<(N + 255) / 256, 256>>>(dOut, dIn, N);
      okc = okc && (chk("kernel") == cudaSuccess);
      cudaDeviceSynchronize();
      okc = okc && (cudaMemcpy(pinned, dOut, (std::size_t)N * sizeof(int), cudaMemcpyDeviceToHost) == cudaSuccess);
      cudaDeviceSynchronize();
      if (okc) { for (int i = 0; i < N; ++i) if (pinned[i] != hostInput[(std::size_t)i] + 1) { okc = false; break; } }
      bool cleanOk = (cudaFree(dIn) == cudaSuccess) && (cudaFree(dOut) == cudaSuccess) && (cudaFreeHost(pinned) == cudaSuccess);
      size_t freeAfter = 0, totalAfter = 0; cudaMemGetInfo(&freeAfter, &totalAfter);
      bool baselineOk = (freeAfter >= freeBefore);  // device memory returned to measured baseline
      okc = okc && cleanOk && baselineOk;
      std::printf("CUDA WORKER executed plan %llu bytes=%llu parity=%d clean=%d baseline=%d freeAfter=%llu\n", (unsigned long long)m.planId.value(), (unsigned long long)bytes, (int)okc, (int)cleanOk, (int)baselineOk, (unsigned long long)freeAfter);
      { char path[128]; std::snprintf(path, sizeof path, "cuda_worker_log_%llu.txt", (unsigned long long)boot.value());
        FILE* f = nullptr; if (fopen_s(&f, path, "w") == 0 && f) { std::fprintf_s(f, "freeBefore=%llu freeAfter=%llu parity=%d clean=%d baseline=%d\n", (unsigned long long)freeBefore, (unsigned long long)freeAfter, (int)okc, (int)cleanOk, (int)baselineOk); std::fclose(f); } }
      Message res; res.type = MessageType::EXECUTION_RESULT; res.planId = m.planId; res.ok = okc; res.bytes = bytes;
      res.worker = wid; res.boot = boot;
      std::vector<std::byte> pl; encodeMessage(res, pl); Frame rf; rf.type = MessageType::EXECUTION_RESULT; rf.payload = pl;
      transport::sendFrame(s, rf, e);
    }
  }
  transport::closeSock(s);
  transport::libraryCleanup();
  return 0;
}
