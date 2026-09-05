#pragma once

#include <cstdint>
#include <vector>
#include <cmath>
#include <string>
#include <limits>
#include "communication_planner/core/enums.hpp"
#include "communication_planner/core/provenance.hpp"

namespace communication_planner {

// One named, explicit cost component with weight + provenance. Policy is never
// hidden in a single opaque score; each named factor is inspectable.
struct CostComponent {
  CostFactor factor{CostFactor::UNKNOWN};
  double weight{0.0};
  double value{0.0};  // nanos-equivalent cost contribution
  Provenance provenance{Provenance::UNKNOWN};

  bool isValid() const noexcept {
    return weight >= 0.0 && !std::isnan(weight) && !std::isinf(weight) &&
           value >= 0.0 && !std::isnan(value) && !std::isinf(value);
  }
};

// A cost summary is the tuple of named components plus a checked total.
// The total must never overflow / become NaN/Inf.
struct CostSummary {
  std::vector<CostComponent> components;
  double total{0.0};

  bool totalValid() const noexcept {
    return !std::isnan(total) && !std::isinf(total);
  }

  static CostSummary of(const std::vector<CostComponent>& comps) {
    CostSummary s;
    s.components = comps;
    double t = 0.0;
    for (const auto& c : comps) {
      if (!c.isValid()) { s.total = toPosInf(); return s; }
      t += c.weight * c.value;
      if (std::isinf(t) || std::isnan(t)) { s.total = toPosInf(); return s; }
    }
    s.total = t;
    return s;
  }

  double uncertaintyCount() const noexcept {
    double u = 0.0;
    for (const auto& c : components) if (c.provenance == Provenance::UNKNOWN) u += 1.0;
    return u;
  }

  static double toPosInf() noexcept { return std::numeric_limits<double>::infinity(); }
};

inline bool operator==(const CostComponent& a, const CostComponent& b) {
  return a.factor == b.factor && a.weight == b.weight && a.value == b.value &&
         a.provenance == b.provenance;
}

}  // namespace communication_planner
