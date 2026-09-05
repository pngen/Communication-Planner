#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>

namespace communication_planner {

enum class Provenance : std::uint8_t {
  MEASURED,
  REPORTED,
  DERIVED,
  ESTIMATED,
  FORECAST,
  SYNTHETIC,
  UNKNOWN,
};

inline std::string_view toString(Provenance p) {
  switch (p) {
    case Provenance::MEASURED: return "MEASURED";
    case Provenance::REPORTED: return "REPORTED";
    case Provenance::DERIVED: return "DERIVED";
    case Provenance::ESTIMATED: return "ESTIMATED";
    case Provenance::FORECAST: return "FORECAST";
    case Provenance::SYNTHETIC: return "SYNTHETIC";
    case Provenance::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

inline std::optional<Provenance> provenanceFromString(std::string_view s) {
  if (s == "MEASURED") return Provenance::MEASURED;
  if (s == "REPORTED") return Provenance::REPORTED;
  if (s == "DERIVED") return Provenance::DERIVED;
  if (s == "ESTIMATED") return Provenance::ESTIMATED;
  if (s == "FORECAST") return Provenance::FORECAST;
  if (s == "SYNTHETIC") return Provenance::SYNTHETIC;
  if (s == "UNKNOWN") return Provenance::UNKNOWN;
  return std::nullopt;
}

}  // namespace communication_planner
