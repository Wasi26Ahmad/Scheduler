#pragma once

#include "infsched/backend.hpp"
#include <atomic>
#include <vector>

namespace infsched {

class MockBackend : public Backend {
public:
  MockBackend(std::string name = "mock_model",
              Duration baseLatency = std::chrono::milliseconds(4),
              Duration perSampleLatency = std::chrono::microseconds(500));

  StatusCode load(const ModelConfig& config) override;
  StatusCode run(const Batch& batch, std::vector<TensorView>& outputs) override;
  Duration estimateWarmup(size_t batchSize) override;
  void unload() override;
  bool isHealthy() const override { return healthy_.load(); }
  const std::string& name() const override { return name_; }

  // Fault injection controls
  void setFailNext(bool fail) { failNext_ = fail; }
  void setHealthy(bool healthy) { healthy_ = healthy; }
  void setArtificialDelay(Duration delay) { artificialDelay_ = delay; }
  void setCacheBudgetLimit(size_t limitBytes) { cacheBudgetLimit_ = limitBytes; }

  uint64_t totalBatchesProcessed() const { return totalBatches_.load(); }
  uint64_t totalSamplesProcessed() const { return totalSamples_.load(); }

private:
  std::string name_;
  Duration baseLatency_;
  Duration perSampleLatency_;
  Duration artificialDelay_{0};
  size_t cacheBudgetLimit_ = 4 * 1024 * 1024;
  std::atomic<bool> healthy_{true};
  std::atomic<bool> failNext_{false};
  std::atomic<uint64_t> totalBatches_{0};
  std::atomic<uint64_t> totalSamples_{0};
  std::vector<float> sampleOutputBuffer_;
};

} // namespace infsched
