#include "infsched/topology.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <unistd.h>

namespace infsched {

namespace {

std::string trim(const std::string& str) {
  auto start = str.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  auto end = str.find_last_not_of(" \t\r\n");
  return str.substr(start, end - start + 1);
}

std::vector<int> parseCpuList(const std::string& line) {
  std::vector<int> cpus;
  std::stringstream ss(trim(line));
  std::string token;
  while (std::getline(ss, token, ',')) {
    token = trim(token);
    if (token.empty()) continue;
    auto dash = token.find('-');
    if (dash != std::string::npos) {
      int start = std::stoi(token.substr(0, dash));
      int end = std::stoi(token.substr(dash + 1));
      for (int i = start; i <= end; ++i) {
        cpus.push_back(i);
      }
    } else {
      cpus.push_back(std::stoi(token));
    }
  }
  std::sort(cpus.begin(), cpus.end());
  cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
  return cpus;
}

size_t parseSizeWithUnit(const std::string& raw) {
  std::string s = trim(raw);
  if (s.empty()) return 0;
  char unit = s.back();
  size_t multiplier = 1;
  if (unit == 'K' || unit == 'k') {
    multiplier = 1024ULL;
    s.pop_back();
  } else if (unit == 'M' || unit == 'm') {
    multiplier = 1024ULL * 1024ULL;
    s.pop_back();
  } else if (unit == 'G' || unit == 'g') {
    multiplier = 1024ULL * 1024ULL * 1024ULL;
    s.pop_back();
  }
  try {
    return static_cast<size_t>(std::stoull(trim(s))) * multiplier;
  } catch (...) {
    return 0;
  }
}

} // namespace

HardwareTopology HardwareTopology::discover() {
  HardwareTopology topo;

  // 1. Check /proc/cpuinfo for instruction flags
  std::ifstream cpuinfo("/proc/cpuinfo");
  if (cpuinfo.is_open()) {
    std::string line;
    while (std::getline(cpuinfo, line)) {
      if (line.rfind("flags", 0) == 0) {
        if (line.find("avx2") != std::string::npos) topo.hasAvx2 = true;
        if (line.find("avx512f") != std::string::npos) topo.hasAvx512 = true;
        if (line.find("avx512_vnni") != std::string::npos || line.find("vnni") != std::string::npos) {
          topo.hasVnni = true;
        }
        if (line.find("amx") != std::string::npos) topo.hasAmx = true;
        if (line.find("cat_l3") != std::string::npos) topo.hasL3Cat = true;
        if (line.find("cat_l2") != std::string::npos) topo.hasL2Cat = true;
        if (line.find("mba") != std::string::npos) topo.hasMba = true;
      }
    }
  }

  // 2. Check /sys/fs/resctrl
  if (std::filesystem::exists("/sys/fs/resctrl/info")) {
    topo.resctrlMounted = true;
  }

  // 3. Scan /sys/devices/system/cpu/
  std::map<int, PhysicalCore> coreMap; // coreId -> PhysicalCore
  int maxLogicalCpu = sysconf(_SC_NPROCESSORS_CONF);
  if (maxLogicalCpu <= 0) maxLogicalCpu = 8;

  for (int cpu = 0; cpu < maxLogicalCpu; ++cpu) {
    std::string sysCpuPath = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
    if (!std::filesystem::exists(sysCpuPath)) continue;

    int coreId = cpu;
    int packageId = 0;

    std::ifstream coreIdFile(sysCpuPath + "/topology/core_id");
    if (coreIdFile.is_open()) coreIdFile >> coreId;

    std::ifstream pkgFile(sysCpuPath + "/topology/physical_package_id");
    if (pkgFile.is_open()) pkgFile >> packageId;

    auto& entry = coreMap[coreId];
    entry.coreId = coreId;
    entry.packageId = packageId;
    if (std::find(entry.logicalCpuIds.begin(), entry.logicalCpuIds.end(), cpu) == entry.logicalCpuIds.end()) {
      entry.logicalCpuIds.push_back(cpu);
    }

    // Read cache indices
    for (int idx = 0; idx < 10; ++idx) {
      std::string idxPath = sysCpuPath + "/cache/index" + std::to_string(idx);
      if (!std::filesystem::exists(idxPath)) break;

      CacheLevel cl;
      std::ifstream levelFile(idxPath + "/level");
      if (levelFile.is_open()) levelFile >> cl.level;

      std::ifstream typeFile(idxPath + "/type");
      if (typeFile.is_open()) typeFile >> cl.type;

      std::ifstream sizeFile(idxPath + "/size");
      if (sizeFile.is_open()) {
        std::string s;
        sizeFile >> s;
        cl.sizeBytes = parseSizeWithUnit(s);
      }

      std::ifstream sharedFile(idxPath + "/shared_cpu_list");
      if (sharedFile.is_open()) {
        std::string s;
        sharedFile >> s;
        auto cpus = parseCpuList(s);
        cl.sharedCpuList.insert(cpus.begin(), cpus.end());
      }

      topo.cpuCaches[cpu].push_back(cl);

      if (cl.level == 2 && cl.type != "Instruction" && cl.sizeBytes > topo.l2BytesPerCore) {
        topo.l2BytesPerCore = cl.sizeBytes;
      }
      if (cl.level == 3 && cl.sizeBytes > 0) {
        if (topo.l3TotalBytes == 0) {
          topo.l3TotalBytes = cl.sizeBytes;
        }
      }
    }
  }

  for (auto& pair : coreMap) {
    std::sort(pair.second.logicalCpuIds.begin(), pair.second.logicalCpuIds.end());
    topo.physicalCores.push_back(pair.second);
  }

  // Fallbacks if /sys was not accessible (e.g. strict container sandbox)
  if (topo.physicalCores.empty()) {
    int totalLogical = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    if (totalLogical <= 0) totalLogical = 4;
    // Assume 2 threads per core if >= 2 cores
    int numPhysical = (totalLogical >= 2) ? totalLogical / 2 : totalLogical;
    for (int i = 0; i < numPhysical; ++i) {
      PhysicalCore pc;
      pc.coreId = i;
      pc.packageId = 0;
      pc.numaNode = 0;
      if (totalLogical == numPhysical * 2) {
        pc.logicalCpuIds = {i, i + numPhysical};
      } else {
        pc.logicalCpuIds = {i};
      }
      topo.physicalCores.push_back(pc);
    }
    topo.l2BytesPerCore = 1280 * 1024; // 1.25 MiB fallback
    topo.l3TotalBytes = 8 * 1024 * 1024; // 8 MiB fallback
  }

  // Count NUMA nodes
  int numaNodes = 0;
  while (std::filesystem::exists("/sys/devices/system/node/node" + std::to_string(numaNodes))) {
    numaNodes++;
  }
  topo.numaNodeCount = std::max(1, numaNodes);

  return topo;
}

} // namespace infsched
