#include "src/daemon/ops_server.hpp"
#include "src/worker/mock_backend.hpp"
#include "src/worker/onnx_backend.hpp"
#include "src/worker/gguf_backend.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <cstring>
#include <map>

namespace infsched {

namespace {

std::map<std::string, std::string> parseQueryParams(const std::string& query) {
  std::map<std::string, std::string> params;
  std::stringstream ss(query);
  std::string item;
  while (std::getline(ss, item, '&')) {
    auto eq = item.find('=');
    if (eq != std::string::npos) {
      params[item.substr(0, eq)] = item.substr(eq + 1);
    }
  }
  return params;
}

} // namespace

OpsServer::OpsServer(int port,
                     std::shared_ptr<MetricsCollector> metrics,
                     std::shared_ptr<ResourceManager> rm,
                     std::shared_ptr<Supervisor> supervisor,
                     std::shared_ptr<Scheduler> scheduler)
    : port_(port),
      metrics_(std::move(metrics)),
      rm_(std::move(rm)),
      supervisor_(std::move(supervisor)),
      scheduler_(std::move(scheduler)) {}

OpsServer::~OpsServer() {
  stop();
}

bool OpsServer::start() {
  serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (serverFd_ < 0) {
    perror("socket");
    return false;
  }

  int opt = 1;
  setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port_);

  if (bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    perror("bind ops server");
    close(serverFd_);
    serverFd_ = -1;
    return false;
  }

  if (listen(serverFd_, 64) != 0) {
    perror("listen ops server");
    close(serverFd_);
    serverFd_ = -1;
    return false;
  }

  running_ = true;
  thread_ = std::make_unique<std::thread>(&OpsServer::serverLoop, this);
  return true;
}

void OpsServer::stop() {
  if (running_.load()) {
    running_ = false;
    if (serverFd_ >= 0) {
      shutdown(serverFd_, SHUT_RDWR);
      close(serverFd_);
      serverFd_ = -1;
    }
    if (thread_ && thread_->joinable()) {
      thread_->join();
    }
  }
}

void OpsServer::serverLoop() {
  while (running_.load()) {
    sockaddr_in clientAddr{};
    socklen_t len = sizeof(clientAddr);
    int clientFd = accept(serverFd_, reinterpret_cast<sockaddr*>(&clientAddr), &len);
    if (clientFd < 0) {
      if (!running_.load()) break;
      continue;
    }
    handleClient(clientFd);
    close(clientFd);
  }
}

