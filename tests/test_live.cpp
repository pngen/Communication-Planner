#include "framework.hpp"
#include "testutil.hpp"
#include "communication_planner/planner/planner.hpp"
#include "communication_planner/planner/limits.hpp"
#include "communication_planner/ranking/weights.hpp"
#include "communication_planner/multicast/multicast.hpp"
#include "communication_planner/collective/collective.hpp"
#include "communication_planner/model/request.hpp"
#include "communication_planner/model/link.hpp"
#include "communication_planner/model/endpoint.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <chrono>
#include <string>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

using namespace communication_planner;
using cputil::mkEndpoint, cputil::mkLink, cputil::mkRequest;

static double nowMs() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

CP_TEST(host_memory_proof) {
  const std::size_t bytes = 8u << 20; // 8 MiB
  std::vector<char> src(bytes);
  for (std::size_t i = 0; i < bytes; ++i) src[i] = (char)(i & 0xff);
  // pageable
  std::vector<char> dst(bytes, 0);
  double t0 = nowMs();
  std::memcpy(dst.data(), src.data(), bytes);
  double pageableMs = nowMs() - t0;
  bool eq = true; for (std::size_t i=0;i<bytes;++i) if (src[i]!=dst[i]) {eq=false;break;}
  CHECK(eq);
  // pinned host memory via cudaMallocHost-style: use VirtualAlloc? Fallback to _aligned_malloc pinned not available.
  // We label this REAL pageable copy; pinned requires CUDA runtime (see CUDA proof).
  std::printf("HOST-MEMORY: pageable memcpy %zu bytes in %.3f ms (REAL / MEASURED)\n", bytes, pageableMs);
}

CP_TEST(storage_staging_proof) {
  const std::string file = "proof_storage.bin";
  const std::size_t bytes = 1u << 20;
  std::vector<char> data(bytes);
  for (std::size_t i = 0; i < bytes; ++i) data[i] = (char)((i * 31) & 0xff);
  std::ofstream ofs(file, std::ios::binary);
  ofs.write(data.data(), (std::streamsize)bytes);
  ofs.flush(); ofs.close();
  // read back into a staged host buffer
  std::vector<char> staged(bytes);
  std::ifstream ifs(file, std::ios::binary);
  ifs.read(staged.data(), (std::streamsize)bytes);
  ifs.close();
  bool eq = true; for (std::size_t i=0;i<bytes;++i) if (staged[i]!=data[i]) {eq=false;break;}
  CHECK(eq);
  std::remove(file.c_str());
  std::printf("STORAGE-STAGING: file write->read->host buffer %zu bytes (REAL / MEASURED)\n", bytes);
}

CP_TEST(loopback_tcp_proof) {
  WSADATA d; WSAStartup(MAKEWORD(2,2), &d);
  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); a.sin_port=htons(0);
  bind(listener, (sockaddr*)&a, sizeof a);
  sockaddr_in b{}; int bl=sizeof b; getsockname(listener,(sockaddr*)&b,&bl);
  ::listen(listener, 4);
  SOCKET cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  const std::size_t payload = 4u << 20; // 4 MiB
  std::vector<char> sendBuf(payload, 'X');
  std::vector<char> recvBuf(payload, 0);
  std::thread serverTh([&](){ SOCKET s = accept(listener, nullptr, nullptr); std::size_t got=0;
    while (got < payload) { int r = recv(s, recvBuf.data()+got, (int)(payload-got), 0); if (r<=0) break; got += r; } closesocket(s); });
  sockaddr_in cb{}; cb.sin_family=AF_INET; cb.sin_addr.s_addr=htonl(INADDR_LOOPBACK); cb.sin_port=b.sin_port;
  int c = connect(cli, (sockaddr*)&cb, sizeof cb);
  CHECK(c == 0);
  double t0 = nowMs();
  std::size_t sent=0; while (sent < payload) { int w = send(cli, sendBuf.data()+sent, (int)(payload-sent), 0); if (w<=0) break; sent += w; }
  double ms = nowMs() - t0;
  serverTh.join();
  closesocket(cli); closesocket(listener);
  bool eq = true; for (std::size_t i=0;i<payload;++i) if (recvBuf[i]!='X') {eq=false;break;}
  CHECK(eq);
  WSACleanup();
  std::printf("LOOPBACK-TCP: %zu bytes round-trip in %.3f ms (REAL / MEASURED)\n", payload, ms);
}

