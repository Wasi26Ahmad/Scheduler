#pragma once

#include "infsched/types.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <sys/types.h>

namespace infsched {

struct WorkerProcess {
  std::string model;
  pid_t pid = -1;
  int ipcSocketFd = -1;
  TimePoint lastHeartbeat = SystemClock::now();
  int restartCount = 0;
  bool isHealthy = true;
};

class Supervisor {
public:
  Supervisor(std::string workerBinaryPath);
  ~Supervisor();

  bool spawnWorker(const ModelConfig& config, const std::vector<int>& cpuPinList);
  void killWorker(const std::string& model);
  void stopAll();

  bool isModelReady(const std::string& model) const;
  bool hasAnyReadyModel() const;
  size_t activeWorkerCount() const;
  
  void checkWorkersHealth(Duration timeout = std::chrono::seconds(3));
  void handleWorkerExit(pid_t pid);

private:
  std::string workerBinaryPath_;
  mutable std::mutex mutex_;
  std::map<std::string, WorkerProcess> workers_;
  std::atomic<bool> running_{true};
};

} // namespace infsched
