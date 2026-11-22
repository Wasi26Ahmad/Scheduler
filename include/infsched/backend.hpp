#pragma once

#include "infsched/types.hpp"
#include <vector>
#include <string>

namespace infsched {

class Backend {
public:
  virtual ~Backend() = default;

  virtual StatusCode load(const ModelConfig& config) = 0;
  virtual StatusCode run(const Batch& batch, std::vector<TensorView>& outputs) = 0;
  virtual Duration estimateWarmup(size_t batchSize) = 0;
  virtual void unload() = 0;
  virtual bool isHealthy() const = 0;
  virtual const std::string& name() const = 0;
};

} // namespace infsched
