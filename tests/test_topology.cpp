#include "infsched/topology.hpp"
#include <cassert>
#include <iostream>

using namespace infsched;

int main() {
  std::cout << "[Test] Running test_topology..." << std::endl;

  auto topo = HardwareTopology::discover();
  std::cout << "  Discovered " << topo.physicalCores.size() << " physical cores." << std::endl;
  assert(!topo.physicalCores.empty());

  for (const auto& pc : topo.physicalCores) {
    assert(!pc.logicalCpuIds.empty());
    std::cout << "  Core " << pc.coreId << " has " << pc.logicalCpuIds.size() << " hyperthreads." << std::endl;
  }

  assert(topo.numaNodeCount >= 1);
  std::cout << "[Test] test_topology PASSED." << std::endl;
  return 0;
}
