#include "infsched/ipc.hpp"
#include "infsched/types.hpp"
#include "src/worker/mock_backend.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>

using namespace infsched;

int main(int argc, char* argv[]) {
  std::string modelName = "test_model";
  std::string backendType = "mock";
  int ipcFd = -1;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--model" && i + 1 < argc) {
      modelName = argv[++i];
    } else if (arg == "--backend" && i + 1 < argc) {
      backendType = argv[++i];
    } else if (arg == "--ipc-fd" && i + 1 < argc) {
      ipcFd = std::stoi(argv[++i]);
    }
  }

  std::unique_ptr<Backend> backend;
  if (backendType == "mock" || backendType == "onnx" || backendType == "gguf") {
    backend = std::make_unique<MockBackend>(modelName);
  } else {
    backend = std::make_unique<MockBackend>(modelName);
  }

  ModelConfig config;
  config.name = modelName;
  config.backendType = backendType;
  backend->load(config);

  if (ipcFd < 0) {
    // If run standalone without IPC socket, exit cleanly
    return 0;
  }

  // Worker IPC loop
  bool running = true;
  while (running) {
    IpcHeader header;
    ssize_t bytes = read(ipcFd, &header, sizeof(header));
    if (bytes <= 0) {
      // Supervisor disconnected
      break;
    }

    if (header.type == MessageType::Heartbeat) {
      HeartbeatMessage msg;
      msg.timestamp = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(SystemClock::now().time_since_epoch()).count());
      msg.isHealthy = backend->isHealthy();
      write(ipcFd, &msg, sizeof(msg));
    } else if (header.type == MessageType::RunBatch) {
      RunBatchHeader rb;
      if (read(ipcFd, &rb, sizeof(rb)) == sizeof(rb)) {
        Batch batch;
        batch.model = modelName;
        batch.requests.resize(rb.batchSize);

        std::vector<TensorView> outputs;
        auto t0 = SystemClock::now();
        StatusCode sc = backend->run(batch, outputs);
        auto t1 = SystemClock::now();

        RunBatchResult res;
        res.batchId = rb.batchId;
        res.statusCode = static_cast<uint32_t>(sc);
        res.executionMicros = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        write(ipcFd, &res, sizeof(res));
      }
    } else if (header.type == MessageType::Shutdown) {
      running = false;
    }
  }

  backend->unload();
  close(ipcFd);
  return 0;
}
