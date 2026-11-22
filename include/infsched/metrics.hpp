#pragma once

#include "infsched/types.hpp"
#include <atomic>
#include <mutex>
#include <map>
#include <string>
#include <vector>

namespace infsched {

class TokenBucket {
public:
  TokenBucket(double rate = 100.0, double burst = 200.0);
  bool tryConsume(double tokens = 1.0);

private:
  double rate_;
  double burstCapacity_;
  double tokens_;
  TimePoint lastRefill_;
  std::mutex mutex_;
};

class MetricsCollector {
public:
  MetricsCollector();

  void recordAdmitted(const std::string& model, const std::string& tenant);
  void recordRejected(const std::string& model, const std::string& tenant, const std::string& reason);
  void recordBatchCompleted(const std::string& model, size_t batchSize, Duration runDuration, Duration totalQueueDuration);
  void recordSchedulerOverhead(Duration overhead);

  std::string toPrometheus() const;

  // Query stats
  uint64_t totalAdmitted() const { return admittedTotal_.load(); }
  uint64_t totalRejected() const { return rejectedTotal_.load(); }
  uint64_t totalCompleted() const { return completedTotal_.load(); }

private:
  std::atomic<uint64_t> admittedTotal_{0};
  std::atomic<uint64_t> rejectedTotal_{0};
  std::atomic<uint64_t> completedTotal_{0};
  std::atomic<uint64_t> deadlineMisses_{0};
  std::atomic<uint64_t> totalOverheadMicros_{0};
  std::atomic<uint64_t> overheadCount_{0};

  mutable std::mutex statsMutex_;
  std::map<std::string, uint64_t> modelRequestCounts_;
  std::map<std::string, uint64_t> rejectReasonCounts_;
  std::vector<int64_t> recentLatenciesUs_;
};

} // namespace infsched
