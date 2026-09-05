#include "communication_planner/persistence/persistence.hpp"

#include <cstring>
#include <cmath>
#include <map>
#include <set>

namespace communication_planner {

std::string toString(PersistErrorCode c) {
  switch (c) {
    case PersistErrorCode::OK: return "OK";
    case PersistErrorCode::BAD_MAGIC: return "BAD_MAGIC";
    case PersistErrorCode::UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
    case PersistErrorCode::TRUNCATION: return "TRUNCATION";
    case PersistErrorCode::CORRUPTION: return "CORRUPTION";
    case PersistErrorCode::CHECKSUM_MISMATCH: return "CHECKSUM_MISMATCH";
    case PersistErrorCode::MALFORMED_LENGTH: return "MALFORMED_LENGTH";
    case PersistErrorCode::OVERSIZED_COUNT: return "OVERSIZED_COUNT";
    case PersistErrorCode::INVALID_ENUM: return "INVALID_ENUM";
    case PersistErrorCode::DUPLICATE_ID: return "DUPLICATE_ID";
    case PersistErrorCode::DUPLICATE_CURRENT_AUTHORITY: return "DUPLICATE_CURRENT_AUTHORITY";
    case PersistErrorCode::GENERATION_REGRESSION: return "GENERATION_REGRESSION";
    case PersistErrorCode::IMPOSSIBLE_LIFECYCLE: return "IMPOSSIBLE_LIFECYCLE";
    case PersistErrorCode::PATH_MISSING_ENDPOINT: return "PATH_MISSING_ENDPOINT";
    case PersistErrorCode::INVALID_STAGE_ORDER: return "INVALID_STAGE_ORDER";
    case PersistErrorCode::CYCLE_FORBIDDEN: return "CYCLE_FORBIDDEN";
    case PersistErrorCode::PAYLOAD_MISMATCH: return "PAYLOAD_MISMATCH";
    case PersistErrorCode::SELECTED_PLAN_NOT_IN_CANDIDATE_SET: return "SELECTED_PLAN_NOT_IN_CANDIDATE_SET";
    case PersistErrorCode::COMMITTED_PLAN_NO_EVIDENCE: return "COMMITTED_PLAN_NO_EVIDENCE";
    case PersistErrorCode::NAN_INF_COST: return "NAN_INF_COST";
    case PersistErrorCode::CP_OVERFLOW: return "CP_OVERFLOW";
    case PersistErrorCode::TRAILING_GARBAGE: return "TRAILING_GARBAGE";
  }
  return "CORRUPTION";
}

namespace {
constexpr std::uint32_t kMagic = 0x43504C52u;   // 'CPLR'
constexpr std::uint32_t kFormatVersion = 1u;
constexpr std::uint64_t kMaxCount = 1u << 20;

// ---- Writer ----
class Writer {
 public:
  void u8(std::uint8_t v) { buf_.push_back(static_cast<std::byte>(v)); }
  void u16(std::uint16_t v) { for (int i=0;i<2;++i) u8(static_cast<std::uint8_t>((v>>(8*i))&0xff)); }
  void u32(std::uint32_t v) { for (int i=0;i<4;++i) u8(static_cast<std::uint8_t>((v>>(8*i))&0xff)); }
  void u64(std::uint64_t v) { for (int i=0;i<8;++i) u8(static_cast<std::uint8_t>((v>>(8*i))&0xff)); }
  void boo(bool b) { u8(b ? 1 : 0); }
  void dbl(double d) { std::uint64_t u; std::memcpy(&u, &d, sizeof u); u64(u); }
  void str(const std::string& s) {
    u64(s.size());
    for (char c : s) u8(static_cast<std::uint8_t>(c));
  }
  const std::vector<std::byte>& data() const { return buf_; }
 private:
  std::vector<std::byte> buf_;
};

class Reader {
 public:
  Reader(const std::byte* data, std::size_t n) : d_(data), n_(n) {}
  bool u8(std::uint8_t* v) { if (pos_+1>n_) return false; *v=toU8(d_[pos_]); pos_+=1; return true; }
  bool u16(std::uint16_t* v) { std::uint8_t b0,b1; if(!u8(&b0)||!u8(&b1)) return false; *v=(std::uint16_t)(b0|(b1<<8)); return true; }
  bool u32(std::uint32_t* v) { std::uint8_t b[4]; for(int i=0;i<4;++i) if(!u8(&b[i])) return false; *v=(std::uint32_t)b[0]|((std::uint32_t)b[1]<<8)|((std::uint32_t)b[2]<<16)|((std::uint32_t)b[3]<<24); return true; }
  bool u64(std::uint64_t* v) { std::uint8_t b[8]; for(int i=0;i<8;++i) if(!u8(&b[i])) return false; std::uint64_t r=0; for(int i=0;i<8;++i) r |= (static_cast<std::uint64_t>(b[i])<<(8*i)); *v=r; return true; }
  bool boo(bool* b) { std::uint8_t v; if(!u8(&v)) return false; *b = v!=0; return true; }
  bool dbl(double* d) { std::uint64_t u; if(!u64(&u)) return false; std::memcpy(d, &u, sizeof u); return true; }
  bool str(std::string* s) {
    std::uint64_t len; if(!u64(&len)) return false;
    if (len > kMaxCount) return false;
    if (pos_+len > n_) return false;
    s->assign(reinterpret_cast<const char*>(d_+pos_), static_cast<std::size_t>(len));
    pos_ += len; return true;
  }
  std::size_t pos() const { return pos_; }
  std::size_t size() const { return n_; }
  bool atEnd() const { return pos_ == n_; }
 private:
  static const std::byte* toByte(const std::byte* p) { return p; }
  static std::uint8_t toU8(const std::byte& b) { return static_cast<std::uint8_t>(b); }
  const std::byte* d_;
  std::size_t n_;
  std::size_t pos_{0};
};

static std::uint64_t fnv1a(const std::byte* d, std::size_t n) {
  std::uint64_t h = 1469598103934665603ull;
  for (std::size_t i=0;i<n;++i) { h ^= static_cast<std::uint64_t>(static_cast<std::uint8_t>(d[i])); h *= 1099511628211ull; }
  return h;
}

static void writeCost(Writer& w, const CostSummary& c) {
  w.u32(static_cast<std::uint32_t>(c.components.size()));
  for (const CostComponent& comp : c.components) {
    w.u8(static_cast<std::uint8_t>(comp.factor));
    w.dbl(comp.weight);
    w.dbl(comp.value);
    w.u8(static_cast<std::uint8_t>(comp.provenance));
  }
  w.dbl(c.total);
}
static bool readCost(Reader& r, CostSummary& c) {
  std::uint32_t count=0; if(!r.u32(&count)) return false;
  if (count > kMaxCount) return false;
  c.components.clear();
  for (std::uint32_t i=0;i<count;++i) {
    CostComponent cc;
    std::uint8_t f=0; if(!r.u8(&f)) return false;
    if (f > static_cast<std::uint8_t>(CostFactor::UNKNOWN)) return false;
    cc.factor = static_cast<CostFactor>(f);
    if(!r.dbl(&cc.weight)) return false;
    if(!r.dbl(&cc.value)) return false;
    std::uint8_t p=0; if(!r.u8(&p)) return false;
    if (p > static_cast<std::uint8_t>(Provenance::UNKNOWN)) return false;
    cc.provenance = static_cast<Provenance>(p);
    c.components.push_back(cc);
  }
  if(!r.dbl(&c.total)) return false;
  return true;
}

static void writeStage(Writer& w, const Stage& s) {
  w.u64(s.id.value()); w.u64(s.generation.value());
  w.u64(s.source.value()); w.u64(s.destination.value()); w.u64(s.link.value());
  w.u8(static_cast<std::uint8_t>(s.transport));
  w.u64(s.payload.count()); w.u64(s.expectedBandwidth.count()); w.u64(s.expectedLatency.count());
  w.u64(s.stagingResource.value());
  w.boo(s.requiresAuthority);
  w.u8(static_cast<std::uint8_t>(s.provenance));
  w.str(s.description);
}
static bool readStage(Reader& r, Stage& s) {
  std::uint64_t v;
  if(!r.u64(&v)) return false; s.id = StageId(v);
  if(!r.u64(&v)) return false; s.generation = StageGeneration(v);
  if(!r.u64(&v)) return false; s.source = EndpointId(v);
  if(!r.u64(&v)) return false; s.destination = EndpointId(v);
  if(!r.u64(&v)) return false; s.link = LinkId(v);
  std::uint8_t tr; if(!r.u8(&tr)) return false;
  if (tr > static_cast<std::uint8_t>(Transport::UNKNOWN)) return false;
  s.transport = static_cast<Transport>(tr);
  if(!r.u64(&v)) return false; s.payload = Bytes(v);
  if(!r.u64(&v)) return false; s.expectedBandwidth = BytesPerSecond(v);
  if(!r.u64(&v)) return false; s.expectedLatency = DurationNs(v);
  if(!r.u64(&v)) return false; s.stagingResource = ResourceId(v);
  if(!r.boo(&s.requiresAuthority)) return false;
  std::uint8_t p; if(!r.u8(&p)) return false;
  if (p > static_cast<std::uint8_t>(Provenance::UNKNOWN)) return false;
  s.provenance = static_cast<Provenance>(p);
  return r.str(&s.description);
}

static void writePath(Writer& w, const Path& p) {
  w.u64(p.id.value()); w.u64(p.generation.value());
  w.u64(p.source.value()); w.u64(p.destination.value());
  w.u64(p.totalPayload.count());
  w.u8(static_cast<std::uint8_t>(p.provenance));
  w.u32(static_cast<std::uint32_t>(p.stages.size()));
  for (const Stage& s : p.stages) writeStage(w, s);
  writeCost(w, p.cost);
  w.str(p.failureDomain);
  w.str(p.reason);
}
static bool readPath(Reader& r, Path& p) {
  std::uint64_t v;
  if(!r.u64(&v)) return false; p.id = PathId(v);
  if(!r.u64(&v)) return false; p.generation = PathGeneration(v);
  if(!r.u64(&v)) return false; p.source = EndpointId(v);
  if(!r.u64(&v)) return false; p.destination = EndpointId(v);
  if(!r.u64(&v)) return false; p.totalPayload = Bytes(v);
  std::uint8_t pr; if(!r.u8(&pr)) return false;
  if (pr > static_cast<std::uint8_t>(Provenance::UNKNOWN)) return false;
  p.provenance = static_cast<Provenance>(pr);
  std::uint32_t nStages=0; if(!r.u32(&nStages)) return false;
  if (nStages > kMaxCount) return false;
  p.stages.clear();
  for (std::uint32_t i=0;i<nStages;++i) { Stage s; if(!readStage(r,s)) return false; p.stages.push_back(std::move(s)); }
  if(!readCost(r,p.cost)) return false;
  if(!r.str(&p.failureDomain)) return false;
  return r.str(&p.reason);
}
}  // namespace

std::vector<std::byte> serializeStore(const StoreState& state) {
  Writer body;
  body.u64(state.epoch.value());
  body.u32(static_cast<std::uint32_t>(state.plans.size()));
  for (const StoreRecord& rec : state.plans) {
    body.u64(rec.plan.id.value());
    body.u64(rec.plan.generation.value());
    body.u64(rec.plan.requestId.value());
    body.u64(rec.plan.requestGeneration.value());
    body.u8(static_cast<std::uint8_t>(rec.plan.shape));
    body.u64(rec.plan.primaryPath.value());
    body.u8(static_cast<std::uint8_t>(rec.plan.feasibility));
    body.u8(static_cast<std::uint8_t>(rec.state));

    body.u32(static_cast<std::uint32_t>(rec.plan.candidatePaths.size()));
    for (const Path& p : rec.plan.candidatePaths) writePath(body, p);
    body.u32(static_cast<std::uint32_t>(rec.plan.fallbackPaths.size()));
    for (const PathId& id : rec.plan.fallbackPaths) body.u64(id.value());
    body.u32(static_cast<std::uint32_t>(rec.plan.orderedStages.size()));
    for (const Stage& s : rec.plan.orderedStages) writeStage(body, s);

    body.u64(rec.plan.topologyGeneration.value());
    body.u64(rec.plan.capacityGeneration.value());
    body.u64(rec.plan.reservationGeneration.value());
    body.u64(rec.plan.congestionGeneration.value());
    body.u64(rec.plan.capabilityGeneration.value());
    body.u64(rec.plan.healthGeneration.value());
    body.u64(rec.plan.placementGeneration.value());
    body.u64(rec.plan.authorityGeneration.value());

    body.u32(static_cast<std::uint32_t>(rec.plan.endpointGenerations.size()));
    for (const auto& pr : rec.plan.endpointGenerations) { body.u64(pr.first.value()); body.u64(pr.second.value()); }
    body.u32(static_cast<std::uint32_t>(rec.plan.linkGenerations.size()));
    for (const auto& pr : rec.plan.linkGenerations) { body.u64(pr.first.value()); body.u64(pr.second.value()); }
    body.u32(static_cast<std::uint32_t>(rec.plan.requiredStagingResources.size()));
    for (const ResourceId& id : rec.plan.requiredStagingResources) body.u64(id.value());
    body.u32(static_cast<std::uint32_t>(rec.plan.requiredResourceClaims.size()));
    for (const ResourceId& id : rec.plan.requiredResourceClaims) body.u64(id.value());

    writeCost(body, rec.plan.cost);
    body.u32(static_cast<std::uint32_t>(rec.plan.explanations.size()));
    for (const std::string& s : rec.plan.explanations) body.str(s);
    body.str(rec.plan.provenance);
    body.u64(rec.plan.revalidationGeneration.value());
    body.boo(rec.plan.revalidateBeforeExecute);
    body.str(rec.plan.handoffRequirement);
    body.u32(static_cast<std::uint32_t>(rec.plan.multicastDestinations.size()));
    for (const EndpointId& id : rec.plan.multicastDestinations) body.u64(id.value());
    body.u8(static_cast<std::uint8_t>(rec.plan.collectiveShape));
    body.u32(static_cast<std::uint32_t>(rec.plan.collectiveParticipants.size()));
    for (const EndpointId& id : rec.plan.collectiveParticipants) body.u64(id.value());
    body.str(rec.plan.description);
    body.str(rec.plan.createdProvenance);
  }

  body.u32(static_cast<std::uint32_t>(state.supersessions.size()));
  for (const SupersessionRecord& sr : state.supersessions) { body.u64(sr.superseded.value()); body.u64(sr.newer.value()); }

  body.u32(static_cast<std::uint32_t>(state.requests.size()));
  for (const CommunicationRequest& req : state.requests) {
    body.u64(req.id.value()); body.u64(req.generation.value());
    body.u8(static_cast<std::uint8_t>(req.shape));
    body.u64(req.source.value());
    body.u64(req.payloadSize.count());
    body.u8(static_cast<std::uint8_t>(req.direction));
    body.u8(static_cast<std::uint8_t>(req.collectiveShape));
    body.u64(req.collectiveCount);
    body.u32(static_cast<std::uint32_t>(req.destinations.size()));
    for (const EndpointId& id : req.destinations) body.u64(id.value());
  }

  const std::vector<std::byte>& b = body.data();
  std::uint64_t sum = fnv1a(b.data(), b.size());
  Writer frame;
  frame.u32(kMagic);
  frame.u32(kFormatVersion);
  frame.u64(sum);
  frame.u64(b.size());
  for (const std::byte x : b) frame.u8(static_cast<std::uint8_t>(x));
  return frame.data();
}

PersistError parseStore(const std::byte* data, std::size_t n, StoreState& out) {
  auto err = [](PersistErrorCode c, std::string d) { PersistError e; e.code=c; e.detail=std::move(d); return e; };
  Reader r(data, n);
  std::uint32_t magic=0, ver=0; std::uint64_t sum=0, len=0;
  if(!r.u32(&magic)) return err(PersistErrorCode::TRUNCATION, "missing magic");
  if (magic != kMagic) return err(PersistErrorCode::BAD_MAGIC, "bad magic");
  if(!r.u32(&ver)) return err(PersistErrorCode::TRUNCATION, "missing version");
  if (ver != kFormatVersion) return err(PersistErrorCode::UNSUPPORTED_VERSION, "unsupported version");
  if(!r.u64(&sum)) return err(PersistErrorCode::TRUNCATION, "missing checksum");
  if(!r.u64(&len)) return err(PersistErrorCode::TRUNCATION, "missing payload length");
  if (len > n - r.pos()) return err(PersistErrorCode::TRUNCATION, "payload length exceeds buffer");
  const std::byte* payload = data + r.pos();
  if (fnv1a(payload, static_cast<std::size_t>(len)) != sum)
    return err(PersistErrorCode::CHECKSUM_MISMATCH, "checksum mismatch");
  Reader pr(payload, static_cast<std::size_t>(len));

  std::uint64_t epoch; if(!pr.u64(&epoch)) return err(PersistErrorCode::TRUNCATION, "missing epoch");
  out.epoch = CoordinatorEpoch(epoch);

  std::uint32_t nPlans=0; if(!pr.u32(&nPlans)) return err(PersistErrorCode::TRUNCATION, "missing plans");
  if (nPlans > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized plan count");
  std::set<CommunicationPlanId> planIds;
  for (std::uint32_t i=0;i<nPlans;++i) {
    StoreRecord rec;
    std::uint64_t v;
    if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "plan id");
    rec.plan.id = CommunicationPlanId(v);
    if (v==0) return err(PersistErrorCode::CORRUPTION, "null plan id");
    if (!planIds.insert(rec.plan.id).second) return err(PersistErrorCode::DUPLICATE_ID, "duplicate plan id");
    if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "plan gen"); rec.plan.generation = CommunicationPlanGeneration(v);
    if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "request id"); rec.plan.requestId = CommunicationRequestId(v);
    if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "request gen"); rec.plan.requestGeneration = CommunicationRequestGeneration(v);
    std::uint8_t sh; if(!pr.u8(&sh)) return err(PersistErrorCode::TRUNCATION, "shape");
    if (sh > static_cast<std::uint8_t>(RequestShape::UNKNOWN)) return err(PersistErrorCode::INVALID_ENUM, "invalid shape");
    rec.plan.shape = static_cast<RequestShape>(sh);
    if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "primary"); rec.plan.primaryPath = PathId(v);
    std::uint8_t fb; if(!pr.u8(&fb)) return err(PersistErrorCode::TRUNCATION, "feasibility");
    if (fb > static_cast<std::uint8_t>(Feasibility::UNKNOWN)) return err(PersistErrorCode::INVALID_ENUM, "invalid feasibility");
    rec.plan.feasibility = static_cast<Feasibility>(fb);
    std::uint8_t st; if(!pr.u8(&st)) return err(PersistErrorCode::TRUNCATION, "state");
    if (st > static_cast<std::uint8_t>(PlanState::RETIRED)) return err(PersistErrorCode::INVALID_ENUM, "invalid state");
    rec.state = static_cast<PlanState>(st);

    std::uint32_t nPaths=0; if(!pr.u32(&nPaths)) return err(PersistErrorCode::TRUNCATION, "paths");
    if (nPaths > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized path count");
    for (std::uint32_t j=0;j<nPaths;++j) { Path p; if(!readPath(pr,p)) return err(PersistErrorCode::CORRUPTION, "path"); rec.plan.candidatePaths.push_back(std::move(p)); }
    std::uint32_t nFall=0; if(!pr.u32(&nFall)) return err(PersistErrorCode::TRUNCATION, "fallbacks");
    if (nFall > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized fallback count");
    for (std::uint32_t j=0;j<nFall;++j) { if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "fallback"); rec.plan.fallbackPaths.push_back(PathId(v)); }
    std::uint32_t nStg=0; if(!pr.u32(&nStg)) return err(PersistErrorCode::TRUNCATION, "stages");
    if (nStg > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized stage count");
    for (std::uint32_t j=0;j<nStg;++j) { Stage s; if(!readStage(pr,s)) return err(PersistErrorCode::CORRUPTION, "stage"); rec.plan.orderedStages.push_back(std::move(s)); }

    const auto rdGen = [&](std::uint64_t* outv)->bool { return pr.u64(outv); };
    if(!rdGen(&v)) return err(PersistErrorCode::TRUNCATION, "topology"); rec.plan.topologyGeneration = TopologyGeneration(v);
    if(!rdGen(&v)) return err(PersistErrorCode::TRUNCATION, "capacity"); rec.plan.capacityGeneration = CapacityGeneration(v);
    if(!rdGen(&v)) return err(PersistErrorCode::TRUNCATION, "reservation"); rec.plan.reservationGeneration = ReservationGeneration(v);
    if(!rdGen(&v)) return err(PersistErrorCode::TRUNCATION, "congestion"); rec.plan.congestionGeneration = CongestionGeneration(v);
    if(!rdGen(&v)) return err(PersistErrorCode::TRUNCATION, "capability"); rec.plan.capabilityGeneration = CapabilityGeneration(v);
    if(!rdGen(&v)) return err(PersistErrorCode::TRUNCATION, "health"); rec.plan.healthGeneration = HealthGeneration(v);
    if(!rdGen(&v)) return err(PersistErrorCode::TRUNCATION, "placement"); rec.plan.placementGeneration = PlacementGeneration(v);
    if(!rdGen(&v)) return err(PersistErrorCode::TRUNCATION, "authority"); rec.plan.authorityGeneration = AuthorityGeneration(v);

    std::uint32_t nEg=0; if(!pr.u32(&nEg)) return err(PersistErrorCode::TRUNCATION, "endpoint gens");
    if (nEg > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized endpoint gens");
    for (std::uint32_t j=0;j<nEg;++j) { EndpointId e; EndpointGeneration g; if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "eid"); e=EndpointId(v); if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "egen"); g=EndpointGeneration(v); rec.plan.endpointGenerations.push_back({e,g}); }
    std::uint32_t nLg=0; if(!pr.u32(&nLg)) return err(PersistErrorCode::TRUNCATION, "link gens");
    if (nLg > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized link gens");
    for (std::uint32_t j=0;j<nLg;++j) { LinkId l; LinkGeneration g; if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "lid"); l=LinkId(v); if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "lgen"); g=LinkGeneration(v); rec.plan.linkGenerations.push_back({l,g}); }
    std::uint32_t nR=0; if(!pr.u32(&nR)) return err(PersistErrorCode::TRUNCATION, "staging resources");
    if (nR > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized staging resources");
    for (std::uint32_t j=0;j<nR;++j) { if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "rid"); rec.plan.requiredStagingResources.push_back(ResourceId(v)); }
    std::uint32_t nC=0; if(!pr.u32(&nC)) return err(PersistErrorCode::TRUNCATION, "claims");
    if (nC > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized claims");
    for (std::uint32_t j=0;j<nC;++j) { if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "claim"); rec.plan.requiredResourceClaims.push_back(ResourceId(v)); }

    if(!readCost(pr, rec.plan.cost)) return err(PersistErrorCode::CORRUPTION, "cost");
    std::uint32_t nExp=0; if(!pr.u32(&nExp)) return err(PersistErrorCode::TRUNCATION, "explanations");
    if (nExp > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized explanations");
    for (std::uint32_t j=0;j<nExp;++j) { std::string s; if(!pr.str(&s)) return err(PersistErrorCode::CORRUPTION, "explanation"); rec.plan.explanations.push_back(std::move(s)); }
    if(!pr.str(&rec.plan.provenance)) return err(PersistErrorCode::CORRUPTION, "provenance");
    if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "reval gen"); rec.plan.revalidationGeneration = RevalidationGeneration(v);
    if(!pr.boo(&rec.plan.revalidateBeforeExecute)) return err(PersistErrorCode::TRUNCATION, "reval bool");
    if(!pr.str(&rec.plan.handoffRequirement)) return err(PersistErrorCode::CORRUPTION, "handoff");
    std::uint32_t nMd=0; if(!pr.u32(&nMd)) return err(PersistErrorCode::TRUNCATION, "multicast dests");
    if (nMd > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized multicast dests");
    for (std::uint32_t j=0;j<nMd;++j) { if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "mdest"); rec.plan.multicastDestinations.push_back(EndpointId(v)); }
    std::uint8_t cs; if(!pr.u8(&cs)) return err(PersistErrorCode::TRUNCATION, "collective shape");
    if (cs > static_cast<std::uint8_t>(CollectiveShape::UNKNOWN)) return err(PersistErrorCode::INVALID_ENUM, "invalid collective shape");
    rec.plan.collectiveShape = static_cast<CollectiveShape>(cs);
    std::uint32_t nCp=0; if(!pr.u32(&nCp)) return err(PersistErrorCode::TRUNCATION, "collective participants");
    if (nCp > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized participants");
    for (std::uint32_t j=0;j<nCp;++j) { if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "cp"); rec.plan.collectiveParticipants.push_back(EndpointId(v)); }
    if(!pr.str(&rec.plan.description)) return err(PersistErrorCode::CORRUPTION, "description");
    if(!pr.str(&rec.plan.createdProvenance)) return err(PersistErrorCode::CORRUPTION, "created provenance");

    out.plans.push_back(std::move(rec));
  }

  std::uint64_t v;
  std::uint32_t nSup=0; if(!pr.u32(&nSup)) return err(PersistErrorCode::TRUNCATION, "supersessions");
  if (nSup > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized supersessions");
  for (std::uint32_t i=0;i<nSup;++i) { SupersessionRecord sr; if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "sup old"); sr.superseded=CommunicationPlanId(v); if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "sup new"); sr.newer=CommunicationPlanId(v); out.supersessions.push_back(sr); }

  std::uint32_t nReq=0; if(!pr.u32(&nReq)) return err(PersistErrorCode::TRUNCATION, "requests");
  if (nReq > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized requests");
  for (std::uint32_t i=0;i<nReq;++i) {
    CommunicationRequest rq;
    if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "req id"); rq.id=CommunicationRequestId(v);
    if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "req gen"); rq.generation=CommunicationRequestGeneration(v);
    std::uint8_t s; if(!pr.u8(&s)) return err(PersistErrorCode::TRUNCATION, "req shape");
    if (s > static_cast<std::uint8_t>(RequestShape::UNKNOWN)) return err(PersistErrorCode::INVALID_ENUM, "invalid req shape");
    rq.shape = static_cast<RequestShape>(s);
    if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "req source"); rq.source=EndpointId(v);
    if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "req payload"); rq.payloadSize=Bytes(v);
    std::uint8_t d; if(!pr.u8(&d)) return err(PersistErrorCode::TRUNCATION, "req dir");
    if (d > 2) return err(PersistErrorCode::INVALID_ENUM, "invalid direction");
    rq.direction = static_cast<Direction>(d);
    std::uint8_t cshape; if(!pr.u8(&cshape)) return err(PersistErrorCode::TRUNCATION, "req cshape");
    if (cshape > static_cast<std::uint8_t>(CollectiveShape::UNKNOWN)) return err(PersistErrorCode::INVALID_ENUM, "invalid collective shape");
    rq.collectiveShape = static_cast<CollectiveShape>(cshape);
    if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "req count"); if (v > 4294967295ull) return err(PersistErrorCode::CP_OVERFLOW, "collective count overflow"); rq.collectiveCount=(unsigned)v;
    std::uint32_t nD=0; if(!pr.u32(&nD)) return err(PersistErrorCode::TRUNCATION, "req dests");
    if (nD > kMaxCount) return err(PersistErrorCode::OVERSIZED_COUNT, "oversized req dests");
    for (std::uint32_t j=0;j<nD;++j) { if(!pr.u64(&v)) return err(PersistErrorCode::TRUNCATION, "req dest"); rq.destinations.push_back(EndpointId(v)); }
    out.requests.push_back(std::move(rq));
  }

  if (!pr.atEnd()) return err(PersistErrorCode::TRAILING_GARBAGE, "trailing garbage in payload");
  // No trailing bytes allowed after payload length.
  if (r.pos() + static_cast<std::size_t>(len) != n) return err(PersistErrorCode::TRAILING_GARBAGE, "trailing bytes after frame");

  return validateStore(out);
}

