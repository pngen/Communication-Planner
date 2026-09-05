#include "communication_planner/protocol/protocol.hpp"
#include <cstring>
#include <cmath>

namespace communication_planner {

std::string toString(MessageType t) {
  switch (t) {
    case MessageType::HELLO: return "HELLO";
    case MessageType::REGISTER: return "REGISTER";
    case MessageType::PUBLISH_ENDPOINT: return "PUBLISH_ENDPOINT";
    case MessageType::PUBLISH_LINK: return "PUBLISH_LINK";
    case MessageType::PUBLISH_TOPOLOGY: return "PUBLISH_TOPOLOGY";
    case MessageType::PUBLISH_CAPACITY: return "PUBLISH_CAPACITY";
    case MessageType::PUBLISH_CONGESTION: return "PUBLISH_CONGESTION";
    case MessageType::SUBMIT_REQUEST: return "SUBMIT_REQUEST";
    case MessageType::QUERY_PLAN: return "QUERY_PLAN";
    case MessageType::PLAN_RESULT: return "PLAN_RESULT";
    case MessageType::COMMIT_REQUEST: return "COMMIT_REQUEST";
    case MessageType::COMMIT_RESULT: return "COMMIT_RESULT";
    case MessageType::REVALIDATE: return "REVALIDATE";
    case MessageType::SUPERSEDE: return "SUPERSEDE";
    case MessageType::CANCEL: return "CANCEL";
    case MessageType::EXECUTION_HANDOFF: return "EXECUTION_HANDOFF";
    case MessageType::EXECUTION_RESULT: return "EXECUTION_RESULT";
    case MessageType::EXECUTE: return "EXECUTE";
    case MessageType::SAVE: return "SAVE";
    case MessageType::SHUTDOWN: return "SHUTDOWN";
    case MessageType::ERROR: return "ERROR";
  }
  return "ERROR";
}

namespace {
constexpr std::uint32_t kFrameMagic = 0x43504600u;  // 'CPF\0'
constexpr std::uint32_t kFrameVersion = 1u;
constexpr std::uint64_t kMaxCount = 1u << 20;

class W {
 public:
  void u8(std::uint8_t v){ b_.push_back(static_cast<std::byte>(v)); }
  void u16(std::uint16_t v){ for(int i=0;i<2;++i) u8((std::uint8_t)((v>>(8*i))&0xff)); }
  void u32(std::uint32_t v){ for(int i=0;i<4;++i) u8((std::uint8_t)((v>>(8*i))&0xff)); }
  void u64(std::uint64_t v){ for(int i=0;i<8;++i) u8((std::uint8_t)((v>>(8*i))&0xff)); }
  void boo(bool b){ u8(b?1:0); }
  void dbl(double d){ std::uint64_t u; std::memcpy(&u,&d,8); u64(u); }
  void str(const std::string& s){ u64(s.size()); for(char c: s) u8((std::uint8_t)c); }
  const std::vector<std::byte>& bytes() const { return b_; }
 private: std::vector<std::byte> b_;
};
class R {
 public:
  R(const std::byte* d, std::size_t n):d_(d),n_(n){}
  bool u8(std::uint8_t* v){ if(pos_+1>n_) return false; *v=(std::uint8_t)d_[pos_]; ++pos_; return true; }
  bool u16(std::uint16_t* v){ std::uint8_t a,b; if(!u8(&a)||!u8(&b)) return false; *v=(std::uint16_t)(a|(b<<8)); return true; }
  bool u32(std::uint32_t* v){ std::uint8_t x[4]; for(int i=0;i<4;++i) if(!u8(&x[i])) return false; *v=(std::uint32_t)x[0]|((std::uint32_t)x[1]<<8)|((std::uint32_t)x[2]<<16)|((std::uint32_t)x[3]<<24); return true; }
  bool u64(std::uint64_t* v){ std::uint8_t x[8]; for(int i=0;i<8;++i) if(!u8(&x[i])) return false; std::uint64_t r=0; for(int i=0;i<8;++i) r|=((std::uint64_t)x[i]<<(8*i)); *v=r; return true; }
  bool boo(bool* b){ std::uint8_t v; if(!u8(&v)) return false; *b=v!=0; return true; }
  bool dbl(double* d){ std::uint64_t u; if(!u64(&u)) return false; std::memcpy(d,&u,8); return true; }
  bool str(std::string* s){ std::uint64_t len; if(!u64(&len)) return false; if(len>kMaxCount) return false; if(pos_+len>n_) return false; s->assign((const char*)(d_+pos_),(std::size_t)len); pos_+=len; return true; }
  std::size_t pos() const { return pos_; }
  std::size_t size() const { return n_; }
  bool atEnd() const { return pos_==n_; }
 private: const std::byte* d_; std::size_t n_; std::size_t pos_{0};
};
static std::uint64_t fnv(const std::byte* d, std::size_t n){ std::uint64_t h=1469598103934665603ull; for(std::size_t i=0;i<n;++i){ h^=(std::uint64_t)(std::uint8_t)d[i]; h*=1099511628211ull; } return h; }

static void encEndpoint(W& w, const Endpoint& e){
  w.u64(e.id.value()); w.u64(e.generation.value()); w.u8((std::uint8_t)e.kind);
  w.u64(e.node.value()); w.u64(e.device.value());
  w.u64(e.capability.generation.value()); w.u32((std::uint32_t)e.capability.transports.size());
  for (Transport t : e.capability.transports) w.u8((std::uint8_t)t);
  w.u8((std::uint8_t)e.capability.provenance);
  for (int i=0;i<8;++i) w.u8((std::uint8_t)e.capability.computeArch[i]);
  w.str(e.address); w.str(e.protocol); w.str(e.version); w.str(e.name);
  w.u8(e.health.healthy?1:0); w.u8(e.reachable?1:0); w.u8(e.ready?1:0);
  w.u64(e.worker.value()); w.u64(e.workerBoot.value());
  w.u64(e.source.value()); w.u64(e.sourceBoot.value());
  w.str(e.failureDomain); w.u8((std::uint8_t)e.provenance);
  w.str(e.location.pcieRoot); w.u64(e.location.numaDistance>0? (std::uint64_t)e.location.numaDistance : 0);
}
static bool decEndpoint(R& r, Endpoint& e){
  std::uint64_t v; if(!r.u64(&v)) return false; e.id=EndpointId(v);
  if(!r.u64(&v)) return false; e.generation=EndpointGeneration(v);
  std::uint8_t k; if(!r.u8(&k)) return false; if(k>(std::uint8_t)EndpointKind::UNKNOWN) return false; e.kind=(EndpointKind)k;
  if(!r.u64(&v)) return false; e.node=NodeId(v);
  if(!r.u64(&v)) return false; e.device=DeviceId(v);
  if(!r.u64(&v)) return false; e.capability.generation=CapabilityGeneration(v);
  std::uint32_t nt; if(!r.u32(&nt)) return false; if(nt>64) return false;
  for(std::uint32_t i=0;i<nt;++i){ std::uint8_t t; if(!r.u8(&t)) return false; if(t>(std::uint8_t)Transport::UNKNOWN) return false; e.capability.transports.insert((Transport)t); }
  std::uint8_t pv; if(!r.u8(&pv)) return false; if(pv>(std::uint8_t)Provenance::UNKNOWN) return false; e.capability.provenance=(Provenance)pv;
  std::string arch; for(int i=0;i<8;++i){ std::uint8_t c; if(!r.u8(&c)) return false; arch.push_back((char)c); } e.setArch(arch);
  if(!r.str(&e.address)) return false; if(!r.str(&e.protocol)) return false; if(!r.str(&e.version)) return false; if(!r.str(&e.name)) return false;
  std::uint8_t hh; if(!r.u8(&hh)) return false; e.health.healthy=hh!=0;
  std::uint8_t rc; if(!r.u8(&rc)) return false; e.reachable=rc!=0;
  std::uint8_t rd; if(!r.u8(&rd)) return false; e.ready=rd!=0;
  if(!r.u64(&v)) return false; e.worker=WorkerId(v);
  if(!r.u64(&v)) return false; e.workerBoot=WorkerBootId(v);
  if(!r.u64(&v)) return false; e.source=SourceId(v);
  if(!r.u64(&v)) return false; e.sourceBoot=SourceBootId(v);
  if(!r.str(&e.failureDomain)) return false;
  std::uint8_t ep; if(!r.u8(&ep)) return false; if(ep>(std::uint8_t)Provenance::UNKNOWN) return false; e.provenance=(Provenance)ep;
  if(!r.str(&e.location.pcieRoot)) return false;
  if(!r.u64(&v)) return false; e.location.numaDistance=(int)v;
  return true;
}
static void encLink(W& w, const Link& l){
  w.u64(l.id.value()); w.u64(l.generation.value());
  w.u64(l.source.value()); w.u64(l.destination.value());
  w.u8((std::uint8_t)l.kind); w.u8((std::uint8_t)l.transport); w.boo(l.directed);
  w.u64(l.capacity.count()); w.u64(l.effectiveBandwidth.count()); w.u64(l.latency.count());
  w.u8(l.health.healthy?1:0); w.u64(l.congestion.generation.value());
  w.dbl(l.congestion.load); w.dbl(l.congestion.penalty); w.boo(l.congestion.hardLimitExceeded);
  w.u8((std::uint8_t)l.congestion.provenance);
  w.u64(l.capacityEvidence.generation.value()); w.u64(l.capacityEvidence.usable.count()); w.u64(l.capacityEvidence.headroom.count()); w.u8((std::uint8_t)l.capacityEvidence.provenance);
  w.u64(l.topologyGeneration.value()); w.u64(l.capabilityGeneration.value());
  w.str(l.failureDomain); w.u64(l.workerBoot.value()); w.u8((std::uint8_t)l.provenance);
}
static bool decLink(R& r, Link& l){
  std::uint64_t v; if(!r.u64(&v)) return false; l.id=LinkId(v);
  if(!r.u64(&v)) return false; l.generation=LinkGeneration(v);
  if(!r.u64(&v)) return false; l.source=EndpointId(v);
  if(!r.u64(&v)) return false; l.destination=EndpointId(v);
  std::uint8_t k; if(!r.u8(&k)) return false; if(k>(std::uint8_t)LinkKind::UNKNOWN) return false; l.kind=(LinkKind)k;
  std::uint8_t tr; if(!r.u8(&tr)) return false; if(tr>(std::uint8_t)Transport::UNKNOWN) return false; l.transport=(Transport)tr;
  if(!r.boo(&l.directed)) return false;
  if(!r.u64(&v)) return false; l.capacity=BytesPerSecond(v);
  if(!r.u64(&v)) return false; l.effectiveBandwidth=BytesPerSecond(v);
  if(!r.u64(&v)) return false; l.latency=DurationNs(v);
  std::uint8_t hh; if(!r.u8(&hh)) return false; l.health.healthy=hh!=0;
  if(!r.u64(&v)) return false; l.congestion.generation=CongestionGeneration(v);
  if(!r.dbl(&l.congestion.load)) return false; if(!r.dbl(&l.congestion.penalty)) return false; if(!r.boo(&l.congestion.hardLimitExceeded)) return false;
  std::uint8_t cp; if(!r.u8(&cp)) return false; if(cp>(std::uint8_t)Provenance::UNKNOWN) return false; l.congestion.provenance=(Provenance)cp;
  if(!r.u64(&v)) return false; l.capacityEvidence.generation=CapacityGeneration(v);
  if(!r.u64(&v)) return false; l.capacityEvidence.usable=BytesPerSecond(v);
  if(!r.u64(&v)) return false; l.capacityEvidence.headroom=BytesPerSecond(v);
  std::uint8_t cap; if(!r.u8(&cap)) return false; if(cap>(std::uint8_t)Provenance::UNKNOWN) return false; l.capacityEvidence.provenance=(Provenance)cap;
  if(!r.u64(&v)) return false; l.topologyGeneration=TopologyGeneration(v);
  if(!r.u64(&v)) return false; l.capabilityGeneration=CapabilityGeneration(v);
  if(!r.str(&l.failureDomain)) return false;
  if(!r.u64(&v)) return false; l.workerBoot=WorkerBootId(v);
  std::uint8_t lp; if(!r.u8(&lp)) return false; if(lp>(std::uint8_t)Provenance::UNKNOWN) return false; l.provenance=(Provenance)lp;
  return true;
}
static void encRequest(W& w, const CommunicationRequest& req){
  w.u64(req.id.value()); w.u64(req.generation.value()); w.u8((std::uint8_t)req.shape);
  w.u64(req.source.value()); w.u64(req.payloadSize.count());
  w.u8((std::uint8_t)req.direction);
  w.boo(req.allowStaging); w.boo(req.allowHostStaging); w.boo(req.allowStorageStaging); w.boo(req.allowRelay); w.boo(req.allowMulticast);
  w.u32(req.maxHops); w.u32(req.maxStages); w.u64(req.externalPriority); w.u64(req.policyGeneration.value());
  w.u32((std::uint32_t)req.allowed.size()); for(Transport t: req.allowed) w.u8((std::uint8_t)t);
  w.u32((std::uint32_t)req.forbidden.size()); for(Transport t: req.forbidden) w.u8((std::uint8_t)t);
  w.u8((std::uint8_t)req.collectiveShape); w.u64(req.collectiveCount);
  w.u32((std::uint32_t)req.destinations.size()); for(EndpointId id: req.destinations) w.u64(id.value());
  w.u32((std::uint32_t)req.participants.size()); for(EndpointId id: req.participants) w.u64(id.value());
  w.str(req.provenance); w.str(req.requiredFailureDomain); w.str(req.preferredLocality);
}
static bool decRequest(R& r, CommunicationRequest& req){
  std::uint64_t v; if(!r.u64(&v)) return false; req.id=CommunicationRequestId(v);
  if(!r.u64(&v)) return false; req.generation=CommunicationRequestGeneration(v);
  std::uint8_t sh; if(!r.u8(&sh)) return false; if(sh>(std::uint8_t)RequestShape::UNKNOWN) return false; req.shape=(RequestShape)sh;
  if(!r.u64(&v)) return false; req.source=EndpointId(v);
  if(!r.u64(&v)) return false; req.payloadSize=Bytes(v);
  std::uint8_t d; if(!r.u8(&d)) return false; if(d>2) return false; req.direction=(Direction)d;
  if(!r.boo(&req.allowStaging)) return false; if(!r.boo(&req.allowHostStaging)) return false; if(!r.boo(&req.allowStorageStaging)) return false; if(!r.boo(&req.allowRelay)) return false; if(!r.boo(&req.allowMulticast)) return false;
  std::uint32_t mh; if(!r.u32(&mh)) return false; req.maxHops=mh; std::uint32_t ms; if(!r.u32(&ms)) return false; req.maxStages=ms;
  if(!r.u64(&v)) return false; req.externalPriority=(unsigned)v;
  if(!r.u64(&v)) return false; req.policyGeneration=PolicyGeneration(v);
  std::uint32_t na; if(!r.u32(&na)) return false; if(na>64) return false; for(std::uint32_t i=0;i<na;++i){ std::uint8_t t; if(!r.u8(&t)) return false; if(t>(std::uint8_t)Transport::UNKNOWN) return false; req.allowed.insert((Transport)t); }
  std::uint32_t nf; if(!r.u32(&nf)) return false; if(nf>64) return false; for(std::uint32_t i=0;i<nf;++i){ std::uint8_t t; if(!r.u8(&t)) return false; if(t>(std::uint8_t)Transport::UNKNOWN) return false; req.forbidden.insert((Transport)t); }
  std::uint8_t cs; if(!r.u8(&cs)) return false; if(cs>(std::uint8_t)CollectiveShape::UNKNOWN) return false; req.collectiveShape=(CollectiveShape)cs;
  if(!r.u64(&v)) return false; if (v > 4294967295ull) return false; req.collectiveCount=(unsigned)v;
  std::uint32_t nd; if(!r.u32(&nd)) return false; if(nd>kMaxCount) return false; for(std::uint32_t i=0;i<nd;++i){ if(!r.u64(&v)) return false; req.destinations.push_back(EndpointId(v)); }
  std::uint32_t np; if(!r.u32(&np)) return false; if(np>kMaxCount) return false; for(std::uint32_t i=0;i<np;++i){ if(!r.u64(&v)) return false; req.participants.push_back(EndpointId(v)); }
  if(!r.str(&req.provenance)) return false; if(!r.str(&req.requiredFailureDomain)) return false; if(!r.str(&req.preferredLocality)) return false;
  return true;
}
}  // namespace

ProtocolError encodeFrame(const Frame& f, std::vector<std::byte>& out){
  if (f.payload.size() > kMaxFramePayload){ ProtocolError e; e.detail="frame payload oversized"; return e; }
  W body;
  body.u32(kFrameMagic); body.u32(kFrameVersion); body.u8((std::uint8_t)f.type);
  body.u64((std::uint64_t)f.payload.size());
  for (std::byte b : f.payload) body.u8((std::uint8_t)b);
  const std::vector<std::byte>& b = body.bytes();
  std::uint64_t checksum = fnv(b.data(), b.size());
  out = b;
  for (int i=0;i<8;++i) out.push_back(static_cast<std::byte>((checksum >> (8*i)) & 0xffu));
  return ProtocolError{};
}

ProtocolError decodeFrame(const std::byte* data, std::size_t n, Frame& out){
  ProtocolError err;
  R r(data,n);
  std::uint32_t m,v; if(!r.u32(&m)){ err.detail="frame truncated: magic"; return err;} if(m!=kFrameMagic){ err.detail="bad frame magic"; return err;}
  if(!r.u32(&v)){ err.detail="frame truncated: version"; return err;} if(v!=kFrameVersion){ err.detail="unsupported frame version"; return err;}
  std::uint8_t t; if(!r.u8(&t)){ err.detail="frame truncated: type"; return err;} if(t>(std::uint8_t)MessageType::ERROR){ err.detail="invalid frame type"; return err;}
  std::uint64_t len; if(!r.u64(&len)){ err.detail="frame truncated: length"; return err;} if(len>kMaxFramePayload){ err.detail="frame payload oversized"; return err;}
  const std::size_t header = r.pos();            // 17 bytes
  if (header + len + 8 > n){ err.detail="frame truncated: payload/checksum"; return err;}
  std::uint64_t bodySum = fnv(data, header + len);
  std::uint64_t checksum = 0;
  for (int i=0;i<8;++i) checksum |= (static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[header+len+i])) << (8*i));
  if (bodySum != checksum){ err.detail="frame checksum mismatch"; return err;}
  out.type=(MessageType)t;
  out.payload.assign(data+header, data+header+len);
  return err;
}

