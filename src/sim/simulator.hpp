#pragma once

#include "infsched/types.hpp"
#include <string>
#include <vector>
#include <map>

namespace infsched {

struct TraceEvent {
  uint64_t timestampMs;
  std::string model;
  std::string tenant;
  uint32_t deadlineMs;
  int priority;
};

struct SimResults {
  double p50LatencyMs = 0.0;
  double p95LatencyMs = 0.0;
  double p99LatencyMs = 0.0;
  double avgLatencyMs = 0.0;
  double deadlineMissRatePct = 0.0;
  double throughputRps = 0.0;
  double noisyNeighborSlowdownPct = 0.0;
  uint64_t totalRequests = 0;
  uint64_t admittedRequests = 0;
  uint64_t droppedRequests = 0;
};

class Simulator {
public:
  Simulator();

  static std::vector<TraceEvent> generateSyntheticTrace(
      size_t count = 1000,
      double rpsProtected = 50.0,
      double rpsNoisyNeighbor = 100.0);

  SimResults runUnmanagedBaseline(const std::vector<TraceEvent>& trace);
  SimResults runManagedScheduler(const std::vector<TraceEvent>& trace);
};

} // namespace infsched
