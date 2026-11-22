#include "src/worker/onnx_backend.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>

namespace infsched {

OnnxBackend::OnnxBackend(std::string name)
    : name_(std::move(name)) {
  outputBuffer_.resize(2048, 0.5f);
}

OnnxBackend::~OnnxBackend() {
  unload();
}

StatusCode OnnxBackend::load(const ModelConfig& config) {
  name_ = config.name;
  modelPath_ = config.modelPath;
  intraOpThreads_ = std::max(1, config.minPhysicalCores);

  // In production, initialize Ort::Env and Ort::Session with intra_op_num_threads pinned to lease
  // and spinning disabled (DisableSpinning threads)
  healthy_ = true;
  return StatusCode::Ok;
}

StatusCode OnnxBackend::run(const Batch& batch, std::vector<TensorView>& outputs) {
  if (!healthy_.load()) return StatusCode::InternalError;

  size_t n = batch.batchSize();
  if (n == 0) return StatusCode::Ok;

  // Compute execution duration based on batch size and intraOpThreads
  Duration duration = baseLatency_ + (perSampleLatency_ * n) / std::max<size_t>(1, intraOpThreads_ / 2 + 1);
  std::this_thread::sleep_for(duration);

  // Compute reference tensor output
  for (size_t i = 0; i < outputBuffer_.size(); ++i) {
    outputBuffer_[i] = std::cos(static_cast<float>(i + n) * 0.05f);
  }

  TensorView out;
  out.data = outputBuffer_.data();
  out.dtype = DataType::Float32;
  out.shape = {static_cast<int64_t>(n), static_cast<int64_t>(outputBuffer_.size() / std::max<size_t>(1, n))};
  outputs.push_back(out);

  return StatusCode::Ok;
}

Duration OnnxBackend::estimateWarmup(size_t batchSize) {
  return baseLatency_ + perSampleLatency_ * batchSize;
}

void OnnxBackend::unload() {
  healthy_ = false;
}

} // namespace infsched
