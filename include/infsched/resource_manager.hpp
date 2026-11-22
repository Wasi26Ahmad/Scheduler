#pragma once

#include "infsched/types.hpp"
#include "infsched/topology.hpp"
#include <mutex>
#include <map>
#include <vector>
#include <set>
#include <optional>

namespace infsched {

struct CoreLease {
  std::string model;
  std::vector<int> physicalCoreIds;
  std::vector<int> logicalCpuIds; // Hyperthreads for sched_setaffinity
  size_t cacheBudgetBytes = 0;
  size_t memoryLimitBytes = 0;
  bool isBorrowedOverflow = false;
};

enum class IsolationLevel {
  Full,
  Partial,
  CoresOnly
};

class ResourceManager {
public:
  explicit ResourceManager(HardwareTopology topo);
  static std::unique_ptr<ResourceManager> create();

  const HardwareTopology& topology() const { return topo_; }
  IsolationLevel isolationLevel() const { return isolationLevel_; }
  std::string isolationLevelString() const;

  std::optional<CoreLease> acquireLease(const ModelConfig& config);
  void releaseLease(const std::string& model);

  std::optional<CoreLease> borrowOverflowCores(const std::string& model, int numCores);
  void returnOverflowCores(const std::string& model);

  bool pinThreadToLease(const CoreLease& lease, pid_t pid = 0);
  size_t calculateCacheCap(const ModelConfig& config, const CoreLease& lease) const;

private:
  HardwareTopology topo_;
  IsolationLevel isolationLevel_ = IsolationLevel::CoresOnly;
  mutable std::mutex mutex_;

  std::set<int> availableCoreIds_;
  std::map<std::string, CoreLease> activeLeases_;
  std::map<std::string, CoreLease> overflowLeases_;
};

} // namespace infsched
