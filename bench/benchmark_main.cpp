#include "infsched/scheduler.hpp"
#include "infsched/topology.hpp"
#include "infsched/resource_manager.hpp"
#include "src/worker/mock_backend.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <future>
#include <numeric>
#include <algorithm>

using namespace infsched;

struct BenchMetrics {
  double p50 = 0, p95 = 0, p99 = 0;
  double throughput = 0;
  double missRate = 0;
  uint64_t admitted = 0;
  uint64_t rejected = 0;
};

BenchMetrics runManagedBenchmark(size_t totalRequests) {
  auto topo = HardwareTopology::discover();
  auto rm = std::make_shared<ResourceManager>(std::move(topo));
  auto costModel = std::make_shared<CostModel>();
  auto metrics = std::make_shared<MetricsCollector>();

  SchedulerPolicy policy;
  policy.maxBatchWait = std::chrono::milliseconds(2);
  policy.safetyMargin = std::chrono::milliseconds(1);
  policy.maxQueueDepth = 10000;

  Scheduler scheduler(policy, rm, costModel, metrics);

  ModelConfig protectedModel;
  protectedModel.name = "protected_net";
  protectedModel.minPhysicalCores = 1;
  protectedModel.maxBatchSize = 8;
  protectedModel.workingSetBytesPerSample = 512 * 1024;
  auto protectedBackend = std::make_shared<MockBackend>("protected_net", std::chrono::milliseconds(1), std::chrono::microseconds(100));
  scheduler.registerModel(protectedModel, protectedBackend);

  ModelConfig noisyModel;
  noisyModel.name = "noisy_bulk";
  noisyModel.minPhysicalCores = 1;
  noisyModel.maxBatchSize = 16;
  noisyModel.workingSetBytesPerSample = 1024 * 1024;
  auto noisyBackend = std::make_shared<MockBackend>("noisy_bulk", std::chrono::milliseconds(2), std::chrono::microseconds(200));
  scheduler.registerModel(noisyModel, noisyBackend);

  scheduler.start();

  std::vector<std::promise<Duration>> promises(totalRequests);
  std::vector<std::future<Duration>> futures;
  std::vector<bool> wasAdmitted(totalRequests, false);
  futures.reserve(totalRequests);
  for (size_t i = 0; i < totalRequests; ++i) {
    futures.push_back(promises[i].get_future());
  }

  auto benchStart = SystemClock::now();
  uint64_t admittedCount = 0;
  uint64_t rejectedCount = 0;

  for (size_t i = 0; i < totalRequests; ++i) {
    Request req;
    req.id = i + 1;
    // Use unique tenant per shard to avoid token bucket starvation in high-throughput bench
    req.tenant = "tenant_" + std::to_string(i % 10);
    req.model = (i % 2 == 0) ? "protected_net" : "noisy_bulk";
    req.deadline = SystemClock::now() + std::chrono::milliseconds(2000);

    auto tSubmit = SystemClock::now();
    auto adm = scheduler.submit(req, [&promises, i, tSubmit](const Batch&, StatusCode, const std::vector<TensorView>&, Duration) {
      auto tDone = SystemClock::now();
      promises[i].set_value(tDone - tSubmit);
    });

    if (adm == AdmissionResult::Accepted) {
      wasAdmitted[i] = true;
      admittedCount++;
    } else {
      wasAdmitted[i] = false;
      rejectedCount++;
      promises[i].set_value(Duration::zero());
    }

    if (i % 25 == 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }

  std::vector<double> latenciesMs;
  latenciesMs.reserve(totalRequests);
  size_t missed = 0;

  for (size_t i = 0; i < totalRequests; ++i) {
    if (!wasAdmitted[i]) continue;
    if (futures[i].wait_for(std::chrono::milliseconds(1500)) == std::future_status::ready) {
      double ms = std::chrono::duration<double, std::milli>(futures[i].get()).count();
      latenciesMs.push_back(ms);
      if (ms > 2000.0) missed++;
    } else {
      missed++;
    }
  }

  auto benchEnd = SystemClock::now();
  scheduler.stop();

  double totalSec = std::chrono::duration<double>(benchEnd - benchStart).count();
  std::sort(latenciesMs.begin(), latenciesMs.end());

  BenchMetrics res;
  if (!latenciesMs.empty()) {
    res.p50 = latenciesMs[latenciesMs.size() * 50 / 100];
    res.p95 = latenciesMs[latenciesMs.size() * 95 / 100];
    res.p99 = latenciesMs[latenciesMs.size() * 99 / 100];
  }
  res.throughput = totalRequests / totalSec;
  res.missRate = admittedCount > 0 ? (static_cast<double>(missed) / admittedCount) * 100.0 : 0.0;
  res.admitted = admittedCount;
  res.rejected = rejectedCount;
  return res;
}

int main() {
  std::cout << "=== infsched CPU Inference Scheduler Live Benchmark ===" << std::endl;
  size_t testRequests = 200;
  std::cout << "Submitting " << testRequests << " concurrent requests under noisy neighbor load...\n";

  auto metrics = runManagedBenchmark(testRequests);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "\nResults Summary:\n";
  std::cout << "  Admitted Requests:  " << metrics.admitted << " / " << testRequests << "\n";
  std::cout << "  p50 Latency:        " << metrics.p50 << " ms\n";
  std::cout << "  p95 Latency:        " << metrics.p95 << " ms\n";
  std::cout << "  p99 Latency:        " << metrics.p99 << " ms\n";
  std::cout << "  Throughput:         " << metrics.throughput << " req/sec\n";
  std::cout << "  Deadline Miss Rate: " << metrics.missRate << " %\n";

  return 0;
}
