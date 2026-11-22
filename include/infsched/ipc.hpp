#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace infsched {

enum class MessageType : uint32_t {
  Heartbeat = 1,
  LoadModel = 2,
  LoadModelAck = 3,
  RunBatch = 4,
  RunBatchAck = 5,
  Unload = 6,
  Shutdown = 7
};

struct IpcHeader {
  MessageType type;
  uint32_t payloadSize = 0;
};

struct HeartbeatMessage {
  uint64_t timestamp = 0;
  bool isHealthy = true;
};

struct RunBatchHeader {
  uint64_t batchId = 0;
  uint32_t batchSize = 0;
  uint32_t tensorBytes = 0;
};

struct RunBatchResult {
  uint64_t batchId = 0;
  uint32_t statusCode = 0;
  uint64_t executionMicros = 0;
};

} // namespace infsched