ProtocolError decodeMessage(MessageType type, const std::byte* payload, std::size_t n, Message& out){
  ProtocolError ok;
  out.type=type;
  R r(payload,n);
  std::uint64_t v;
  auto fail=[&](const std::string& d){ ProtocolError e; e.detail=d; return e; };
  switch(type){
    case MessageType::HELLO:
      if(!r.u64(&v)) return fail("HELLO trunc"); out.epoch=CoordinatorEpoch(v); break;
    case MessageType::REGISTER:
      if(!r.u64(&v)) return fail("REG worker"); out.worker=WorkerId(v);
      if(!r.u64(&v)) return fail("REG boot"); out.boot=WorkerBootId(v);
      if(!r.str(&out.name)) return fail("REG name"); break;
    case MessageType::PUBLISH_ENDPOINT:
      if(!r.u64(&v)) return fail("EP worker"); out.worker=WorkerId(v);
      if(!r.u64(&v)) return fail("EP boot"); out.boot=WorkerBootId(v);
      if(!decEndpoint(r,out.endpoint)) return fail("EP endpoint"); break;
    case MessageType::PUBLISH_LINK:
      if(!r.u64(&v)) return fail("LK worker"); out.worker=WorkerId(v);
      if(!r.u64(&v)) return fail("LK boot"); out.boot=WorkerBootId(v);
      if(!decLink(r,out.link)) return fail("LK link"); break;
    case MessageType::PUBLISH_CAPACITY:
      if(!r.u64(&v)) return fail("CAP link"); out.linkId=LinkId(v);
      if(!r.u64(&v)) return fail("CAP gen"); out.capacity.generation=CapacityGeneration(v);
      if(!r.u64(&v)) return fail("CAP usable"); out.capacity.usable=BytesPerSecond(v);
      if(!r.u64(&v)) return fail("CAP head"); out.capacity.headroom=BytesPerSecond(v);
      { std::uint8_t p; if(!r.u8(&p)) return fail("CAP prov"); if(p>(std::uint8_t)Provenance::UNKNOWN) return fail("CAP prov enum"); out.capacity.provenance=(Provenance)p; }
      break;
    case MessageType::PUBLISH_CONGESTION:
      if(!r.u64(&v)) return fail("COG link"); out.linkId=LinkId(v);
      if(!r.u64(&v)) return fail("COG gen"); out.congestion.generation=CongestionGeneration(v);
      if(!r.dbl(&out.congestion.load)) return fail("COG load");
      if(!r.dbl(&out.congestion.penalty)) return fail("COG pen");
      if(!r.boo(&out.congestion.hardLimitExceeded)) return fail("COG hard");
      { std::uint8_t p; if(!r.u8(&p)) return fail("COG prov"); if(p>(std::uint8_t)Provenance::UNKNOWN) return fail("COG prov enum"); out.congestion.provenance=(Provenance)p; }
      break;
    case MessageType::SUBMIT_REQUEST:
      if(!decRequest(r,out.request)) return fail("SUBMIT request"); break;
    case MessageType::PLAN_RESULT:
      if(!r.boo(&out.ok)) return fail("PR ok");
      { std::uint8_t fb; if(!r.u8(&fb)) return fail("PR feas"); if(fb>(std::uint8_t)Feasibility::UNKNOWN) return fail("PR feas enum"); if(fb==(std::uint8_t)Feasibility::FEASIBLE){ /* read plan */ } }
      if(out.ok){
        // minimal plan field read (id/gen/request/gen + primary) for proof
        if(!r.u64(&v)) return fail("PR plan id"); out.plan.id=CommunicationPlanId(v);
        if(!r.u64(&v)) return fail("PR plan gen"); out.plan.generation=CommunicationPlanGeneration(v);
        if(!r.u64(&v)) return fail("PR req id"); out.plan.requestId=CommunicationRequestId(v);
        if(!r.u64(&v)) return fail("PR req gen"); out.plan.requestGeneration=CommunicationRequestGeneration(v);
        if(!r.u64(&v)) return fail("PR primary"); out.plan.primaryPath=PathId(v);
        std::uint32_t np; if(!r.u32(&np)) return fail("PR paths"); if(np>kMaxCount) return fail("PR paths count");
        for(std::uint32_t i=0;i<np;++i){ Path p; std::uint64_t id; if(!r.u64(&id)) return fail("PR path id"); p.id=PathId(id); if(!r.u64(&v)) return fail("PR path payload"); p.totalPayload=Bytes(v); std::uint32_t ns; if(!r.u32(&ns)) return fail("PR stages"); if(ns>kMaxCount) return fail("PR stages count"); for(std::uint32_t j=0;j<ns;++j){ Stage st; if(!r.u64(&v)) return fail("PR st"); st.link=LinkId(v); if(!r.u64(&v)) return fail("PR st"); st.source=EndpointId(v); if(!r.u64(&v)) return fail("PR st"); st.destination=EndpointId(v); if(!r.u64(&v)) return fail("PR st"); st.payload=Bytes(v); p.stages.push_back(st); } out.plan.candidatePaths.push_back(p); }
        std::uint32_t ns2; if(!r.u32(&ns2)) return fail("PR ordered stages"); if(ns2>kMaxCount) return fail("PR ordered count");
        for(std::uint32_t i=0;i<ns2;++i){ Stage st; if(!r.u64(&v)) return fail("PR os"); st.link=LinkId(v); if(!r.u64(&v)) return fail("PR os"); st.source=EndpointId(v); if(!r.u64(&v)) return fail("PR os"); st.destination=EndpointId(v); if(!r.u64(&v)) return fail("PR os"); st.payload=Bytes(v); out.plan.orderedStages.push_back(st); }
        if(!r.u32(&np)) return fail("PR endp gens"); if(np>kMaxCount) return fail("PR eg count");
        for(std::uint32_t i=0;i<np;++i){ EndpointId e; EndpointGeneration g; if(!r.u64(&v)) return fail("PR eg"); e=EndpointId(v); if(!r.u64(&v)) return fail("PR eg"); g=EndpointGeneration(v); out.plan.endpointGenerations.push_back({e,g}); }
      }
      break;
    case MessageType::REVALIDATE:
    case MessageType::COMMIT_REQUEST:
    case MessageType::EXECUTION_HANDOFF:
    case MessageType::QUERY_PLAN:
      if(!r.u64(&v)) return fail("ID"); out.planId=CommunicationPlanId(v); break;
    case MessageType::CANCEL:
    case MessageType::SUPERSEDE:
      if(!r.u64(&v)) return fail("ID"); out.requestId=CommunicationRequestId(v);
      if(!r.u64(&v)) return fail("ID2"); out.planId=CommunicationPlanId(v); break;
    case MessageType::COMMIT_RESULT:
      if(!r.boo(&out.ok)) return fail("RES ok"); if(!r.u64(&v)) return fail("RES id"); out.planId=CommunicationPlanId(v); break;
    case MessageType::EXECUTION_RESULT:
      if(!r.boo(&out.ok)) return fail("ER ok"); if(!r.u64(&v)) return fail("ER id"); out.planId=CommunicationPlanId(v);
      if(!r.u64(&v)) return fail("ER worker"); out.worker=WorkerId(v);
      if(!r.u64(&v)) return fail("ER boot"); out.boot=WorkerBootId(v); break;
    case MessageType::EXECUTE:
      if(!r.u64(&v)) return fail("EX id"); out.planId=CommunicationPlanId(v);
      if(!r.u64(&v)) return fail("EX bytes"); out.bytes=v;
      if(!r.u64(&v)) return fail("EX worker"); out.worker=WorkerId(v);
      if(!r.u64(&v)) return fail("EX boot"); out.boot=WorkerBootId(v); break;
    case MessageType::PUBLISH_TOPOLOGY:
      if(!r.u64(&v)) return fail("TOPO gen"); out.endpoint.capability.generation=CapabilityGeneration(v); break;
    case MessageType::SAVE: break;
    case MessageType::SHUTDOWN: break;
    case MessageType::ERROR: if(!r.str(&out.error)) return fail("ERR"); break;
  }
  return ok;
}