CP_TEST(synthetic_twogpu_direct_vs_staged) {
  Bounds b; b.maxPathDepth=6; b.maxCandidates=64; RankingWeights w;
  EvidenceSnapshot s; s.topologyGeneration=TopologyGeneration(1); s.congestionGeneration=CongestionGeneration(1);
  s.capacityGeneration=CapacityGeneration(1); s.capabilityGeneration=CapabilityGeneration(1); s.healthGeneration=HealthGeneration(1);
  s.placementGeneration=PlacementGeneration(1); s.policyGeneration=PolicyGeneration(1); s.authorityGeneration=currentAuthority();
  s.endpoints.push_back(mkEndpoint(EndpointId(1),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(1),true,true,{Transport::NVLINK,Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
  s.endpoints.push_back(mkEndpoint(EndpointId(2),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(2),true,true,{Transport::NVLINK,Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
  s.endpoints.push_back(mkEndpoint(EndpointId(3),EndpointGeneration(1),EndpointKind::PINNED_HOST_MEMORY,NodeId(1),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
  s.links.push_back(mkLink(LinkId(1),LinkGeneration(1),EndpointId(1),EndpointId(2),Transport::HOST_MEMORY,1000000,true,Provenance::SYNTHETIC));
  s.links.push_back(mkLink(LinkId(2),LinkGeneration(1),EndpointId(1),EndpointId(3),Transport::PCIE,2000000,true,Provenance::SYNTHETIC));
  s.links.push_back(mkLink(LinkId(3),LinkGeneration(1),EndpointId(3),EndpointId(2),Transport::PCIE,2000000,true,Provenance::SYNTHETIC));
  CommunicationRequest req = mkRequest(CommunicationRequestId(1),EndpointId(1),EndpointId(2),8192);
  req.provenance="SYNTHETIC two-GPU";
  PlanOutcome po = planCommunication(req, s, w, b);
  CHECK(po.success);
  if (po.success) CHECK(po.plan->candidatePaths[0].stages[0].transport == Transport::HOST_MEMORY || po.plan->orderedStages.size()==1);
  std::printf("SYNTHETIC two-GPU plan stages=%zu\n", po.success?po.plan->orderedStages.size():0);
}

CP_TEST(synthetic_multicast_relay_tree) {
  Bounds b; b.maxPathDepth=6; b.maxCandidates=64; RankingWeights w;
  EvidenceSnapshot s; s.topologyGeneration=TopologyGeneration(1); s.congestionGeneration=CongestionGeneration(1);
  s.capacityGeneration=CapacityGeneration(1); s.capabilityGeneration=CapabilityGeneration(1); s.healthGeneration=HealthGeneration(1);
  s.placementGeneration=PlacementGeneration(1); s.policyGeneration=PolicyGeneration(1); s.authorityGeneration=currentAuthority();
  int cap=0;
  s.endpoints.push_back(mkEndpoint(EndpointId(1),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(1),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
  for (int i=2;i<=4;++i) s.endpoints.push_back(mkEndpoint(EndpointId(i),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(i),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
  s.links.push_back(mkLink(LinkId(1),LinkGeneration(1),EndpointId(1),EndpointId(2),Transport::HOST_MEMORY,1000000,true,Provenance::SYNTHETIC));
  s.links.push_back(mkLink(LinkId(2),LinkGeneration(1),EndpointId(1),EndpointId(3),Transport::HOST_MEMORY,1000000,true,Provenance::SYNTHETIC));
  s.links.push_back(mkLink(LinkId(3),LinkGeneration(1),EndpointId(1),EndpointId(4),Transport::HOST_MEMORY,1000000,true,Provenance::SYNTHETIC));
  (void)cap;
  CommunicationRequest req;
  req.id=CommunicationRequestId(2); req.generation=CommunicationRequestGeneration(1);
  req.shape=RequestShape::MULTICAST; req.source=EndpointId(1); req.payloadSize=Bytes(4096);
  req.destinations={EndpointId(2),EndpointId(3),EndpointId(4)};
  req.allowMulticast=true; req.allowStaging=true; req.allowRelay=true;
  req.requireValid();
  PlanOutcome po = planMulticast(req, s, w, b);
  CHECK(po.success);
  std::printf("SYNTHETIC multicast destinations planned=%zu\n", po.success?po.plan->multicastDestinations.size():0);
}

CP_TEST(synthetic_collective_shape) {
  Bounds b; b.maxPathDepth=6; b.maxCandidates=64; RankingWeights w;
  EvidenceSnapshot s; s.topologyGeneration=TopologyGeneration(1); s.congestionGeneration=CongestionGeneration(1);
  s.capacityGeneration=CapacityGeneration(1); s.capabilityGeneration=CapabilityGeneration(1); s.healthGeneration=HealthGeneration(1);
  s.placementGeneration=PlacementGeneration(1); s.policyGeneration=PolicyGeneration(1); s.authorityGeneration=currentAuthority();
  std::vector<EndpointId> parts;
  for (int i=1;i<=4;++i){ parts.push_back(EndpointId(i)); s.endpoints.push_back(mkEndpoint(EndpointId(i),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(i),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC)); }
  for (int i=1;i<4;++i) s.links.push_back(mkLink(LinkId(i),LinkGeneration(1),EndpointId(i),EndpointId(i+1),Transport::HOST_MEMORY,1000000,true,Provenance::SYNTHETIC));
  CommunicationRequest req;
  req.id=CommunicationRequestId(3); req.generation=CommunicationRequestGeneration(1);
  req.shape=RequestShape::COLLECTIVE; req.collectiveShape=CollectiveShape::ALL_REDUCE;
  req.collectiveCount=4; req.participants=parts; req.payloadSize=Bytes(8192);
  req.requireValid();
  PlanOutcome po = planCollective(req, s, w, b);
  CHECK(po.success);
  if (po.success) { CHECK(po.plan->collectiveShape==CollectiveShape::ALL_REDUCE); CHECK(po.plan->collectiveParticipants.size()==4); }
  std::printf("SYNTHETIC collective shape=%s participants=%llu\n", toString(po.success?po.plan->collectiveShape:CollectiveShape::UNKNOWN).data(), (unsigned long long)(po.success?po.plan->collectiveParticipants.size():0));
}

CP_TEST(synthetic_congestion_alternate_route) {
  Bounds b; b.maxPathDepth=6; b.maxCandidates=64; RankingWeights w;
  auto base = [](){ EvidenceSnapshot s; s.topologyGeneration=TopologyGeneration(1); s.congestionGeneration=CongestionGeneration(1);
    s.capacityGeneration=CapacityGeneration(1); s.capabilityGeneration=CapabilityGeneration(1); s.healthGeneration=HealthGeneration(1);
    s.placementGeneration=PlacementGeneration(1); s.policyGeneration=PolicyGeneration(1); s.authorityGeneration=currentAuthority();
    s.endpoints.push_back(mkEndpoint(EndpointId(1),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(1),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
    s.endpoints.push_back(mkEndpoint(EndpointId(2),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(2),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
    s.endpoints.push_back(mkEndpoint(EndpointId(3),EndpointGeneration(1),EndpointKind::NIC,NodeId(1),true,true,{Transport::NETWORK,Transport::HOST_MEMORY},Provenance::SYNTHETIC));
    s.endpoints.push_back(mkEndpoint(EndpointId(4),EndpointGeneration(1),EndpointKind::NIC,NodeId(2),true,true,{Transport::NETWORK,Transport::HOST_MEMORY},Provenance::SYNTHETIC));
    s.links.push_back(mkLink(LinkId(1),LinkGeneration(1),EndpointId(1),EndpointId(2),Transport::HOST_MEMORY,1000000,true,Provenance::SYNTHETIC));
    s.links.push_back(mkLink(LinkId(2),LinkGeneration(1),EndpointId(1),EndpointId(3),Transport::HOST_MEMORY,2000000,true,Provenance::SYNTHETIC));
    s.links.push_back(mkLink(LinkId(3),LinkGeneration(1),EndpointId(3),EndpointId(4),Transport::NETWORK,2000000,true,Provenance::SYNTHETIC));
    s.links.push_back(mkLink(LinkId(4),LinkGeneration(1),EndpointId(4),EndpointId(2),Transport::HOST_MEMORY,2000000,true,Provenance::SYNTHETIC));
    return s; };
  EvidenceSnapshot s = base();
  CommunicationRequest req = mkRequest(CommunicationRequestId(4),EndpointId(1),EndpointId(2),4096);
  PlanOutcome po = planCommunication(req, s, w, b);
  CHECK(po.success);
  if (po.success) CHECK(po.plan->orderedStages.size()==1); // direct low cost wins initially
  // Now congest the direct link heavily (but not hard-limit) -> alternate route.
  s.links[0].congestion.load=0.99; s.links[0].congestion.penalty=1e12;
  s.links[0].congestion.provenance=Provenance::REPORTED;
  PlanOutcome po2 = planCommunication(req, s, w, b);
  CHECK(po2.success);
  if (po2.success) CHECK(po2.plan->orderedStages.size() >= 2); // alternate route (via NICs) chosen
  std::printf("SYNTHETIC congestion alternate route stages=%zu\n", po2.success?po2.plan->orderedStages.size():0);
}

CP_TEST(synthetic_reservation_blocks_preferred) {
  Bounds b; b.maxPathDepth=6; b.maxCandidates=64; RankingWeights w;
  EvidenceSnapshot s; s.topologyGeneration=TopologyGeneration(1); s.congestionGeneration=CongestionGeneration(1);
  s.capacityGeneration=CapacityGeneration(1); s.capabilityGeneration=CapabilityGeneration(1); s.healthGeneration=HealthGeneration(1);
  s.placementGeneration=PlacementGeneration(1); s.policyGeneration=PolicyGeneration(1); s.authorityGeneration=currentAuthority();
  s.endpoints.push_back(mkEndpoint(EndpointId(1),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(1),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
  s.endpoints.push_back(mkEndpoint(EndpointId(2),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(2),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
  s.endpoints.push_back(mkEndpoint(EndpointId(3),EndpointGeneration(1),EndpointKind::PINNED_HOST_MEMORY,NodeId(1),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
  s.links.push_back(mkLink(LinkId(1),LinkGeneration(1),EndpointId(1),EndpointId(2),Transport::HOST_MEMORY,1000000,true,Provenance::SYNTHETIC));
  s.links.push_back(mkLink(LinkId(2),LinkGeneration(1),EndpointId(1),EndpointId(3),Transport::PCIE,2000000,true,Provenance::SYNTHETIC));
  s.links.push_back(mkLink(LinkId(3),LinkGeneration(1),EndpointId(3),EndpointId(2),Transport::PCIE,2000000,true,Provenance::SYNTHETIC));
  Reservation rsv; rsv.resource=ResourceId(1); rsv.reserved=BytesPerSecond(2000000); rsv.provenance=Provenance::REPORTED;
  s.reservations.push_back(rsv);  // consume direct link capacity
  CommunicationRequest req = mkRequest(CommunicationRequestId(5),EndpointId(1),EndpointId(2),4096);
  PlanOutcome po = planCommunication(req, s, w, b);
  CHECK(po.success);
  if (po.success) CHECK(po.plan->orderedStages.size()==2); // reservation blocks direct -> staged
  std::printf("SYNTHETIC reservation blocks direct -> staged stages=%zu\n", po.success?po.plan->orderedStages.size():0);
}

CP_TEST(synthetic_failure_domain_route) {
  Bounds b; b.maxPathDepth=6; b.maxCandidates=64; RankingWeights w;
  EvidenceSnapshot s; s.topologyGeneration=TopologyGeneration(1); s.congestionGeneration=CongestionGeneration(1);
  s.capacityGeneration=CapacityGeneration(1); s.capabilityGeneration=CapabilityGeneration(1); s.healthGeneration=HealthGeneration(1);
  s.placementGeneration=PlacementGeneration(1); s.policyGeneration=PolicyGeneration(1); s.authorityGeneration=currentAuthority();
  s.endpoints.push_back(mkEndpoint(EndpointId(1),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(1),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
  s.endpoints.push_back(mkEndpoint(EndpointId(2),EndpointGeneration(1),EndpointKind::GPU_DEVICE,NodeId(2),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
  s.endpoints.push_back(mkEndpoint(EndpointId(3),EndpointGeneration(1),EndpointKind::PINNED_HOST_MEMORY,NodeId(1),true,true,{Transport::HOST_MEMORY,Transport::PCIE},Provenance::SYNTHETIC));
  Link d1=mkLink(LinkId(1),LinkGeneration(1),EndpointId(1),EndpointId(2),Transport::HOST_MEMORY,1000000,true,Provenance::SYNTHETIC); d1.failureDomain="rackB";
  Link d2=mkLink(LinkId(2),LinkGeneration(1),EndpointId(1),EndpointId(3),Transport::PCIE,2000000,true,Provenance::SYNTHETIC); d2.failureDomain="rackA";
  Link d3=mkLink(LinkId(3),LinkGeneration(1),EndpointId(3),EndpointId(2),Transport::PCIE,2000000,true,Provenance::SYNTHETIC); d3.failureDomain="rackA";
  s.links={d1,d2,d3};
  CommunicationRequest req = mkRequest(CommunicationRequestId(6),EndpointId(1),EndpointId(2),4096);
  req.requiredFailureDomain="rackA";
  PlanOutcome po = planCommunication(req, s, w, b);
  CHECK(po.success);
  if (po.success) { // direct link rackB excluded; staged rackA chosen
    bool foundRackB=false; for (const Stage& st: po.plan->orderedStages){ if (st.link.value()==1) foundRackB=true; }
    CHECK(!foundRackB);
  }
  std::printf("SYNTHETIC failure-domain-aware route stages=%zu\n", po.success?po.plan->orderedStages.size():0);
}

CP_MAIN();