void OpsServer::handleClient(int clientFd) {
  char buffer[4096];
  ssize_t bytesRead = read(clientFd, buffer, sizeof(buffer) - 1);
  if (bytesRead <= 0) return;
  buffer[bytesRead] = '\0';

  std::string req(buffer);
  std::string method = "GET";
  std::string fullPath = "/";

  auto firstSpace = req.find(' ');
  if (firstSpace != std::string::npos) {
    method = req.substr(0, firstSpace);
    auto secondSpace = req.find(' ', firstSpace + 1);
    if (secondSpace != std::string::npos) {
      fullPath = req.substr(firstSpace + 1, secondSpace - (firstSpace + 1));
    }
  }

  std::string path = fullPath;
  std::string queryStr;
  auto qPos = fullPath.find('?');
  if (qPos != std::string::npos) {
    path = fullPath.substr(0, qPos);
    queryStr = fullPath.substr(qPos + 1);
  }

  std::string response;
  if (path == "/healthz") {
    std::string body = "{\"status\": \"alive\"}\n";
    response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
  } else if (path == "/readyz") {
    bool ready = scheduler_ && !scheduler_->isDraining();
    std::string isoLevel = rm_ ? rm_->isolationLevelString() : "unknown";
    size_t activeCount = scheduler_ ? scheduler_->listModels().size() : 0;

    std::ostringstream body;
    body << "{\n"
         << "  \"ready\": " << (ready ? "true" : "false") << ",\n"
         << "  \"isolation_level\": \"" << isoLevel << "\",\n"
         << "  \"models_loaded\": " << activeCount << "\n"
         << "}\n";
    std::string bodyStr = body.str();

    std::string statusLine = ready ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 503 Service Unavailable\r\n";
    response = statusLine + "Content-Type: application/json\r\nContent-Length: " +
               std::to_string(bodyStr.size()) + "\r\nConnection: close\r\n\r\n" + bodyStr;
  } else if (path == "/metrics") {
    std::string promText = metrics_ ? metrics_->toPrometheus() : "";
    response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; version=0.0.4\r\nContent-Length: " +
               std::to_string(promText.size()) + "\r\nConnection: close\r\n\r\n" + promText;
  } else if (path == "/models" && method == "GET") {
    // List models
    std::ostringstream ss;
    ss << "[\n";
    if (scheduler_) {
      auto models = scheduler_->listModels();
      for (size_t i = 0; i < models.size(); ++i) {
        const auto& m = models[i];
        ss << "  {\n"
           << "    \"name\": \"" << m.name << "\",\n"
           << "    \"backend\": \"" << m.backendType << "\",\n"
           << "    \"max_batch_size\": " << m.maxBatchSize << ",\n"
           << "    \"queue_depth\": " << m.queueDepth << ",\n"
           << "    \"cache_budget_bytes\": " << m.cacheBudgetBytes << ",\n"
           << "    \"physical_cores\": [";
        for (size_t c = 0; c < m.physicalCores.size(); ++c) {
          ss << m.physicalCores[c] << (c + 1 < m.physicalCores.size() ? ", " : "");
        }
        ss << "],\n"
           << "    \"logical_cpus\": [";
        for (size_t l = 0; l < m.logicalCpus.size(); ++l) {
          ss << m.logicalCpus[l] << (l + 1 < m.logicalCpus.size() ? ", " : "");
        }
        ss << "]\n"
           << "  }" << (i + 1 < models.size() ? ",\n" : "\n");
      }
    }
    ss << "]\n";
    std::string body = ss.str();
    response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
  } else if (path == "/models/load") {
    // Dynamic on-demand model load
    auto params = parseQueryParams(queryStr);
    std::string name = params["name"];
    std::string backendType = params.count("backend") ? params["backend"] : "onnx";
    std::string modelPath = params.count("path") ? params["path"] : "";
    int cores = params.count("cores") ? std::stoi(params["cores"]) : 1;
    size_t maxBatch = params.count("max_batch") ? std::stoul(params["max_batch"]) : 16;

    if (name.empty()) {
      std::string body = "{\"error\": \"Missing 'name' parameter\"}\n";
      response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: " +
                 std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    } else {
      ModelConfig config;
      config.name = name;
      config.backendType = backendType;
      config.modelPath = modelPath;
      config.minPhysicalCores = cores;
      config.maxPhysicalCores = std::max(cores, cores + 1);
      config.maxBatchSize = maxBatch;

      std::shared_ptr<Backend> backend;
      if (backendType == "gguf") {
        backend = std::make_shared<GgufBackend>(name);
      } else if (backendType == "onnx") {
        backend = std::make_shared<OnnxBackend>(name);
      } else {
        backend = std::make_shared<MockBackend>(name);
      }

      bool ok = scheduler_ && scheduler_->registerModel(config, backend);
      if (ok) {
        std::string body = "{\"status\": \"loaded\", \"name\": \"" + name + "\"}\n";
        response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                   std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
      } else {
        std::string body = "{\"error\": \"Failed to acquire core lease or load model\"}\n";
        response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nContent-Length: " +
                   std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
      }
    }
  } else if (path == "/models/unload") {
    auto params = parseQueryParams(queryStr);
    std::string name = params["name"];
    if (name.empty()) {
      std::string body = "{\"error\": \"Missing 'name' parameter\"}\n";
      response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: " +
                 std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    } else {
      if (scheduler_) {
        scheduler_->unregisterModel(name);
      }
      if (supervisor_) {
        supervisor_->killWorker(name);
      }
      std::string body = "{\"status\": \"unloaded\", \"name\": \"" + name + "\"}\n";
      response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                 std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }
  } else {
    std::string body = "{\"error\": \"not found\"}\n";
    response = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: " +
               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
  }

  ssize_t written = write(clientFd, response.data(), response.size());
  (void)written;
}

} // namespace infsched
