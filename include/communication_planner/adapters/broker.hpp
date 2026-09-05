#pragma once
#include <vector>
#include <string>
#include <map>
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/units.hpp"

namespace communication_planner {

class ResourceBroker {
 public:
  virtual ~ResourceBroker() = default;
  virtual bool acquire(const std::vector<ResourceId>& resources,
                       const std::vector<Bytes>& sizes, std::string& err) = 0;
  virtual void release(const std::vector<ResourceId>& resources) = 0;
  virtual bool capacityAvailable(ResourceId id) const = 0;
  virtual std::uint64_t capacity(ResourceId id) const { (void)id; return 0; }
};

class MemoryResourceBroker : public ResourceBroker {
 public:
  explicit MemoryResourceBroker(std::uint64_t defaultCap = (1ull << 30)) : defaultCap_(defaultCap) {}
  void setCapacity(ResourceId id, std::uint64_t bytes) { caps_[id] = bytes; }
  void setFailNext(int n) { failAfter_ = n; }
  int  failCountdown() const { return failAfter_; }
  bool acquire(const std::vector<ResourceId>& resources,
               const std::vector<Bytes>& sizes, std::string& err) override {
    if (failAfter_ == 0) { err = "broker scripted acquire failure"; return false; }
    if (failAfter_ > 0) --failAfter_;
    for (std::size_t i = 0; i < resources.size(); ++i) {
      std::uint64_t cap = (caps_.count(resources[i]) ? caps_[resources[i]] : defaultCap_);
      if (held_.count(resources[i]) && held_[resources[i]] + sizes[i].count() > cap) { err = "insufficient capacity"; return false; }
      if (!held_.count(resources[i]) && sizes[i].count() > cap) { err = "insufficient capacity"; return false; }
    }
    for (std::size_t i = 0; i < resources.size(); ++i) held_[resources[i]] += sizes[i].count();
    return true;
  }
  void release(const std::vector<ResourceId>& resources) override {
    for (ResourceId id : resources) { held_.erase(id); }
  }
  bool capacityAvailable(ResourceId id) const override {
    std::uint64_t cap = (caps_.count(id) ? caps_.at(id) : defaultCap_);
    auto it = held_.find(id);
    std::uint64_t h = (it != held_.end()) ? it->second : 0;
    return h < cap;
  }
  std::uint64_t capacity(ResourceId id) const override {
    return caps_.count(id) ? caps_.at(id) : defaultCap_;
  }
  std::map<ResourceId, std::uint64_t> snapshotHeld() const { return held_; }
  std::uint64_t held(ResourceId id) const { auto it = held_.find(id); return it != held_.end() ? it->second : 0; }
 private:
  std::uint64_t defaultCap_;
  std::map<ResourceId, std::uint64_t> caps_;
  std::map<ResourceId, std::uint64_t> held_;
  int failAfter_{-1};
};

}  // namespace communication_planner
