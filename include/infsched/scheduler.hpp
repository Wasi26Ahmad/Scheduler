#pragma once

#include "infsched/types.hpp"
#include "infsched/resource_manager.hpp"
#include "infsched/cost_model.hpp"
#include "infsched/metrics.hpp"
#include "infsched/backend.hpp"

#include <deque>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>

namespace infsched {

using BatchCompletionCallback = std::function<void(const Batch&, StatusCode, const std::vector<TensorView>&, Duration)>;

struct SchedulerPolicy {
  Duration maxBatchWait = std::chrono::milliseconds(2);
  Duration safetyMargin = std::chrono::milliseconds(1);
  size_t maxQueueDepth = 5000;
  bool enableOverflowBorrowing = true;
};

class Scheduler {
public:
  Scheduler(SchedulerPolicy policy,
            std::shared_ptr<ResourceManager> rm,
            std::shared_ptr<CostModel> costModel,
            std::shared_ptr<MetricsCollector> metrics);
  ~Scheduler();

  bool registerModel(const ModelConfig& config, std::shared_ptr<Backend> backend);
  void unregisterModel(const std::string& modelName);

  AdmissionResult submit(Request req, BatchCompletionCallback cb = nullptr);

  void start();
  void stop();
  void drain(Duration timeout);

  bool isDraining() const { return draining_.load(); }
  size_t queueSize(const std::string& model) const;

  struct ModelInfo {
    std::string name;
    std::string backendType;
    size_t queueDepth = 0;
    std::vector<int> physicalCores;
    std::vector<int> logicalCpus;
    size_t cacheBudgetBytes = 0;
    size_t maxBatchSize = 16;
  };
  std::vector<ModelInfo> listModels() const;

private:
  struct ModelQueue {
    ModelConfig config;
    CoreLease lease;
    std::shared_ptr<Backend> backend;
    size_t cacheCap = 16;
    std::deque<std::pair<Request, BatchCompletionCallback>> queue;
    std::mutex queueMutex;
    std::condition_variable cv;
    std::unique_ptr<std::thread> workerThread;
    std::atomic<bool> running{false};
  };

  void modelLoop(const std::string& modelName);
  bool checkAdmission(const Request& req, Duration estimatedWait, Duration estimatedRun);

  SchedulerPolicy policy_;
  std::shared_ptr<ResourceManager> rm_;
  std::shared_ptr<CostModel> costModel_;
  std::shared_ptr<MetricsCollector> metrics_;

  mutable std::mutex modelsMutex_;
  std::map<std::string, std::shared_ptr<ModelQueue>> models_;
  std::map<std::string, TokenBucket> tenantBuckets_;
  std::atomic<bool> running_{false};
  std::atomic<bool> draining_{false};
};

} // namespace infsched
