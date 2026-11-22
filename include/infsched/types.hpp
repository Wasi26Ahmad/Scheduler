#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace infsched {

using SystemClock = std::chrono::steady_clock;
using TimePoint = SystemClock::time_point;
using Duration = SystemClock::duration;

enum class DataType {
  Float32,
  Int8,
  Int32,
  Int64,
  Float16
};

inline size_t getDataTypeSize(DataType dt) {
  switch (dt) {
    case DataType::Int8: return 1;
    case DataType::Float16: return 2;
    case DataType::Float32: return 4;
    case DataType::Int32: return 4;
    case DataType::Int64: return 8;
    default: return 4;
  }
}

struct TensorView {
  void* data = nullptr;
  DataType dtype = DataType::Float32;
  std::vector<int64_t> shape;

  size_t totalElements() const {
    if (shape.empty()) return 0;
    size_t count = 1;
    for (int64_t dim : shape) {
      if (dim > 0) count *= static_cast<size_t>(dim);
    }
    return count;
  }

  size_t totalBytes() const {
    return totalElements() * getDataTypeSize(dtype);
  }
};

struct Request {
  uint64_t id = 0;
  std::string model;
  std::string tenant = "default";
  std::vector<TensorView> inputs;
  TimePoint arrivalTime = SystemClock::now();
  TimePoint deadline = SystemClock::now() + std::chrono::milliseconds(100);
  int priority = 0;
};

struct Batch {
  std::string model;
  std::vector<Request> requests;
  size_t batchSize() const { return requests.size(); }
};

enum class StatusCode {
  Ok,
  QueueFull,
  OverQuota,
  DeadlineExceeded,
  ModelNotFound,
  WorkerUnavailable,
  InternalError,
  Draining
};

enum class AdmissionResult {
  Accepted,
  QueueFull,
  OverQuota,
  ModelNotFound,
  Draining,
  DeadlineAlreadyMissed
};

struct ModelConfig {
  std::string name;
  std::string modelPath;
  std::string backendType = "mock"; // mock, onnx, gguf
  size_t maxBatchSize = 16;
  size_t memoryLimitBytes = 1024ULL * 1024ULL * 1024ULL; // 1 GB default
  size_t workingSetBytesPerSample = 512 * 1024; // 512 KB per batch item
  int minPhysicalCores = 1;
  int maxPhysicalCores = 2;
  int defaultPriority = 0;
};

struct QuotaConfig {
  double tokensPerSecond = 100.0;
  double burstCapacity = 200.0;
};

struct CostEstimate {
  Duration estimatedLatency;
  double confidence = 1.0;
};

} // namespace infsched
