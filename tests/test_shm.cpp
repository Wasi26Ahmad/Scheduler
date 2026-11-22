#include "infsched/shm.hpp"
#include <cassert>
#include <iostream>
#include <cstring>

using namespace infsched;

int main() {
  std::cout << "[Test] Running test_shm..." << std::endl;

  size_t testSize = 64 * 1024; // 64 KiB
  auto shm = SharedMemoryBuffer::create("test_tensor_buffer", testSize);
  assert(shm != nullptr);
  assert(shm->fd() >= 0);
  assert(shm->data() != nullptr);
  assert(shm->size() == testSize);

  // Write test pattern
  const char* pattern = "ZeroCopyTensorDataPayload123456789";
  size_t len = std::strlen(pattern);
  std::memcpy(shm->data(), pattern, len);

  // Map duplicate view from fd
  auto shmView = SharedMemoryBuffer::fromFd(shm->fd(), testSize);
  assert(shmView != nullptr);
  assert(std::memcmp(shmView->data(), pattern, len) == 0);

  std::cout << "[Test] test_shm PASSED." << std::endl;
  return 0;
}