ProtocolError encodeMessage(const Message& m, std::vector<std::byte>& payload){
  W w;
  switch(m.type){
    case MessageType::HELLO: w.u64(m.epoch.value()); break;
    case MessageType::REGISTER: w.u64(m.worker.value()); w.u64(m.boot.value()); w.str(m.name); break;
    case MessageType::PUBLISH_ENDPOINT: w.u64(m.worker.value()); w.u64(m.boot.value()); encEndpoint(w,m.endpoint); break;
    case MessageType::PUBLISH_LINK: w.u64(m.worker.value()); w.u64(m.boot.value()); encLink(w,m.link); break;
    case MessageType::PUBLISH_CAPACITY: w.u64(m.linkId.value()); w.u64(m.capacity.generation.value()); w.u64(m.capacity.usable.count()); w.u64(m.capacity.headroom.count()); w.u8((std::uint8_t)m.capacity.provenance); break;
    case MessageType::PUBLISH_CONGESTION: w.u64(m.linkId.value()); w.u64(m.congestion.generation.value()); w.dbl(m.congestion.load); w.dbl(m.congestion.penalty); w.boo(m.congestion.hardLimitExceeded); w.u8((std::uint8_t)m.congestion.provenance); break;
    case MessageType::SUBMIT_REQUEST: encRequest(w,m.request); break;
    case MessageType::PLAN_RESULT:
      w.boo(m.ok); w.u8((std::uint8_t)(m.ok?Feasibility::FEASIBLE:Feasibility::REJECT_ENDPOINT));
      if(m.ok){
        w.u64(m.plan.id.value()); w.u64(m.plan.generation.value()); w.u64(m.plan.requestId.value()); w.u64(m.plan.requestGeneration.value()); w.u64(m.plan.primaryPath.value());
        w.u32((std::uint32_t)m.plan.candidatePaths.size());
        for(const Path& p: m.plan.candidatePaths){ w.u64(p.id.value()); w.u64(p.totalPayload.count()); w.u32((std::uint32_t)p.stages.size()); for(const Stage& st: p.stages){ w.u64(st.link.value()); w.u64(st.source.value()); w.u64(st.destination.value()); w.u64(st.payload.count()); } }
        w.u32((std::uint32_t)m.plan.orderedStages.size());
        for(const Stage& st: m.plan.orderedStages){ w.u64(st.link.value()); w.u64(st.source.value()); w.u64(st.destination.value()); w.u64(st.payload.count()); }
        w.u32((std::uint32_t)m.plan.endpointGenerations.size());
        for(const auto& pr: m.plan.endpointGenerations){ w.u64(pr.first.value()); w.u64(pr.second.value()); }
      }
      break;
    case MessageType::REVALIDATE:
    case MessageType::COMMIT_REQUEST:
    case MessageType::EXECUTION_HANDOFF:
    case MessageType::QUERY_PLAN: w.u64(m.planId.value()); break;
    case MessageType::CANCEL:
    case MessageType::SUPERSEDE: w.u64(m.requestId.value()); w.u64(m.planId.value()); break;
    case MessageType::COMMIT_RESULT: w.boo(m.ok); w.u64(m.planId.value()); break;
    case MessageType::EXECUTION_RESULT: w.boo(m.ok); w.u64(m.planId.value()); w.u64(m.worker.value()); w.u64(m.boot.value()); break;
    case MessageType::EXECUTE: w.u64(m.planId.value()); w.u64(m.bytes); w.u64(m.worker.value()); w.u64(m.boot.value()); break;
    case MessageType::PUBLISH_TOPOLOGY: w.u64(m.endpoint.capability.generation.value()); break;
    case MessageType::SAVE: break;
    case MessageType::SHUTDOWN: break;
    case MessageType::ERROR: w.str(m.error); break;
  }
  payload = w.bytes();
  return ProtocolError{};
}

}  // namespace communication_planner
