#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <limits>

namespace communication_planner {

struct Bytes {
  std::uint64_t v{0};
  constexpr Bytes() = default;
  explicit constexpr Bytes(std::uint64_t b) : v(b) {}
  constexpr std::uint64_t count() const noexcept { return v; }
};

struct BytesPerSecond {
  std::uint64_t v{0};
  constexpr BytesPerSecond() = default;
  explicit constexpr BytesPerSecond(std::uint64_t b) : v(b) {}
  constexpr std::uint64_t count() const noexcept { return v; }
};

struct DurationNs {
  std::uint64_t v{0};
  constexpr DurationNs() = default;
  explicit constexpr DurationNs(std::uint64_t d) : v(d) {}
  constexpr std::uint64_t count() const noexcept { return v; }
};

struct Count {
  std::uint64_t v{0};
  constexpr Count() = default;
  explicit constexpr Count(std::uint64_t c) : v(c) {}
  constexpr std::uint64_t count() const noexcept { return v; }
};

inline std::optional<std::uint64_t> checkedAdd(std::uint64_t a, std::uint64_t b) {
  if (a > std::numeric_limits<std::uint64_t>::max() - b) return std::nullopt;
  return a + b;
}
inline std::optional<std::uint64_t> checkedMul(std::uint64_t a, std::uint64_t b) {
  if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) return std::nullopt;
  return a * b;
}
inline std::optional<std::uint64_t> checkedDivCeil(std::uint64_t a, std::uint64_t b) {
  if (b == 0) return std::nullopt;
  std::uint64_t q = a / b;
  if (a % b != 0) {
    if (q == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    ++q;
  }
  return q;
}

}  // namespace communication_planner
