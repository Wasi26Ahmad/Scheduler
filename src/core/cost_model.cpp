#include "infsched/cost_model.hpp"

namespace infsched {

CostModel::CostModel(double alpha) : alpha_(alpha) {}

void CostModel::recordWarmup(const std::string& model, size_t batchSize, Duration measuredLatency) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& stats = models_[model];
  auto& bc = stats.perBatchCost[batchSize];
  bc.emaLatency = measuredLatency;
  bc.sampleCount = 1;

  if (batchSize == 1) {
    stats.baseLatency = measuredLatency;
  } else if (batchSize > 1) {
    auto b1 = stats.perBatchCost.find(1);
    if (b1 != stats.perBatchCost.end()) {
      auto diff = measuredLatency > b1->second.emaLatency ? (measuredLatency - b1->second.emaLatency) : Duration(0);
      stats.perSampleLatency = diff / (batchSize - 1);
    }
  }
}

void CostModel::update(const std::string& model, size_t batchSize, Duration measuredLatency) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& stats = models_[model];
  auto& bc = stats.perBatchCost[batchSize];

  if (bc.sampleCount == 0) {
    bc.emaLatency = measuredLatency;
    bc.sampleCount = 1;
  } else {
    // Exponential Moving Average
    double oldUs = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(bc.emaLatency).count());
    double newUs = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(measuredLatency).count());
    double updatedUs = (1.0 - alpha_) * oldUs + alpha_ * newUs;
    bc.emaLatency = std::chrono::microseconds(static_cast<int64_t>(updatedUs));
    bc.sampleCount++;
  }

  // Refine linear interpolation slope
  if (batchSize == 1) {
    stats.baseLatency = bc.emaLatency;
  }
}

Duration CostModel::estimate(const std::string& model, size_t batchSize) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = models_.find(model);
  if (it == models_.end()) {
    // Default fallback: 5ms base + 1ms per sample
    return std::chrono::milliseconds(5 + (batchSize > 0 ? (batchSize - 1) : 0));
  }

  const auto& stats = it->second;
  auto bIt = stats.perBatchCost.find(batchSize);
  if (bIt != stats.perBatchCost.end() && bIt->second.sampleCount > 0) {
    return bIt->second.emaLatency;
  }

  // Interpolate or extrapolate using baseLatency + (batchSize - 1) * perSampleLatency
  if (batchSize <= 1) {
    return stats.baseLatency;
  }
  return stats.baseLatency + stats.perSampleLatency * (batchSize - 1);
}

} // namespace infsched
