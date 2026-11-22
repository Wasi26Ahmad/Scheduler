#include "infsched/resource_manager.hpp"
#include <cassert>
#include <iostream>

using namespace infsched;

int main() {
  std::cout << "[Test] Running test_resource_manager..." << std::endl;

  auto rm = ResourceManager::create();
  assert(rm != nullptr);

  ModelConfig modelA;
  modelA.name = "model_a";
  modelA.minPhysicalCores = 1;
  modelA.maxPhysicalCores = 2;
  modelA.maxBatchSize = 16;
  modelA.workingSetBytesPerSample = 512 * 1024;

  auto leaseA = rm->acquireLease(modelA);
  assert(leaseA.has_value());
  assert(!leaseA->physicalCoreIds.empty());
  assert(!leaseA->logicalCpuIds.empty());
  assert(leaseA->cacheBudgetBytes > 0);

  size_t cacheCap = rm->calculateCacheCap(modelA, *leaseA);
  assert(cacheCap > 0 && cacheCap <= modelA.maxBatchSize);

  // Test non-overlapping physical cores
  ModelConfig modelB;
  modelB.name = "model_b";
  modelB.minPhysicalCores = 1;

  auto leaseB = rm->acquireLease(modelB);
  if (rm->topology().physicalCores.size() >= 2) {
    assert(leaseB.has_value());
    // Ensure core IDs do not intersect
    for (int coreA : leaseA->physicalCoreIds) {
      for (int coreB : leaseB->physicalCoreIds) {
        assert(coreA != coreB);
      }
    }
  }

  // Release lease
  rm->releaseLease("model_a");
  if (leaseB) {
    rm->releaseLease("model_b");
  }

  std::cout << "[Test] test_resource_manager PASSED." << std::endl;
  return 0;
}
