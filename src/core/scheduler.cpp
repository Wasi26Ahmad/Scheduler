#include "infsched/scheduler.hpp"

#include <iostream>
#include <algorithm>

namespace infsched {

Scheduler::Scheduler(SchedulerPolicy policy,
                     std::shared_ptr<ResourceManager> rm,
                     std::shared_ptr<CostModel> costModel,
                     std::shared_ptr<MetricsCollector> metrics)
    : policy_(policy), rm_(std::move(rm)), costModel_(std::move(costModel)), metrics_(std::move(metrics)) {}

Scheduler::~Scheduler() {
  stop();
}

bool Scheduler::registerModel(const ModelConfig& config, std::shared_ptr<Backend> backend) {
  std::lock_guard<std::mutex> lock(modelsMutex_);
  if (models_.find(config.name) != models_.end()) {
    return false;
  }

  auto leaseOpt = rm_->acquireLease(config);
  if (!leaseOpt) {
    return false;
  }

  auto mq = std::make_shared<ModelQueue>();
  mq->config = config;
  mq->lease = *leaseOpt;
  mq->backend = std::move(backend);
  mq->cacheCap = rm_->calculateCacheCap(config, mq->lease);
  mq->running = true;

  // Perform backend warmup
  mq->backend->load(config);
  Duration warm1 = mq->backend->estimateWarmup(1);
  Duration warmCap = mq->backend->estimateWarmup(std::min<size_t>(4, config.maxBatchSize));
  costModel_->recordWarmup(config.name, 1, warm1);
  costModel_->recordWarmup(config.name, std::min<size_t>(4, config.maxBatchSize), warmCap);

  models_[config.name] = mq;

  if (running_.load()) {
    mq->workerThread = std::make_unique<std::thread>(&Scheduler::modelLoop, this, config.name);
  }

  return true;
}

void Scheduler::unregisterModel(const std::string& modelName) {
  std::shared_ptr<ModelQueue> mq;
  {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    auto it = models_.find(modelName);
    if (it != models_.end()) {
      mq = it->second;
      models_.erase(it);
    }
  }

  if (mq) {
    mq->running = false;
    mq->cv.notify_all();
    if (mq->workerThread && mq->workerThread->joinable()) {
      mq->workerThread->join();
    }
    mq->backend->unload();
    rm_->releaseLease(modelName);
  }
}

void Scheduler::start() {
  std::lock_guard<std::mutex> lock(modelsMutex_);
  if (running_.load()) return;
  running_ = true;
  draining_ = false;

  for (auto& pair : models_) {
    auto& mq = pair.second;
    if (!mq->workerThread) {
      mq->running = true;
      mq->workerThread = std::make_unique<std::thread>(&Scheduler::modelLoop, this, pair.first);
    }
  }
}

void Scheduler::stop() {
  running_ = false;
  draining_ = true;

  std::vector<std::shared_ptr<ModelQueue>> active;
  {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    for (auto& pair : models_) {
      active.push_back(pair.second);
    }
  }

  for (auto& mq : active) {
    mq->running = false;
    mq->cv.notify_all();
    if (mq->workerThread && mq->workerThread->joinable()) {
      mq->workerThread->join();
    }
    mq->workerThread.reset();
  }
}

void Scheduler::drain(Duration timeout) {
  draining_ = true;
  auto deadline = SystemClock::now() + timeout;

  while (SystemClock::now() < deadline) {
    bool allEmpty = true;
    {
      std::lock_guard<std::mutex> lock(modelsMutex_);
      for (const auto& pair : models_) {
        std::lock_guard<std::mutex> ql(pair.second->queueMutex);
        if (!pair.second->queue.empty()) {
          allEmpty = false;
          break;
        }
      }
    }
    if (allEmpty) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

size_t Scheduler::queueSize(const std::string& model) const {
  std::lock_guard<std::mutex> lock(modelsMutex_);
  auto it = models_.find(model);
  if (it == models_.end()) return 0;
  std::lock_guard<std::mutex> ql(it->second->queueMutex);
  return it->second->queue.size();
}

AdmissionResult Scheduler::submit(Request req, BatchCompletionCallback cb) {
  auto startSubmit = SystemClock::now();

  if (draining_.load()) {
    metrics_->recordRejected(req.model, req.tenant, "draining");
    return AdmissionResult::Draining;
  }

  std::shared_ptr<ModelQueue> mq;
  {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    auto it = models_.find(req.model);
    if (it == models_.end()) {
      metrics_->recordRejected(req.model, req.tenant, "model_not_found");
      return AdmissionResult::ModelNotFound;
    }
    mq = it->second;

    // Tenant token bucket quota
    auto& bucket = tenantBuckets_[req.tenant];
    if (!bucket.tryConsume(1.0)) {
      metrics_->recordRejected(req.model, req.tenant, "over_quota");
      return AdmissionResult::OverQuota;
    }
  }

  // Queue admission check
  std::unique_lock<std::mutex> ql(mq->queueMutex);
  if (mq->queue.size() >= policy_.maxQueueDepth) {
    metrics_->recordRejected(req.model, req.tenant, "queue_full");
    return AdmissionResult::QueueFull;
  }

  // Calculate backlog time and single-sample run time
  Duration estRun1 = costModel_->estimate(req.model, 1);
  Duration estBacklog{0};
  if (!mq->queue.empty()) {
    size_t qSize = mq->queue.size();
    size_t estBatches = (qSize + mq->cacheCap - 1) / std::max<size_t>(1, mq->cacheCap);
    estBacklog = estBatches * costModel_->estimate(req.model, mq->cacheCap);
  }

  auto now = SystemClock::now();
  if (now + estBacklog + estRun1 > req.deadline) {
    metrics_->recordRejected(req.model, req.tenant, "deadline_missed");
    return AdmissionResult::DeadlineAlreadyMissed;
  }

  mq->queue.push_back({std::move(req), std::move(cb)});
  ql.unlock();
  mq->cv.notify_one();

  auto endSubmit = SystemClock::now();
  metrics_->recordSchedulerOverhead(endSubmit - startSubmit);
  metrics_->recordAdmitted(mq->config.name, "tenant");

  return AdmissionResult::Accepted;
}

void Scheduler::modelLoop(const std::string& modelName) {
  std::shared_ptr<ModelQueue> mq;
  {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    auto it = models_.find(modelName);
    if (it != models_.end()) {
      mq = it->second;
    }
  }

  if (!mq) return;

  // Pin worker thread to its core lease
  rm_->pinThreadToLease(mq->lease);

  while (mq->running.load() || !mq->queue.empty()) {
    std::vector<std::pair<Request, BatchCompletionCallback>> batchItems;
    size_t cap = std::min(mq->config.maxBatchSize, mq->cacheCap);

    {
      std::unique_lock<std::mutex> lock(mq->queueMutex);
      mq->cv.wait(lock, [&]() {
        return !mq->running.load() || !mq->queue.empty();
      });

      if (!mq->running.load() && mq->queue.empty()) {
        break;
      }

      // Check slack of the oldest request
      while (!mq->queue.empty()) {
        auto now = SystemClock::now();
        const auto& oldest = mq->queue.front().first;
        size_t currentQSize = mq->queue.size();
        size_t n = std::min(currentQSize, cap);
        Duration runTime = costModel_->estimate(modelName, n);

        Duration slack = Duration::zero();
        if (oldest.deadline > now + runTime + policy_.safetyMargin) {
          slack = oldest.deadline - (now + runTime + policy_.safetyMargin);
        }

        // If batch is not full and slack allows waiting for more arrivals, wait up to maxBatchWait
        if (n < cap && slack > policy_.maxBatchWait && mq->running.load()) {
          auto waitDuration = std::min(policy_.maxBatchWait, slack);
          mq->cv.wait_for(lock, waitDuration);
          continue;
        }

        break;
      }

      // Collect batch items, dropping expired requests
      auto now = SystemClock::now();
      while (!mq->queue.empty() && batchItems.size() < cap) {
        auto item = std::move(mq->queue.front());
        mq->queue.pop_front();

        Duration run1 = costModel_->estimate(modelName, 1);
        if (now + run1 > item.first.deadline) {
          // Drop expired request
          metrics_->recordRejected(modelName, item.first.tenant, "deadline_missed");
          if (item.second) {
            item.second(Batch{modelName, {item.first}}, StatusCode::DeadlineExceeded, {}, Duration::zero());
          }
          continue;
        }

        batchItems.push_back(std::move(item));
      }
    }

    if (batchItems.empty()) {
      continue;
    }

    // Build batch
    Batch batch;
    batch.model = modelName;
    for (const auto& item : batchItems) {
      batch.requests.push_back(item.first);
    }

    // Check if overflow cores could be borrowed for large batches
    bool borrowed = false;
    if (policy_.enableOverflowBorrowing && batch.batchSize() >= cap && mq->config.maxPhysicalCores > mq->config.minPhysicalCores) {
      int needed = mq->config.maxPhysicalCores - mq->config.minPhysicalCores;
      auto ovf = rm_->borrowOverflowCores(modelName, needed);
      if (ovf) {
        borrowed = true;
      }
    }

    // Execute batch
    auto execStart = SystemClock::now();
    std::vector<TensorView> outputs;
    StatusCode status = mq->backend->run(batch, outputs);
    auto execEnd = SystemClock::now();
    Duration measuredLatency = execEnd - execStart;

    if (borrowed) {
      rm_->returnOverflowCores(modelName);
    }

    // Update cost model with empirical measurement
    if (status == StatusCode::Ok) {
      costModel_->update(modelName, batch.batchSize(), measuredLatency);
    }

    // Notify callbacks and record metrics
    Duration avgQueueWait{0};
    for (const auto& item : batchItems) {
      if (execStart > item.first.arrivalTime) {
        avgQueueWait += (execStart - item.first.arrivalTime);
      }
    }
    if (!batchItems.empty()) {
      avgQueueWait /= batchItems.size();
    }

    metrics_->recordBatchCompleted(modelName, batch.batchSize(), measuredLatency, avgQueueWait);

    for (const auto& item : batchItems) {
      if (item.second) {
        item.second(batch, status, outputs, measuredLatency);
      }
    }
  }
}

std::vector<Scheduler::ModelInfo> Scheduler::listModels() const {
  std::lock_guard<std::mutex> lock(modelsMutex_);
  std::vector<ModelInfo> result;
  result.reserve(models_.size());
  for (const auto& pair : models_) {
    const auto& mq = pair.second;
    ModelInfo info;
    info.name = mq->config.name;
    info.backendType = mq->config.backendType;
    info.maxBatchSize = mq->config.maxBatchSize;
    info.physicalCores = mq->lease.physicalCoreIds;
    info.logicalCpus = mq->lease.logicalCpuIds;
    info.cacheBudgetBytes = mq->lease.cacheBudgetBytes;
    {
      std::lock_guard<std::mutex> ql(mq->queueMutex);
      info.queueDepth = mq->queue.size();
    }
    result.push_back(std::move(info));
  }
  return result;
}

} // namespace infsched
