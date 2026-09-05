#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>

namespace communication_planner {

template <typename Tag>
class Generation {
 public:
  using value_type = std::uint64_t;

  constexpr Generation() noexcept = default;
  explicit constexpr Generation(value_type v) noexcept : value_(v) {}

  constexpr value_type value() const noexcept { return value_; }
  constexpr bool isNull() const noexcept { return value_ == 0; }
  explicit constexpr operator bool() const noexcept { return value_ != 0; }

  Generation next() const noexcept { return Generation(value_ + 1); }
  Generation& operator++() noexcept { ++value_; return *this; }
  Generation operator++(int) noexcept { Generation t = *this; ++value_; return t; }

  constexpr bool operator==(const Generation& o) const noexcept { return value_ == o.value_; }
  constexpr bool operator!=(const Generation& o) const noexcept { return value_ != o.value_; }
  constexpr bool operator<(const Generation& o) const noexcept { return value_ < o.value_; }
  constexpr bool operator<=(const Generation& o) const noexcept { return value_ <= o.value_; }
  constexpr bool operator>(const Generation& o) const noexcept { return value_ > o.value_; }
  constexpr bool operator>=(const Generation& o) const noexcept { return value_ >= o.value_; }

  std::string str() const { return std::to_string(value_); }

  static std::optional<Generation> fromString(std::string_view s) {
    if (s.empty()) return std::nullopt;
    value_type v = 0;
    for (char c : s) {
      if (c < '0' || c > '9') return std::nullopt;
      value_type d = static_cast<value_type>(c - '0');
      if (v > (UINT64_MAX - d) / 10) return std::nullopt;
      v = v * 10 + d;
    }
    return Generation(v);
  }

 private:
  value_type value_{0};
};

}  // namespace communication_planner