PersistError validateStore(const StoreState& state) {
  auto err = [](PersistErrorCode c, std::string d) { PersistError e; e.code=c; e.detail=std::move(d); return e; };
  std::set<CommunicationPlanId> planIds;
  for (const StoreRecord& rec : state.plans) {
    if (!planIds.insert(rec.plan.id).second) return err(PersistErrorCode::DUPLICATE_ID, "duplicate plan id");
    const CommunicationPlan& p = rec.plan;
    // cost finite
    if (!p.cost.totalValid()) return err(PersistErrorCode::NAN_INF_COST, "cost total NaN/Inf");
    for (const CostComponent& cc : p.cost.components) {
      if (!cc.isValid()) return err(PersistErrorCode::NAN_INF_COST, "cost component NaN/Inf");
    }
    // selected plan must be in candidate set
    bool foundPrimary = false;
    std::set<PathId> pathIds;
    for (const Path& path : p.candidatePaths) {
      if (path.id == p.primaryPath) foundPrimary = true;
      if (!pathIds.insert(path.id).second) return err(PersistErrorCode::DUPLICATE_ID, "duplicate path id");
      // payload consistency
      for (const Stage& s : path.stages) {
        if (s.payload.count() != path.totalPayload.count())
          return err(PersistErrorCode::PAYLOAD_MISMATCH, "stage payload mismatch");
      }
      // stage ordering contiguous & no cycles
      EndpointId prev = path.source;
      std::set<EndpointId> seen;
      bool first = true;
      for (const Stage& s : path.stages) {
        if (s.source != prev) return err(PersistErrorCode::INVALID_STAGE_ORDER, "stage order broken");
        if (s.source == s.destination) return err(PersistErrorCode::CYCLE_FORBIDDEN, "self stage cycle");
        if (!first) { /* interior */ }
        if (!seen.insert(s.destination).second) return err(PersistErrorCode::CYCLE_FORBIDDEN, "endpoint revisit cycle");
        prev = s.destination;
        first = false;
      }
      if (!path.stages.empty() && prev != path.destination)
        return err(PersistErrorCode::PATH_MISSING_ENDPOINT, "path chain does not reach destination");
      // endpoint references covered by bound generations
      std::set<EndpointId> bound;
      for (const auto& pr : p.endpointGenerations) bound.insert(pr.first);
      for (const Stage& s : path.stages) {
        if (bound.count(s.source) == 0 || bound.count(s.destination) == 0)
          return err(PersistErrorCode::PATH_MISSING_ENDPOINT, "stage endpoint not bound");
      }
    }
    if (!foundPrimary && !p.primaryPath.isNull())
      return err(PersistErrorCode::SELECTED_PLAN_NOT_IN_CANDIDATE_SET, "primary path not in candidate path set");
    // committed evidence
    if (rec.state == PlanState::COMMITTED || rec.state == PlanState::AWAITING_EXECUTION ||
        rec.state == PlanState::ACTIVE) {
      if (p.requiredResourceClaims.empty())
        return err(PersistErrorCode::COMMITTED_PLAN_NO_EVIDENCE, "committed plan lacks resource commitment evidence");
    }
  }
  std::set<CommunicationRequestId> reqIds;
  for (const CommunicationRequest& rq : state.requests) {
    if (!reqIds.insert(rq.id).second) return err(PersistErrorCode::DUPLICATE_ID, "duplicate request id");
  }
  return PersistError{};
}

void conservativeRecovery(StoreState& state) {
  for (StoreRecord& rec : state.plans) {
    const PlanState s = rec.state;
    if (s == PlanState::PLAN_READY || s == PlanState::AWAITING_RESOURCE_COMMIT ||
        s == PlanState::COMMITTED || s == PlanState::AWAITING_EXECUTION ||
        s == PlanState::ACTIVE) {
      rec.state = PlanState::REVALIDATION_REQUIRED;
    }
  }
}

}  // namespace communication_planner
