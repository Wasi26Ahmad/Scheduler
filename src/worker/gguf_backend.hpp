#pragma once

#include "infsched/backend.hpp"
#include <atomic>
#include <vector>
#include <string>

namespace infsched {

class GgufBackend : public Backend {
public:
  explicit GgufBackend(std::string name = "gguf_model");
  ~GgufBackend() override;

  StatusCode load(const ModelConfig& config) override;
  StatusCode run(const Batch& batch, std::vector<TensorView>& outputs) override;
  Duration estimateWarmup(size_t batchSize) override;
  void unload() override;
  bool isHealthy() const override { return healthy_.load(); }
  const std::string& name() const override { return name_; }

  size_t getKvCacheUsageBytes() const { return kvCacheUsageBytes_.load(); }

private:
  std::string name_;
  std::string modelPath_;
  size_t maxTokensPerBatch_ = 256;
  std::atomic<bool> healthy_{false};
  std::atomic<size_t> kvCacheUsageBytes_{0};
  Duration baseStepLatency_{std::chrono::milliseconds(8)};
  std::vector<int32_t> outputTokens_;
};

} // namespace infsched
