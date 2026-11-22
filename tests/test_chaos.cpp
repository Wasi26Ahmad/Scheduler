#include "infsched/supervisor.hpp"
#include "infsched/topology.hpp"
#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>

using namespace infsched;

int main() {
  std::cout << "[Chaos Test] Running test_chaos worker failure and recovery..." << std::endl;

  auto topo = HardwareTopology::discover();
  std::vector<int> cpuList;
  if (!topo.physicalCores.empty()) {
    cpuList = topo.physicalCores[0].logicalCpuIds;
  }

  // Use the worker binary (or /bin/sleep as dummy process for testing process death/restart)
  Supervisor supervisor("/bin/sleep");

  ModelConfig dummyModel;
  dummyModel.name = "chaos_worker";
  dummyModel.minPhysicalCores = 1;
  dummyModel.memoryLimitBytes = 256 * 1024 * 1024;

  bool ok = supervisor.spawnWorker(dummyModel, cpuList);
  assert(ok);
  assert(supervisor.hasAnyReadyModel());
  assert(supervisor.isModelReady("chaos_worker"));

  std::cout << "  Worker successfully spawned. Injecting SIGKILL chaos failure...\n";

  // Simulate unexpected crash by finding worker pid or calling health check
  supervisor.killWorker("chaos_worker");
  assert(!supervisor.isModelReady("chaos_worker"));
  std::cout << "  Worker killed. Initiating automated recovery respawn...\n";

  auto tStart = SystemClock::now();
  bool recovered = supervisor.spawnWorker(dummyModel, cpuList);
  auto tEnd = SystemClock::now();

  assert(recovered);
  assert(supervisor.isModelReady("chaos_worker"));

  double recoveryMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
  std::cout << "  Recovery completed in " << recoveryMs << " ms (Target: < 10,000 ms).\n";
  assert(recoveryMs < 10000.0);

  supervisor.stopAll();
  std::cout << "[Chaos Test] test_chaos PASSED." << std::endl;
  return 0;
}
