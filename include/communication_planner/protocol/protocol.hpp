#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include <optional>
#include "communication_planner/core/identifiers.hpp"
#include "communication_planner/core/enums.hpp"
#include "communication_planner/model/endpoint.hpp"
#include "communication_planner/model/link.hpp"
#include "communication_planner/model/request.hpp"
#include "communication_planner/model/plan.hpp"
#include "communication_planner/planner/snapshot.hpp"
#include "communication_planner/planner/planner.hpp"

namespace communication_planner {

enum class MessageType : std::uint8_t {
  HELLO, REGISTER, PUBLISH_ENDPOINT, PUBLISH_LINK, PUBLISH_TOPOLOGY,
  PUBLISH_CAPACITY, PUBLISH_CONGESTION, SUBMIT_REQUEST, QUERY_PLAN, PLAN_RESULT,
  COMMIT_REQUEST, COMMIT_RESULT, REVALIDATE, SUPERSEDE, CANCEL, EXECUTION_HANDOFF,
  EXECUTION_RESULT, SAVE, SHUTDOWN, ERROR
};
std::string toString(MessageType t);

struct Frame {
  MessageType type{MessageType::HELLO};
  std::vector<std::byte> payload;
};

struct ProtocolError {
  std::string detail;
  bool ok() const { return detail.empty(); }
};

// A structured message carrying the fields for each type.
struct Message {
  MessageType type{MessageType::HELLO};

  CoordinatorEpoch epoch;
  WorkerId worker;
  WorkerBootId boot;
  SourceId source;
  SourceBootId sourceBoot;
  std::string name;

  Endpoint endpoint;
  Link link;
  CommunicationRequest request;
  CommunicationPlan plan;

  LinkId linkId;
  Capacity capacity;
  Congestion congestion;

  CommunicationPlanId planId;
  CommunicationRequestId requestId;
  bool ok{false};
  std::string error;
};

static constexpr std::uint32_t kMaxFramePayload = 64u * 1024u * 1024u;

// Versioned, checksummed framed codec. Bounded payload, invalid-enum rejection.
ProtocolError encodeFrame(const Frame& f, std::vector<std::byte>& out);
ProtocolError decodeFrame(const std::byte* data, std::size_t n, Frame& out);

ProtocolError encodeMessage(const Message& m, std::vector<std::byte>& payload);
ProtocolError decodeMessage(MessageType type, const std::byte* payload, std::size_t n, Message& out);

}  // namespace communication_planner
