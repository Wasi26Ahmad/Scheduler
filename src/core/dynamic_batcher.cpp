#include "infsched/dynamic_batcher.hpp"
#include <algorithm>

namespace infsched {

DynamicBatcher::Decision DynamicBatcher::evaluate(
    const std::deque<Request>& queue,
    size_t maxBatchCap,
    Duration estimatedRunTime,
    Duration maxWait,
    Duration safetyMargin,
    TimePoint now) {
  Decision dec;
  if (queue.empty() || maxBatchCap == 0) {
    return dec;
  }

  size_t count = std::min(queue.size(), maxBatchCap);
  const auto& oldest = queue.front();

  Duration slack = Duration::zero();
  if (oldest.deadline > now + estimatedRunTime + safetyMargin) {
    slack = oldest.deadline - (now + estimatedRunTime + safetyMargin);
  }

  if (count < maxBatchCap && slack > maxWait) {
    dec.shouldFormBatch = false;
    dec.waitDuration = std::min(maxWait, slack);
    dec.batchSize = count;
  } else {
    dec.shouldFormBatch = true;
    dec.waitDuration = Duration::zero();
    dec.batchSize = count;
  }

  return dec;
}

} // namespace infsched
