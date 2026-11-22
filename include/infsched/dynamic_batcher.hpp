#pragma once

#include "infsched/types.hpp"
#include <deque>
#include <vector>

namespace infsched {

class DynamicBatcher {
public:
  struct Decision {
    bool shouldFormBatch = false;
    Duration waitDuration{0};
    size_t batchSize = 0;
  };

  static Decision evaluate(
      const std::deque<Request>& queue,
      size_t maxBatchCap,
      Duration estimatedRunTime,
      Duration maxWait = std::chrono::milliseconds(2),
      Duration safetyMargin = std::chrono::milliseconds(1),
      TimePoint now = SystemClock::now());
};

} // namespace infsched
