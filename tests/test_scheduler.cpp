#include "infsched/scheduler.hpp"
#include "src/worker/mock_backend.hpp"
#include <cassert>
#include <iostream>
#include <future>

using namespace infsched;

int main() {
  std::cout << "[Test] Running test_scheduler..." << std::endl;

  auto topo = HardwareTopology::discover();
  auto rm = std::make_shared<ResourceManager>(std::move(topo));
  auto costModel = std::make_shared<CostModel>();
  auto metrics = std::make_shared<MetricsCollector>();

  SchedulerPolicy policy;
  policy.maxBatchWait = std::chrono::milliseconds(3);
  policy.safetyMargin = std::chrono::milliseconds(1);

  Scheduler scheduler(policy, rm, costModel, metrics);

  ModelConfig config;
  config.name = "mnist";
  config.minPhysicalCores = 1;
  config.maxBatchSize = 4;
  config.workingSetBytesPerSample = 256 * 1024;

  auto backend = std::make_shared<MockBackend>("mnist", std::chrono::milliseconds(2), std::chrono::microseconds(200));
  bool regOk = scheduler.registerModel(config, backend);
  assert(regOk);

  scheduler.start();

  // Test submitting a valid request
  std::promise<StatusCode> prom;
  auto fut = prom.get_future();

  Request req;
  req.id = 101;
  req.model = "mnist";
  req.tenant = "tenant_test";
  req.deadline = SystemClock::now() + std::chrono::milliseconds(50);

  auto adm = scheduler.submit(req, [&](const Batch& b, StatusCode sc, const std::vector<TensorView>&, Duration) {
    assert(b.batchSize() >= 1);
    prom.set_value(sc);
  });

  assert(adm == AdmissionResult::Accepted);
  assert(fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  assert(fut.get() == StatusCode::Ok);

  // Test unknown model rejection
  Request badReq;
  badReq.model = "non_existent_model";
  auto badAdm = scheduler.submit(badReq, nullptr);
  assert(badAdm == AdmissionResult::ModelNotFound);

  // Test deadline already missed rejection
  Request expiredReq;
  expiredReq.model = "mnist";
  expiredReq.deadline = SystemClock::now() - std::chrono::milliseconds(10);
  auto expiredAdm = scheduler.submit(expiredReq, nullptr);
  assert(expiredAdm == AdmissionResult::DeadlineAlreadyMissed);

  // Test draining
  scheduler.drain(std::chrono::milliseconds(100));
  assert(scheduler.isDraining());

  Request drainReq;
  drainReq.model = "mnist";
  assert(scheduler.submit(drainReq, nullptr) == AdmissionResult::Draining);

  scheduler.stop();
  std::cout << "[Test] test_scheduler PASSED." << std::endl;
  return 0;
}
