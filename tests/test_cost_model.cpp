#include "infsched/cost_model.hpp"
#include <cassert>
#include <iostream>

using namespace infsched;

int main() {
  std::cout << "[Test] Running test_cost_model..." << std::endl;

  CostModel cm(0.5); // alpha = 0.5 for fast convergence

  // Warmup calibration
  cm.recordWarmup("resnet", 1, std::chrono::milliseconds(4));
  cm.recordWarmup("resnet", 4, std::chrono::milliseconds(10));

  Duration est1 = cm.estimate("resnet", 1);
  assert(est1 == std::chrono::milliseconds(4));

  Duration est4 = cm.estimate("resnet", 4);
  assert(est4 == std::chrono::milliseconds(10));

  // Extrapolation / interpolation
  Duration est2 = cm.estimate("resnet", 2);
  assert(est2 > est1 && est2 < est4);

  // Update with empirical measurement
  cm.update("resnet", 1, std::chrono::milliseconds(6));
  Duration updatedEst1 = cm.estimate("resnet", 1);
  // (4ms + 6ms) / 2 = 5ms
  assert(updatedEst1 == std::chrono::milliseconds(5));

  std::cout << "[Test] test_cost_model PASSED." << std::endl;
  return 0;
}
