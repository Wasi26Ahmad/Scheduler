#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <map>
#include <set>

namespace infsched {

struct CacheLevel {
  int level = 0;
  std::string type; // Data, Instruction, Unified
  size_t sizeBytes = 0;
  std::set<int> sharedCpuList;
};

struct PhysicalCore {
  int coreId = 0;
  int packageId = 0;
  int numaNode = 0;
  std::vector<int> logicalCpuIds; // Hyperthreads
};

struct HardwareTopology {
  std::vector<PhysicalCore> physicalCores;
  std::map<int, std::vector<CacheLevel>> cpuCaches; // logical CPU -> cache levels
  
  // Aggregated cache stats
  size_t l2BytesPerCore = 0;
  size_t l3TotalBytes = 0;
  
  // Hardware capabilities detected
  bool hasAvx2 = false;
  bool hasAvx512 = false;
  bool hasVnni = false;
  bool hasAmx = false;
  bool hasL3Cat = false;
  bool hasL2Cat = false;
  bool hasMba = false;
  bool resctrlMounted = false;
  int numaNodeCount = 1;

  static HardwareTopology discover();
};

} // namespace infsched
