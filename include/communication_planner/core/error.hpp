#pragma once

#include <stdexcept>
#include <string>

namespace communication_planner {

class ComError : public std::runtime_error {
 public:
  explicit ComError(const std::string& msg) : std::runtime_error(msg) {}
};

}  // namespace communication_planner
