#pragma once

#include "infsched/metrics.hpp"
#include "infsched/resource_manager.hpp"
#include "infsched/supervisor.hpp"
#include "infsched/scheduler.hpp"

#include <string>
#include <thread>
#include <atomic>
#include <memory>

namespace infsched {

class OpsServer {
public:
  OpsServer(int port,
            std::shared_ptr<MetricsCollector> metrics,
            std::shared_ptr<ResourceManager> rm,
            std::shared_ptr<Supervisor> supervisor,
            std::shared_ptr<Scheduler> scheduler);
  ~OpsServer();

  bool start();
  void stop();

private:
  void serverLoop();
  void handleClient(int clientFd);

  int port_;
  int serverFd_ = -1;
  std::shared_ptr<MetricsCollector> metrics_;
  std::shared_ptr<ResourceManager> rm_;
  std::shared_ptr<Supervisor> supervisor_;
  std::shared_ptr<Scheduler> scheduler_;
  std::atomic<bool> running_{false};
  std::unique_ptr<std::thread> thread_;
};

} // namespace infsched
