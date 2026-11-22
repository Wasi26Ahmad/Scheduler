#pragma once

#include "infsched/backend.hpp"
#include <atomic>
#include <vector>
#include <string>

namespace infsched {

class OnnxBackend : public Backend {
public:
  explicit OnnxBackend(std::string name = "onnx_model");
  ~OnnxBackend() override;

  StatusCode load(const ModelConfig& config) override;
  StatusCode run(const Batch& batch, std::vector<TensorView>& outputs) override;
  Duration estimateWarmup(size_t batchSize) override;
  void unload() override;
  bool isHealthy() const override { return healthy_.load(); }
  const std::string& name() const override { return name_; }

private:
  std::string name_;
  std::string modelPath_;
  size_t intraOpThreads_ = 2;
  std::atomic<bool> healthy_{false};
  Duration baseLatency_{std::chrono::milliseconds(3)};
  Duration perSampleLatency_{std::chrono::microseconds(350)};
  std::vector<float> outputBuffer_;
};

} // namespace infsched
