#include "src/worker/mock_backend.hpp"

#include <thread>
#include <chrono>
#include <cmath>

namespace infsched {

MockBackend::MockBackend(std::string name, Duration baseLatency, Duration perSampleLatency)
    : name_(std::move(name)), baseLatency_(baseLatency), perSampleLatency_(perSampleLatency) {
  sampleOutputBuffer_.resize(1024, 1.0f);
}

StatusCode MockBackend::load(const ModelConfig& config) {
  name_ = config.name;
  healthy_ = true;
  return StatusCode::Ok;
}

StatusCode MockBackend::run(const Batch& batch, std::vector<TensorView>& outputs) {
  if (!healthy_.load()) {
    return StatusCode::InternalError;
  }
  if (failNext_.exchange(false)) {
    return StatusCode::InternalError;
  }

  size_t n = batch.batchSize();
  if (n == 0) return StatusCode::Ok;

  // Compute duration
  Duration totalLatency = baseLatency_ + perSampleLatency_ * n + artificialDelay_;

  // Cache budget effect: if working set exceeds cache budget, simulate cache thrashing latency penalty
  size_t estimatedWorkingSet = n * 512 * 1024; // 512 KB per sample
  if (estimatedWorkingSet > cacheBudgetLimit_) {
    double ratio = static_cast<double>(estimatedWorkingSet) / static_cast<double>(cacheBudgetLimit_);
    totalLatency += std::chrono::duration_cast<Duration>(perSampleLatency_ * (n * (ratio - 1.0) * 1.5));
  }

  // Perform actual vector math
  for (size_t i = 0; i < sampleOutputBuffer_.size(); ++i) {
    sampleOutputBuffer_[i] = std::sin(static_cast<float>(i) * 0.01f) * static_cast<float>(n);
  }

  // Sleep to simulate computation time
  std::this_thread::sleep_for(totalLatency);

  totalBatches_++;
  totalSamples_ += n;

  TensorView outTv;
  outTv.data = sampleOutputBuffer_.data();
  outTv.dtype = DataType::Float32;
  outTv.shape = {static_cast<int64_t>(n), static_cast<int64_t>(sampleOutputBuffer_.size() / std::max<size_t>(1, n))};
  outputs.push_back(outTv);

  return StatusCode::Ok;
}

Duration MockBackend::estimateWarmup(size_t batchSize) {
  return baseLatency_ + perSampleLatency_ * batchSize;
}

void MockBackend::unload() {
  healthy_ = false;
}

} // namespace infsched
