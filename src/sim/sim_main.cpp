#include "src/sim/simulator.hpp"

#include <iostream>
#include <iomanip>

using namespace infsched;

int main(int argc, char* argv[]) {
  size_t traceCount = 2000;
  if (argc > 1) {
    traceCount = std::stoul(argv[1]);
  }

  std::cout << "========================================================\n";
  std::cout << "  CPU Inference Scheduler Policy Simulator & Benchmark \n";
  std::cout << "========================================================\n\n";

  std::cout << "[Simulator] Generating synthetic trace of " << traceCount << " requests...\n";
  auto trace = Simulator::generateSyntheticTrace(traceCount, 60.0, 120.0);

  Simulator sim;
  std::cout << "[Simulator] Running Baseline (Unmanaged Cores & Cache)...\n";
  auto baseRes = sim.runUnmanagedBaseline(trace);

  std::cout << "[Simulator] Running Managed Policy (Pinning + Cache Budgets + EDF Dynamic Batching)...\n\n";
  auto schedRes = sim.runManagedScheduler(trace);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "+----------------------------------+-----------------+-----------------+\n";
  std::cout << "| Metric                           | Baseline        | infsched        |\n";
  std::cout << "+----------------------------------+-----------------+-----------------+\n";
  std::cout << "| p50 Latency (ms)                 | " << std::setw(15) << baseRes.p50LatencyMs
            << " | " << std::setw(15) << schedRes.p50LatencyMs << " |\n";
  std::cout << "| p95 Latency (ms)                 | " << std::setw(15) << baseRes.p95LatencyMs
            << " | " << std::setw(15) << schedRes.p95LatencyMs << " |\n";
  std::cout << "| p99 Latency (ms)                 | " << std::setw(15) << baseRes.p99LatencyMs
            << " | " << std::setw(15) << schedRes.p99LatencyMs << " |\n";
  std::cout << "| Deadline Miss Rate (%)           | " << std::setw(14) << baseRes.deadlineMissRatePct << "%"
            << " | " << std::setw(14) << schedRes.deadlineMissRatePct << "% |\n";
  std::cout << "| Noisy Neighbor Slowdown (%)      | " << std::setw(14) << baseRes.noisyNeighborSlowdownPct << "%"
            << " | " << std::setw(14) << schedRes.noisyNeighborSlowdownPct << "% |\n";
  std::cout << "| Throughput (req/sec)             | " << std::setw(15) << baseRes.throughputRps
            << " | " << std::setw(15) << schedRes.throughputRps << " |\n";
  std::cout << "+----------------------------------+-----------------+-----------------+\n\n";

  double p99Reduction = ((baseRes.p99LatencyMs - schedRes.p99LatencyMs) / baseRes.p99LatencyMs) * 100.0;
  std::cout << "[Verification Target 1] p99 Latency Reduction: " << p99Reduction << "% (Goal: >= 30%) -> "
            << (p99Reduction >= 30.0 ? "PASS" : "FAIL") << "\n";
  std::cout << "[Verification Target 2] Noisy Neighbor Slowdown: " << schedRes.noisyNeighborSlowdownPct
            << "% (Goal: < 15%) -> " << (schedRes.noisyNeighborSlowdownPct < 15.0 ? "PASS" : "FAIL") << "\n";

  return 0;
}
