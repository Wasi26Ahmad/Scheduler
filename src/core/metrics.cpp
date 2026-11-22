#include "infsched/metrics.hpp"

#include <sstream>
#include <algorithm>
#include <numeric>

namespace infsched {

TokenBucket::TokenBucket(double rate, double burst)
    : rate_(rate), burstCapacity_(burst), tokens_(burst), lastRefill_(SystemClock::now()) {}

bool TokenBucket::tryConsume(double tokens) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto now = SystemClock::now();
  double elapsedSec = std::chrono::duration<double>(now - lastRefill_).count();
  lastRefill_ = now;

  tokens_ = std::min(burstCapacity_, tokens_ + elapsedSec * rate_);
  if (tokens_ >= tokens) {
    tokens_ -= tokens;
    return true;
  }
  return false;
}

MetricsCollector::MetricsCollector() = default;

void MetricsCollector::recordAdmitted(const std::string& model, const std::string& /*tenant*/) {
  admittedTotal_++;
  std::lock_guard<std::mutex> lock(statsMutex_);
  modelRequestCounts_[model]++;
}

void MetricsCollector::recordRejected(const std::string& /*model*/, const std::string& /*tenant*/, const std::string& reason) {
  rejectedTotal_++;
  std::lock_guard<std::mutex> lock(statsMutex_);
  rejectReasonCounts_[reason]++;
  if (reason == "deadline_missed") {
    deadlineMisses_++;
  }
}

void MetricsCollector::recordBatchCompleted(const std::string& /*model*/, size_t batchSize, Duration runDuration, Duration totalQueueDuration) {
  completedTotal_ += batchSize;
  int64_t totalUs = std::chrono::duration_cast<std::chrono::microseconds>(runDuration + totalQueueDuration).count();
  std::lock_guard<std::mutex> lock(statsMutex_);
  recentLatenciesUs_.push_back(totalUs);
  if (recentLatenciesUs_.size() > 10000) {
    recentLatenciesUs_.erase(recentLatenciesUs_.begin(), recentLatenciesUs_.begin() + 1000);
  }
}

void MetricsCollector::recordSchedulerOverhead(Duration overhead) {
  int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(overhead).count();
  totalOverheadMicros_ += us;
  overheadCount_++;
}

std::string MetricsCollector::toPrometheus() const {
  std::ostringstream ss;
  ss << "# HELP infsched_requests_admitted_total Total inference requests admitted\n";
  ss << "# TYPE infsched_requests_admitted_total counter\n";
  ss << "infsched_requests_admitted_total " << admittedTotal_.load() << "\n\n";

  ss << "# HELP infsched_requests_rejected_total Total inference requests rejected\n";
  ss << "# TYPE infsched_requests_rejected_total counter\n";
  ss << "infsched_requests_rejected_total " << rejectedTotal_.load() << "\n\n";

  ss << "# HELP infsched_requests_completed_total Total inference requests completed\n";
  ss << "# TYPE infsched_requests_completed_total counter\n";
  ss << "infsched_requests_completed_total " << completedTotal_.load() << "\n\n";

  uint64_t count = overheadCount_.load();
  double avgOverheadUs = count > 0 ? static_cast<double>(totalOverheadMicros_.load()) / count : 0.0;
  ss << "# HELP infsched_scheduler_overhead_microseconds Average scheduling overhead per request\n";
  ss << "# TYPE infsched_scheduler_overhead_microseconds gauge\n";
  ss << "infsched_scheduler_overhead_microseconds " << avgOverheadUs << "\n\n";

  std::lock_guard<std::mutex> lock(statsMutex_);
  for (const auto& pair : rejectReasonCounts_) {
    ss << "infsched_rejections_by_reason{reason=\"" << pair.first << "\"} " << pair.second << "\n";
  }

  for (const auto& pair : modelRequestCounts_) {
    ss << "infsched_requests_by_model{model=\"" << pair.first << "\"} " << pair.second << "\n";
  }

  if (!recentLatenciesUs_.empty()) {
    std::vector<int64_t> sorted = recentLatenciesUs_;
    std::sort(sorted.begin(), sorted.end());
    size_t n = sorted.size();
    int64_t p50 = sorted[n * 50 / 100];
    int64_t p95 = sorted[n * 95 / 100];
    int64_t p99 = sorted[n * 99 / 100];

    ss << "\n# HELP infsched_latency_microseconds Inference latency summary\n";
    ss << "# TYPE infsched_latency_microseconds summary\n";
    ss << "infsched_latency_microseconds{quantile=\"0.5\"} " << p50 << "\n";
    ss << "infsched_latency_microseconds{quantile=\"0.95\"} " << p95 << "\n";
    ss << "infsched_latency_microseconds{quantile=\"0.99\"} " << p99 << "\n";
  }

  return ss.str();
}

} // namespace infsched
