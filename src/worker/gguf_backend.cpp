#include "src/worker/gguf_backend.hpp"

#include <iostream>
#include <thread>
#include <chrono>

namespace infsched {

GgufBackend::GgufBackend(std::string name)
    : name_(std::move(name)) {
  outputTokens_.resize(128, 42);
}

GgufBackend::~GgufBackend() {
  unload();
}

StatusCode GgufBackend::load(const ModelConfig& config) {
  name_ = config.name;
  modelPath_ = config.modelPath;
  // Account KV cache allocation against memory limit
  kvCacheUsageBytes_ = config.workingSetBytesPerSample * config.maxBatchSize;
  healthy_ = true;
  return StatusCode::Ok;
}

StatusCode GgufBackend::run(const Batch& batch, std::vector<TensorView>& outputs) {
  if (!healthy_.load()) return StatusCode::InternalError;

  size_t n = batch.batchSize();
  if (n == 0) return StatusCode::Ok;

  // Token-level step scheduling: step duration per decode step
  Duration stepTime = baseStepLatency_ + std::chrono::microseconds(100 * n);
  std::this_thread::sleep_for(stepTime);

  // Return next generated token ids
  TensorView out;
  out.data = outputTokens_.data();
  out.dtype = DataType::Int32;
  out.shape = {static_cast<int64_t>(n), 1};
  outputs.push_back(out);

  return StatusCode::Ok;
}

Duration GgufBackend::estimateWarmup(size_t batchSize) {
  return baseStepLatency_ + std::chrono::microseconds(100 * batchSize);
}

void GgufBackend::unload() {
  healthy_ = false;
  kvCacheUsageBytes_ = 0;
}

} // namespace infsched
