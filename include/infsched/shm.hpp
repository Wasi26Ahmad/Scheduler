#pragma once

#include "infsched/types.hpp"
#include <string>
#include <memory>

namespace infsched {

class SharedMemoryBuffer {
public:
  static std::shared_ptr<SharedMemoryBuffer> create(const std::string& name, size_t sizeBytes);
  static std::shared_ptr<SharedMemoryBuffer> fromFd(int fd, size_t sizeBytes);

  ~SharedMemoryBuffer();

  int fd() const { return fd_; }
  void* data() const { return addr_; }
  size_t size() const { return size_; }

  SharedMemoryBuffer(const SharedMemoryBuffer&) = delete;
  SharedMemoryBuffer& operator=(const SharedMemoryBuffer&) = delete;

private:
  SharedMemoryBuffer(int fd, void* addr, size_t size, bool ownsFd);

  int fd_ = -1;
  void* addr_ = nullptr;
  size_t size_ = 0;
  bool ownsFd_ = true;
};

} // namespace infsched
