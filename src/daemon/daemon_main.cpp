#include "infsched/topology.hpp"
#include "infsched/resource_manager.hpp"
#include "infsched/cost_model.hpp"
#include "infsched/metrics.hpp"
#include "infsched/scheduler.hpp"
#include "infsched/supervisor.hpp"
#include "src/daemon/ops_server.hpp"
#include "src/worker/mock_backend.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <csignal>
#include <unistd.h>
#include <fstream>
#include <sstream>

using namespace infsched;

static std::atomic<bool> g_shutdownRequested{false};

void signalHandler(int sig) {
  if (sig == SIGINT || sig == SIGTERM) {
    g_shutdownRequested = true;
  }
}

int main(int argc, char* argv[]) {
  std::string configPath = "configs/infsched.yaml";
  int opsPort = 9090;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      configPath = argv[++i];
    } else if (arg == "--ops-port" && i + 1 < argc) {
      opsPort = std::stoi(argv[++i]);
    }
  }

  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);

  std::cout << "=== CPU Inference Scheduler (infsched) v0.1.0 ===" << std::endl;

  // 1. Discover Hardware Topology
  auto topo = HardwareTopology::discover();
  std::cout << "[Topology] Physical Cores: " << topo.physicalCores.size() << std::endl;
  for (const auto& pc : topo.physicalCores) {
    std::cout << "  Core " << pc.coreId << " (Package " << pc.packageId << ") Threads: [";
    for (size_t i = 0; i < pc.logicalCpuIds.size(); ++i) {
      std::cout << pc.logicalCpuIds[i] << (i + 1 < pc.logicalCpuIds.size() ? ", " : "");
    }
    std::cout << "]" << std::endl;
  }
  std::cout << "[Topology] L2 Cache per core: " << (topo.l2BytesPerCore / 1024) << " KiB" << std::endl;
  std::cout << "[Topology] L3 Shared Cache: " << (topo.l3TotalBytes / (1024 * 1024)) << " MiB" << std::endl;
  std::cout << "[Topology] Features: AVX2=" << topo.hasAvx2
            << " AVX-512=" << topo.hasAvx512
            << " VNNI=" << topo.hasVnni
            << " AMX=" << topo.hasAmx
            << " L3_CAT=" << topo.hasL3Cat
            << " L2_CAT=" << topo.hasL2Cat
            << " MBA=" << topo.hasMba
            << " resctrl_mounted=" << topo.resctrlMounted << std::endl;

  auto rm = std::make_shared<ResourceManager>(std::move(topo));
  std::cout << "[Isolation] Active isolation level: " << rm->isolationLevelString() << std::endl;

  auto costModel = std::make_shared<CostModel>();
  auto metrics = std::make_shared<MetricsCollector>();

  SchedulerPolicy policy;
  policy.maxBatchWait = std::chrono::milliseconds(2);
  policy.safetyMargin = std::chrono::milliseconds(1);
  policy.enableOverflowBorrowing = true;

  auto scheduler = std::make_shared<Scheduler>(policy, rm, costModel, metrics);

  // Register default models
  ModelConfig modelA;
  modelA.name = "resnet50";
  modelA.minPhysicalCores = 1;
  modelA.maxPhysicalCores = 2;
  modelA.maxBatchSize = 16;
  modelA.workingSetBytesPerSample = 512 * 1024;
  auto backendA = std::make_shared<MockBackend>("resnet50", std::chrono::milliseconds(3), std::chrono::microseconds(300));
  scheduler->registerModel(modelA, backendA);

  ModelConfig modelB;
  modelB.name = "bert-base";
  modelB.minPhysicalCores = 1;
  modelB.maxPhysicalCores = 2;
  modelB.maxBatchSize = 8;
  modelB.workingSetBytesPerSample = 1024 * 1024;
  auto backendB = std::make_shared<MockBackend>("bert-base", std::chrono::milliseconds(6), std::chrono::microseconds(800));
  scheduler->registerModel(modelB, backendB);

  scheduler->start();

  // Supervisor for worker health
  auto supervisor = std::make_shared<Supervisor>("./infsched-worker");

  // Start Ops Server
  OpsServer ops(opsPort, metrics, rm, supervisor, scheduler);
  if (!ops.start()) {
    std::cerr << "Failed to start Ops server on port " << opsPort << std::endl;
  } else {
    std::cout << "[Ops] Listening on http://0.0.0.0:" << opsPort << " (/healthz, /readyz, /metrics)" << std::endl;
  }

  std::cout << "[Daemon] Scheduler daemon running. Press Ctrl+C to terminate." << std::endl;

  while (!g_shutdownRequested.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  std::cout << "\n[Daemon] Graceful shutdown initiated. Draining in-flight requests..." << std::endl;
  scheduler->drain(std::chrono::seconds(5));
  scheduler->stop();
  ops.stop();

  std::cout << "[Daemon] Shutdown complete." << std::endl;
  return 0;
}
