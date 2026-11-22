#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

int main(int argc, char* argv[]) {
  std::string command = "ready";
  std::string host = "127.0.0.1";
  int port = 9090;

  std::string modelName;
  std::string backendType = "onnx";
  std::string modelPath;
  int cores = 1;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "ready" || arg == "health" || arg == "metrics" ||
        arg == "list" || arg == "load" || arg == "unload") {
      command = arg;
      if ((command == "load" || command == "unload") && i + 1 < argc && argv[i + 1][0] != '-') {
        modelName = argv[++i];
      }
    } else if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = std::stoi(argv[++i]);
    } else if (arg == "--backend" && i + 1 < argc) {
      backendType = argv[++i];
    } else if (arg == "--path" && i + 1 < argc) {
      modelPath = argv[++i];
    } else if (arg == "--cores" && i + 1 < argc) {
      cores = std::stoi(argv[++i]);
    }
  }

  std::string method = "GET";
  std::string endpoint = "/readyz";

  if (command == "health") {
    endpoint = "/healthz";
  } else if (command == "metrics") {
    endpoint = "/metrics";
  } else if (command == "list") {
    endpoint = "/models";
  } else if (command == "load") {
    method = "POST";
    endpoint = "/models/load?name=" + modelName + "&backend=" + backendType +
               "&cores=" + std::to_string(cores) + "&path=" + modelPath;
  } else if (command == "unload") {
    method = "POST";
    endpoint = "/models/unload?name=" + modelName;
  }

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    std::cerr << "Failed to create socket\n";
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

  if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "Connection failed to " << host << ":" << port << "\n";
    close(sock);
    return 1;
  }

  std::string req = method + " " + endpoint + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
  ssize_t w = write(sock, req.data(), req.size());
  (void)w;

  char buffer[4096];
  std::string response;
  ssize_t bytes = 0;
  while ((bytes = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
    buffer[bytes] = '\0';
    response += buffer;
  }
  close(sock);

  // Parse HTTP status
  if (response.find("HTTP/1.1 200 OK") != std::string::npos) {
    auto bodyPos = response.find("\r\n\r\n");
    if (bodyPos != std::string::npos) {
      std::cout << response.substr(bodyPos + 4);
    } else {
      std::cout << response;
    }
    return 0;
  }

  std::cerr << "Service returned error: " << response << "\n";
  return 1;
}
