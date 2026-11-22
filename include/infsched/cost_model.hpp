#pragma once

#include "infsched/types.hpp"
#include <mutex>
#include <map>
#include <vector>

namespace infsched {

class CostModel {
public:
  CostModel(double alpha = 0.2);

  void recordWarmup(const std::string& model, size_t batchSize, Duration measuredLatency);
  void update(const std::string& model, size_t batchSize, Duration measuredLatency);
  Duration estimate(const std::string& model, size_t batchSize) const;

private:
  double alpha_; // EMA weight
  mutable std::mutex mutex_;

  struct BatchCost {
    Duration emaLatency = std::chrono::milliseconds(5);
    size_t sampleCount = 0;
  };

  struct ModelStats {
    std::map<size_t, BatchCost> perBatchCost;
    Duration baseLatency = std::chrono::milliseconds(5);
    Duration perSampleLatency = std::chrono::milliseconds(1);
  };

  mutable std::map<std::string, ModelStats> models_;
};

} // namespace infsched
