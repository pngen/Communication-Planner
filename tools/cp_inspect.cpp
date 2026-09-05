#include "communication_planner/persistence/persistence.hpp"
#include "communication_planner/model/plan.hpp"
#include <cstdio>
#include <fstream>
#include <vector>
#include <string>

using namespace communication_planner;

// cp_inspect <store-file> : inspect a persisted CommunicationPlanner store.
int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: cp_inspect <store-file>\n"); return 2; }
  std::ifstream ifs(argv[1], std::ios::binary | std::ios::ate);
  if (!ifs) { std::printf("cannot open store: %s\n", argv[1]); return 1; }
  std::streamsize sz = ifs.tellg(); ifs.seekg(0, std::ios::beg);
  std::vector<char> buf((std::size_t)sz);
  ifs.read(buf.data(), sz);
  StoreState st;
  PersistError pe = parseStore(reinterpret_cast<const std::byte*>(buf.data()), (std::size_t)sz, st);
  if (!pe.ok()) { std::printf("store parse error: %s (%s)\n", std::string(toString(pe.code)).c_str(), pe.detail.c_str()); return 1; }
  std::printf("CommunicationPlanner store: epoch=%llu plans=%zu requests=%zu supersessions=%zu\n",
    (unsigned long long)st.epoch.value(), st.plans.size(), st.requests.size(), st.supersessions.size());
  for (const StoreRecord& rec : st.plans) {
    const CommunicationPlan& p = rec.plan;
    std::printf("  plan %llu/%llu req=%llu state=%s shape=%s stages=%zu primary=%llu cost=%.3f\n",
      (unsigned long long)p.id.value(), (unsigned long long)p.generation.value(),
      (unsigned long long)p.requestId.value(), std::string(toString(rec.state)).c_str(),
      std::string(toString(p.shape)).c_str(), p.orderedStages.size(),
      (unsigned long long)p.primaryPath.value(), p.cost.total);
    for (const Stage& stg : p.orderedStages)
      std::printf("    stage tr=%s %s->%s payload=%llu\n", std::string(toString(stg.transport)).c_str(),
        stg.source.str().c_str(), stg.destination.str().c_str(), (unsigned long long)stg.payload.count());
  }
  return 0;
}
