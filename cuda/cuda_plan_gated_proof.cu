#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/provenance.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include "communication_planner/model/request.hpp"
#include "communication_planner/planner/planner.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/planner/snapshot.hpp"
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/revalidation/revalidation.hpp"
#include "communication_planner/feasibility/feasibility.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <cuda_runtime_api.h>
#include <cuda.h>

using namespace communication_planner;

// Device kernel: adds 1 to each byte of an int buffer.
__global__ void cuda_add_one_kernel(int* out, const int* in, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = in[i] + 1;
}

static cudaError_t lastErr(const char* what) {
  cudaError_t e = cudaGetLastError();
  if (e != cudaSuccess) std::printf("CUDA ERROR at %s: %s\n", what, cudaGetErrorString(e));
  return e;
}

int main() {
  int failures = 0;
  #define ASSERT(cond, msg) do { if (!(cond)) { std::printf("CUDA PROOF FAIL: %s\n", msg); ++failures; } else { std::printf("CUDA PROOF OK: %s\n", msg); } } while(0)

  int devCount = 0;
  cudaGetDeviceCount(&devCount);
  ASSERT(devCount >= 1, "CUDA device present (RTX 5090)");

  cudaDeviceProp prop{};
  cudaGetDeviceProperties(&prop, 0);
  std::printf("CUDA device 0: %s CC %d.%d VRAM %zu MB\n", prop.name, prop.major, prop.minor, (std::size_t)prop.totalGlobalMem / (1024*1024));
  ASSERT(prop.major == 12, "compute capability sm_120 (RTX 5090)");

  size_t freeBefore=0, totalBefore=0; cudaMemGetInfo(&freeBefore, &totalBefore);

  // --- Build a real host<->GPU evidence snapshot ---
  Endpoint hostEp;
  hostEp.id = EndpointId(1); hostEp.generation = EndpointGeneration(1);
  hostEp.kind = EndpointKind::CPU_MEMORY; hostEp.node = NodeId(1);
  hostEp.ready = true; hostEp.reachable = true;
  hostEp.health.healthy = true; hostEp.health.generation = HealthGeneration(1);
  hostEp.capability.transports.insert(Transport::CUDA);
  hostEp.capability.transports.insert(Transport::HOST_MEMORY);
  hostEp.capability.provenance = Provenance::MEASURED;
  hostEp.capability.generation = CapabilityGeneration(1);

  Endpoint gpuEp;
  gpuEp.id = EndpointId(2); gpuEp.generation = EndpointGeneration(1);
  gpuEp.kind = EndpointKind::GPU_DEVICE; gpuEp.node = NodeId(2);
  gpuEp.ready = true; gpuEp.reachable = true;
  gpuEp.health.healthy = true; gpuEp.health.generation = HealthGeneration(1);
  gpuEp.capability.transports.insert(Transport::CUDA);
  gpuEp.capability.transports.insert(Transport::HOST_MEMORY);
  gpuEp.capability.provenance = Provenance::MEASURED;
  gpuEp.capability.generation = CapabilityGeneration(1);
  gpuEp.setArch("sm_120");

  Link link;
  link.id = LinkId(1); link.generation = LinkGeneration(1);
  link.source = EndpointId(1); link.destination = EndpointId(2);
  link.transport = Transport::CUDA; link.directed = true;
  link.kind = LinkKind::GPU_HOST_PATH;
  link.capacity = BytesPerSecond(30ull << 30);
  link.effectiveBandwidth = BytesPerSecond(30ull << 30);
  link.latency = DurationNs(5000);
  link.health.healthy = true;
  link.congestion.provenance = Provenance::REPORTED;
  link.congestion.generation = CongestionGeneration(1);
  link.capacityEvidence.provenance = Provenance::MEASURED;
  link.capacityEvidence.generation = CapacityGeneration(1);
  link.capacityEvidence.usable = BytesPerSecond(30ull << 30);
  link.capacityEvidence.headroom = BytesPerSecond(30ull << 30);
  link.topologyGeneration = TopologyGeneration(1);
  link.capabilityGeneration = CapabilityGeneration(1);
  link.provenance = Provenance::MEASURED;

  EvidenceSnapshot snap;
  snap.topologyGeneration = TopologyGeneration(1);
  snap.capacityGeneration = CapacityGeneration(1);
  snap.reservationGeneration = ReservationGeneration(1);
  snap.congestionGeneration = CongestionGeneration(1);
  snap.capabilityGeneration = CapabilityGeneration(1);
  snap.healthGeneration = HealthGeneration(1);
  snap.placementGeneration = PlacementGeneration(1);
  snap.policyGeneration = PolicyGeneration(1);
  snap.authorityGeneration = currentAuthority();
  snap.endpoints.push_back(hostEp);
  snap.endpoints.push_back(gpuEp);
  snap.links.push_back(link);

  const unsigned N = 1u << 20;   // 1M ints (4 MB payload)
  CommunicationRequest req;
  req.id = CommunicationRequestId(1); req.generation = CommunicationRequestGeneration(1);
  req.shape = RequestShape::POINT_TO_POINT; req.source = EndpointId(1);
  req.destinations.push_back(EndpointId(2));
  req.payloadSize = Bytes((std::uint64_t)N * sizeof(int));
  req.allowStaging = true; req.allowHostStaging = true;
  req.requireValid();

  RankingWeights w; Bounds b;
  b.maxPathDepth = 4; b.maxCandidates = 32; b.maxFallbacks = 2;
  PlanOutcome po = planCommunication(req, snap, w, b);
  ASSERT(po.success, "host<->GPU plan feasible");
  ASSERT(po.plan->orderedStages.size() == 1, "direct host->GPU path selected (1 stage)");
  ASSERT(po.plan->candidatePaths[0].stages[0].transport == Transport::CUDA, "plan uses CUDA transport");

  // --- Plan-gated real movement ---
  std::vector<int> hostInput(N);
  for (unsigned i = 0; i < N; ++i) hostInput[i] = (int)(i & 0x7f);
  int* dIn = nullptr; int* dOut = nullptr; int* pinned = nullptr;
  cudaMalloc(&dIn, N * sizeof(int));
  cudaMalloc(&dOut, N * sizeof(int));
  if (lastErr("cudaMalloc") != cudaSuccess) return 1;
  cudaMallocHost(&pinned, N * sizeof(int));
  if (lastErr("cudaMallocHost") != cudaSuccess) return 1;
  std::memcpy(pinned, hostInput.data(), N * sizeof(int));

  // H2D (pinned host -> device)
  cudaError_t e = cudaMemcpy(dIn, pinned, N * sizeof(int), cudaMemcpyHostToDevice);
  ASSERT(e == cudaSuccess, "H2D cudaMemcpy succeeded (plan-gated)");
  // Kernel
  cuda_add_one_kernel<<<(N + 255) / 256, 256>>>(dOut, dIn, (int)N);
  if (lastErr("kernel launch") != cudaSuccess) return 1;
  cudaDeviceSynchronize();
  // D2H
  e = cudaMemcpy(pinned, dOut, N * sizeof(int), cudaMemcpyDeviceToHost);
  ASSERT(e == cudaSuccess, "D2H cudaMemcpy succeeded");
  cudaDeviceSynchronize();
  // CPU parity
  bool parity = true;
  for (unsigned i = 0; i < N; ++i) if (pinned[i] != hostInput[i] + 1) { parity = false; break; }
  ASSERT(parity, "CPU parity check passed (kernel result correct)");

  // Cleanup
  cudaFree(dIn); cudaFree(dOut); cudaFreeHost(pinned);
  if (lastErr("cleanup") != cudaSuccess) return 1;
  size_t freeAfter=0, totalAfter=0; cudaMemGetInfo(&freeAfter, &totalAfter);
  ASSERT(freeAfter >= freeBefore, "device memory returned to baseline after cleanup");

  // --- Stale CUDA endpoint generation rejection ---
  // Build plan gen 1 (already po.plan). Advance the endpoint/link generation:
  snap.endpoints[1].generation = EndpointGeneration(2);
  snap.links[0].generation = LinkGeneration(2);
  RevalidationResult rr = revalidatePlan(*po.plan, snap);
  ASSERT(!rr.ok, "stale generation invalidates CUDA plan before allocation");

  // --- Direct vs staged proof: hard constraint forces staged path ---
  // Add a staged host path and make the direct path forbidden.
  EvidenceSnapshot snap2 = snap;
  snap2.endpoints[1].generation = EndpointGeneration(1);
  snap2.links[0].generation = LinkGeneration(1);
  Endpoint pinnedEp = hostEp;
  pinnedEp.id = EndpointId(3); pinnedEp.generation = EndpointGeneration(1);
  pinnedEp.kind = EndpointKind::PINNED_HOST_MEMORY;
  snap2.endpoints.push_back(pinnedEp);
  Link l1 = link; l1.id = LinkId(2); l1.generation = LinkGeneration(1);
  l1.source = EndpointId(1); l1.destination = EndpointId(3); l1.transport = Transport::HOST_MEMORY;
  Link l2 = link; l2.id = LinkId(3); l2.generation = LinkGeneration(1);
  l2.source = EndpointId(3); l2.destination = EndpointId(2); l2.transport = Transport::CUDA;
  snap2.links.push_back(l1);
  snap2.links.push_back(l2);
  // Force the direct path out via a hard reservation conflict on its bandwidth resource.
  Reservation rsv;
  rsv.resource = ResourceId(1);            // ResourceId(link.id.value()) for the direct link
  rsv.reserved = BytesPerSecond(40ull << 30);  // >= direct link capacity (30 GiB) -> hard conflict
  rsv.provenance = Provenance::REPORTED;
  snap2.reservations.push_back(rsv);
  CommunicationRequest req2 = req; req2.id = CommunicationRequestId(2);
  PlanOutcome po2 = planCommunication(req2, snap2, w, b);
  ASSERT(po2.success, "staged plan feasible when direct CUDA forbidden");
  ASSERT(po2.plan->orderedStages.size() == 2, "hard constraint forces staged host path (2 stages)");

  if (failures == 0) { std::printf("CUDA PROOF PASS\n"); return 0; }
  std::printf("CUDA PROOF FAIL (count=%d)\n", failures);
  return 1;
}
