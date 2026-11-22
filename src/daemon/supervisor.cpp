#include "infsched/supervisor.hpp"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sched.h>
#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <cstring>

namespace infsched {

Supervisor::Supervisor(std::string workerBinaryPath)
    : workerBinaryPath_(std::move(workerBinaryPath)) {}

Supervisor::~Supervisor() {
  stopAll();
}

bool Supervisor::spawnWorker(const ModelConfig& config, const std::vector<int>& cpuPinList) {
  std::lock_guard<std::mutex> lock(mutex_);

  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
    perror("socketpair");
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(sv[0]);
    close(sv[1]);
    return false;
  }

  if (pid == 0) {
    // Child worker process
    close(sv[0]);

    // 1. Die if supervisor dies
    prctl(PR_SET_PDEATHSIG, SIGTERM);

    // 2. Pin to allocated cores
    if (!cpuPinList.empty()) {
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      for (int cpu : cpuPinList) {
        CPU_SET(cpu, &cpuset);
      }
      sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    }

    // 3. Set memory limit
    if (config.memoryLimitBytes > 0) {
      struct rlimit rl;
      rl.rlim_cur = config.memoryLimitBytes;
      rl.rlim_max = config.memoryLimitBytes;
      setrlimit(RLIMIT_AS, &rl);
    }

    // Exec worker binary with IPC fd as argument
    std::string fdStr = std::to_string(sv[1]);
    std::string nameArg = config.name;
    std::string pathArg = config.modelPath.empty() ? "none" : config.modelPath;
    std::string backendArg = config.backendType;

    char* argv[] = {
      const_cast<char*>(workerBinaryPath_.c_str()),
      const_cast<char*>("--model"), const_cast<char*>(nameArg.c_str()),
      const_cast<char*>("--backend"), const_cast<char*>(backendArg.c_str()),
      const_cast<char*>("--ipc-fd"), const_cast<char*>(fdStr.c_str()),
      nullptr
    };

    execv(workerBinaryPath_.c_str(), argv);

    // If exec fails, exit child
    std::cerr << "Failed to exec worker binary: " << workerBinaryPath_ << std::endl;
    _exit(1);
  }

  // Parent process
  close(sv[1]);

  WorkerProcess wp;
  wp.model = config.name;
  wp.pid = pid;
  wp.ipcSocketFd = sv[0];
  wp.lastHeartbeat = SystemClock::now();
  wp.isHealthy = true;

  workers_[config.name] = wp;
  return true;
}

void Supervisor::killWorker(const std::string& model) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = workers_.find(model);
  if (it != workers_.end()) {
    if (it->second.pid > 0) {
      kill(it->second.pid, SIGTERM);
      int status;
      waitpid(it->second.pid, &status, WNOHANG);
    }
    if (it->second.ipcSocketFd >= 0) {
      close(it->second.ipcSocketFd);
    }
    workers_.erase(it);
  }
}

void Supervisor::stopAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& pair : workers_) {
    if (pair.second.pid > 0) {
      kill(pair.second.pid, SIGTERM);
      int status;
      waitpid(pair.second.pid, &status, 0);
    }
    if (pair.second.ipcSocketFd >= 0) {
      close(pair.second.ipcSocketFd);
      pair.second.ipcSocketFd = -1;
    }
  }
  workers_.clear();
}

bool Supervisor::isModelReady(const std::string& model) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = workers_.find(model);
  if (it == workers_.end()) return false;
  return it->second.isHealthy && (it->second.pid > 0);
}

bool Supervisor::hasAnyReadyModel() const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& pair : workers_) {
    if (pair.second.isHealthy && pair.second.pid > 0) {
      return true;
    }
  }
  return false;
}

size_t Supervisor::activeWorkerCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t count = 0;
  for (const auto& pair : workers_) {
    if (pair.second.isHealthy && pair.second.pid > 0) {
      count++;
    }
  }
  return count;
}

void Supervisor::checkWorkersHealth(Duration timeout) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto now = SystemClock::now();

  for (auto& pair : workers_) {
    auto& wp = pair.second;
    // Check if process has terminated
    int status;
    pid_t res = waitpid(wp.pid, &status, WNOHANG);
    if (res > 0) {
      // Worker exited!
      wp.isHealthy = false;
      wp.pid = -1;
      continue;
    }

    // Check heartbeat timeout
    if (now - wp.lastHeartbeat > timeout) {
      wp.isHealthy = false;
      // Restart or kill unresponsive worker
      kill(wp.pid, SIGKILL);
      waitpid(wp.pid, &status, 0);
      wp.pid = -1;
    }
  }
}

void Supervisor::handleWorkerExit(pid_t pid) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& pair : workers_) {
    if (pair.second.pid == pid) {
      pair.second.isHealthy = false;
      pair.second.pid = -1;
      pair.second.restartCount++;
      break;
    }
  }
}

} // namespace infsched
