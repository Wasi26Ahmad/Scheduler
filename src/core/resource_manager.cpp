#include "infsched/resource_manager.hpp"

#include <sched.h>
#include <pthread.h>
#include <iostream>
#include <algorithm>

namespace infsched {

ResourceManager::ResourceManager(HardwareTopology topo) : topo_(std::move(topo)) {
  for (const auto& pc : topo_.physicalCores) {
    availableCoreIds_.insert(pc.coreId);
  }

  // Determine isolation level
  if (topo_.resctrlMounted && topo_.hasL3Cat && topo_.hasMba) {
    isolationLevel_ = IsolationLevel::Full;
  } else if (topo_.hasL2Cat || topo_.hasL3Cat || topo_.l3TotalBytes > 0) {
    isolationLevel_ = IsolationLevel::Partial;
  } else {
    isolationLevel_ = IsolationLevel::CoresOnly;
  }
}

std::unique_ptr<ResourceManager> ResourceManager::create() {
  return std::make_unique<ResourceManager>(HardwareTopology::discover());
}

std::string ResourceManager::isolationLevelString() const {
  switch (isolationLevel_) {
    case IsolationLevel::Full: return "full";
    case IsolationLevel::Partial: return "partial";
    case IsolationLevel::CoresOnly: return "cores-only";
    default: return "cores-only";
  }
}

std::optional<CoreLease> ResourceManager::acquireLease(const ModelConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (activeLeases_.find(config.name) != activeLeases_.end()) {
    return activeLeases_[config.name];
  }

  int coresNeeded = std::max(1, config.minPhysicalCores);
  if (static_cast<int>(availableCoreIds_.size()) < coresNeeded) {
    // If not enough unassigned physical cores, try to allocate at least 1 core if available
    if (availableCoreIds_.empty()) {
      return std::nullopt;
    }
    coresNeeded = 1;
  }

  CoreLease lease;
  lease.model = config.name;
  lease.memoryLimitBytes = config.memoryLimitBytes;

  auto it = availableCoreIds_.begin();
  for (int i = 0; i < coresNeeded && it != availableCoreIds_.end(); ++i) {
    int coreId = *it;
    lease.physicalCoreIds.push_back(coreId);
    it = availableCoreIds_.erase(it);

    // Find physical core details to get all logical CPUs (hyperthreads)
    for (const auto& pc : topo_.physicalCores) {
      if (pc.coreId == coreId) {
        for (int lcpu : pc.logicalCpuIds) {
          lease.logicalCpuIds.push_back(lcpu);
        }
        break;
      }
    }
  }

  // Software cache budget: private L2 of allocated cores + proportional fraction of L3
  size_t totalPhysicalCores = topo_.physicalCores.empty() ? 1 : topo_.physicalCores.size();
  size_t privateL2 = lease.physicalCoreIds.size() * topo_.l2BytesPerCore;
  size_t shareOfL3 = (topo_.l3TotalBytes * lease.physicalCoreIds.size()) / totalPhysicalCores;
  lease.cacheBudgetBytes = privateL2 + shareOfL3;
  if (lease.cacheBudgetBytes == 0) {
    lease.cacheBudgetBytes = 4 * 1024 * 1024; // 4 MiB fallback
  }

  activeLeases_[config.name] = lease;
  return lease;
}

void ResourceManager::releaseLease(const std::string& model) {
  std::lock_guard<std::mutex> lock(mutex_);
  returnOverflowCores(model);

  auto it = activeLeases_.find(model);
  if (it != activeLeases_.end()) {
    for (int coreId : it->second.physicalCoreIds) {
      availableCoreIds_.insert(coreId);
    }
    activeLeases_.erase(it);
  }
}

std::optional<CoreLease> ResourceManager::borrowOverflowCores(const std::string& model, int numCores) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (availableCoreIds_.empty() || numCores <= 0) {
    return std::nullopt;
  }

  int canBorrow = std::min(numCores, static_cast<int>(availableCoreIds_.size()));
  CoreLease lease;
  lease.model = model;
  lease.isBorrowedOverflow = true;

  auto it = availableCoreIds_.begin();
  for (int i = 0; i < canBorrow && it != availableCoreIds_.end(); ++i) {
    int coreId = *it;
    lease.physicalCoreIds.push_back(coreId);
    it = availableCoreIds_.erase(it);

    for (const auto& pc : topo_.physicalCores) {
      if (pc.coreId == coreId) {
        for (int lcpu : pc.logicalCpuIds) {
          lease.logicalCpuIds.push_back(lcpu);
        }
        break;
      }
    }
  }

  overflowLeases_[model] = lease;
  return lease;
}

void ResourceManager::returnOverflowCores(const std::string& model) {
  auto it = overflowLeases_.find(model);
  if (it != overflowLeases_.end()) {
    for (int coreId : it->second.physicalCoreIds) {
      availableCoreIds_.insert(coreId);
    }
    overflowLeases_.erase(it);
  }
}

bool ResourceManager::pinThreadToLease(const CoreLease& lease, pid_t pid) {
  if (lease.logicalCpuIds.empty()) return false;

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  for (int cpu : lease.logicalCpuIds) {
    CPU_SET(cpu, &cpuset);
  }

  int rc = 0;
  if (pid == 0) {
    rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  } else {
    rc = sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset);
  }
  return rc == 0;
}

size_t ResourceManager::calculateCacheCap(const ModelConfig& config, const CoreLease& lease) const {
  if (config.workingSetBytesPerSample == 0) {
    return config.maxBatchSize;
  }
  size_t budget = lease.cacheBudgetBytes;
  size_t cap = budget / config.workingSetBytesPerSample;
  if (cap == 0) cap = 1;
  return std::min(cap, config.maxBatchSize);
}

} // namespace infsched
